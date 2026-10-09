// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'seeded-tests) " + source) }

// Each run reseeds itself, so what it prints is the same under any CLJ_SCHED_SEED: make test-seeded diffs two.
extension CoreTests {
	@Suite(.enabled(if: schedulerSeed != nil)) struct SeededTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("(ns seeded-tests (:require [clojure.core.async :refer [chan go <! >! <!! timeout close! poll! to-chan! mult tap]]))")
			_ = try cljEvalScoped("""
			(in-ns 'seeded-tests)
			;; Four writers, a timeout of a random length before each write: the order is the schedule's and the clock's.
			(defn order-log []
			  (let [log (atom []) c (chan)]
			    (dotimes [i 4] (go (dotimes [j 3] (<! (timeout (rand-int 3))) (swap! log conj [i j]) (>! c [i j]))))
			    (dotimes [_ 12] (<!! c))
			    @log))
			;; The ASYNC-127 block of corpus/core-async's ops-tests, verbatim, with the corpus's three verdicts: an
			;; orphaned t-1 is its watchdog's :timeout.
			(defn async-127 []
			  (let [ch (to-chan! [1 2 3])
			        m (mult ch)
			        t-1 (chan)
			        t-2 (chan)
			        t-3 (chan)]
			    (tap m t-1)
			    (tap m t-2)
			    (tap m t-3)
			    (close! t-3)
			    (try
			      (with-deadline 1000
			        (let [r [(<!! t-1) (poll! t-1) (<!! t-2) (<!! t-1) (poll! t-1)]]
			          (if (= r [1 nil 1 2 nil]) :pass :fail)))
			      (catch :timeout e :timeout))))
			""")
		}

		// What a race leaves parked holds itself through its own frame, out of the collector's reach (NOTES "Corpus").
		private func run(_ seed: UInt64, _ form: String) throws -> Value {
			_ = clj_debug_cancel_live_coros()
			runtimeSettled("before seed \(seed)")
			clj_debug_sched_reseed(seed)
			return try eval(form)
		}

		@Test func oneSeedOneSchedule() throws {
			var schedules = Set<String>()
			for seed in UInt64(1)...16 {
				let first = try run(seed, "(order-log)").description
				let again = try run(seed, "(order-log)").description
				#expect(first == again, "seed \(seed): \(first), then \(again)")
				schedules.insert(first)
				print("seeded: order-log seed \(seed): \(first)")
			}
			#expect(schedules.count > 8, "16 seeds made \(schedules.count) schedules")
		}

		// The corpus measured 285 fails, 11 passes and 4 timeouts in 300 real runs (NOTES "Corpus").
		@Test func async127ReachesEachVerdictBySomeSeed() throws {
			var first: [String: UInt64] = [:]
			var counts: [String: Int] = [:]
			for seed in UInt64(0)..<300 {
				let verdict = try run(seed, "(async-127)").description
				counts[verdict, default: 0] += 1
				if first[verdict] == nil { first[verdict] = seed }
			}
			print("seeded: ASYNC-127 over seeds 0..<300: \(counts.sorted { $0.key < $1.key }.map { "\($0.key) \($0.value)" }.joined(separator: ", "))")
			for verdict in [":pass", ":fail", ":timeout"] {
				guard let seed = first[verdict] else {
					Issue.record("no seed of 0..<300 reached \(verdict)")
					continue
				}
				print("seeded: ASYNC-127 seed \(seed): \(verdict)")
				for _ in 0..<3 { #expect(try run(seed, "(async-127)").description == verdict, "seed \(seed) replayed") }
			}
		}
	}
}
