// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'evac-tests) " + source) }

private func kw(_ s: String) -> Value { Value(keyword: s) }

// The coroutine of a go channel, owned by the caller.
private func coro(of ch: Value) -> Value { Value(owning: clj_debug_chan_coro(ch.raw)) }

private func take(_ ch: Value) -> Value { Value(owning: clj_chan_take(ch.raw)) }

// ASan frames are several times larger and its shadow dominates the footprint: the size gates hold outside it.
private let underASan = dlsym(UnsafeMutableRawPointer(bitPattern: -2), "__asan_init") != nil

// Evacuates once the coroutine is parked; false when it did not park within the wait.
private func evacuateOnceParked(_ c: Value, ms: Int = 5000) -> Bool {
	for _ in 0..<(ms * 5) {
		if clj_debug_coro_evacuate(c.raw) { return true }
		usleep(200)
	}
	return false
}

// Evacuation of cold parked coroutines (NOTES.md "Coroutines"): every kind of wait wakes correctly after it.
extension CoreTests {
	@Suite struct EvacTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("(ns evac-tests (:require [clojure.core.async :refer [chan <! >! <!! >!! close! timeout go go-scoped alts! cancel!]]))")
			for k in ["v", "put-done", "timed", "locked", "delivered", "slept", "scoped", "h", "alts", "joined"] { _ = kw(k) }
			_ = try cljEvalScoped("""
			(in-ns 'evac-tests)
			(declare gate sink gate2 hold held o p gate3 fut gate4 gates done loaded-value)
			(defn deep [g n acc] (if (zero? n) (do (<! g) acc) (+ 1 (deep g (dec n) (inc acc)))))
			(defn inner [g] (<! g))
			(defn outer [g] (inner g))
			(defn forever [n] (+ 1 (forever (inc n))))
			""")
		}

		// A stack several pages deep (50 KB in debug, more under ASan) is copied out and back twice; the locals survive.
		@Test func roundTripDeepStackKeepsTheFrames() throws {
			let base = CoroBaseline()
			do {
				_ = try eval("(def gate (chan))")
				let ch = try eval("(go (deep gate 60 0))")
				let c = coro(of: ch)
				try #require(evacuateOnceParked(c))
				#expect(clj_debug_coro_evacuated(c.raw))
				let bytes = clj_debug_coro_evacuated_bytes()
				#expect(bytes >= 30 * 1024, "\(bytes) live bytes for 60 interpreted frames")
				let fromBlob = Value(owning: clj_coro_parked_trace(c.raw))
				#expect(clj_debug_coro_restore(c.raw))
				#expect(!clj_debug_coro_evacuated(c.raw))
				let resident = Value(owning: clj_coro_parked_trace(c.raw))
				#expect(fromBlob == resident)
				#expect(fromBlob.description.contains("deep"))
				#expect(clj_debug_coro_evacuate(c.raw))
				_ = try eval("(>!! gate :v)")
				#expect(take(ch) == 120)
				_ = try eval("(def gate nil)")
			}
			base.check()
		}

		// The waker of each kind of wait touches only heap state, so an evacuated coroutine wakes with the right value.
		@Test func everyKindOfWaitWakesAfterEvacuation() throws {
			let base = CoroBaseline()
			do {
				_ = try eval("(def gate (chan)) (def sink (chan)) (def gate2 (chan)) (def hold (chan)) (def held (chan)) (def o (atom nil)) (def p (promise)) (def gate3 (chan)) (def fut (future (<!! gate3))) (def gate4 (chan))")
				let taker = try eval("(go (<! gate))")
				try #require(evacuateOnceParked(coro(of: taker)))
				_ = try eval("(>!! gate :v)")
				#expect(take(taker) == kw("v"))

				let putter = try eval("(go (>! sink :v) :put-done)")
				try #require(evacuateOnceParked(coro(of: putter)))
				#expect(try eval("(<!! sink)") == kw("v"))
				#expect(take(putter) == kw("put-done"))

				let alts = try eval("(go (let [[v c] (alts! [gate2 (chan)])] [v (= c gate2)]))")
				try #require(evacuateOnceParked(coro(of: alts)))
				_ = try eval("(>!! gate2 :alts)")
				#expect(take(alts) == (try eval("[:alts true]")))

				let timed = try eval("(go (<! (timeout 300)) :timed)")
				try #require(evacuateOnceParked(coro(of: timed)))
				#expect(take(timed) == kw("timed"))

				// The contender parks in the mutex's lot while the holder waits on a gate.
				let holder = try eval("(go (locking o (>! held :h) (<! hold)) :h)")
				#expect(try eval("(<!! held)") == kw("h"))
				let contender = try eval("(go (locking o :locked))")
				try #require(evacuateOnceParked(coro(of: contender)))
				_ = try eval("(>!! hold :h)")
				#expect(take(contender) == kw("locked"))
				#expect(take(holder) == kw("h"))

				let deref = try eval("(go @p)")
				try #require(evacuateOnceParked(coro(of: deref)))
				_ = try eval("(deliver p :delivered)")
				#expect(take(deref) == kw("delivered"))

				let fderef = try eval("(go @fut)")
				try #require(evacuateOnceParked(coro(of: fderef)))
				_ = try eval("(>!! gate3 :v)")
				#expect(take(fderef) == kw("v"))

				let slept = try eval("(go (Thread/sleep 300) :slept)")
				try #require(evacuateOnceParked(coro(of: slept)))
				#expect(take(slept) == kw("slept"))

				// The scope's join is an uncancellable take.
				let scoped = try eval("(go (go-scoped (go (<! gate4))) :scoped)")
				try #require(evacuateOnceParked(coro(of: scoped)))
				_ = try eval("(>!! gate4 :v)")
				#expect(take(scoped) == kw("scoped"))
				_ = try eval("(def gate nil) (def sink nil) (def gate2 nil) (def hold nil) (def held nil) (def o nil) (def p nil) (def gate3 nil) (def fut nil) (def gate4 nil)")
			}
			base.check()
		}

		// load-file reads on the blocking pool over a heap copy of its context: evacuated while the read blocks on a FIFO.
		@Test func blockingJobSurvivesEvacuation() throws {
			let base = CoroBaseline()
			do {
				let path = NSTemporaryDirectory() + "evac-\(getpid())-\(UInt32.random(in: 0..<UInt32.max)).clj"
				#expect(mkfifo(path, 0o600) == 0)
				defer { unlink(path) }
				_ = try eval("(def loaded-value (atom nil))")
				let loaded = try eval("(go (load-file \"\(path)\"))")
				try #require(evacuateOnceParked(coro(of: loaded)))
				let fd = open(path, O_WRONLY)
				#expect(fd >= 0)
				let src = "(in-ns 'evac-tests) (reset! loaded-value (+ 40 2))\n"
				_ = src.withCString { write(fd, $0, strlen($0)) }
				close(fd)
				#expect(take(loaded) == nil)
				#expect(try eval("@loaded-value") == 42)
				_ = try eval("(def loaded-value nil)")
			}
			base.check()
		}

		@Test func cancellationOfAnEvacuatedCoroutine() throws {
			let base = CoroBaseline()
			do {
				_ = try eval("(def gate (chan))")
				let ch = try eval("(go (try (<! gate) (catch :cancelled e (ex-message e))))")
				let c = coro(of: ch)
				try #require(evacuateOnceParked(c))
				clj_release(clj_chan_cancel(ch.raw))
				#expect(take(ch) == "Coroutine cancelled")
				#expect(!clj_debug_coro_evacuated(c.raw))
				_ = try eval("(def gate nil)")
			}
			base.check()
		}

		// The trace of a parked coroutine reads its frames from the blob while evacuated; nil once it runs again.
		@Test func traceOfAnEvacuatedCoroutine() throws {
			let base = CoroBaseline()
			do {
				_ = try eval("(def gate (chan))")
				let ch = try eval("(go (outer gate))")
				let c = coro(of: ch)
				try #require(evacuateOnceParked(c))
				let text = Value(owning: clj_coro_parked_trace(c.raw)).description
				#expect(text.contains("inner") && text.contains("outer"), "\(text)")
				_ = try eval("(>!! gate :v)")
				#expect(take(ch) == kw("v"))
				#expect(Value(owning: clj_coro_parked_trace(c.raw)) == nil)
				_ = try eval("(def gate nil)")
			}
			base.check()
		}

		// After a restore the stack limit still holds: unbounded recursion throws "Stack overflow" in that coroutine.
		@Test func stackOverflowAfterRestore() throws {
			let base = CoroBaseline()
			do {
				_ = try eval("(def gate (chan))")
				let ch = try eval("(go (<! gate) (try (forever 0) (catch :default e (ex-message e))))")
				try #require(evacuateOnceParked(coro(of: ch)))
				_ = try eval("(>!! gate :v)")
				#expect(take(ch) == "Stack overflow")
				_ = try eval("(def gate nil)")
			}
			base.check()
		}

		// An aggressive sweep takes the cold coroutines and leaves a ping-pong pair alone.
		@Test func sweepTakesColdNotHot() throws {
			let base = CoroBaseline()
			let period = clj_coro_evac_sweep_ms()
			defer { clj_coro_set_evac_sweep_ms(period) }
			do {
				clj_coro_set_evac_sweep_ms(2)
				_ = try eval("(def gate (chan))")
				_ = try eval("(dotimes [i 10] (go (<! gate)))")
				let before = clj_debug_coro_evacuations()
				let pair = try eval("""
				(let [ping (chan) pong (chan)
				      p (go (loop [i 0] (when (< i 20000) (>! ping i) (<! pong) (recur (inc i)))) (close! ping))
				      q (go (loop [] (when-let [v (<! ping)] (>! pong v) (recur))))]
				  [p q])
				""")
				var waited = 0
				while clj_debug_coro_evacuated_count() < 10 && waited < 5000 {
					usleep(1000)
					waited += 1
				}
				#expect(clj_debug_coro_evacuated_count() >= 10)
				let join = try eval("(fn [[p q]] (<!! p) (<!! q) :joined)")
				var arg = pair.raw
				#expect(Value(owning: withUnsafePointer(to: &arg) { clj_invoke(join.raw, $0, 1) }) == kw("joined"))
				// The pair parked ~40 000 times inside the sweep's window: an evacuation of it is the bound, not the rule.
				let hot = clj_debug_coro_evacuations() - before - 10
				#expect(hot <= 4, "\(hot) evacuations of the hot pair")
				_ = try eval("(close! gate) (def gate nil)")
			}
			base.check()
		}

		// 10 000 parked go blocks: the blobs hold their live bytes only, and every one wakes with its value.
		@Test func tenThousandParkedEvacuated() throws {
			let base = CoroBaseline()
			do {
				_ = try eval("(def gates (vec (repeatedly 10 chan))) (def done (chan 10000))")
				_ = try eval("(dotimes [i 10000] (let [g (nth gates (mod i 10))] (go (<! g) (>! done i))))")
				let gates = try eval("gates")
				func pending() -> UInt32 { (0..<10).reduce(0) { $0 + clj_debug_chan_pending(clj_vector_nth(gates.raw, UInt32($1)), false) } }
				var waited = 0
				while pending() < 10000 && waited < 5000 {
					usleep(1000)
					waited += 1
				}
				#expect(pending() == 10000)
				let before = clj_debug_phys_footprint()
				// An aggressive sweep (CLJ_EVAC_SWEEP_MS=1 in the stress loop) may have taken some already.
				let n = clj_coro_evacuate_all()
				#expect(clj_debug_coro_evacuated_count() == 10000, "\(n) evacuated now")
				// ~3.7 KB live per interpreted go block: two 832-byte eval frames, the entry's sigjmp_buf, the switch frame.
				let bytes = clj_debug_coro_evacuated_bytes()
				#expect(underASan || bytes <= 10000 * 4096, "\(bytes) bytes in blobs")
				let after = clj_debug_phys_footprint()
				// Most of 10 000 pages went back; other suites run in parallel, so the bound is loose.
				#expect(underASan || n < 10000 || (before > after && before - after > 100 * 1024 * 1024), "footprint \(before / 1024) KB → \(after / 1024) KB")
				#expect(try eval("(doseq [g gates] (close! g)) (reduce + (repeatedly 10000 #(<!! done)))") == 49_995_000)
				#expect(clj_debug_coro_evacuated_count() == 0)
				_ = try eval("(def gates nil) (def done nil)")
			}
			base.check()
		}
	}
}
