// @ai-generated(solo)
import CljCore
import Darwin
import Foundation
import Testing
@testable import Pippin

// Design §4, «BRC с одним владельцем»: the test thread adopts the carrier, a fresh pthread stands for the pool.
private let counted = clj_debug_live_objects() >= 0

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'main-rc-tests) " + source) }

private func ops() -> [Int64] {
	var out = [Int64](repeating: 0, count: Int(CLJ_RC_KINDS))
	clj_debug_rc_ops(&out)
	return out
}

// What the main carrier did between two snapshots: plain ops, each kind of RMW, and references handed back to it.
private struct MainOps: Equatable, CustomStringConvertible {
	var plain = 0, edgeIn = 0, edgeOut = 0, inPlace = 0, merged = 0, spill = 0, deferred = 0

	init(plain: Int = 0, edgeIn: Int = 0, edgeOut: Int = 0, inPlace: Int = 0, merged: Int = 0, spill: Int = 0, deferred: Int = 0) {
		(self.plain, self.edgeIn, self.edgeOut, self.inPlace, self.merged, self.spill, self.deferred) = (plain, edgeIn, edgeOut, inPlace, merged, spill, deferred)
	}

	init(_ a: [Int64], _ b: [Int64]) {
		func d(_ k: Int) -> Int { Int(b[k] - a[k]) }
		self.init(plain: d(CLJ_RC_MAIN_PLAIN), edgeIn: d(CLJ_RC_MAIN_EDGE_IN), edgeOut: d(CLJ_RC_MAIN_EDGE_OUT), inPlace: d(CLJ_RC_MAIN_FREE_IN_PLACE),
		          merged: d(CLJ_RC_MAIN_MERGED), spill: d(CLJ_RC_MAIN_SPILL), deferred: d(CLJ_RC_DEFERRED))
	}

	var description: String { "plain \(plain) in \(edgeIn) out \(edgeOut) in-place \(inPlace) merged \(merged) spill \(spill) deferred \(deferred)" }
}

// Runs body to its end on a fresh pthread: an execution of its own, never the main carrier.
private func onAnotherThread(_ body: @escaping () -> Void) {
	final class Box {
		let body: () -> Void
		init(_ body: @escaping () -> Void) { self.body = body }
	}
	var thread: pthread_t?
	let box = Unmanaged.passRetained(Box(body)).toOpaque()
	_ = pthread_create(&thread, nil, { arg in
		Unmanaged<Box>.fromOpaque(arg).takeRetainedValue().body()
		return nil
	}, box)
	pthread_join(thread!, nil)
}

// A cons over a cons, shared on the calling thread: the inner one is freed through its parent.
private func sharedPair() -> clj_value {
	let inner = clj_cons_new(clj_fixnum(1), CLJ_NIL)
	let outer = clj_cons_new(inner, CLJ_NIL)
	clj_release(inner)
	clj_share(outer)
	return outer
}

private func conses() -> Int64 { withUnsafePointer(to: clj_cons_type) { clj_debug_live_objects_of($0) } }

// Pending releases hold their objects, and what those reach, until main turns: settling pumps.
private func settledAsMain() -> Bool {
	for _ in 0..<1000 {
		clj_sched_main_pump()
		if clj_debug_runtime_settle(0, 10) && clj_debug_rc_pending() == 0 { return true }
	}
	print("unsettled: coros \(clj_debug_live_coros()) timers \(clj_debug_timers_held()) blocking \(clj_debug_blocking_held()) pending \(clj_debug_rc_pending())")
	return false
}

private func awaitPending() -> Bool {
	for _ in 0..<10_000 where clj_debug_rc_pending() == 0 { usleep(1000) }
	return clj_debug_rc_pending() > 0
}

// Abandon drains what the body left for main.
private func asMain(_ body: () throws -> Void) rethrows {
	clj_debug_sched_main_adopt()
	defer { clj_debug_sched_main_abandon() }
	try body()
}

extension CoreTests {
	@Suite struct MainRCTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("(ns main-rc-tests (:require [clojure.core.async :refer [chan <! >! <!! >!! go go-main go-loop close! offer! poll!]]))")
			_ = try cljEvalScoped("(in-ns 'main-rc-tests) (declare rc-a rc-gate rc-out rc-in rc-workers rc-main-step) (defrecord RcProbe [k])")
			_ = Value(keyword: "k")
			_ = Value(keyword: "main")
			_ = Value(keyword: "go")
		}

		// Inside an episode main touches only main16: a thousand retain/release pairs execute no RMW.
		@Test func anEpisodeIsPlain() {
			runtimeSettled("before the conses")
			let base = conses()
			asMain {
				let v = sharedPair()
				#expect(clj_debug_rc_count(v) == 1)
				let a = ops()
				for _ in 0..<1000 { _ = clj_retain(v) }
				for _ in 0..<1000 { clj_release(v) }
				let b = ops()
				if counted { #expect(MainOps(a, b) == MainOps(plain: 2000)) }
				clj_release(v)
				if counted { #expect(MainOps(b, ops()) == MainOps(inPlace: 2)) }
			}
			#expect(conses() == base)
		}

		// Swift's copy of the inline retain/release reaches the main path through the out-of-line slow path.
		@Test func swiftValuesOnMainArePlain() {
			runtimeSettled("before the conses")
			let base = conses()
			asMain {
				let v = Value(owning: sharedPair())
				let a = ops()
				do {
					let copies = (0..<100).map { _ in Value(borrowing: v.raw) }
					#expect(copies.count == 100)
				}
				if counted { #expect(MainOps(a, ops()) == MainOps(plain: 200)) }
			}
			#expect(conses() == base)
		}

		// Pool-born, so merged: main's retain clears the bit, and its last release finds a zero word, no RMW.
		@Test func aPoolBornObjectMainReleasesLast() {
			runtimeSettled("before the conses")
			let base = conses()
			asMain {
				var v: clj_value = CLJ_NIL
				onAnotherThread { v = sharedPair() }
				let a = ops()
				_ = clj_retain(v)
				#expect(clj_debug_rc_count(v) == 2)
				onAnotherThread { clj_release(v) }
				#expect(conses() == base + 2)
				clj_release(v)
				if counted { #expect(MainOps(a, ops()) == MainOps(edgeIn: 1, inPlace: 1, merged: 1)) }
				#expect(conses() == base)
			}
		}

		// The other order: main's episode end finds the pool's count and sets MERGED; the pool's release frees.
		@Test func aPoolBornObjectThePoolReleasesLast() {
			runtimeSettled("before the conses")
			let base = conses()
			asMain {
				var v: clj_value = CLJ_NIL
				onAnotherThread { v = sharedPair() }
				let a = ops()
				_ = clj_retain(v)
				clj_release(v)
				if counted { #expect(MainOps(a, ops()) == MainOps(edgeIn: 1, edgeOut: 1)) }
				#expect(conses() == base + 2)
				onAnotherThread { clj_release(v) }
				#expect(conses() == base)
			}
		}

		// A main16 reference dropped on another thread takes rc below zero and waits for main's drain, which frees.
		@Test func aMainBornReferenceDroppedElsewhereComesBack() {
			runtimeSettled("before the conses")
			let base = conses()
			asMain {
				let v = sharedPair()
				#expect(clj_debug_rc_count(v) == 1)
				let a = ops()
				onAnotherThread { clj_release(v) }
				#expect(clj_debug_rc_pending() == 1)
				#expect(conses() == base + 2)
				clj_rc_main_drain()
				#expect(clj_debug_rc_pending() == 0)
				if counted { #expect(MainOps(a, ops()) == MainOps(inPlace: 2, deferred: 1)) }
				#expect(conses() == base)
			}
		}

		// The last two references of a main-born object, one in each count: either order frees once.
		@Test(arguments: [false, true]) func theLastTwoReferencesEitherOrder(mainFirst: Bool) {
			runtimeSettled("before the conses")
			let base = conses()
			asMain {
				let v = sharedPair()
				onAnotherThread { _ = clj_retain(v) }
				#expect(clj_debug_rc_count(v) == 2)
				let a = ops()
				if mainFirst {
					clj_release(v)
					#expect(conses() == base + 2)
					if counted { #expect(MainOps(a, ops()) == MainOps(edgeOut: 1)) }
					// The other thread frees the outer cons; the inner one's reference is main16's and comes back.
					onAnotherThread { clj_release(v) }
					#expect(conses() == base + 1)
					#expect(clj_debug_rc_pending() == 1)
					clj_rc_main_drain()
					if counted { #expect(MainOps(a, ops()) == MainOps(edgeOut: 1, inPlace: 1, deferred: 1)) }
				} else {
					onAnotherThread { clj_release(v) }
					#expect(conses() == base + 2)
					clj_release(v)
					if counted { #expect(MainOps(a, ops()) == MainOps(inPlace: 2)) }
				}
				#expect(clj_debug_rc_pending() == 0)
				#expect(conses() == base)
			}
		}

		// Both references in main16, one handed over: in either order it comes back through the drain.
		@Test(arguments: [false, true]) func twoMainReferencesOneHandedOver(mainFirst: Bool) {
			runtimeSettled("before the conses")
			let base = conses()
			asMain {
				let v = sharedPair()
				_ = clj_retain(v)
				let a = ops()
				if mainFirst {
					clj_release(v)
					onAnotherThread { clj_release(v) }
				} else {
					onAnotherThread { clj_release(v) }
					clj_release(v)
				}
				#expect(clj_debug_rc_pending() == 1)
				#expect(conses() == base + 2)
				clj_rc_main_drain()
				if counted { #expect(MainOps(a, ops()) == MainOps(plain: 1, inPlace: 2, deferred: 1)) }
				#expect(conses() == base)
			}
		}

		// Without a main carrier nobody writes main16: a reference given back is taken by the releasing thread.
		@Test func afterAbandonTheReleasingThreadTakesItBack() {
			runtimeSettled("before the conses")
			let base = conses()
			var v: clj_value = CLJ_NIL
			asMain { v = sharedPair() }
			let a = ops()
			onAnotherThread { clj_release(v) }
			#expect(clj_debug_rc_pending() == 0)
			if counted { #expect(MainOps(a, ops()) == MainOps(deferred: 2)) }
			#expect(conses() == base)
		}

		// main16 at its top moves half into rc; main's releases then end the episode and go on through rc.
		@Test func anOverflowSpillsIntoRc() {
			runtimeSettled("before the conses")
			let base = conses()
			asMain {
				let v = sharedPair()
				let n = 70_000, spilled = 0x8000
				let a = ops()
				for _ in 0..<n { _ = clj_retain(v) }
				#expect(clj_debug_rc_count(v) == Int64(n + 1))
				let b = ops()
				if counted { #expect(MainOps(a, b) == MainOps(plain: n - 1, spill: 1)) }
				for _ in 0..<n { clj_release(v) }
				#expect(conses() == base + 2)
				clj_release(v)
				if counted { #expect(MainOps(b, ops()) == MainOps(plain: n - spilled, edgeOut: 1, inPlace: 1, merged: spilled)) }
				#expect(conses() == base)
			}
		}

		// On main, uniqueness reads main16 and the word: a published main-born object is unique to main.
		@Test func uniquenessOnMain() {
			asMain {
				let v = sharedPair()
				#expect(clj_is_unique(v) == clj_reuse_enabled())
				onAnotherThread { _ = clj_retain(v) }
				#expect(!clj_is_unique(v))
				onAnotherThread { clj_release(v) }
				#expect(clj_is_unique(v) == clj_reuse_enabled())
				_ = clj_retain(v)
				#expect(!clj_is_unique(v))
				clj_release(v)
				clj_release(v)
				var w: clj_value = CLJ_NIL
				onAnotherThread { w = sharedPair() }
				// main holds the pool-born object's one reference, in rc
				#expect(clj_is_unique(w) == clj_reuse_enabled())
				_ = clj_retain(w)
				#expect(!clj_is_unique(w))
				clj_release(w)
				#expect(clj_is_unique(w) == clj_reuse_enabled())
				clj_release(w)
			}
		}

		// A go-main block holds a pool value from an atom across a park while the pool drops the atom's reference.
		@Test func aPoolValueInAnAtomMainReleasesLast() throws {
			let base = CoroBaseline()
			try asMain {
				let probe = try eval("(->RcProbe nil)")
				let type = clj_type_of(probe.raw)
				let live = clj_debug_live_objects_of(type)
				_ = try eval("(def rc-a (atom nil)) (def rc-gate (chan)) (def rc-out (chan 1))")
				_ = try eval("(<!! (go (reset! rc-a (->RcProbe [1 2 3]))))")
				_ = try eval("(go-main (let [v @rc-a] (<! rc-gate) (>! rc-out (count (:k v)))))")
				clj_sched_main_pump()
				_ = try eval("(<!! (go (reset! rc-a nil)))")
				#expect(clj_debug_live_objects_of(type) == live + 1, "main still holds it")
				let a = ops()
				#expect(try eval("(offer! rc-gate :go)") == true)
				clj_sched_main_pump()
				#expect(try eval("(<!! rc-out)") == 3)
				#expect(clj_debug_live_objects_of(type) == live)
				if counted { #expect(MainOps(a, ops()).inPlace >= 1) }
				_ = try eval("(def rc-a nil) (def rc-gate nil) (def rc-out nil)")
			}
			base.check()
		}

		// A main-born value put! to a pool coroutine that drops it lives until main's turn.
		@Test func aMainValuePutToThePool() throws {
			let base = CoroBaseline()
			try asMain {
				let probe = try eval("(->RcProbe nil)")
				let type = clj_type_of(probe.raw)
				let live = clj_debug_live_objects_of(type)
				_ = try eval("(def rc-in (chan 1)) (def rc-out (chan 1))")
				_ = try eval("(go (let [v (<! rc-in)] (>! rc-out (count (:k v)))))")
				let a = ops()
				_ = try eval("(>!! rc-in (->RcProbe [1 2]))")
				#expect(try eval("(<!! rc-out)") == 2)
				#expect(awaitPending())
				#expect(clj_debug_live_objects_of(type) == live + 1, "the dropped reference waits for main")
				#expect(settledAsMain())
				#expect(clj_debug_live_objects_of(type) == live)
				if counted { #expect(MainOps(a, ops()).deferred >= 1) }
				_ = try eval("(def rc-in nil) (def rc-out nil)")
			}
			base.check()
		}

		// Main, go-main blocks and four pool coroutines pass, hold and drop the same values: each freed once, none left.
		@Test func mainAndPoolPassAndDrop() throws {
			let base = CoroBaseline()
			try asMain {
				_ = try eval("""
				(def rc-a (atom {})) (def rc-in (chan 64)) (def rc-out (chan 64))
				(def rc-workers
				  (mapv (fn [w] (go-loop [n 0]
				                  (if-let [x (<! rc-in)]
				                    (do (swap! rc-a assoc w x)
				                        (let [s @rc-a] (offer! rc-out [x (count s)]))
				                        (recur (inc n)))
				                    n)))
				        (range 4)))
				(def rc-main-step
				  (fn [i]
				    (let [v (vec (range (mod i 9)))]
				      (offer! rc-in v)
				      (swap! rc-a assoc :main v)
				      (when (zero? (mod i 3)) (go-main (let [s @rc-a] (count (get s 0)))))
				      (let [s @rc-a got (poll! rc-out)] (+ (count s) (count (first got)))))))
				""")
				let step = try eval("rc-main-step")
				let iterations = underSanitizer ? 5_000 : 50_000
				let a = ops()
				for i in 0..<iterations {
					_ = try step(Value(i))
					if i % 16 == 0 { clj_sched_main_pump() }
				}
				if counted {
					let d = MainOps(a, ops())
					#expect(d.edgeIn > 0 && d.edgeOut + d.inPlace > 0 && d.deferred > 0, "\(d)")
				}
				_ = try eval("(close! rc-in)")
				_ = try eval("(reduce + (map <!! rc-workers))")
				_ = try eval("(reset! rc-a nil) (while (poll! rc-out))")
				_ = try eval("(def rc-a nil) (def rc-in nil) (def rc-out nil) (def rc-workers nil) (def rc-main-step nil)")
				#expect(settledAsMain())
			}
			base.check()
		}
	}
}
