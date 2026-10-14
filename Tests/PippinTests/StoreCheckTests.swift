// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// The cycle check at a store (design §7, «Проверка цикла в момент записи»): a missed flag leaks, and the audit fails.

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'store-check-tests) " + source) }

private func stat(_ i: Int) -> Int64 {
	var s = [Int64](repeating: 0, count: CLJ_CC_STAT_COUNT)
	clj_debug_cc_stats(&s)
	return s[i]
}

private func bindNative(_ name: String, arity: UInt32, _ fn: clj_native_fn) {
	let ns = Value(symbol: "store-check-tests"), sym = Value(symbol: name)
	let f = Value(owning: clj_fn_native(sym.raw, fn, arity, arity))
	withExtendedLifetime((ns, sym, f)) { clj_var_bind_root(clj_ns_intern(clj_ns_find_or_create(ns.raw), sym.raw), f.raw) }
}

// In CycleTests, not a suite of its own: a new suite moves the committed shard deal (scripts/test-times.json).
extension CoreTests.CycleTests {
	private func prepared() throws {
		clj_debug_cc_audit_every(1)
		_ = try cljEvalScoped("(ns store-check-tests)")
		bindNative("cc-collect!", arity: 0) { _, _ in clj_fixnum(Int(clj_debug_cc_collect_local())) }
		bindNative("on-cycle?", arity: 1) { args, _ in clj_bool(clj_debug_cc_on_cycle(args![0]) != 0) }
		_ = try cljEvalScoped("(in-ns 'store-check-tests) (deftype Leaf [cell])")
		for k in ["a", "self", "on-click", "n", "v", "k"] { _ = Value(keyword: k) }
	}

	// The form drops at least `cycles` cycles; its value, released after the form, holds the member that goes last.
	private func dropped(_ form: String, cycles: Int64 = 1, _ location: SourceLocation = #_sourceLocation) throws {
		let base = CoroBaseline(location)
		let freed = stat(CLJ_CC_STAT_FREED)
		_ = try eval(form)
		clj_cc_collect()
		base.check(location)
		#expect(stat(CLJ_CC_STAT_FREED) - freed >= cycles, "\(form)", sourceLocation: location)
		clj_debug_cc_audit_every(0)
	}

	@Test func selfEdges() throws {
		try prepared()
		try dropped("(let [v (volatile! nil)] (vreset! v v) [v])")
		try dropped("(let [a (object-array 1)] (aset a 0 a) [a])")
		try dropped("(let [v (volatile! nil)] (vreset! v [v]) [v])")
		try dropped("(let [v (volatile! nil) x [v]] (vreset! v x) [x])")
	}

	// Two reference types: either store closes the ring, either member is released last.
	@Test func twoMutablesInEveryOrder() throws {
		try prepared()
		for (first, second) in [("(vreset! a b)", "(vreset! b a)"), ("(vreset! b a)", "(vreset! a b)")] {
			for last in ["a", "b"] {
				try dropped("(let [a (volatile! nil) b (volatile! nil)] \(first) \(second) [\(last)])")
			}
		}
		for last in ["o", "v"] {
			try dropped("(let [o (object-array 1) v (volatile! nil)] (aset o 0 v) (vreset! v o) [\(last)])")
			try dropped("(let [o (object-array 1) v (volatile! nil)] (vreset! v o) (aset o 0 v) [\(last)])")
		}
	}

	// design §7, «Метка «ацикличен»», «Отвергнуто»: the rings the age rule leaves without a candidate.
	@Test func theAgeRuleCounterexamples() throws {
		try prepared()
		for last in ["l", "x", "a"] {
			try dropped("(let [a (object-array 1) l (Leaf. a) x [l]] (aset a 0 x) [\(last)])")
		}
		for last in ["p", "w", "u", "v", "z"] {
			try dropped("(let [w (object-array 1) p [w] u (object-array 1) v [p] z [u]] (aset w 0 z) (aset u 0 v) [\(last)])")
			try dropped("(let [w (object-array 1) p [w] u (object-array 1) v [p] z [u]] (aset u 0 v) (aset w 0 z) [\(last)])")
		}
		#expect(try eval("""
		(let [w (object-array 1) p [w] u (object-array 1) v [p] z [u]]
		  (aset w 0 z) (aset u 0 v)
		  (every? on-cycle? [w p u v z]))
		""") == Value(true))
	}

	// An edit in place at rc 1 is a store: the volatile's map takes the volatile back.
	@Test func inPlaceEdits() throws {
		try prepared()
		try dropped("(let [v (volatile! (hash-map :a 1))] (vswap! v assoc :self v) [v])")
		try dropped("(let [v (volatile! (hash-map :a 1))] (vswap! v assoc :self v) [@v])")
		try dropped("(let [v (volatile! [])] (vswap! v conj v) [@v])")
		try dropped("(let [v (volatile! {})] (vswap! v assoc :on-click #(vswap! v update :n (fnil inc 0))) ((:on-click @v)) [(:on-click @v)])")
	}

	@Test func transients() throws {
		try prepared()
		for last in ["t", "v"] {
			try dropped("(let [t (transient {}) v (volatile! nil)] (vreset! v t) (assoc! t :v v) [\(last)])")
			try dropped("(let [t (transient {}) v (volatile! t)] (assoc! t :v v) [\(last)])")
			// A transient vector is the vector itself: conj! onto one the volatile also holds copies, so no ring.
			try dropped("(let [t (transient []) v (volatile! t)] (conj! t v) [\(last)])", cycles: 0)
		}
	}

	// The third ring runs through a cell realized before l was born: the walk must not skip it.
	@Test func lazySeqs() throws {
		try prepared()
		try dropped("(let [v (volatile! nil) s (lazy-seq (cons 1 @v))] (vreset! v s) (doall (take 3 s)) [s])")
		try dropped("(let [v (volatile! nil) s (lazy-seq (cons 1 @v))] (vreset! v s) (doall (take 3 s)) [(seq s)])")
		try dropped("(let [v (volatile! nil) r (doall (lazy-seq (cons v nil))) l (lazy-seq (cons 1 r))] (vreset! v l) [(seq l)])")
		// A promise is a channel, born shared.
		try dropped("(let [p (promise) s (lazy-seq (cons 1 @p))] (deliver p s) (doall (take 3 s)) nil)")
	}

	// The UI case: a handler over the atom in the atom's own state. An atom is born shared, so the shared rule takes it.
	@Test func anAtomWithAClosureOverItself() throws {
		try prepared()
		try dropped("(let [state (atom {})] (swap! state assoc :on-click #(swap! state update :n (fnil inc 0))) ((:on-click @state)) [(:on-click @state)])")
		try dropped("(let [state (atom {})] (dotimes [i 10] (swap! state assoc :on-click #(swap! state assoc :n i))) [@state])")
	}

	// Two threads close one ring of atoms between them; neither store sees the other's.
	@Test func twoThreadsCloseOneSharedRing() throws {
		try prepared()
		try dropped("""
		(let [a (atom nil) b (atom nil) f (future (reset! a b)) g (future (reset! b a))] @f @g nil)
		""")
	}

	// Past its budget the check files the owner; the next local collection flags the live ring, or frees a dead one.
	@Test func aCheckPastItsBudget() throws {
		try prepared()
		let deferred = stat(CLJ_CC_STAT_STORE_DEFERRED)
		try dropped("(let [v (volatile! nil) big (mapv volatile! (range 2000)) x [big v]] (vreset! v x) [x])")
		#expect(stat(CLJ_CC_STAT_STORE_DEFERRED) - deferred >= 1)
		try prepared()
		let base = CoroBaseline()
		#expect(try eval("""
		(let [v (volatile! nil) big (mapv volatile! (range 2000)) x [big v]]
		  (vreset! v x) (cc-collect!) (and (on-cycle? v) (on-cycle? x)))
		""") == Value(true))
		clj_cc_collect()
		base.check()
	}

	// No cycle, no flag: a tree of arrays filled before any heap edge reaches them, and a store of plain data.
	@Test func noCycleNoFlag() throws {
		try prepared()
		let base = CoroBaseline()
		#expect(try eval("""
		(let [leaves (mapv (fn [i] (Leaf. (object-array [(object-array 2) i]))) (range 8))
		      root (Leaf. (object-array leaves))
		      v (volatile! nil)]
		  (vreset! v [1 2 3])
		  (not-any? on-cycle? (conj leaves root v)))
		""") == Value(true))
		clj_cc_collect()
		base.check()
	}
}
