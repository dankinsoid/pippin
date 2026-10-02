// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'chan-tests) " + source) }

extension CoreTests {
	@Suite struct ChanStressTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("(ns chan-tests (:require [clojure.core.async :as a :refer [chan <! >! <!! >!! timeout go thread]]))")
		}

		// Each alts! leaves a stale node in the port it lost, which must not hold the port it won (parked_result).
		// @ai-generated(solo)
		@Test func racingAltsPuttersLeaveNoChannelCycle() throws {
			let race = "(let [c (chan) d (chan) g1 (go (first (a/alts! [[c 1] d]))) g2 (go (first (a/alts! [[c 2] d])))] (<!! (timeout 1)) (let [got (<!! c)] (>!! d :d) (count (set [(<!! g1) (<!! g2) got]))))"
			// The first run interns what the form names.
			#expect(try eval(race) == 3)
			let base = CoroBaseline()
			for _ in 0..<300 { #expect(try eval(race) == 3) }
			base.check()
		}

		// Every idle pool thread takes a body that waits for the last one, which needs a thread of its own.
		// @ai-generated(solo)
		@Test func aThreadBodyNeverQueuesBehindBodiesWaitingForIt() throws {
			_ = try eval("(let [gate (chan)] (dotimes [_ 4] (thread (<!! gate))) (<!! (timeout 50)) (a/close! gate))")
			runtimeSettled("before the pool is counted")
			let idle = clj_debug_blocking_threads()
			#expect(idle >= 4)
			let run = try eval("""
				(let [c (chan) done (chan 1)]
				  (dotimes [_ \(idle)] (thread (<!! c)))
				  (thread (dotimes [_ \(idle)] (>!! c 1)) (>!! done :done))
				  [c done])
				""")
			// Polled rather than raced against a timeout, whose timer would outlive the test.
			let poll = try eval("(fn [[_ done]] (a/poll! done))")
			var finished = false
			for _ in 0..<1000 where !finished {
				finished = try poll.apply([run]) == Value(keyword: "done")
				if !finished { usleep(10_000) }
			}
			_ = try eval("(fn [[c _]] (a/close! c))").apply([run])
			#expect(finished)
		}

		// Past the 64 threads internal jobs are capped at: 100 bodies, each waiting on the one spawned after it.
		// @ai-generated(solo)
		@Test func threadBodiesNeverQueue() throws {
			let n = 100
			let run = try eval("""
				(let [cs (vec (repeatedly \(n + 1) chan))]
				  (dotimes [i \(n)]
				    (if (= i \(n - 1))
				      (thread (>!! (cs i) 0))
				      (thread (>!! (cs i) (inc (<!! (cs (inc i))))))))
				  cs)
				""")
			let poll = try eval("(fn [cs] (a/poll! (cs 0)))")
			var got: Value = nil
			for _ in 0..<1000 where got == nil {
				got = try poll.apply([run])
				if got == nil { usleep(10_000) }
			}
			#expect(got == Value(n - 1))
		}

		// An idle pool thread exits after the keep-alive; a binding frame conveyed past that exit stays its body's,
		// though the next thread's execution likely sits at the same address.
		// @ai-generated(solo)
		@Test func idlePoolThreadsRetire() throws {
			_ = try eval("(def ^:dynamic *conveyed* :root)")
			clj_debug_blocking_keep_alive_ms(50)
			defer { clj_debug_blocking_keep_alive_ms(0) }
			func threads(_ ok: (Int) -> Bool) -> Bool {
				for _ in 0..<500 {
					if ok(clj_debug_blocking_threads()) { return true }
					usleep(10_000)
				}
				return false
			}
			_ = try eval("(let [cs (vec (repeatedly 8 chan))] (doseq [c cs] (thread (<!! c))) (doseq [c cs] (>!! c 1)))")
			#expect(threads { $0 == 0 })
			let k = 16
			let run = try eval("""
				(let [hold (chan) gate (chan) out (chan \(k))]
				  (dotimes [_ \(k)]
				    (thread (binding [*conveyed* 1]
				              (thread (<!! gate)
				                      (>!! out (<!! (thread (try (set! *conveyed* 2) :set (catch :default e :refused))))))
				              (<!! hold))))
				  [gate out hold])
				""")
			#expect(threads { $0 == 2 * k })
			// The binders' threads go, the ones holding their frames wait on the gate.
			_ = try eval("(fn [[_ _ hold]] (a/close! hold))").apply([run])
			#expect(threads { $0 == k })
			// A retired thread is uncounted before its exit frees the execution whose address the next one may take.
			usleep(100_000)
			_ = try eval("(fn [[gate _ _]] (a/close! gate))").apply([run])
			let poll = try eval("(fn [[_ out _]] (a/poll! out))")
			var got: [Value] = []
			for _ in 0..<1000 where got.count < k {
				let v = try poll.apply([run])
				if v == nil { usleep(10_000) } else { got.append(v) }
			}
			#expect(got == Array(repeating: Value(keyword: "refused"), count: k))
			// Each batch's threads open a dispatch window (the deref) and retire; the next batch takes their readers.
			let batch = "(let [a (atom 0) cs (vec (repeatedly 8 chan))] (doseq [c cs] (thread (<!! c) @a)) (doseq [c cs] (>!! c 1)))"
			var readers = 0
			for round in 0..<4 {
				_ = try eval(batch)
				#expect(threads { $0 == 0 })
				usleep(100_000)
				if round == 0 { readers = clj_debug_proto_readers() }
			}
			#expect(clj_debug_proto_readers() < readers + 8)
		}

		// Each spawn's share check meets the previous go's channel while that go's finish releases its fn (NOTES "RC").
		// @ai-generated(solo)
		@Test func theShareCheckStopsAtAFinishingCoroutine() throws {
			let base = CoroBaseline()
			clj_debug_share_check_every(1)
			defer { clj_debug_share_check_every(0) }
			#expect(try eval("(loop [i 0 prev (go 0)] (if (< i 20000) (recur (inc i) (go (+ i (count [prev])))) (<!! prev)))") == 20000)
			base.check()
		}

		@Test func stressSpawn() throws {
			let coros = clj_debug_live_coros()
			#expect(try eval("(let [done (chan 100000)] (dotimes [i 100000] (go (>! done i))) (dotimes [i 100000] (<!! done)) 1)") == 1)
			#expect(clj_debug_coro_settle(coros, 5000))
		}

		@Test func stressBenchShapes() throws {
			_ = try cljEvalScoped("(in-ns 'chan-tests) (require '[clojure.core.async :refer [go-loop alts! close!]])")
			let shapes = [
				("ping-pong", """
				(fn [n]
				  (let [ping (chan) pong (chan)
				        p (go-loop [i 0] (when (< i n) (>! ping i) (<! pong) (recur (inc i))))
				        q (go-loop [] (when-let [v (<! ping)] (>! pong v) (recur)))]
				    (<!! p) (close! ping) (<!! q) n))
				"""),
				("buffered", """
				(fn [n]
				  (let [c (chan 1024)
				        p (go (dotimes [i n] (>! c i)) (close! c))
				        q (go-loop [s 0] (if-let [v (<! c)] (recur (+ s v)) s))]
				    (<!! p) (<!! q)))
				"""),
				("alts", """
				(fn [n]
				  (let [a (chan) b (chan)
				        p (go (dotimes [i n] (if (even? i) (>! a i) (>! b i))) (close! a) (close! b))
				        q (go-loop [s 0 open 2] (if (pos? open) (let [[v _] (alts! [a b])] (if (nil? v) (recur s (dec open)) (recur (+ s v) open))) s))]
				    (<!! p) (<!! q)))
				"""),
				("timeout", "(fn [n] (<!! (go (dotimes [i n] (<! (timeout 0))) n)))"),
			]
			for (name, src) in shapes {
				let coros = clj_debug_live_coros()
				let f = try eval(src)
				for _ in 0..<2 {
					var arg = clj_fixnum(name == "timeout" ? 1000 : 20000)
					let r = withUnsafePointer(to: &arg) { clj_invoke(f.raw, $0, 1) }
					#expect(r != CLJ_THROWN)
					clj_release(r)
				}
				#expect(clj_debug_coro_settle(coros, 5000), "\(name)")
			}
		}

		// A lost wakeup shows as a hang: a go from outside the pool while every carrier sleeps must wake one.
		// A few rounds past 10 ms are the OS scheduling the woken thread late on a loaded machine: up to 1 %, as on
		// the 4-core CI runner under ASan (4 rounds, the slowest 67 ms).
		@Test func goFromMainWithAColdPoolRunsAtOnce() throws {
			let f = try eval("(fn [] (<!! (go 1)))")
			let carriers = clj_debug_sched_carriers()
			var cold = 0, late = 0
			var slowest: UInt64 = 0
			for _ in 0..<1000 {
				var waited = 0
				while clj_debug_sched_sleeping() < carriers && waited < 50_000 {
					usleep(100)
					waited += 100
				}
				if clj_debug_sched_sleeping() == carriers { cold += 1 }
				let done = DispatchSemaphore(value: 0), gone = DispatchSemaphore(value: 0)
				let watchdog = Thread {
					if done.wait(timeout: .now() + .milliseconds(10)) == .timedOut {
						clj_debug_sched_dump()
						if done.wait(timeout: .now() + .seconds(5)) == .timedOut {
							FileHandle.standardError.write(Data("go from main with a cold pool never ran: a lost wakeup\n".utf8))
							exit(3)
						}
					}
					gone.signal()
				}
				watchdog.start()
				let t0 = DispatchTime.now().uptimeNanoseconds
				let r = clj_invoke(f.raw, nil, 0)
				let took = DispatchTime.now().uptimeNanoseconds - t0
				done.signal()
				gone.wait()
				#expect(r == clj_fixnum(1))
				clj_release(r)
				slowest = max(slowest, took)
				if took > 10_000_000 { late += 1 }
			}
			#expect(late <= 10, "\(late) rounds past 10 ms, the slowest \(slowest) ns")
			#expect(cold > 900, "the pool went cold in \(cold) of 1000 rounds")
		}

		@Test func stressNested() throws {
			let exprs = ["(<!! (go (<! (go (<! (go 3))))))", "(<!! (thread (+ 1 2)))", "(let [c (chan)] (thread (>!! c :from-thread)) (<!! c))", "(<!! (go nil))", "(let [g (go :early)] (<!! (timeout 5)) [(<!! g) (<!! g)])"]
			for e in exprs {
				let coros = clj_debug_live_coros()
				for _ in 0..<100 { _ = try eval(e) }
				#expect(clj_debug_coro_settle(coros, 3000), "\(e)")
			}
		}
	}
}
