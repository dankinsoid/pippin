// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

// The kinds of a tree analyzed in `user`, in pre-order, and its codec data.
private func analyzed(_ source: String) throws -> ([clj_node_kind], String) {
	let form = try Value(reading: source)
	var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
	guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
	defer { clj_release(clj_from_ptr(node)) }
	var kinds: [clj_node_kind] = []
	func walk(_ n: UnsafePointer<clj_node>) {
		kinds.append(n.pointee.kind)
		var children: [UnsafePointer<clj_node>] = []
		withUnsafeMutablePointer(to: &children) { ctx in
			clj_node_children(n, { child, ctx in ctx!.assumingMemoryBound(to: [UnsafePointer<clj_node>].self).pointee.append(child!) }, ctx)
		}
		for c in children { walk(c) }
	}
	walk(node)
	let data = Value(owning: clj_node_to_data(node))
	return (kinds, data.description)
}

extension CoreTests {
	// Design §10 step 7a, optimizer.c: a small pure closure's arity body copied into its caller behind a guard on the root.
	@Suite struct InlineTests {
		let rt = Runtime()

		@Test func aSmallPureCalleeIsInlinedAndItsCallKept() throws {
			_ = try rt.eval("(defn il-sq [x] (* x x)) (defn il-use [y] (il-sq (inc y)))")
			#expect(try rt.eval("(il-use 2)") == 9)
			let (kinds, data) = try analyzed("(fn [y] (il-sq (inc y)))")
			#expect(kinds.contains(CLJ_NODE_ROOT_IS) && kinds.contains(CLJ_NODE_INLINE))
			// the codec writes the call the guard falls back to, never the copy
			#expect(!data.contains("root-is") && data.contains("[:invoke [:var user/il-sq]"))
			#expect(try rt.eval("(with-redefs [il-sq (fn [x] (- x))] (il-use 2))") == -3)
			_ = try rt.eval("(defn il-sq [x] (+ x x))")
			#expect(try rt.eval("(il-use 2)") == 6)
		}

		// Effects past alloc and throw, recursion, a closure in the body, a dynamic var, a top-level call: the call stays.
		@Test func whatIsNotInlined() throws {
			_ = try rt.eval("""
			(defn il-loud [x] (println x) x)
			(defn il-rec [n] (if (pos? n) (il-rec (dec n)) n))
			(defn il-maker [x] (fn [] x))
			(defn ^:dynamic il-dyn [x] x)
			(defn il-small [x] (inc x))
			""")
			for call in ["(il-loud 1)", "(il-rec 3)", "(il-maker 1)", "(il-dyn 1)"] {
				#expect(!(try analyzed("(fn [] \(call))").0.contains(CLJ_NODE_INLINE)), Comment(rawValue: call))
			}
			#expect(!(try analyzed("(il-small 1)").0.contains(CLJ_NODE_INLINE)))
			#expect(try analyzed("(fn [] (il-small 1))").0.contains(CLJ_NODE_INLINE))
			let was = clj_inline_set_enabled(false)
			defer { _ = clj_inline_set_enabled(was) }
			#expect(!(try analyzed("(fn [] (il-small 1))").0.contains(CLJ_NODE_INLINE)))
		}

		// The callee's frame in a trace, at the call's position, and the arguments once each, in order.
		@Test func traceAndArguments() throws {
			_ = try rt.eval("""
			(defn il-boom [x] (if (pos? x) (throw (ex-info "boom" {})) x))
			(defn il-call [x] (il-boom x))
			(defn il-pair [a b] [a b])
			""")
			#expect(try rt.eval("(try (il-call 1) (catch :default e (subvec (mapv :fn (ex-trace e)) 0 2)))") == Value(reading: "[user/il-boom user/il-call]"))
			#expect(try rt.eval("((fn [] (let [log (atom [])] [(il-pair (do (swap! log conj 1) 1) (do (swap! log conj 2) 2)) @log])))") == Value(reading: "[[1 2] [1 2]]"))
		}
	}
}
