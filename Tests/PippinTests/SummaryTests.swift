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

	// Every diagnostic as "E " or "W " plus its message.
	var diagnostics: [String] {
		(0..<clj_facts_ndiagnostics(table)).map { i in
			let d = clj_facts_diagnostic(table, i)!
			var buf = [CChar](repeating: 0, count: 512)
			_ = clj_diagnostic_message(d, &buf, buf.count)
			return (d.pointee.severity == CLJ_DIAG_ERROR ? "E " : "W ") + String(cString: buf)
		}
	}

	var errors: Int { Int(clj_facts_nerrors(table)) }

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
			(do :=> :cat :maybe :or :and :enum := :any :nil :int :double :number :string :keyword :symbol :boolean :map :vector :set :seq :fn
			    :fixnum :long :bigint :ratio :decimal :char :sorted-map :sorted-set :record :array :var :atom :uuid :inst :regex :host :tuple :*
			    :sequential :seqable :x :k :min :n :facts/warnings :effects :park :alloc :throw :io :effects/severity :error
			    (require 'clojure.core.async)
			    (def sum-chan (clojure.core.async/chan))
			    (defn sum-parks [_] (clojure.core.async/<! sum-chan))
			    (defn sum-inc [x] (inc x))
			    (defn sum-len [xs] (count xs))
			    (defn sum-pred [x] (nil? x))
			    (defn sum-box [x] [x])
			    (defn sum-throw [x] (if x (throw (ex-info "no" {})) x))
			    (defn sum-fact [n] (if (zero? n) 1 (* n (sum-fact (dec n)))))
			    (defn sum-sq [x] (* x x))
			    (defn sum-mix [x] (if (number? x) (inc x) (str x)))
			    (declare sum-odd)
			    (defn sum-even [n] (if (zero? n) true (sum-odd (dec n))))
			    (defn sum-odd [n] (if (zero? n) false (sum-even (dec n))))
			    (defn sum-either [x flag] (if flag (inc x) (name x)))
			    (defn sum-ann [x] (inc x))
			    (alter-meta! #'sum-ann assoc :=> [:=> [:cat :int] :int])
			    (defn sum-bad {:=> [:=> [:cat :string] :any]} [x] (inc x))
			    (defn sum-decl {:=> [:=> [:cat [:maybe :map] [:enum :a :b]] [:or :string :nil]]} [m k] (get m k))
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
			    (def sum-inst (->SumT))
			    (defn sum-atom-deref [] @sum-atom)
			    (defn sum-any-deref [p] @p)
			    (defn sum-strict {:=> [:=> [:cat [:=> {:effects #{} :effects/severity :error} [:cat] :any]] :any]} [f] (f)))
			""")
		}

		private func effects(_ store: OpaquePointer, _ name: String, _ nargs: UInt32) throws -> UInt32 {
			let v = try rt.eval("#'" + name)
			guard let s = clj_summary_of_var(store, v.raw, nargs) else { return UInt32(CLJ_EFFECT_ANY) }
			return s.pointee.effects
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
				// count declares :any for its argument: the vocabulary has no tag for arrays (NOTES.md)
				#expect(s_sum_len_1 == "[⊤/maybe] -> fixnum/never at")
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
				#expect(s_count_1 == "[⊤/maybe] -> fixnum|long|bigint/never at")
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

		// A :=> declaration meets the inferred summary; a contradiction is a diagnostic that names both, never a failure.
		@Test func annotationMeetsInference() throws {
			let before = clj_debug_live_objects()
			do {
				let store = clj_summaries_new()!
				defer { clj_summaries_free(store) }
				let v = try rt.eval("#'sum-ann")
				let s = clj_summary_of_var(store, v.raw, 1)!
				#expect(describe(s) == "[fixnum|long|bigint/never] -> fixnum|long|bigint/never at")
				#expect(s.pointee.inferred && s.pointee.annotated)
				#expect(clj_summaries_ndiagnostics(store) == 0)
				_ = try summary(store, "sum-bad", 1)
				#expect(clj_summaries_ndiagnostics(store) == 1 && clj_summaries_nerrors(store) == 1)
				var buf = [CChar](repeating: 0, count: 512)
				_ = clj_diagnostic_message(clj_summaries_diagnostic(store, 0), &buf, buf.count)
				let message = String(cString: buf)
				#expect(message.hasPrefix("user/sum-bad: the declaration says argument 0 is string, the body uses it as fixnum|long|bigint|ratio|decimal|double at "))
				// a conflicting annotation is dropped, the inferred requirement stands
				let s_sum_bad_1 = try summary(store, "sum-bad", 1)
				#expect(s_sum_bad_1 == "[fixnum|long|bigint|ratio|decimal|double/never] -> fixnum|long|bigint|ratio|decimal|double/never at")
				// the vocabulary: :maybe, :enum singletons joined, :or with :nil; an opaque body is the declaration alone
				let s_sum_decl_2 = try summary(store, "sum-decl", 2)
				#expect(s_sum_decl_2 == "[nil|map|sorted-map|record/maybe, keyword/never] -> nil|string/maybe at")
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

		// A call site whose argument has a numeric domain takes the result of the body walked with the parameter in that
		// domain, cached beside the generic entry; the requirements stay the generic entry's (NOTES.md "Facts").
		@Test func specializedResults() throws {
			let before = clj_debug_live_objects()
			do {
				let store = clj_summaries_new()!
				defer { clj_summaries_free(store) }
				let s_sum_sq_1 = try summary(store, "sum-sq", 1)
				#expect(s_sum_sq_1 == "[⊤/maybe] -> fixnum|long|bigint|ratio|decimal|double/never at")
				#expect(try Summarized("(fn [] (sum-sq 3))", store: store).fact(CLJ_NODE_INVOKE) == "fixnum|long/never")
				#expect(try Summarized("(fn [] (sum-sq 1.5))", store: store).fact(CLJ_NODE_INVOKE) == "double/never")
				#expect(try Summarized("(fn [x] (sum-sq x))", store: store).fact(CLJ_NODE_INVOKE) == "fixnum|long|bigint|ratio|decimal|double/never")
				let mix3 = try Summarized("(fn [] (sum-mix 3))", store: store).fact(CLJ_NODE_INVOKE)
				#expect(mix3 == "fixnum|long/never")
				// a string has no domain: the generic entry, whose walk with the parameter at TOP reaches both branches
				#expect(try Summarized("(fn [] (sum-mix \"a\"))", store: store).fact(CLJ_NODE_INVOKE) == "fixnum|long|bigint|ratio|decimal|double|string/never")
				// the recursive fixpoint under a domain
				let fact5 = try Summarized("(fn [] (sum-fact 5))", store: store).fact(CLJ_NODE_INVOKE)
				#expect(fact5 == "fixnum|long/never")
				// a loop variable fed by a specialized result stays in the domain
				let t = try Summarized("(fn [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (sum-sq i))) acc)))", store: store)
				#expect(t.fact(CLJ_NODE_LOOP) == "fixnum|long/never")
				var domains: [clj_domain] = [CLJ_DOMAIN_INT64]
				let v = try rt.eval("#'sum-sq")
				#expect(clj_summary_of_var_at(store, v.raw, 1, &domains) != nil)
				domains = [CLJ_DOMAIN_ANY]
				let generic = clj_summary_of_var_at(store, v.raw, 1, &domains)
				#expect(generic != nil && describe(generic!) == "[⊤/maybe] -> fixnum|long|bigint|ratio|decimal|double/never at")
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
				#expect(t.diagnostics.count == 1 && t.errors == 1)
				let message = t.diagnostics.first ?? ""
				#expect(message.hasPrefix("E user/sum-need uses argument 0 as nil|map|sorted-map|record at "))
				#expect(message.hasSuffix(", vector is passed at 1:8"))
				#expect(t.argument(of: "user/sum-need", 0) == "vector/never=[1 2]")
				#expect(clj_facts_narrowed_args(t.table) == 0)
				// the same argument on a dead branch is no conflict
				#expect(try Summarized("(fn [x] (if (nil? x) (if (string? x) (sum-need [1 2]) 1) 2))", store: store).diagnostics.isEmpty)
				// a requirement wider than the union cap is still a requirement: (keys 1) is proven to throw
				#expect(try Summarized("(fn [] (keys 1))", store: store).diagnostics == ["E clojure.core/keys requires argument 0 to be nil|map|sorted-map|record, fixnum is passed at 1:8"])
				// inside a try that catches, the proven throw is what the code expects: a warning, not the gate's error
				let caught = try Summarized("(fn [] (try (keys 1) (catch Exception e nil)))", store: store)
				#expect(caught.errors == 0)
				#expect(caught.diagnostics == ["W clojure.core/keys requires argument 0 to be nil|map|sorted-map|record, fixnum is passed at 1:13, caught by the enclosing try"])
				#expect(try Summarized("(fn [] (try (keys 1) (finally nil)))", store: store).errors == 1)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// ⊤ meeting a declaration is a warning, on by default and off under {:facts/warnings false} in the ns meta;
		// ⊤ meeting an inferred requirement is silent.
		@Test func topIntoDeclarationWarns() throws {
			let store = clj_summaries_new()!
			defer { clj_summaries_free(store) }
			let warned = try Summarized("(fn [x] (inc x))", store: store)
			#expect(warned.errors == 0)
			#expect(warned.diagnostics == ["W clojure.core/inc declares argument 0 as fixnum|long|bigint|ratio|decimal|double, nothing is known about what is passed at 1:9"])
			// an inferred requirement: sum-inc requires a number of x because inc does, and says nothing about TOP
			#expect(try Summarized("(fn [x] (sum-inc x))", store: store).diagnostics.isEmpty)
			// something is known: no warning
			#expect(try Summarized("(fn [x] (when (some? x) (inc x)))", store: store).diagnostics.isEmpty)
			// the switch lives in the namespace's meta and the ns form's attr-map sets it
			_ = try rt.eval("(alter-meta! (the-ns 'user) assoc :facts/warnings false)")
			#expect(try rt.eval("(:facts/warnings (meta (the-ns 'user)))") == Value(false))
			#expect(try Summarized("(fn [x] (inc x))", store: store).diagnostics.isEmpty)
			_ = try rt.eval("(alter-meta! (the-ns 'user) dissoc :facts/warnings)")
			#expect(try Summarized("(fn [x] (inc x))", store: store).diagnostics.count == 1)
			_ = try rt.eval("(ns sum-quiet {:facts/warnings false}) (in-ns 'user)")
			#expect(try rt.eval("(meta (the-ns 'sum-quiet))") == Value(reading: "{:facts/warnings false}"))
			#expect(!clj_facts_warnings_enabled(try rt.eval("(the-ns 'sum-quiet)").raw))
			// a declared result the body leaves at TOP
			_ = try rt.eval("(defn sum-top {:=> [:=> [:cat :any] :int]} [x] (get x :n))")
			_ = try summary(store, "sum-top", 1)
			#expect(clj_summaries_ndiagnostics(store) == 1 && clj_summaries_nerrors(store) == 0)
		}

		// The :effects requirement of design §4: a park is legal under swap!'s mutex, so it lints and ⊤ is silent.
		@Test func effectsRequirementLintsParking() throws {
			let store = clj_summaries_new()!
			defer { clj_summaries_free(store) }
			let parked = try Summarized("(fn [a] (swap! a (fn [_] (clojure.core.async/<! sum-chan))))", store: store)
			#expect(parked.errors == 0)
			#expect(parked.diagnostics == ["W clojure.core/swap! does not allow argument 1 to park, the function passed at 1:18 parks"])
			// through a named fn, since the effect travels in the summary
			#expect(try Summarized("(fn [a] (swap! a sum-parks))", store: store).diagnostics.count == 1)
			// a known-clean f, and one nothing is known about
			#expect(try Summarized("(fn [a] (swap! a inc))", store: store).diagnostics.isEmpty)
			#expect(try Summarized("(fn [a] (swap! a (fn [x] (assoc x :n 1))))", store: store).diagnostics.isEmpty)
			// an f nothing is known about: the lint is silent, the declaration's own ⊤ warning is not
			let opaque = try Summarized("(fn [a f] (swap! a f))", store: store).diagnostics
			#expect(opaque == ["W clojure.core/swap! declares argument 1 as fn, nothing is known about what is passed at 1:11"])
			// the three states of the ladder: a park, a clean body, and a deref of something that may be a promise
			#expect(try effects(store, "sum-parks", 1) & UInt32(CLJ_EFFECT_PARK) != 0)
			#expect(try effects(store, "sum-atom-deref", 0) & UInt32(CLJ_EFFECT_OPAQUE) == 0)
			#expect(try effects(store, "sum-any-deref", 1) & UInt32(CLJ_EFFECT_OPAQUE) != 0)
			// :effects/severity :error is the other class: parking cannot work there, so a known park is an error
			let hard = try Summarized("(fn [] (sum-strict (fn [] (clojure.core.async/<! sum-chan))))", store: store)
			#expect(hard.errors == 1)
			#expect(hard.diagnostics == ["E user/sum-strict does not allow argument 0 to park, the function passed at 1:20 parks"])
			// and ⊤ under it is a warning, which the same ⊤ against the lint is not
			let hardTop = try Summarized("(fn [g] (sum-strict g))", store: store)
			#expect(hardTop.errors == 0)
			#expect(hardTop.diagnostics == ["W user/sum-strict declares argument 0 as fn, nothing is known about what is passed at 1:9",
			                                "W user/sum-strict does not allow argument 0 to park, the function passed at 1:9 may park"])
			// the lint follows the annotation: a thunk that parks under the forcing claim of a lazy seq
			#expect(try Summarized("(fn [] (lazy-seq (clojure.core.async/<! sum-chan)))", store: store).diagnostics.count == 1)
		}

		// schema → fact for the vocabulary; an unknown tag is TOP and reported as not whole, never an error.
		@Test func schemaProjection() throws {
			let before = clj_debug_live_objects()
			do {
				let rows: [(String, String, Bool)] = [
					(":any", "⊤/maybe", true),
					(":nil", "nil/always", true),
					(":int", "fixnum|long|bigint/never", true),
					(":number", "fixnum|long|bigint|ratio|decimal|double/never", true),
					(":boolean", "bool/never", true),
					(":map", "map|sorted-map|record/never", true),
					(":set", "set|sorted-set/never", true),
					(":seq", "seq/never", true),
					("[:maybe :string]", "nil|string/maybe", true),
					("[:or :keyword :symbol]", "keyword|symbol/never", true),
					("[:and :number :int]", "fixnum|long|bigint/never", true),
					("[:= 3]", "fixnum/never=3", true),
					("[:= :k]", "keyword/never=:k", true),
					("[:= \"s\"]", "string/never", true),
					("[:enum 1 2]", "fixnum/never", true),
					("[:vector {:min 4} :int]", "vector/never", true),
					("[:tuple :int :int]", "vector/never", true),
					("[:=> [:cat :int] :int]", "fn/never", true),
					("[:fn :x]", "⊤/maybe", true),
					(":array", "⊤/maybe", false),
					("[:or :int :array]", "⊤/maybe", false),
					("[:maybe :seqable]", "⊤/maybe", false),
					("[:sequential :int]", "⊤/maybe", false),
				]
				for (source, expected, whole) in rows {
					let schema = try Value(reading: source)
					var f = clj_fact()
					let ok = clj_fact_of_schema(schema.raw, &f)
					#expect(describe(f) == expected, Comment(rawValue: source))
					#expect(ok == whole, Comment(rawValue: source))
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The signature table is the AOT form of :=> metas (design §3): every entry projects to the vocabulary and
		// back, or is a listed gap — a kind without a tag, or a transfer function, which is out of scope.
		@Test func signatureTableRoundTrips() throws {
			var expressible = 0, transfer: [String] = [], gaps: [String: [String]] = [:]
			for i in 0..<clj_facts_nsignatures() {
				var name: UnsafePointer<CChar>? = nil
				var arity: UInt32 = 0, result = clj_fact(), isTransfer = false
				#expect(clj_facts_signature(i, &name, &arity, &result, &isTransfer))
				let entry = String(cString: name!)
				if isTransfer {
					transfer.append(entry)
					continue
				}
				let schema = Value(owning: clj_fact_to_schema(result))
				var back = clj_fact()
				let whole = clj_fact_of_schema(schema.raw, &back)
				if whole && clj_fact_eq(back, result) { expressible += 1 } else { gaps[describe(result), default: []].append(entry) }
			}
			let total = Int(clj_facts_nsignatures())
			#expect(expressible == total - transfer.count - gaps.values.reduce(0) { $0 + $1.count })
			#expect((expressible, total, transfer.count, gaps.values.reduce(0) { $0 + $1.count }) == (154, 201, 16, 31),
			        Comment(rawValue: "\(expressible) of \(total), \(transfer.count) transfer, gaps \(gaps.values.reduce(0) { $0 + $1.count })"))
			#expect(Set(transfer) == ["+", "-", "*", "/", "quot", "rem", "inc", "dec", "conj", "assoc", "into", "with-meta", "vary-meta", "dissoc", "disj", "empty"])
			// the kinds the vocabulary has no tag for (NOTES.md "Facts" lists them): design §3's vocabulary gap, not a second mechanism
			let gapFacts = Set(gaps.keys)
			#expect(gapFacts == ["array/never", "atom/never", "char/never", "fixnum/never", "map/never", "regex/never", "set/never",
			                     "sorted-map/never", "sorted-set/never", "uuid/never"], Comment(rawValue: gaps.sorted { $0.key < $1.key }.description))
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
			var forms = 0, bottoms = 0, errors = 0, hits: UInt32 = 0, narrowed: UInt32 = 0
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
					errors += Int(clj_facts_nerrors(table))
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
			#expect(errors == 0)
			#expect(hits > 100)
			#expect(clj_summaries_nerrors(store) == 0)
			#expect(clj_summaries_widenings(store) == 0)
		}
	}
}
