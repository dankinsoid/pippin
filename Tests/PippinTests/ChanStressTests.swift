// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval("(in-ns 'chan-tests) " + source) }

extension CoreTests {
	@Suite struct ChanStressTests {
		init() throws {
			clj_init()
			_ = try cljEval("(ns chan-tests (:require [clojure.core.async :refer [chan <! >! <!! >!! timeout go thread]]))")
		}

		@Test func stressSpawn() throws {
			let coros = clj_debug_live_coros()
			#expect(try eval("(let [done (chan 100000)] (dotimes [i 100000] (go (>! done i))) (dotimes [i 100000] (<!! done)) 1)") == 1)
			#expect(clj_debug_coro_settle(coros, 5000))
		}

		@Test func stressBenchShapes() throws {
			_ = try cljEval("(in-ns 'chan-tests) (require '[clojure.core.async :refer [go-loop alts! close!]])")
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
		// A few rounds past 10 ms are the OS scheduling the woken thread late on a loaded machine.
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
			#expect(late <= 3, "\(late) rounds past 10 ms, the slowest \(slowest) ns")
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
