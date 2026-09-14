// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

// An analyzed tree with its exec table; releases both.
private final class Tree {
	let node: UnsafeMutablePointer<clj_node>
	let exec: clj_value

	init(node: UnsafeMutablePointer<clj_node>) {
		self.node = node
		exec = clj_exec_new(node)
	}

	static func analyze(_ source: String) throws -> Tree {
		let form = try Value(reading: source)
		guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, nil) }) else { throw ClojureError.takePending() }
		return Tree(node: node)
	}

	static func read(_ data: Value) throws -> Tree {
		guard let node = withExtendedLifetime(data, { clj_node_from_data(data.raw) }) else { throw ClojureError.takePending() }
		return Tree(node: node)
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

	var count: UInt32 { node.pointee.nnodes }

	deinit {
		clj_release(exec)
		clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
	}
}

// The error message of a failing step, nil when it succeeds.
private func message<T>(_ body: () throws -> T) -> String? {
	do {
		_ = try body()
		return nil
	} catch {
		return (error as? ClojureError)?.message ?? "\(error)"
	}
}

extension CoreTests {
	@Suite struct NodeDataTests {
		init() {
			clj_init()
			for k in ["const", "local", "captured", "var", "the-var", "if", "do", "let", "loop", "recur", "fn", "invoke", "def", "vector", "map",
			          "try", "throw", "all", "error", "default", "k", "code", "kw", "yes", "no", "nope"] {
				_ = Value(keyword: k)
			}
		}

		// to_data -> pr-str -> read -> from_data yields the same data, and the read-back tree runs the same.
		@Test func roundTrip() throws {
			_ = try cljEval("(def nd-x) (def nd-f)")
			let before = clj_debug_live_objects()
			do {
				let forms = [
					"(let [x 1] [x \\a 1.5 \"s\" nil true 'sym :kw '(1 2) {:k [x]}])",
					"(let [a 1 b (+ a 1)] (if (< a b) [a b (+ a b)] {:k a}))",
					"(if (let [x 2] (> x 1)) :yes)",
					"(loop [i 0 acc []] (if (< i 3) (recur (+ i 1) (conj acc i)) acc))",
					"(let [k 10 f (fn nd-self ([] (nd-self 1)) ([x] (+ x k)) ([x & more] (apply + x k more)))] [(f) (f 5) (f 1 2 3)])",
					"(let [a 1] ((fn [] (let [g (fn [] a)] (g)))))",
					"(try (throw (ex-info \"boom\" {:code 7})) (catch ExceptionInfo e (ex-data e)) (finally (println \"cleanup\")))",
					"(try (throw 42) (catch ExceptionInfo e :no) (catch :default e [e]))",
					"(try (println \"body\") (finally (println \"done\")))",
					"(def nd-x (+ 20 22))",
					"(def nd-f (fn [n] (if (< n 1) 0 (+ n (nd-f (- n 1))))))",
					"(do (println \"side\" nd-x (nd-f 4)) [nd-x (var nd-x)])",
					"(let [x 5] {:k x (+ x 1) [x]})",
				]
				for source in forms {
					let analyzed = try Tree.analyze(source)
					let data = try analyzed.data()
					let text = try #require(Value(owning: clj_pr_str(data.raw)).string)
					let read = try Tree.read(try Value(reading: text))
					#expect(try read.data() == data, Comment(rawValue: source))
					#expect(read.count == analyzed.count, Comment(rawValue: source))
					var first: Value = nil, second: Value = nil
					let out1 = try capturingOutput { first = try analyzed.run() }
					let out2 = try capturingOutput { second = try read.run() }
					#expect(first == second, Comment(rawValue: source))
					#expect(out1 == out2, Comment(rawValue: source))
				}
				#expect(try cljEval("[nd-x (nd-f 3)]") == [42, 6])
				_ = try cljEval("(def nd-x nil) (def nd-f nil)")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every node carries the position of its innermost enclosing list as a trailing pair (a list a macro
		// rebuilt has none: `a` in `([] a)` reports the fn form); a tree read from data without positions
		// encodes without them.
		@Test func encodingShape() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree.analyze("(let [x 1] (if x (inc x) [x]))")
				#expect(try tree.data() == Value(reading: "[:let [[0 [:const 1 1 1]]] [:if [:local 0 1 12] [:invoke [:var clojure.core/inc 1 18] [:local 0 1 18] 1 18] [:vector [:local 0 1 12] 1 12] 1 12] 1 1]"))
				let fn = try Tree.analyze("(let [a 1] (fn ([] a) ([x & r] (recur x r))))")
				#expect(try fn.data() == Value(reading: "[:let [[0 [:const 1 1 1]]] [:fn nil [[0 false nil 0 [:captured 0 1 12]] [1 true nil 2 [:recur [0 1] [[:local 0 1 32] [:local 1 1 32]] 1 32]]] [[:local 0]] 1 12] 1 1]"))
				let bare = try Tree.read(Value(reading: "[:let [[0 [:const 1]]] [:if [:local 0] [:invoke [:var clojure.core/inc] [:local 0]] [:vector [:local 0]]]]"))
				#expect(try bare.data() == Value(reading: "[:let [[0 [:const 1]]] [:if [:local 0] [:invoke [:var clojure.core/inc] [:local 0]] [:vector [:local 0]]]]"))
				#expect(bare.node.pointee.line == 0)
				let positioned = try Tree.read(Value(reading: "[:if [:const true 3 4] [:const 1] [:const 2] 3 2]"))
				#expect(positioned.node.pointee.line == 3 && positioned.node.pointee.col == 2)
				#expect(positioned.node.pointee.u.if_.test.pointee.line == 3 && positioned.node.pointee.u.if_.test.pointee.col == 4)
				#expect(positioned.node.pointee.u.if_.then.pointee.line == 0)
				#expect(try positioned.data() == Value(reading: "[:if [:const true 3 4] [:const 1] [:const 2] 3 2]"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A constant a macro smuggled in from the host has no printed form that reads back.
		@Test func notSerializable() throws {
			_ = try cljEval("(defmacro nd-embed [] inc) (defmacro nd-embed-in-vec [] [inc])")
			let before = clj_debug_live_objects()
			do {
				#expect(message { try Tree.analyze("(nd-embed)").data() } == "not serializable: fn")
				#expect(message { try Tree.analyze("[1 (nd-embed-in-vec)]").data() } == "not serializable: fn")
				#expect(message { try Tree.read(Value(reading: "[:nope 1]")) } == "malformed node data, unknown node kind: [:nope 1]")
				#expect(message { try Tree.read(Value(reading: "[:if [:const 1]]")) } == "malformed node data, expected [test then else?]: [:if [:const 1]]")
				#expect(message { try Tree.read(Value(reading: "[:fn nil [[1 false nil 1 [:local 1]]] []]")) } == "malformed node data, slot 1 outside a frame of 1")
				#expect(message { try Tree.read(Value(reading: "[:fn nil [[0 false nil 0 [:captured 0]]] []]")) } == "malformed node data, capture 0 outside an environment of 0")
				#expect(message { try Tree.read(Value(reading: "[:fn nil [[0 false nil 0 [:fn nil [[0 false nil 0 [:const 1]]] [[:local 1]]]]] []]")) } == "malformed node data, capture source 1 outside the creating frame")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
