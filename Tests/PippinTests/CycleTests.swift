// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// The cycle collector (design §7, «Сборщик циклов: как он устроен»; cc.c).

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'cycle-tests) " + source) }

private func kw(_ s: String) -> Value { Value(keyword: s) }

private func stat(_ i: Int) -> Int64 {
	var s = [Int64](repeating: 0, count: CLJ_CC_STAT_COUNT)
	clj_debug_cc_stats(&s)
	return s[i]
}

private func freedStat() -> Int64 { stat(CLJ_CC_STAT_FREED) }

extension CoreTests {
	@Suite struct CycleTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("""
			(ns cycle-tests (:require [clojure.core.async :as a]))
			(deftype Node [cell])
			(def rooted nil)
			(defn ring [n] (let [x (atom nil)] (reset! x {:n n :back (fn [] x)}) nil))
			""")
			for k in ["n", "back", "k", "self", "j", "x", "next", "v", "leaf", "shared"] { _ = kw(k) }
		}

		// Each form leaves only a cycle behind: RC alone keeps it, so the baseline returning is the collector's work.
		private func collected(_ form: String, atLeast: Int64, _ location: SourceLocation = #_sourceLocation) throws {
			let base = CoroBaseline(location)
			let freed = freedStat()
			_ = try eval(form)
			clj_cc_collect()
			base.check(location)
			#expect(freedStat() - freed >= atLeast, sourceLocation: location)
		}

		@Test func aSelfReferencingAtom() throws {
			try collected("(let [a (atom nil)] (reset! a a) nil)", atLeast: 1)
		}

		// The cycle is garbage when the last outside reference to the map goes; the atom is never decremented then.
		@Test func anAtomMapClosureRing() throws {
			try collected("(let [a (atom nil) m {:k (fn [] @a)}] (reset! a m) nil)", atLeast: 3)
			try collected("(dotimes [i 300] (ring i))", atLeast: 900)
		}

		// A deftype has no mutable fields (analyzer.c, set!): its cycle closes through a volatile in a field.
		@Test func aCycleThroughADeftype() throws {
			try collected("(let [v (volatile! nil) n (Node. v)] (vreset! v n) nil)", atLeast: 2)
			try collected("(let [v (volatile! nil) n (Node. v) a (atom n)] (vreset! v n) nil)", atLeast: 3)
			try collected("(let [v (volatile! nil)] (vreset! v (Node. (Node. v))) nil)", atLeast: 3)
		}

		// A var is immortal, so no cycle runs through one: a ring hangs off a var's root and the rebind lets it go.
		@Test func aRingRootedAtAVarAfterItsRebind() throws {
			_ = try eval("(def rooted (let [x (atom nil)] (reset! x {:back (fn [] x)}) x))")
			let base = CoroBaseline()
			let freed = freedStat()
			_ = try eval("(def rooted nil)")
			clj_cc_collect()
			#expect(freedStat() - freed >= 3)
			base.check()
		}

		@Test func aCycleThroughAChannelBuffer() throws {
			try collected("(let [c (a/chan 1)] (a/>!! c c) nil)", atLeast: 1)
			try collected("(let [c (a/chan 4)] (a/>!! c {:k [c]}) (a/>!! c 1) nil)", atLeast: 3)
		}

		// letfn's cells are volatiles holding the fns that deref them: a cycle per call (NOTES "Corpus").
		@Test func letfnCells() throws {
			try collected("(dotimes [i 1000] (letfn [(f [n] (if (pos? n) (g (dec n)) n)) (g [n] (f n))] (f 3)))", atLeast: 2000)
		}

		// A ring over a DAG whose vector is shared fifty times and held from Swift: the ring goes, the DAG stays.
		@Test func aDagWithHeavySharing() throws {
			let base = CoroBaseline()
			let freed = freedStat()
			var mid: Value? = try eval("""
			(let [leaf {:leaf (atom 1)}
			      mid (vec (repeat 1000 leaf))
			      a (atom nil)]
			  (reset! a (vec (for [i (range 50)] {:k i :shared mid :back (fn [] a)})))
			  mid)
			""")
			clj_cc_collect()
			#expect(freedStat() - freed >= 102)
			#expect(mid!.array?.count == 1000)
			#expect(try eval("(fn [m] (reduce + (map (comp deref :leaf) m)))")(mid!) == 1000)
			let held = clj_debug_live_objects()
			for _ in 0..<3 {
				_ = Value(owning: clj_retain(mid!.raw))
				clj_cc_collect()
			}
			#expect(clj_debug_live_objects() == held)
			mid = nil
			base.check()
		}

		// Mutators touching a collection's nodes mid-walk put them back in the buffer, never on the free list.
		@Test func sharedCyclesFromManyThreads() throws {
			try collected("""
			(let [fs (doall (for [i (range 16)]
			                  (future (dotimes [j 500]
			                            (let [x (atom nil)] (reset! x {:x x :j j}))))))]
			  (run! deref fs)
			  nil)
			""", atLeast: 16 * 500)
			try collected("""
			(let [st (atom {})
			      fs (doall (for [i (range 8)]
			                  (future (dotimes [j 1000]
			                            (let [h (fn [] @st)]
			                              (swap! st assoc (mod j 50) h)
			                              (swap! st dissoc (mod (+ j 7) 50)))))))]
			  (run! deref fs)
			  (reset! st nil)
			  nil)
			""", atLeast: 1)
			try collected("""
			(let [c (a/chan 64)
			      fs (doall (for [i (range 8)]
			                  (future (dotimes [j 500]
			                            (let [x (atom nil)] (reset! x [x]) (a/>!! c x) (a/<!! c))))))]
			  (run! deref fs)
			  nil)
			""", atLeast: 8 * 500)
		}

		// The main carrier hands candidates past its bound to the background and collects the rest when idle.
		@Test func theMainCarrierHandsOffAndCollectsWhenIdle() throws {
			let base = CoroBaseline()
			let handoffs = stat(CLJ_CC_STAT_HANDOFFS)
			clj_debug_cc_as_main(1)
			defer { clj_debug_cc_as_main(0) }
			_ = try eval("(dotimes [i 5000] (let [v (volatile! nil)] (vreset! v [v i])))")
			#expect(clj_debug_cc_pending_local() == 4096)
			#expect(stat(CLJ_CC_STAT_HANDOFFS) - handoffs >= 5000 - 4096)
			var rounds = 0
			while clj_debug_cc_pending_local() > 0 && rounds < 1000 {
				#expect(clj_debug_cc_main_idle() > 0)
				rounds += 1
			}
			#expect(clj_debug_cc_pending_local() == 0)
			base.check()
		}

		// A channel's finalize frees the ring its each_child reads: within a cycle, children go before finalizers.
		@Test func membersWithFinalizers() throws {
			try collected("(dotimes [i 100] (let [c (a/chan 2) d (a/chan 2)] (a/>!! c d) (a/>!! d c) (a/>!! c (atom c))))", atLeast: 300)
		}
	}
}
