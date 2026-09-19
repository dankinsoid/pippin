// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// A tree analyzed in `user` and its table built with a store; the tree is kept for the table's life.
private final class Joined {
	let node: UnsafeMutablePointer<clj_node>
	let table: OpaquePointer
	let nodes: [UnsafePointer<clj_node>]

	init(_ source: String, store: OpaquePointer) throws {
		let form = try Value(reading: source)
		var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
		guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
		self.node = node
		table = clj_facts_of_with(node, store)!
		var all: [UnsafePointer<clj_node>] = []
		Joined.collect(node, &all)
		nodes = all
	}

	static func collect(_ n: UnsafePointer<clj_node>, _ out: inout [UnsafePointer<clj_node>]) {
		out.append(n)
		var children: [UnsafePointer<clj_node>] = []
		withUnsafeMutablePointer(to: &children) { ctx in
			clj_node_children(n, { child, ctx in
				ctx!.assumingMemoryBound(to: [UnsafePointer<clj_node>].self).pointee.append(child!)
			}, ctx)
		}
		for c in children { collect(c, &out) }
	}

	func fact(_ id: UInt32) -> String { describe(clj_facts_node(table, id)!.pointee) }

	// The fact of argument i of the n-th INTRINSIC node in pre-order.
	func intrinsicArg(_ i: Int, occurrence: Int = 0) -> String {
		let matching = nodes.filter { $0.pointee.kind == CLJ_NODE_INTRINSIC }
		guard occurrence < matching.count else { return "<missing>" }
		return fact(matching[occurrence].pointee.u.intrinsic.args[i]!.pointee.id)
	}

	func intrinsic(_ occurrence: Int = 0) -> String {
		let matching = nodes.filter { $0.pointee.kind == CLJ_NODE_INTRINSIC }
		return occurrence < matching.count ? fact(matching[occurrence].pointee.id) : "<missing>"
	}

	// Records every site and value read of the table under the owner, as a consumer does.
	func record(owner: UnsafeRawPointer) {
		for i in 0..<clj_facts_nsites(table) {
			var node: UInt32 = 0, nargs: UInt32 = 0
			var v: clj_value = 0
			var args: UnsafePointer<clj_fact>?
			var inFn = false
			_ = clj_facts_site(table, i, &node, &v, &nargs, &args, &inFn)
			clj_callers_add_site(owner, v, nargs, args)
		}
		for i in 0..<clj_facts_nvalue_reads(table) {
			var inFn = false
			clj_callers_add_value_read(owner, clj_facts_value_read(table, i, &inFn))
		}
	}

	var joins: [(reason: clj_join_reason, params: [String], callers: [String], selfRounds: UInt32, selfWidened: Bool)] {
		(0..<clj_facts_njoins(table)).map { i in
			let j = clj_facts_join_at(table, i)!.pointee
			var params: [String] = [], callers: [String] = []
			withUnsafePointer(to: j.params) { p in
				p.withMemoryRebound(to: clj_fact.self, capacity: Int(CLJ_FN_MAX_FIXED) + 1) { facts in
					for k in 0..<Int(j.nparams) { params.append(describe(facts[k])) }
				}
			}
			withUnsafePointer(to: j.callers) { p in
				p.withMemoryRebound(to: clj_fact.self, capacity: Int(CLJ_FN_MAX_FIXED) + 1) { facts in
					for k in 0..<Int(j.nparams) { callers.append(describe(facts[k])) }
				}
			}
			return (clj_join_reason(rawValue: j.reason), params, callers, j.self_rounds, j.self_widened)
		}
	}

	var diagnostics: [String] {
		(0..<clj_facts_ndiagnostics(table)).map { i in
			let d = clj_facts_diagnostic(table, i)!
			var buf = [CChar](repeating: 0, count: 512)
			_ = clj_diagnostic_message(d, &buf, buf.count)
			return (d.pointee.severity == CLJ_DIAG_ERROR ? "E " : "W ") + String(cString: buf)
		}
	}

	var bottoms: Int {
		nodes.filter { clj_facts_value_node($0.pointee.kind) && clj_facts_node(table, $0.pointee.id)!.pointee.types == 0 }.count
	}

	deinit {
		clj_facts_free(table)
		clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
	}
}

private func describe(_ f: clj_fact) -> String {
	var text: String
	if f.types == 0 { text = "⊥" } else if f.types == UInt32(CLJ_T_TOP) { text = "⊤" } else {
		var names: [String] = []
		for bit in 0..<27 where f.types & (1 << bit) != 0 { names.append(String(cString: clj_fact_kind_name(1 << bit))) }
		text = names.joined(separator: "|")
	}
	switch clj_null(rawValue: UInt32(f.null)) {
	case CLJ_NULL_NEVER: text += "/never"
	case CLJ_NULL_ALWAYS: text += "/always"
	case CLJ_NULL_MAYBE: text += "/maybe"
	default: text += "/⊥"
	}
	if f.singleton != CLJ_UNBOUND { text += "=" + Value(borrowing: f.singleton).description }
	return text
}

// Owners are opaque keys; a distinct address per test keeps their sites apart.
nonisolated(unsafe) private let ownerA = UnsafeMutableRawPointer.allocate(byteCount: 1, alignment: 1)
nonisolated(unsafe) private let ownerB = UnsafeMutableRawPointer.allocate(byteCount: 1, alignment: 1)
nonisolated(unsafe) private let ownerC = UnsafeMutableRawPointer.allocate(byteCount: 1, alignment: 1)

extension CoreTests {
	// The reverse index and the caller join (callers.c, facts.c, NOTES.md "Facts").
	@Suite(.serialized) struct CallersTests {
		let rt = Runtime()

		init() {
			_ = try? rt.eval("""
			(do (defn cj-f [n] (inc n))
			    (defn cj-str [s] (name s))
			    (defn ^:dynamic cj-dyn [n] (inc n))
			    (defn cj-two ([a] (inc a)) ([a b] (+ a b)))
			    (defn cj-rest [a & more] (inc a))
			    (def cj-box (atom 1))
			    (defn cj-fact [n] (if (<= n 1) 1 (* n (cj-fact (dec n)))))
			    (defn cj-fib [n] (if (< n 2) n (+ (cj-fib (- n 1)) (cj-fib (- n 2)))))
			    (defn cj-halve [x] (if (< x 1.0) x (cj-halve (/ x 2.0))))
			    (defn cj-mixed [n x] (if (< x 1.0) n (cj-mixed (inc n) (/ x 2.0))))
			    (defn cj-opaque [n] (if (zero? n) 0 (cj-opaque @cj-box)))
			    (defn cj-rotate [x] (if (number? x) (cj-rotate (str x)) (if (string? x) (cj-rotate (keyword x)) (if (keyword? x) (cj-rotate (name x)) (cj-rotate (vector x))))))
			    (declare cj-two-way))
			""")
		}

		private func store() -> OpaquePointer {
			let s = clj_summaries_new()!
			clj_summaries_use_callers(s, true)
			return s
		}

		// The parameter follows the recorded sites: none, fixnum, fixnum|double, then TOP once the var is read as a value.
		@Test func joinFollowsTheRecordedSites() throws {
			let v = try rt.eval("#'cj-f")
			let s = store()
			defer { clj_summaries_free(s) }
			let def = "(defn cj-f [n] (inc n))"
			do {
				let t = try Joined(def, store: s)
				#expect(t.joins.count == 1)
				#expect(t.joins[0].reason == CLJ_JOIN_NO_SITES)
				#expect(t.joins[0].params == ["⊤/maybe"])
				#expect(t.intrinsicArg(0) == "fixnum|long|bigint|ratio|decimal|double/never")
			}
			let e0 = clj_var_callers_epoch(v.raw)
			try Joined("(defn cj-caller-1 [] (cj-f 1))", store: s).record(owner: ownerA)
			#expect(clj_callers_nsites(v.raw) == 1)
			#expect(clj_var_callers_epoch(v.raw) == e0 + 1)
			do {
				let t = try Joined(def, store: s)
				#expect(t.joins[0].reason == CLJ_JOIN_OK)
				#expect(t.joins[0].params == ["fixnum/never"])
				#expect(t.intrinsicArg(0) == "fixnum/never")
				#expect(t.intrinsic() == "fixnum|long/never")
				#expect(clj_facts_valid(t.table))
				// a caller inside a fn body of another form widens the join; the earlier table is stale
				try Joined("(defn cj-caller-2 [x] (cj-f (double x)))", store: s).record(owner: ownerB)
				#expect(!clj_facts_valid(t.table))
			}
			do {
				let t = try Joined(def, store: s)
				#expect(t.joins[0].params == ["fixnum|double/never"])
				#expect(t.intrinsicArg(0) == "fixnum|double/never")
			}
			// the same site recorded twice under one owner is one site; the owner's death takes it away
			try Joined("(defn cj-caller-2 [x] (cj-f (double x)))", store: s).record(owner: ownerB)
			#expect(clj_callers_nsites(v.raw) == 2)
			clj_callers_forget(ownerB)
			#expect(clj_callers_nsites(v.raw) == 1)
			#expect(try Joined(def, store: s).joins[0].params == ["fixnum/never"])
			// a first-class use: the var may be called from anywhere
			try Joined("(defn cj-caller-3 [] (map cj-f [1 2]))", store: s).record(owner: ownerC)
			#expect(clj_callers_nvalue_reads(v.raw) == 1)
			do {
				let t = try Joined(def, store: s)
				#expect(t.joins[0].reason == CLJ_JOIN_FIRST_CLASS)
				#expect(t.joins[0].params == ["⊤/maybe"])
			}
			clj_callers_forget(ownerC)
			clj_callers_forget(ownerA)
			#expect(clj_callers_nsites(v.raw) == 0)
			#expect(try Joined(def, store: s).joins[0].reason == CLJ_JOIN_NO_SITES)
		}

		// A TOP position keeps the others; a site enters the arity the evaluator would; a dynamic var takes no join.
		@Test func topRulesAndArities() throws {
			let two = try rt.eval("#'cj-two"), rest = try rt.eval("#'cj-rest"), dyn = try rt.eval("#'cj-dyn")
			let s = store()
			defer { clj_summaries_free(s) }
			try Joined("(defn cj-caller-4 [x] [(cj-two 1 x) (cj-two 2) (cj-rest 3 x x) (cj-dyn 4)])", store: s).record(owner: ownerA)
			defer { clj_callers_forget(ownerA) }
			#expect(clj_callers_nsites(two.raw) == 2)
			#expect(clj_callers_nsites(rest.raw) == 1)
			let t = try Joined("(defn cj-two ([a] (inc a)) ([a b] (+ a b)))", store: s)
			#expect(t.joins.count == 2)
			#expect(t.joins[0].params == ["fixnum/never"])
			#expect(t.joins[1].reason == CLJ_JOIN_TOP_ARG)
			#expect(t.joins[1].params == ["fixnum/never", "⊤/maybe"])
			let r = try Joined("(defn cj-rest [a & more] (inc a))", store: s)
			#expect(r.joins[0].params == ["fixnum/never"])
			let d = try Joined("(defn ^:dynamic cj-dyn [n] (inc n))", store: s)
			#expect(d.joins.count == 0)
			#expect(clj_callers_nsites(dyn.raw) == 1)
			let off = clj_summaries_new()!
			defer { clj_summaries_free(off) }
			#expect(try Joined("(defn cj-two ([a] (inc a)) ([a b] (+ a b)))", store: off).joins.count == 0)
		}

		// Contradicting every recorded caller proves nothing about calls the index does not see: a warning, no ⊥.
		@Test func conflictAgainstCallersWarns() throws {
			let s = store()
			defer { clj_summaries_free(s) }
			try Joined("(defn cj-caller-5 [] (cj-str 1))", store: s).record(owner: ownerA)
			defer { clj_callers_forget(ownerA) }
			let t = try Joined("(defn cj-str [s] (name s))", store: s)
			#expect(t.joins[0].params == ["fixnum/never"])
			#expect(clj_facts_nerrors(t.table) == 0)
			#expect(t.bottoms == 0)
			#expect(t.diagnostics.contains { $0.hasPrefix("W clojure.core/name") && $0.contains("under what the recorded callers pass") })
		}

		// A fn's own site is no recorded site: it enters the entry's own join, re-evaluated with the parameters at the
		// join over the external callers until nothing moves (NOTES.md "Facts", the caller join).
		@Test func selfSiteEntersTheEntrysOwnJoin() throws {
			let fact = try rt.eval("#'cj-fact"), fib = try rt.eval("#'cj-fib")
			let s = store()
			defer { clj_summaries_free(s) }
			let factDef = "(defn cj-fact [n] (if (<= n 1) 1 (* n (cj-fact (dec n)))))"
			do {
				// no external caller: TOP, and the fixpoint is not run
				let t = try Joined(factDef, store: s)
				#expect(t.joins[0].reason == CLJ_JOIN_NO_SITES)
				#expect(t.joins[0].selfRounds == 0)
				t.record(owner: ownerA)
				#expect(clj_callers_nsites(fact.raw) == 0)
				clj_callers_forget(ownerA)
			}
			try Joined("(defn cj-caller-6 [] [(cj-fact 20) (cj-fib 25)])", store: s).record(owner: ownerA)
			defer { clj_callers_forget(ownerA) }
			#expect(clj_callers_nsites(fact.raw) == 1)
			do {
				let t = try Joined(factDef, store: s)
				#expect(t.joins[0].reason == CLJ_JOIN_OK)
				#expect(t.joins[0].callers == ["fixnum/never"])
				#expect(t.joins[0].params == ["fixnum|long/never"])
				#expect(t.joins[0].selfRounds == 2)
				#expect(!t.joins[0].selfWidened)
				// the body under the join: the multiply over int64 operands, the recursive result int64 too
				#expect(t.intrinsicArg(0, occurrence: 1) == "fixnum|long/never")
				#expect(t.intrinsicArg(1, occurrence: 1) == "fixnum|long/never")
				#expect(t.intrinsic(1) == "fixnum|long/never")
				#expect(clj_facts_valid(t.table))
				// re-recording the def's own table adds no site of its var
				t.record(owner: ownerB)
				#expect(clj_callers_nsites(fact.raw) == 1)
				#expect(clj_facts_valid(t.table))
				clj_callers_forget(ownerB)
			}
			do {
				let t = try Joined("(defn cj-fib [n] (if (< n 2) n (+ (cj-fib (- n 1)) (cj-fib (- n 2)))))", store: s)
				#expect(t.joins[0].params == ["fixnum|long/never"])
				#expect(t.intrinsic(1) == "fixnum|long/never")
			}
			#expect(clj_callers_nsites(fib.raw) == 1)
		}

		// A double self-site, a mixed one, one that passes TOP from a deref, an arity calling the fn's other arity,
		// and a self-site that keeps climbing: widened to TOP after three rounds.
		@Test func selfSiteKinds() throws {
			let s = store()
			defer { clj_summaries_free(s) }
			try Joined("(defn cj-caller-7 [] [(cj-halve 8.0) (cj-mixed 0 8.0) (cj-opaque 3) (cj-rotate 1) (cj-two-way 5)])", store: s).record(owner: ownerA)
			defer { clj_callers_forget(ownerA) }
			let h = try Joined("(defn cj-halve [x] (if (< x 1.0) x (cj-halve (/ x 2.0))))", store: s)
			#expect(h.joins[0].params == ["double/never"])
			#expect(h.joins[0].selfRounds == 1)
			let m = try Joined("(defn cj-mixed [n x] (if (< x 1.0) n (cj-mixed (inc n) (/ x 2.0))))", store: s)
			#expect(m.joins[0].params == ["fixnum|long/never", "double/never"])
			#expect(m.joins[0].selfRounds == 2)
			let o = try Joined("(defn cj-opaque [n] (if (zero? n) 0 (cj-opaque @cj-box)))", store: s)
			#expect(o.joins[0].reason == CLJ_JOIN_TOP_ARG)
			#expect(o.joins[0].callers == ["fixnum/never"])
			#expect(o.joins[0].params == ["⊤/maybe"])
			#expect(o.joins[0].selfRounds == 2)
			#expect(!o.joins[0].selfWidened)
			let r = try Joined("(defn cj-rotate [x] (if (number? x) (cj-rotate (str x)) (if (string? x) (cj-rotate (keyword x)) (if (keyword? x) (cj-rotate (name x)) (cj-rotate (vector x))))))", store: s)
			#expect(r.joins[0].params == ["⊤/maybe"])
			#expect(r.joins[0].selfWidened)
			#expect(r.joins[0].selfRounds == 4)
			#expect(r.diagnostics.isEmpty)
			// the one-parameter arity's call of the two-parameter one is a site of the index (another arity is a caller like
			// any other); the var is only declared, so the interpreter recorded no site of its own
			let two = try rt.eval("#'cj-two-way")
			#expect(clj_callers_nsites(two.raw) == 1)
			let twoDef = "(defn cj-two-way ([n] (cj-two-way n 0)) ([n acc] (if (zero? n) acc (cj-two-way (dec n) (+ acc n)))))"
			do {
				let w = try Joined(twoDef, store: s)
				#expect(w.joins[0].params == ["fixnum/never"])
				#expect(w.joins[1].reason == CLJ_JOIN_NO_SITES)
				w.record(owner: ownerB)
			}
			defer { clj_callers_forget(ownerB) }
			#expect(clj_callers_nsites(two.raw) == 2)
			let w = try Joined(twoDef, store: s)
			#expect(w.joins[1].callers == ["fixnum/never", "fixnum/never"])
			#expect(w.joins[1].params == ["fixnum|long/never", "fixnum|long/never"])
		}

		// A form that calls f at its top level, outside any fn, is a site too; a nested fn's site belongs to the form.
		@Test func sitesInEveryPosition() throws {
			let v = try rt.eval("#'cj-f")
			let s = store()
			defer { clj_summaries_free(s) }
			let t = try Joined("(do (cj-f 1) (fn [] (cj-f 2)) #'cj-f)", store: s)
			#expect(clj_facts_nsites(t.table) == 2)
			#expect(clj_facts_nvalue_reads(t.table) == 1)
			t.record(owner: ownerA)
			defer { clj_callers_forget(ownerA) }
			// two sites of one owner passing the same kinds are one entry: the join cannot tell them apart
			#expect(clj_callers_nsites(v.raw) == 1)
			#expect(clj_callers_nvalue_reads(v.raw) == 1)
		}
	}
}
