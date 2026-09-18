// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// A tree analyzed in `user`, its table built with a summary store, and a pre-order index of the nodes.
private final class Summarized {
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
		Summarized.collect(node, &all)
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

	// The n-th node of a kind in pre-order.
	func fact(_ kind: clj_node_kind, _ occurrence: Int = 0) -> String {
		let matching = nodes.filter { $0.pointee.kind == kind }
		return occurrence < matching.count ? fact(matching[occurrence].pointee.id) : "<missing>"
	}

	// The receiver of the n-th call whose head is a keyword constant.
	func keywordReceiver(_ occurrence: Int = 0) -> (String, UnsafePointer<clj_type>?) {
		let calls = nodes.filter { $0.pointee.kind == CLJ_NODE_INVOKE && $0.pointee.u.invoke.fn.pointee.kind == CLJ_NODE_CONST }
		guard occurrence < calls.count else { return ("<missing>", nil) }
		let f = clj_facts_node(table, calls[occurrence].pointee.u.invoke.args[0]!.pointee.id)!.pointee
		return (describe(f), f.desc)
	}

	// The fact of the i-th argument of the n-th call of a var by name.
	func argument(of name: String, _ i: Int, occurrence: Int = 0) -> String {
		let calls = nodes.filter {
			guard $0.pointee.kind == CLJ_NODE_INVOKE, $0.pointee.u.invoke.fn.pointee.kind == CLJ_NODE_VAR else { return false }
			return Value(borrowing: $0.pointee.u.invoke.fn.pointee.u.var).description == "#'" + name
		}
		guard occurrence < calls.count else { return "<missing>" }
		return fact(calls[occurrence].pointee.u.invoke.args[i]!.pointee.id)
	}

	var conflicts: [String] {
		(0..<clj_facts_ncall_conflicts(table)).map { i in
			var buf = [CChar](repeating: 0, count: 512)
			_ = clj_call_conflict_message(clj_facts_call_conflict(table, i), &buf, buf.count)
			return String(cString: buf)
		}
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

// "<params> -> <ret>" plus the effect letters (a, t, i, w) and r for a summary the fixpoint ran on.
private func describe(_ s: UnsafePointer<clj_summary>) -> String {
	let sum = s.pointee
	var params: [String] = []
	withUnsafePointer(to: sum.params) { p in
		p.withMemoryRebound(to: clj_fact.self, capacity: Int(CLJ_FN_MAX_FIXED) + 1) { facts in
			for i in 0..<Int(sum.nparams) { params.append(describe(facts[i])) }
		}
	}
	var effects = ""
	if sum.effects & UInt32(CLJ_EFFECT_ALLOC) != 0 { effects += "a" }
	if sum.effects & UInt32(CLJ_EFFECT_THROW) != 0 { effects += "t" }
	if sum.effects & UInt32(CLJ_EFFECT_IO) != 0 { effects += "i" }
	if sum.effects & UInt32(CLJ_EFFECT_ATOM) != 0 { effects += "w" }
	return "[" + params.joined(separator: ", ") + "] -> " + describe(sum.ret) + " " + (effects.isEmpty ? "pure" : effects) + (sum.recursive ? " r" : "")
}

extension CoreTests {
	// Function summaries and pass 2 over the facts lattice (summary.c, facts.c, NOTES.md "Facts").
	@Suite struct SummaryTests {
		let rt = Runtime()

		// Vars, their names and the keywords the pass interns live for the process: made before any live-object window.
		init() {
			_ = try? rt.eval("""
			(do :clj/facts :args :ret
			    (defn sum-inc [x] (inc x))
			    (defn sum-len [xs] (count xs))
			    (defn sum-pred [x] (nil? x))
			    (defn sum-box [x] [x])
			    (defn sum-throw [x] (if x (throw (ex-info "no" {})) x))
			    (defn sum-fact [n] (if (zero? n) 1 (* n (sum-fact (dec n)))))
			    (declare sum-odd)
			    (defn sum-even [n] (if (zero? n) true (sum-odd (dec n))))
			    (defn sum-odd [n] (if (zero? n) false (sum-even (dec n))))
			    (defn sum-either [x flag] (if flag (inc x) (name x)))
			    (defn sum-ann [x] (inc x))
			    (alter-meta! #'sum-ann assoc :clj/facts {:args [:int] :ret :int})
			    (defn sum-bad [x] (inc x))
			    (alter-meta! #'sum-bad assoc :clj/facts {:args [:string]})
			    (defn sum-redef [x] (inc x))
			    (defn sum-need [m] (keys m))
			    (def sum-atom (atom 1))
			    (defprotocol SumP (sum-m [this]))
			    (deftype SumT [] SumP (sum-m [_] 1))
			    (defprotocol SumQ (sum-q [this]))
			    (deftype SumT2 [] SumQ (sum-q [_] 1))
			    (defrecord SumR [] SumQ (sum-q [_] 2))
			    (defrecord SumRec [a b])
			    (deftype SumTy [a])
			    (def sum-inst (->SumT)))
			""")
		}

		private func summary(_ store: OpaquePointer, _ name: String, _ nargs: UInt32) throws -> String {
			let v = try rt.eval("#'" + name)
			guard let s = clj_summary_of_var(store, v.raw, nargs) else { return "<none>" }
			return describe(s)
		}

		// A parameter's requirement is the meet of its uses on every path, the result the body's fact, effects what fell out.
		@Test func simpleSummaries() throws {
			let before = clj_debug_live_objects()
			do {
				let store = clj_summaries_new()!
				defer { clj_summaries_free(store) }
				let s_sum_inc_1 = try summary(store, "sum-inc", 1)
				#expect(s_sum_inc_1 == "[fixnum|long|bigint|ratio|decimal|double/never] -> fixnum|long|bigint|ratio|decimal|double/never at")
				let s_sum_len_1 = try summary(store, "sum-len", 1)
				#expect(s_sum_len_1 == "[nil|string|seq|vector|map|set|sorted-map|sorted-set|record|array/maybe] -> fixnum/never at")
				let s_sum_pred_1 = try summary(store, "sum-pred", 1)
				#expect(s_sum_pred_1 == "[⊤/maybe] -> bool/never pure")
				let s_sum_box_1 = try summary(store, "sum-box", 1)
				#expect(s_sum_box_1 == "[⊤/maybe] -> vector/never a")
				let s_sum_throw_1 = try summary(store, "sum-throw", 1)
				#expect(s_sum_throw_1 == "[⊤/maybe] -> nil|bool/maybe atiw")
				// uses on different branches join: x must be a number or an ident or a string, not both
				let s_sum_either_2 = try summary(store, "sum-either", 2)
				#expect(s_sum_either_2 == "[fixnum|long|bigint|ratio|decimal|double|string|keyword|symbol/never, ⊤/maybe] -> fixnum|long|bigint|ratio|decimal|double|string/never at")
				// no arity for the count, no summary
				let s_sum_inc_2 = try summary(store, "sum-inc", 2)
				#expect(s_sum_inc_2 == "<none>")
				// an annotated builtin has a summary although it has no body
				let s_count_1 = try summary(store, "count", 1)
				#expect(s_count_1 == "[nil|string|seq|vector|map|set|sorted-map|sorted-set|record|array/maybe] -> fixnum/never at")
				// an unannotated builtin has none
				let s_str_1 = try summary(store, "str", 1)
				#expect(s_str_1 == "<none>")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Recursion runs the fixpoint from an optimistic ⊥ result; it terminates within the widening bound.
		@Test func recursiveFixpoint() throws {
			let before = clj_debug_live_objects()
			do {
				let store = clj_summaries_new()!
				defer { clj_summaries_free(store) }
				let s_sum_fact_1 = try summary(store, "sum-fact", 1)
				#expect(s_sum_fact_1 == "[fixnum|long|bigint|ratio|decimal|double/never] -> fixnum|long|bigint|ratio|decimal|double/never at r")
				let s_sum_even_1 = try summary(store, "sum-even", 1)
				#expect(s_sum_even_1 == "[fixnum|long|bigint|ratio|decimal|double/never] -> bool/never at r")
				let s_sum_odd_1 = try summary(store, "sum-odd", 1)
				// computed inside sum-even's fixpoint it was transient; asked for on its own it sees sum-even finished
				#expect(s_sum_odd_1 == "[fixnum|long|bigint|ratio|decimal|double/never] -> bool/never at")
				#expect(clj_summaries_rounds(store) >= 1)
				#expect(clj_summaries_widenings(store) == 0)
				// the second lookup is a cache hit: the counters stand
				let rounds = clj_summaries_rounds(store)
				_ = try summary(store, "sum-fact", 1)
				#expect(clj_summaries_rounds(store) == rounds)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// An annotation meets the inferred summary; a contradiction is an error that names both.
		@Test func annotationMeetsInference() throws {
			let before = clj_debug_live_objects()
			do {
				let store = clj_summaries_new()!
				defer { clj_summaries_free(store) }
				let v = try rt.eval("#'sum-ann")
				let s = clj_summary_of_var(store, v.raw, 1)!
				#expect(describe(s) == "[fixnum|long|bigint/never] -> fixnum|long|bigint/never at")
				#expect(s.pointee.inferred && s.pointee.annotated)
				#expect(clj_summaries_nannotation_conflicts(store) == 0)
				_ = try summary(store, "sum-bad", 1)
				#expect(clj_summaries_nannotation_conflicts(store) == 1)
				var buf = [CChar](repeating: 0, count: 512)
				_ = clj_annotation_conflict_message(clj_summaries_annotation_conflict(store, 0), &buf, buf.count)
				let message = String(cString: buf)
				#expect(message.hasPrefix("user/sum-bad: annotation says argument 0 is string, the body uses it as fixnum|long|bigint|ratio|decimal|double at "))
				// a conflicting annotation is dropped, the inferred requirement stands
				let s_sum_bad_1 = try summary(store, "sum-bad", 1)
				#expect(s_sum_bad_1 == "[fixnum|long|bigint|ratio|decimal|double/never] -> fixnum|long|bigint|ratio|decimal|double/never at")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A summary is cached with the var's epoch: a redefinition invalidates it and every table that read it.
		@Test func redefinitionInvalidates() throws {
			let v = try rt.eval("#'sum-redef")
			let e0 = clj_var_epoch(v.raw)
			let store = clj_summaries_new()!
			defer { clj_summaries_free(store) }
			#expect(try summary(store, "sum-redef", 1).hasSuffix("-> fixnum|long|bigint|ratio|decimal|double/never at"))
			#expect(clj_summaries_epoch_seen(store, v.raw) == e0)
			let table = try Summarized("(fn [] (sum-redef 1))", store: store)
			#expect(clj_facts_valid(table.table))
			#expect(clj_facts_ndeps(table.table) == 1)
			_ = try rt.eval("(defn sum-redef [x] (str x))")
			#expect(clj_var_epoch(v.raw) == e0 + 1)
			#expect(!clj_facts_valid(table.table))
			let s_sum_redef_1 = try summary(store, "sum-redef", 1)
			#expect(s_sum_redef_1 == "[⊤/maybe] -> string/never at")
			#expect(clj_summaries_invalidated(store) == 1)
			#expect(clj_summaries_epoch_seen(store, v.raw) == e0 + 1)
			_ = try rt.eval("(defn sum-redef [x] (inc x))")
		}

		// A var read answers the kind of its root and never a singleton; a call takes the summary's result.
		@Test func varReadsAndCallResults() throws {
			let before = clj_debug_live_objects()
			do {
				let store = clj_summaries_new()!
				defer { clj_summaries_free(store) }
				#expect(try Summarized("(fn [] sum-atom)", store: store).fact(CLJ_NODE_VAR) == "atom/never")
				#expect(try Summarized("(fn [] sum-inc)", store: store).fact(CLJ_NODE_VAR) == "fn/never")
				#expect(try Summarized("(fn [x] (sum-len x))", store: store).fact(CLJ_NODE_INVOKE) == "fixnum/never")
				#expect(try Summarized("(fn [x] (sum-fact x))", store: store).fact(CLJ_NODE_INVOKE) == "fixnum|long|bigint|ratio|decimal|double/never")
				// the requirement narrows the argument and the slot behind it
				let t = try Summarized("(fn [x] (sum-inc x) x)", store: store)
				#expect(t.argument(of: "user/sum-inc", 0) == "fixnum|long|bigint|ratio|decimal|double/never")
				#expect(t.fact(CLJ_NODE_LOCAL, 1) == "fixnum|long|bigint|ratio|decimal|double/never")
				#expect(clj_facts_narrowed_args(t.table) == 1)
				// a direct fn's arity is summarized by node identity
				#expect(try Summarized("(fn [x] (let [f (fn [a] (inc a))] (f x)))", store: store).fact(CLJ_NODE_DIRECT_CALL) == "fixnum|long|bigint|ratio|decimal|double/never")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A protocol receiver meets the join of the kinds in the protocol's tables: one deftype is a known host type.
		@Test func protocolReceiverJoin() throws {
			let before = clj_debug_live_objects()
			do {
				let store = clj_summaries_new()!
				defer { clj_summaries_free(store) }
				#expect(try Summarized("(fn [x] (sum-m x))", store: store).argument(of: "user/sum-m", 0) == "host/never")
				let q = try Summarized("(fn [x] (sum-q x))", store: store).argument(of: "user/sum-q", 0)
				#expect(q == "record|host/never")
				let s_sum_m_1 = try summary(store, "sum-m", 1)
				#expect(s_sum_m_1 == "[host/never] -> ⊤/maybe atiw")
				// a var-bound instance as the receiver is known from the root's kind alone
				#expect(try Summarized("(fn [] (sum-m sum-inst))", store: store).argument(of: "user/sum-m", 0) == "host/never")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// ->Foo and map->Foo answer the record kind with its descriptor, so (:k m) below them sits on a known record.
		@Test func recordConstructorResult() throws {
			let before = clj_debug_live_objects()
			do {
				let store = clj_summaries_new()!
				defer { clj_summaries_free(store) }
				let type = try rt.eval("SumRec")
				let s____um_ec_2 = try summary(store, "->SumRec", 2)
				#expect(s____um_ec_2 == "[⊤/maybe, ⊤/maybe] -> record/never at")
				let s_map___um_ec_1 = try summary(store, "map->SumRec", 1)
				#expect(s_map___um_ec_1 == "[⊤/maybe] -> record/never at")
				let t = try Summarized("(fn [] (let [m (->SumRec 1 2)] (:a m)))", store: store)
				let (fact, desc) = t.keywordReceiver()
				#expect(fact == "record/never")
				#expect(desc.map { UnsafeRawPointer($0) } == UnsafeRawPointer(clj_to_ptr(type.raw)))
				#expect(try Summarized("(fn [m] (:a (map->SumRec m)))", store: store).keywordReceiver().0 == "record/never")
				// a deftype constructor answers host with its descriptor
				let s____um_y_1 = try summary(store, "->SumTy", 1)
				#expect(s____um_y_1 == "[⊤/maybe] -> host/never at")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A proven conflict is reported with both positions and stored nowhere: the argument keeps the caller's fact.
		@Test func twoPositionConflict() throws {
			let before = clj_debug_live_objects()
			do {
				let store = clj_summaries_new()!
				defer { clj_summaries_free(store) }
				let t = try Summarized("(fn [] (sum-need [1 2]))", store: store)
				#expect(t.conflicts.count == 1)
				let message = t.conflicts.first ?? ""
				#expect(message.hasPrefix("sum-need uses argument 0 as nil|map|sorted-map|record at "))
				#expect(message.hasSuffix(", vector is passed at 1:8"))
				#expect(t.argument(of: "user/sum-need", 0) == "vector/never=[1 2]")
				#expect(clj_facts_narrowed_args(t.table) == 0)
				// the same argument on a dead branch is no conflict
				#expect(try Summarized("(fn [x] (if (nil? x) (if (string? x) (sum-need [1 2]) 1) 2))", store: store).conflicts.isEmpty)
				// a requirement wider than the union cap is still a requirement: (count 1) is proven to throw
				#expect(try Summarized("(fn [] (count 1))", store: store).conflicts == ["count requires argument 0 to be nil|string|seq|vector|map|set|sorted-map|sorted-set|record|array, fixnum is passed at 1:8"])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every form of core.clj with the summaries: no ⊥ that a throw, a recur or a dead branch does not explain.
		@Test func noContradictionOverCoreWithSummaries() throws {
			let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
				.appendingPathComponent("Sources/CljCore/boot/core.clj")
			let source = try String(contentsOf: url, encoding: .utf8)
			let ns = try rt.eval("(find-ns 'clojure.core)")
			var env = clj_env(ns: ns.raw, line: 0, col: 0)
			let store = clj_summaries_new()!
			defer { clj_summaries_free(store) }
			var forms = 0, bottoms = 0, hits: UInt32 = 0, narrowed: UInt32 = 0
			var reader = clj_reader()
			try source.withCString { cstr in
				clj_reader_init(&reader, cstr, strlen(cstr))
				clj_reader_use_namespaces(&reader)
				while true {
					var form: clj_value = CLJ_NIL
					guard clj_read(&reader, &form) == CLJ_READ_OK else { break }
					defer { clj_release(form) }
					guard let node = clj_analyze(form, &env) else {
						clj_release(clj_take_pending())
						continue
					}
					forms += 1
					let table = clj_facts_of_with(node, store)!
					hits += clj_facts_summary_hits(table)
					narrowed += clj_facts_narrowed_args(table)
					var all: [UnsafePointer<clj_node>] = []
					Summarized.collect(node, &all)
					for n in all where clj_facts_value_node(n.pointee.kind) {
						let fact = clj_facts_node(table, n.pointee.id)!.pointee
						guard fact.types == 0, fact.unreachable == 0 else { continue }
						var sub: [UnsafePointer<clj_node>] = []
						Summarized.collect(n, &sub)
						if !sub.contains(where: { $0.pointee.kind == CLJ_NODE_THROW || $0.pointee.kind == CLJ_NODE_RECUR }) {
							bottoms += 1
							Issue.record(Comment(rawValue: "core.clj:\(n.pointee.line) ⊥ without a throw"))
						}
					}
					clj_facts_free(table)
					clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
				}
			}
			#expect(forms > 250)
			#expect(bottoms == 0)
			#expect(hits > 100)
			#expect(clj_summaries_nannotation_conflicts(store) == 0)
			#expect(clj_summaries_widenings(store) == 0)
		}
	}
}
