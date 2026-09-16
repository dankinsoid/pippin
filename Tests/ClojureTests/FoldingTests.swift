// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

private func kw(_ s: String) -> Value { Value(keyword: s) }

// An analyzed tree with its exec table; releases both.
private final class Tree {
	let node: UnsafeMutablePointer<clj_node>
	let exec: clj_value

	init(_ source: String) throws {
		let form = try Value(reading: source)
		var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
		guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
		self.node = node
		exec = clj_exec_new(node)
	}

	func data() throws -> Value {
		let raw = clj_node_to_data(node)
		if raw == CLJ_THROWN { throw ClojureError.takePending() }
		return Value(owning: raw)
	}

	func run() throws -> Value {
		let raw = clj_exec_run(exec)
		if raw == CLJ_THROWN { throw ClojureError.takePending() }
		return Value(owning: raw)
	}

	var kinds: [clj_node_kind] {
		var out: [clj_node_kind] = []
		collect(node, &out)
		return out
	}

	private func collect(_ n: UnsafePointer<clj_node>, _ out: inout [clj_node_kind]) {
		out.append(n.pointee.kind)
		var children: [UnsafePointer<clj_node>] = []
		withUnsafeMutablePointer(to: &children) { ctx in
			clj_node_children(n, { child, ctx in
				ctx!.assumingMemoryBound(to: [UnsafePointer<clj_node>].self).pointee.append(child!)
			}, ctx)
		}
		for c in children { collect(c, &out) }
	}

	deinit {
		clj_release(exec)
		clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
	}
}

private func message<T>(_ body: () throws -> T) -> String? {
	do {
		_ = try body()
		return nil
	} catch {
		return (error as? ClojureError)?.message ?? "\(error)"
	}
}

private func coreVar(_ name: String) -> clj_value {
	let sym = Value(symbol: name)
	return withExtendedLifetime(sym) { clj_ns_resolve(clj_ns_core(), sym.raw) }
}

extension CoreTests {
	// The optimizer folds a pure intrinsic on constant arguments and an `if` on a constant test (optimizer.c).
	@Suite struct FoldingTests {
		let rt = Runtime()

		init() {
			for k in ["a", "b", "x", "none", "yes", "no", "never", "const", "local", "captured", "var", "the-var", "if", "do", "let", "loop", "recur", "fn", "invoke",
			          "intrinsic", "def", "vector", "map", "try", "throw", "all", "error", "fused", "outer", "direct-fn", "direct-call", "fn", "line",
			          "column", "name"] { _ = kw(k) }
		}

		// The folded node keeps the call's position; the value is what the call would have produced.
		@Test func pureIntrinsicsOnConstantsFold() throws {
			let before = clj_debug_live_objects()
			do {
				let cases: [(String, String)] = [
					("(+ 1 2)", "[:const 3 1 1]"),
					("(+ 1 (* 2 3))", "[:const 7 1 1]"),
					("(inc (- 10 4))", "[:const 7 1 1]"),
					("(/ 1 2)", "[:const 1/2 1 1]"),
					("(< 1 2)", "[:const true 1 1]"),
					("(= [1 {:a 2}] [1 {:a 2}])", "[:const true 1 1]"),
					("(not= :a :a)", "[:const false 1 1]"),
					("(nil? nil)", "[:const true 1 1]"),
					("(vector? [])", "[:const true 1 1]"),
					("(get {:a [1 2]} :a)", "[:const [1 2] 1 1]"),
					("(get {:a 1} :b 2)", "[:const 2 1 1]"),
					("(nth [1 2 3] 1)", "[:const 2 1 1]"),
					("(nth [1 2 3] 5 :none)", "[:const :none 1 1]"),
					("(count [1 2 3])", "[:const 3 1 1]"),
					("(count nil)", "[:const 0 1 1]"),
					("(first \"ab\")", "[:const \\a 1 1]"),
					("(first '(1 2))", "[:const 1 1 1]"),
					("(rest '(1 2))", "[:const (2) 1 1]"),
					("(next '(1))", "[:const nil 1 1]"),
					("(conj [1] 2)", "[:const [1 2] 1 1]"),
					("(conj nil 1)", "[:const (1) 1 1]"),
					("(assoc {} :a 1)", "[:const {:a 1} 1 1]"),
					("(assoc [1 2] 0 :x)", "[:const [:x 2] 1 1]"),
					("(contains? {:a 1} :a)", "[:const true 1 1]"),
					("(empty? [])", "[:const true 1 1]"),
					("(seq '(1))", "[:const (1) 1 1]"),
				]
				for (source, expected) in cases {
					let tree = try Tree(source)
					#expect(try tree.data() == Value(reading: expected), Comment(rawValue: source))
					#expect(try tree.run() == Value(reading: expected).array?[1], Comment(rawValue: source))
				}
				// Nested inside other nodes, and only where every argument is a constant.
				#expect(try Tree("(let [x (+ 1 2)] (+ x (* 2 3)))").data()
					== Value(reading: "[:let [[0 [:const 3 1 9]]] [:intrinsic clojure.core/+ [:local 0 1 18] [:const 6 1 23] 1 18] 1 1]"))
				#expect(try Tree("[(+ 1 2) (inc 4)]").data() == Value(reading: "[:vector [:const 3 1 2] [:const 5 1 10]]"))
				#expect(try Tree("(fn [] (+ 1 2))").kinds == [CLJ_NODE_FN, CLJ_NODE_CONST])
				#expect(try Tree("(count (fn [] 1))").kinds == [CLJ_NODE_INTRINSIC, CLJ_NODE_FN, CLJ_NODE_CONST])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A result the codec reads back as another type, or an argument it cannot carry, keeps the call.
		@Test func onlyWhatReadsBackAsItselfFolds() throws {
			_ = try rt.eval("(defmacro fold-fns [] [inc])")
			let before = clj_debug_live_objects()
			do {
				for source in ["(seq [1 2])", "(rest [1 2])", "(seq \"ab\")", "(next [1 2 3])", "(cons 1 [2])", "(cons 1 '(2))"] {
					let kinds = try Tree(source).kinds
					#expect(kinds.first == CLJ_NODE_INTRINSIC && kinds.dropFirst().allSatisfy { $0 == CLJ_NODE_CONST }, Comment(rawValue: source))
				}
				#expect(try Tree("(seq [1 2])").run() == Value(list: [1, 2]))
				#expect(try Tree("(cons 1 [2])").run() == Value(list: [1, 2]))
				// A fn inside a constant vector is no fold input, whatever the accessor.
				#expect(try Tree("(first (fold-fns))").kinds == [CLJ_NODE_INTRINSIC, CLJ_NODE_CONST])
				#expect(try Tree("(count (fold-fns))").kinds == [CLJ_NODE_INTRINSIC, CLJ_NODE_CONST])
				#expect(try Tree("(count (fold-fns))").run() == 1)
				// A var constant is not data either.
				#expect(try Tree("(nil? (var fold-fns))").kinds == [CLJ_NODE_INTRINSIC, CLJ_NODE_CONST])
			}
			#expect(clj_debug_live_objects() == before)
			_ = try rt.eval("(def fold-fns nil)")
		}

		// A fold that throws leaves the node: the program throws at run time, from the same node.
		@Test func throwingFoldStaysACall() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try Tree("(/ 1 0)").data() == Value(reading: "[:intrinsic clojure.core// [:const 1 1 1] [:const 0 1 1] 1 1]"))
				#expect(message { try Tree("(/ 1 0)").run() } == "Divide by zero")
				#expect(try Tree("(nth [1] 5)").kinds == [CLJ_NODE_INTRINSIC, CLJ_NODE_CONST, CLJ_NODE_CONST])
				#expect(message { try Tree("(nth [1] 5)").run() } == "Index 5 out of bounds for length 1")
				#expect(message { try Tree("(+ 1 \"a\")").run() } == "string cannot be cast to a number")
				#expect(message { try Tree("(+ 9223372036854775807 1)").run() } == "integer overflow")
				#expect(message { try Tree("(first 1)").run() } == "Don't know how to create ISeq from: long")
				// Inside an if, the throwing branch is what remains.
				#expect(try Tree("(if true (/ 1 0) 2)").data() == Value(reading: "[:intrinsic clojure.core// [:const 1 1 10] [:const 0 1 10] 1 10]"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The taken branch replaces the if, with its own position; a missing else is nil.
		@Test func ifOnAConstantTestFolds() throws {
			let before = clj_debug_live_objects()
			do {
				let cases: [(String, String)] = [
					("(if true 1 2)", "[:const 1 1 1]"),
					("(if false 1 2)", "[:const 2 1 1]"),
					("(if nil 1)", "[:const nil 1 1]"),
					("(if 0 :yes :no)", "[:const :yes 1 1]"),
					("(if (< 1 2) :yes :no)", "[:const :yes 1 1]"),
					("(if (< 2 1) (println \"x\") (+ 1 2))", "[:const 3 1 27]"),
					("(when false (println \"x\"))", "[:const nil 1 1]"),
					("(cond false 1 :else 2)", "[:const 2 1 1]"),
					("(if true (if false 1 2) 3)", "[:const 2 1 10]"),
					("(let [x 1] (if true (inc x) x))", "[:let [[0 [:const 1 1 1]]] [:intrinsic clojure.core/inc [:local 0 1 21] 1 21] 1 1]"),
					("(let [x 1] (if false (inc x) x))", "[:let [[0 [:const 1 1 1]]] [:local 0 :last 1 12] 1 1]"),
				]
				for (source, expected) in cases {
					let tree = try Tree(source)
					#expect(try tree.data() == Value(reading: expected), Comment(rawValue: source))
				}
				#expect(try Tree("(if 0 :yes :no)").run() == kw("yes"))
				#expect(try Tree("(let [x 1] (if true (inc x) x))").run() == 2)
				// A local's test stays an if.
				#expect(try Tree("(let [x true] (if x 1 2))").kinds == [CLJ_NODE_LET, CLJ_NODE_CONST, CLJ_NODE_IF, CLJ_NODE_LOCAL, CLJ_NODE_CONST, CLJ_NODE_CONST])
				// A direct call in the taken branch still reaches its fn; a recur in it still reaches its loop.
				let direct = try Tree("(let [f (fn [x] (inc x))] (if true (f 1) 2))")
				#expect(direct.kinds == [CLJ_NODE_LET, CLJ_NODE_DIRECT_FN, CLJ_NODE_INTRINSIC, CLJ_NODE_LOCAL, CLJ_NODE_DIRECT_CALL, CLJ_NODE_CONST])
				#expect(try direct.run() == 2)
				#expect(try Tree("(loop [i 0] (if (< i 3) (if true (recur (inc i)) :never) i))").run() == 3)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A var rebound before analysis is called through the guard, not folded with the boot fn.
		@Test func reboundVarIsNotFolded() throws {
			let plus = coreVar("+")
			let boot = clj_var_root(plus)
			let before = clj_debug_live_objects()
			do {
				do {
					let minus = try rt.eval("(fn [a b] (- a b))")
					withExtendedLifetime(minus) { clj_var_bind_root(plus, minus.raw) }
				}
				let tree = try Tree("(+ 1 2)")
				#expect(tree.kinds == [CLJ_NODE_INTRINSIC, CLJ_NODE_CONST, CLJ_NODE_CONST])
				#expect(try tree.run() == -1)
				clj_var_bind_root(plus, boot)
				#expect(try tree.run() == 3)
				#expect(try Tree("(+ 1 2)").kinds == [CLJ_NODE_CONST])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The var's meta and the frames of a runtime error come from nodes folding does not touch.
		@Test func defMetaAndErrorPositionsAreUnchanged() throws {
			_ = try rt.eval("(def fold-x) (def fold-boom) (def fold-boom-x)")
			let before = clj_debug_live_objects()
			do {
				#expect(try Tree("(def fold-x (+ 1 2))").data()
					== Value(reading: "[:def user/fold-x [:const 3 1 13] [:const {:ns user :name fold-x :line 1 :column 1} 1 1] false false 1 1]"))
				_ = try rt.eval("(def fold-x (+ 1 2))")
				#expect(try rt.eval("[fold-x (:line (meta #'fold-x)) (:column (meta #'fold-x)) (:name (meta #'fold-x))]") == [3, 1, 1, Value(symbol: "fold-x")])
				_ = try rt.eval("(defn fold-boom [] (nth [1] 5))\n(defn fold-boom-x [x] (nth [1] x))")
				let report = "(catch :default e (let [t (ex-trace e)] [(ex-message e) (count t) (:line (first t)) (:column (first t))]))"
				let folded = try rt.eval("(try (fold-boom) \(report))")
				let unfolded = try rt.eval("(try (fold-boom-x 5) \(report))")
				#expect(folded == unfolded)
				#expect(folded == ["Index 5 out of bounds for length 1", 1, 1, 6])
				_ = try rt.eval("(def fold-x nil) (def fold-boom nil) (def fold-boom-x nil)")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
