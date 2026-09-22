// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'coro-tests) " + source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalErrorScoped("(in-ns 'coro-tests) " + source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

nonisolated(unsafe) private var uncaughtReports = 0

extension CoreTests {
	@Suite struct CoroTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("(ns coro-tests (:require [clojure.core.async :refer [chan <! >! <!! >!! close! timeout go go-main go-loop thread alts! alts!! cancel! cancelled? suspend! resume! suspended?]]))")
			for k in ["main", "pool", "affinity", "a", "b", "done", "x", "from-bare", "from-coro", "ran", "v", "from-run-loop", "twice", "took"] { _ = kw(k) }
			_ = try cljEvalScoped("(in-ns 'coro-tests) (declare parked-gate parked-done main-out main-ui main-in loop-out suspended-g)")
			// A timer still holding its guard channel would fail the next suite's live-object baseline.
			_ = try cljEvalScoped("(in-ns 'coro-tests) (defn joined [ch ms] (let [t (timeout ms) v (first (alts!! [ch t]))] (<!! t) v))")
		}

		// The switch is the hand-written asm: ~20 instructions each way, so a round trip is tens of nanoseconds.
		@Test func switchCost() throws {
			let base = CoroBaseline()
			do {
				let ns = clj_bench_switch_ns(200_000)
				#expect(ns < 200, "\(ns) ns per switch")
			}
			base.check()
		}

		// 10k parked coroutines: virtual is the reserve, physical the pages each one touched (one or two). Ten
		// gates, since a channel takes at most 1024 pending takes.
		@Test func tenThousandParked() throws {
			let base = CoroBaseline()
			do {
				let before = clj_debug_phys_footprint()
				_ = try eval("(def parked-gate (vec (repeatedly 10 chan))) (def parked-done (chan 10000))")
				_ = try eval("(dotimes [i 10000] (let [g (nth parked-gate (mod i 10))] (go (<! g) (>! parked-done i))))")
				let gates = try eval("parked-gate")
				func pending() -> UInt32 { (0..<10).reduce(0) { $0 + clj_debug_chan_pending(clj_vector_nth(gates.raw, UInt32($1)), false) } }
				var waited = 0
				while pending() < 10000 && waited < 5000 {
					usleep(1000)
					waited += 1
				}
				#expect(pending() == 10000)
				let after = clj_debug_phys_footprint()
				let perCoro = (after - before) / 10000
				#expect(perCoro < 64 * 1024, "\(perCoro) bytes physical per parked coroutine (\(clj_coro_stack_size() / 1024) KB reserved each)")
				_ = try eval("(doseq [g parked-gate] (close! g)) (dotimes [i 10000] (<!! parked-done))")
				_ = try eval("(def parked-gate nil) (def parked-done nil)")
			}
			base.check()
		}

		@Test func goFromABareThreadAndFromACoroutine() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(<!! (go :from-bare))") == kw("from-bare"))
				#expect(try eval("(<!! (go (<! (go :from-coro))))") == kw("from-coro"))
				// From a plain pthread with no runtime state of its own.
				let t = Thread {
					let v = try? cljEvalScoped("(clojure.core.async/<!! (clojure.core.async/go 7))")
					#expect(v == 7)
				}
				t.start()
				while !t.isFinished { usleep(500) }
			}
			base.check()
		}

		// The main carrier: adopted by the test thread, pumped by hand; a :main atom refuses the pool.
		@Test func mainAffinity() throws {
			let base = CoroBaseline()
			do {
				#expect(message("(go-main 1)") == "No main carrier: the host has not installed one (clj_sched_main_install)")
				clj_debug_sched_main_adopt()
				defer { clj_debug_sched_main_abandon() }
				_ = try eval("(def main-out (chan 1)) (def main-ui (atom 0 :affinity :main))")
				_ = try eval("(go-main (swap! main-ui inc) (>! main-out :ran))")
				#expect(try eval("(clojure.core.async/poll! main-out)") == nil)
				clj_sched_main_pump()
				#expect(try eval("(<!! main-out)") == kw("ran"))
				#expect(try eval("@main-ui") == 1)
				// A pool coroutine touching the main atom: an error with a trace, not a silent race.
				#expect(try eval("(<!! (go (try (swap! main-ui inc) (catch :default e (ex-message e)))))") == "swap! on an atom with :affinity :main from off the main carrier")
				#expect(try eval("(<!! (go (try @main-ui (catch :default e (ex-message e)))))") == "deref on an atom with :affinity :main from off the main carrier")
				// A main coroutine parks and resumes on the main carrier only.
				_ = try eval("(def main-in (chan))")
				_ = try eval("(go-main (>! main-out (<! main-in)))")
				clj_sched_main_pump()
				#expect(try eval("(clojure.core.async/offer! main-in :v)") == true)
				#expect(try eval("(clojure.core.async/poll! main-out)") == nil)
				clj_sched_main_pump()
				#expect(try eval("(<!! main-out)") == kw("v"))
				_ = try eval("(def main-out nil) (def main-ui nil) (def main-in nil)")
			}
			base.check()
		}

		// The real source on the main thread's run loop: the test runs on the main actor and turns the loop.
		@Test @MainActor func mainRunLoopSource() throws {
			try #require(Thread.isMainThread)
			let base = CoroBaseline()
			do {
				clj_sched_main_install()
				defer { clj_debug_sched_main_abandon() }
				_ = try eval("(def loop-out (chan 1))")
				_ = try eval("(go (>! loop-out (<! (go-main :from-run-loop))))")
				var turns = 0
				while try eval("(clojure.core.async/poll! loop-out)") == nil && turns < 200 {
					CFRunLoopRunInMode(CFRunLoopMode.defaultMode, 0.01, true)
					turns += 1
				}
				#expect(turns < 200)
				_ = try eval("(def loop-out nil)")
			}
			base.check()
		}

		@Test func parkUnderARuntimeLockIsAnError() throws {
			let base = CoroBaseline()
			do {
				// A lazy seq's thunk parks legally (the forcing lock is a coroutine mutex).
				#expect(try eval("(let [c (chan 1)] (>!! c 5) (<!! (go (first (lazy-seq [(<! c)])))))") == 5)
				#expect(clj_debug_park_under_lock_is_error())
			}
			base.check()
		}

		@Test func deadlineIsPerCoroutine() throws {
			let base = CoroBaseline()
			do {
				// A deadline set on the spawner is conveyed; the spawner's own clock is untouched by the child.
				clj_deadline_set_ms(200)
				defer { clj_deadline_set_ms(0) }
				#expect(try eval("(<!! (go (try (loop [i 0] (recur (inc i))) (catch :cancelled e (ex-message e)))))") == "Execution timed out")
				clj_deadline_set_ms(0)
				#expect(try eval("(<!! (go (loop [i 0] (if (< i 100000) (recur (inc i)) i))))") == 100000)
			}
			base.check()
		}

		// Every cancellation path is :cancelled and :default lets it by (design §4, NReplTests proves interrupt).
		@Test func everyCancellationIsCancelledType() throws {
			// Warms whatever this scenario interns on first use, ahead of the baseline (RecordTests does the
			// same by defining its record first).
			_ = try eval("(let [g (go (try (loop [i 0] (recur (inc i))) (catch :default e :caught)))] (<!! (timeout 5)) (cancel! g) (<!! g))")
			let base = CoroBaseline()
			do {
				// Explicit cancel, no channel involved: eval.c's loop-tick check.
				#expect(try eval("""
					(let [g (go (try (loop [i 0] (recur (inc i)))
					                  (catch :cancelled e [(ex-type e) (:cancel/kind (ex-data e))])))]
					  (<!! (timeout 5)) (cancel! g) (<!! g))
					""") == [kw("cancelled"), kw("explicit")])
				#expect(try eval("(let [g (go (try (loop [i 0] (recur (inc i))) (catch :default e :caught)))] (<!! (timeout 5)) (cancel! g) (<!! g))") == nil)
				// A cancelled channel operation: chan.c's own park-point check, not the loop-tick.
				#expect(try eval("""
					(let [c (chan) g (go (try (<! c) (catch :cancelled e [(ex-type e) (:cancel/kind (ex-data e))])))]
					  (<!! (timeout 5)) (cancel! g) (<!! g))
					""") == [kw("cancelled"), kw("explicit")])
				#expect(try eval("(let [c (chan) g (go (try (<! c) (catch :default e :caught)))] (<!! (timeout 5)) (cancel! g) (<!! g))") == nil)
				// The deadline: same :cancelled type, :cancel/kind :deadline instead.
				clj_deadline_set_ms(50)
				#expect(try eval("(<!! (go (try (loop [i 0] (recur (inc i))) (catch :cancelled e [(ex-type e) (:cancel/kind (ex-data e))]))))")
					== [kw("cancelled"), kw("deadline")])
				clj_deadline_set_ms(50)
				#expect(try eval("(<!! (go (try (loop [i 0] (recur (inc i))) (catch :default e :caught))))") == nil)
				clj_deadline_set_ms(0)
				// (cancelled?) polls the same flag without waiting for a park point or a loop-tick throw.
				#expect(try eval("(let [g (go (loop [n 0] (if (cancelled?) n (recur (inc n)))))] (<!! (timeout 5)) (cancel! g) (int? (<!! g)))") == true)
			}
			base.check()
		}

		// suspend! rides the same poisoned deadline a cancellation does, but the tick parks on a gate instead of
		// throwing, and resume! puts the body back where it stood (design §4, "Стек как объект", item 3).
		@Test func suspendParksTheBodyAndResumeLetsItOn() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("""
					(let [r (atom 0)
					      g (go (try (loop [] (swap! r inc) (recur)) (catch :cancelled e :done)))]
					  (<!! (timeout 10))
					  (let [asked (suspend! g)]
					    (<!! (timeout 10))
					    (let [a @r]
					      (<!! (timeout 10))
					      (let [b @r
					            gated (suspended? g)
					            lifted (resume! g)]
					        (<!! (timeout 10))
					        (let [c @r]
					          (cancel! g)
					          [asked gated (= a b) lifted (suspended? g) (> c b)
					           (joined g 200)])))))
					""") == [true, true, true, true, false, true, kw("done")])
			}
			base.check()
		}

		// The precondition of design §4: a suspension is legal only where nothing is held. A body spinning inside
		// locking and swap! leaves both before it parks, so another coroutine takes the same monitor and atom while
		// it sits on the gate — a park under the cmutex would hold them until resume!.
		@Test func aSuspendedBodyHoldsNoMutex() throws {
			let base = CoroBaseline()
			do {
				let got = try eval("""
					(let [r (atom 0)
					      g (go (try (loop []
					                   (locking r (swap! r (fn [v] (loop [i 0] (if (< i 4000) (recur (inc i)) (inc v))))))
					                   (recur))
					                 (catch :cancelled e :done)))]
					  (<!! (timeout 20))
					  (suspend! g)
					  (<!! (timeout 20))
					  (let [a @r
					        took (joined (go (locking r (swap! r identity)) :took) 200)]
					    (cancel! g)
					    [took (= a @r) (joined g 200)]))
					""")
				#expect(got == [kw("took"), true, kw("done")], "\(got)")
			}
			base.check()
		}

		// A suspended coroutine is not a leak: a cancel! releases the gate and the tick it wakes into throws once,
		// and neither the catch nor the finally parks on the gate again on the way out.
		@Test func aCancelReachesASuspendedCoroutine() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("""
					(let [n (atom 0)
					      g (go (try (loop [] (recur))
					                 (catch :cancelled e (swap! n inc) :done)
					                 (finally (swap! n inc))))]
					  (<!! (timeout 10))
					  (suspend! g)
					  (<!! (timeout 10))
					  [(cancel! g) (joined g 200) @n (suspended? g) (resume! g)])
					""") == [true, kw("done"), 2, false, false])
				// A channel with no body, and a body that has finished, take no request.
				#expect(try eval("(let [c (chan) g (go 1)] (<!! g) [(suspend! c) (resume! c) (suspended? c) (suspend! g) (suspended? g)])")
					== [false, false, false, false, false])
			}
			base.check()
		}

		// A request that lands on a coroutine parked on a channel leaves it parked — it is already not running —
		// and is met at the first tick after its wake.
		@Test func aSuspendOfAParkedCoroutineIsMetWhenItWakes() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("""
					(let [c (chan) r (atom 0)
					      g (go (try (<! c) (loop [] (swap! r inc) (recur)) (catch :cancelled e :done)))]
					  (<!! (timeout 10))
					  (let [asked (suspend! g)]
					    (>!! c 1)
					    (<!! (timeout 20))
					    (let [a @r]
					      (<!! (timeout 10))
					      (let [b @r]
					        (resume! g)
					        (<!! (timeout 10))
					        (let [d @r]
					          (cancel! g)
					          [asked (= a b) (> d b) (joined g 200)])))))
					""") == [true, true, true, kw("done")])
			}
			base.check()
		}

		// The deadline the suspension's poison replaced comes back with the resume: the body still dies at its own
		// deadline, and a lost one would spin for ever instead.
		@Test func aDeadlineOutlivesASuspension() throws {
			let base = CoroBaseline()
			do {
				clj_deadline_set_ms(300)
				_ = try eval("(def suspended-g (go (try (loop [] (recur)) (catch :cancelled e (:cancel/kind (ex-data e))))))")
				clj_deadline_set_ms(0)
				#expect(try eval("""
					(do (<!! (timeout 20))
					    (let [asked (suspend! suspended-g)]
					      (<!! (timeout 20))
					      [asked (resume! suspended-g) (joined suspended-g 1000)]))
					""") == [true, true, kw("deadline")])
				_ = try eval("(def suspended-g nil)")
			}
			base.check()
		}

		// Two plain cancellations are one immortal value, so the throw is a pointer store (design §4).
		@Test func plainCancellationsAreOneValue() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("""
					(let [spin (fn [] (go (try (loop [i 0] (recur (inc i))) (catch :cancelled e e))))
					      a (spin) b (spin)]
					  (<!! (timeout 5))
					  (cancel! a) (cancel! b)
					  (identical? (<!! a) (<!! b)))
					""") == true)
			}
			base.check()
		}

		// An uncaught cancellation ends a coroutine normally: no uncaught-handler call at all (design §4).
		@Test func uncaughtCancellationIsNotAFailure() throws {
			let base = CoroBaseline()
			do {
				uncaughtReports = 0
				clj_coro_set_uncaught_handler { _, _ in uncaughtReports += 1 }
				defer { clj_coro_set_uncaught_handler(nil) }
				#expect(try eval("(let [g (go (loop [i 0] (recur (inc i))))] (<!! (timeout 5)) (cancel! g) (<!! g))") == nil)
				#expect(uncaughtReports == 0)
				// A genuine error still reports, so the handler above is proven live, not merely unwired.
				#expect(try eval("(<!! (go (throw (ex-info \"boom\" {}))))") == nil)
				#expect(uncaughtReports == 1)
			}
			base.check()
		}
	}
}
