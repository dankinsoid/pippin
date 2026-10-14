// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// The acyclic marks (design §7, «Метка «ацикличен»»). Marks are unshared-only: objects stay in a form's locals.

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'acyclic-tests) " + source) }

private func stat(_ i: Int) -> Int64 {
	var s = [Int64](repeating: 0, count: CLJ_CC_STAT_COUNT)
	clj_debug_cc_stats(&s)
	return s[i]
}

private func bindNative(_ name: String, arity: UInt32, _ fn: clj_native_fn) {
	let ns = Value(symbol: "acyclic-tests"), sym = Value(symbol: name)
	let f = Value(owning: clj_fn_native(sym.raw, fn, arity, arity))
	withExtendedLifetime((ns, sym, f)) { clj_var_bind_root(clj_ns_intern(clj_ns_find_or_create(ns.raw), sym.raw), f.raw) }
}

extension CoreTests {
	@Suite struct AcyclicMarkTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("(ns acyclic-tests)")
			bindNative("cc-collect!", arity: 0) { _, _ in clj_fixnum(Int(clj_debug_cc_collect_local())) }
			bindNative("cc-file!", arity: 1) { args, _ in
				clj_debug_cc_file_local(args![0])
				return CLJ_NIL
			}
			bindNative("cc-acyclic?", arity: 1) { args, _ in clj_bool(clj_debug_cc_acyclic(args![0]) != 0) }
			_ = try cljEvalScoped("""
			(in-ns 'acyclic-tests)
			(deftype Node [cell])
			(defn mark!
			  "x filed and collected twice, as two local collections would see it; whether x is marked after."
			  [x]
			  (dotimes [_ 2] (cc-file! x) (cc-collect!))
			  (cc-acyclic? x))
			""")
			for k in ["back", "probe", "k", "copied", "flat-marked", "flat-unmarked"] { _ = Value(keyword: k) }
		}

		// The form answers whether its structure was marked before the cycle closed; the cycle must go.
		private func collected(_ form: String, marked: Bool, _ location: SourceLocation = #_sourceLocation) throws {
			clj_debug_cc_audit_every(1)
			let base = CoroBaseline(location)
			let freed = stat(CLJ_CC_STAT_FREED)
			#expect(try eval(form) == Value(marked), sourceLocation: location)
			clj_cc_collect()
			base.check(location)
			#expect(stat(CLJ_CC_STAT_FREED) - freed >= 1, sourceLocation: location)
		}

		// A B+ tree in miniature: arrays filled at construction, deftype nodes over them.
		@Test func aLiveTreeIsMarkedAndNotWalkedAgain() throws {
			clj_debug_cc_audit_every(1)
			let base = CoroBaseline()
			let marked = stat(CLJ_CC_STAT_MARKED), skipped = stat(CLJ_CC_STAT_SKIPPED)
			let m = try eval("""
			(let [leaves (mapv (fn [i] (Node. (object-array (map str (range i (+ i 8)))))) (range 0 64 8))
			      root (Node. (object-array leaves))]
			  (and (mark! root) (every? cc-acyclic? leaves)
			       (do (cc-file! root) (cc-file! (first leaves)) (cc-collect!) true)))
			""")
			#expect(m == Value(true))
			#expect(stat(CLJ_CC_STAT_MARKED) - marked >= 2 * 8 + 2)
			#expect(stat(CLJ_CC_STAT_SKIPPED) - skipped >= 2)
			base.check()
		}

		// A live ring below leaves the node above unmarked: once that node goes, RC alone could not free the ring.
		@Test func aRingBelowIsNoMark() throws {
			try collected("(let [v (volatile! nil) root (Node. (object-array [v]))] (vreset! v [v]) (mark! root))", marked: false)
		}

		@Test func asetIntoAMarkedArray() throws {
			let epochs = stat(CLJ_CC_STAT_EPOCHS)
			try collected("""
			(let [arr (object-array 2) leaf (Node. arr) root (Node. (object-array [leaf]))]
			  (let [m (mark! root)] (aset arr 0 root) m))
			""", marked: true)
			#expect(stat(CLJ_CC_STAT_EPOCHS) - epochs >= 1)
			try collected("(let [arr (object-array 1)] (let [m (mark! arr)] (aset arr 0 arr) m))", marked: true)
			try collected("(let [arr (object-array 1) root (Node. arr)] (let [m (mark! root)] (aset arr 0 [root]) m))", marked: true)
		}

		// A deftype has no mutable field: its cycle closes through a volatile in a field.
		@Test func aVolatileBelowAMark() throws {
			try collected("""
			(let [v (volatile! nil) leaf (Node. v) root (Node. (object-array [leaf]))]
			  (let [m (mark! root)] (vreset! v root) m))
			""", marked: true)
			try collected("""
			(let [v (volatile! nil) root (Node. (object-array [(Node. v) (Node. (object-array 4))]))]
			  (let [m (mark! root)] (vswap! v conj root) m))
			""", marked: true)
		}

		// An atom is born shared: the store publishes the marked structure, and the shared collection takes the cycle.
		@Test func anAtomBelowAMark() throws {
			try collected("""
			(let [a (atom nil) root (Node. (object-array [(Node. a) (Node. (object-array 3))]))]
			  (let [m (mark! root)] (reset! a root) m))
			""", marked: true)
		}

		// A transient map is edited in place at any count: no mark stands above one, whether the walk enters it or not.
		@Test func aTransientBelowAMark() throws {
			clj_debug_cc_audit_every(1)
			for t in ["(transient {})", "(transient {:k (object-array 1)})"] {
				let base = CoroBaseline()
				let freed = stat(CLJ_CC_STAT_FREED)
				// :flat-unmarked, :flat-marked or :copied: the answer must hold no object past the baseline.
				let r = try eval("""
				(let [t \(t) flat (identical? t (assoc! t :probe 1)) root (Node. (object-array [t]))]
				  (let [m (mark! root)] (assoc! t :back root) (cond (not flat) :copied m :flat-marked :else :flat-unmarked)))
				""")
				#expect(r != Value(keyword: "flat-marked"), "\(t)")
				clj_cc_collect()
				base.check()
				#expect(stat(CLJ_CC_STAT_FREED) - freed >= 1, "\(t)")
			}
			// One made after the mark gets below it only through a store into a guarded array.
			try collected("""
			(let [arr (object-array 1) root (Node. arr)]
			  (let [m (mark! root) t (transient {})] (aset arr 0 t) (assoc! t :back root) m))
			""", marked: true)
		}

		// A lazy seq still to be realized is a store to come: nothing above it is marked.
		@Test func aLazySeqRealizedBelowAMark() throws {
			try collected("""
			(let [cell (volatile! nil) s (lazy-seq (cons @cell nil)) root (Node. (object-array [s (object-array 1)]))]
			  (vreset! cell root)
			  (let [m (mark! root)] (doall s) m))
			""", marked: false)
		}

		// The in-place assoc asks each trie level's uniqueness, which clears its mark; the array closes the cycle.
		@Test func anInPlaceEditBelowAMarkedChain() throws {
			clj_debug_cc_audit_every(1)
			let base = CoroBaseline()
			let freed = stat(CLJ_CC_STAT_FREED)
			let arr = clj_array_new(CLJ_ARRAY_OBJECT, 1)
			var v = clj_vector_empty()
			for i in 0..<40 { v = clj_vector_conj(v, i == 0 ? arr : clj_fixnum(i)) }
			clj_release(arr)
			for _ in 0..<2 {
				clj_debug_cc_file_local(v)
				_ = clj_debug_cc_collect_local()
			}
			#expect(clj_debug_cc_acyclic(v) != 0)
			let a2 = clj_array_new(CLJ_ARRAY_OBJECT, 1)
			v = clj_vector_assoc(v, 1, a2)
			#expect(clj_debug_cc_acyclic(v) == 0)
			_ = clj_array_set(a2, 0, v)
			clj_release(a2)
			clj_release(v)
			_ = clj_debug_cc_collect_local()
			clj_cc_collect()
			#expect(stat(CLJ_CC_STAT_FREED) - freed >= 1)
			base.check()
		}
	}
}
