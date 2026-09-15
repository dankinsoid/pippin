// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

private func kw(_ s: String) -> Value { Value(keyword: s) }

private func clojureError(_ rt: Runtime, _ source: String) -> ClojureError? {
	do {
		_ = try rt.eval(source)
		return nil
	} catch let e as ClojureError {
		return e
	} catch {
		return nil
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

private func userVar(_ name: String) -> clj_value {
	let sym = Value(symbol: name)
	return withExtendedLifetime(sym) { clj_ns_resolve(clj_ns_user(), sym.raw) }
}

// An analyzed tree with its exec: its node kinds tell which path a call takes, its site counters the closure one.
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

	init(data: Value) throws {
		guard let node = withExtendedLifetime(data, { clj_node_from_data(data.raw) }) else { throw ClojureError.takePending() }
		self.node = node
		exec = clj_exec_new(node)
	}

	deinit {
		clj_release(exec)
		clj_release(clj_from_ptr(node))
	}

	func run() throws -> Value {
		let raw = clj_exec_run(exec)
		if raw == CLJ_THROWN { throw ClojureError.takePending() }
		return Value(owning: raw)
	}

	func data() throws -> Value {
		let raw = clj_node_to_data(node)
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

	var directFns: Int { kinds.filter { $0 == CLJ_NODE_DIRECT_FN }.count }
	var closures: Int { kinds.filter { $0 == CLJ_NODE_FN }.count }
	var directCalls: Int { kinds.filter { $0 == CLJ_NODE_DIRECT_CALL }.count }

	func hits(_ site: UInt32 = 0) -> Int64 { clj_debug_exec_ic_hits(exec, clj_debug_exec_invoke_id(exec, site)) }
}

extension CoreTests {
	// A let/loop-bound fn used only as the head of calls runs without a closure (optimizer.c, direct_pass).
	@Suite struct DirectCallTests {
		let rt = Runtime()

		init() {
			for k in ["y", "h", "x", "n", "done", "outer", "direct-fn", "direct-call", "const", "local", "captured", "var", "the-var", "if", "do", "let",
			          "loop", "recur", "fn", "invoke", "intrinsic", "def", "vector", "map", "try", "throw", "all", "error", "fused"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		// Runs the source and reports how many direct calls it made against the closure sites it hit.
		private func paths(_ source: String, _ expected: Value) throws -> (direct: Int64, tree: Tree) {
			let tree = try Tree(source)
			let before = clj_debug_direct_calls()
			#expect(try tree.run() == expected, Comment(rawValue: source))
			return (clj_debug_direct_calls() - before, tree)
		}

		@Test func headOnlyUsesBecomeDirect() throws {
			let before = clj_debug_live_objects()
			do {
				let (direct, tree) = try paths("(let [f (fn [x] (inc x))] (loop [i 0] (if (< i 5) (recur (f i)) i)))", 5)
				#expect(direct == 5 && tree.directFns == 1 && tree.closures == 0 && tree.directCalls == 1)
				let (d2, t2) = try paths("(let [f (fn [] 1) x (f)] [x (f)])", [1, 1])
				#expect(d2 == 2 && t2.directFns == 1 && t2.directCalls == 2)
				let (d3, t3) = try paths("(let [f (fn ([] 0) ([x] x) ([x y] (+ x y)))] [(f) (f 1) (f 1 2)])", [0, 1, 3])
				#expect(d3 == 3 && t3.directFns == 1 && t3.directCalls == 3)
				let (d4, t4) = try paths("(loop [i 0 acc []] (let [f (fn [] i)] (if (< i 3) (recur (inc i) (conj acc (f))) acc)))", [0, 1, 2])
				#expect(d4 == 3 && t4.directFns == 1)
				let (d5, t5) = try paths("(loop [i 0 acc 0] (if (< i 5) (let [add (fn [x] (+ acc x))] (recur (inc i) (add i))) acc))", 10)
				#expect(d5 == 5 && t5.directFns == 1)
				let (d6, t6) = try paths("(let [f (fn [x] x)] (f (f 1)))", 1)
				#expect(d6 == 2 && t6.directCalls == 2)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every disqualifier keeps the closure and its behaviour.
		@Test func escapesKeepTheClosure() throws {
			let before = clj_debug_live_objects()
			do {
				let cases: [(String, Value)] = [
					("(let [f (fn [x] x)] (identity f) (f 1))", 1),
					("(let [f (fn [x] x)] [(f 1) (fn? f)])", [1, true]),
					("(let [f (fn [x] x)] ((fn [] (f 2))))", 2),
					("(let [f (fn [x] x)] (loop [i 0 g f] (if (< i 2) (recur (inc i) f) (g i))))", 2),
					("(loop [f (fn [x] x) i 0] (if (< i 2) (recur f (inc i)) (f i)))", 2),
					("(loop [f (fn [x] x) i 0] (if (< i 2) (recur (fn [x] (inc x)) (inc i)) (f i)))", 3),
					("(let [f (fn [x] x)] (try (f 1) (catch :default e f) (finally nil)))", 1),
					("(let [f (fn [x & more] [x more])] (f 1 2))", [1, Value(list: [2])]),
					("(let [f (fn [x] x) g f] (g 1))", 1),
					("(let [f (fn [x] x)] (doall (map (fn [y] (f y)) [1 2])))", Value(list: [1, 2])),
					("(let [f (fn f [n] (if (pos? n) (first (map f [(dec n)])) n))] (f 3))", 0),
				]
				for (source, expected) in cases {
					let tree = try Tree(source)
					let calls = clj_debug_direct_calls()
					#expect(try tree.run() == expected, Comment(rawValue: source))
					#expect(tree.directFns == 0 && tree.directCalls == 0, Comment(rawValue: source))
					#expect(tree.closures >= 1, Comment(rawValue: source))
					#expect(clj_debug_direct_calls() == calls, Comment(rawValue: source))
				}
				// A call past the fn's arities keeps the closure and the runtime error it gives.
				let mismatch = try Tree("(let [f (fn [x] x)] (f 1 2))")
				#expect(mismatch.directFns == 0)
				#expect(message { try mismatch.run() } == "Wrong number of args (2) passed to: fn")
				let named = try Tree("(let [f (fn named [x] x)] (f))")
				#expect(named.directFns == 0)
				#expect(message { try named.run() } == "Wrong number of args (0) passed to: named")
				// A mix: the escaping one stays a closure, the head-only one goes direct.
				let mixed = try Tree("(let [f (fn [x] x) g (fn [y] (* 2 y))] [(g 2) (map f [1])])")
				#expect(try mixed.run() == [4, Value(list: [1])])
				#expect(mixed.directFns == 1 && mixed.closures == 1)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Non-tail recursion gets a frame per activation; tail recursion is a recur in the same frame.
		@Test func recursion() throws {
			let before = clj_debug_live_objects()
			do {
				let (d1, t1) = try paths("(let [fact (fn fact [n] (if (< n 2) 1 (* n (fact (dec n)))))] (fact 10))", 3628800)
				#expect(d1 == 10 && t1.directFns == 1 && t1.directCalls == 2)
				let (d2, t2) = try paths("(let [down (fn down [n acc] (if (zero? n) acc (down (dec n) (conj acc n))))] (down 40 []))", Value((1...40).reversed().map { Value($0) }))
				#expect(d2 == 41 && t2.directFns == 1)
				let (d3, t3) = try paths("(let [sum (fn [n acc] (if (zero? n) acc (recur (dec n) (+ acc n))))] (sum 1000 0))", 500500)
				#expect(d3 == 1 && t3.directFns == 1)
				// Each activation keeps its own param after the inner call returns.
				let (d4, _) = try paths("(let [f (fn f [n] (if (zero? n) [] (conj (f (dec n)) n)))] (f 5))", [1, 2, 3, 4, 5])
				#expect(d4 == 6)
				// Two arities, the self call crossing them.
				let (d5, _) = try paths("(let [f (fn f ([n] (f n 0)) ([n acc] (if (zero? n) acc (f (dec n) (+ acc n)))))] (f 4))", 10)
				#expect(d5 == 6)
				// Mutual recursion through an inner direct fn: g's frame links to f's, f's to the top one.
				let (d6, t6) = try paths("(let [f (fn f [n] (let [g (fn [m] (if (pos? m) (f (dec m)) :done))] (g n)))] (f 3))", kw("done"))
				#expect(d6 == 8 && t6.directFns == 2 && t6.closures == 0)
				// The stack guard covers direct frames as it does closures.
				#expect(message { try Tree("(let [f (fn f [n] (+ 1 (f n)))] (f 0))").run() } == "Stack overflow")
				#expect(clj_shadow_stack_depth() == 0)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A free variable is read from the defining frame through the static link, at any nesting depth.
		@Test func freeVariablesAndShadowing() throws {
			let before = clj_debug_live_objects()
			do {
				let (d1, t1) = try paths("(let [x 10 f (fn [y] (+ x y))] (let [x 20] (f 1)))", 11)
				#expect(d1 == 1 && t1.directFns == 1)
				let (d2, t2) = try paths("(let [f (fn [] 1)] (let [f (fn [] 2)] (f)))", 2)
				#expect(d2 == 1 && t2.directFns == 2)
				let (d3, t3) = try paths("(let [f (fn [] 1) g (fn [] (f)) f (fn [] 3)] [(g) (f)])", [1, 3])
				#expect(d3 == 3 && t3.directFns == 3)
				let (d4, t4) = try paths("(let [a 1 f (fn [x] (let [b 2 g (fn [y] (let [h (fn [z] (+ a b x y z))] (h 5)))] (g 4)))] (f 3))", 15)
				#expect(d4 == 3 && t4.directFns == 3 && t4.closures == 0)
				// A closure made inside a direct body captures through the link and outlives the call.
				let (d5, t5) = try paths("(let [k 7 f (fn [x] (fn [] (+ k x)))] (let [g (f 1)] (g)))", 8)
				#expect(d5 == 1 && t5.directFns == 1 && t5.closures == 1)
				// The definer's captured environment is shared: a closure's let-bound helper reads its captures.
				#expect(try rt.eval("(let [k 3 outer (fn [x] (let [h (fn [y] (+ k x y))] (h 1)))] (outer 2))") == 6)
				#expect(try rt.eval("((fn [k] (let [h (fn [y] (+ k y))] (h 1))) 41)") == 42)
				let (d6, _) = try paths("(let [f (fn [x] x) g (fn [y] (f (f y)))] (g 1))", 1)
				#expect(d6 == 3)
				// Reading a loop var from a helper defined per iteration sees that iteration's value.
				#expect(try rt.eval("(loop [i 0 acc []] (if (< i 3) (let [f (fn [] i) g (fn [] (f))] (recur (inc i) (conj acc (g)))) acc))") == [0, 1, 2])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A throw inside the body unwinds through the direct frame: the trace names the fn as a closure would.
		@Test func errorsAndTraces() throws {
			try declare("dc-outer", "dc-anon", "dc-arity")
			_ = try rt.eval("""
			(defn dc-outer [x] (let [helper (fn helper [y] (throw (ex-info "h" {:y y})))] (helper x)))
			(defn dc-anon [x] (let [h (fn [y] (throw (ex-info "anon" {:y y})))] (h x)))
			(defn dc-arity [x] (let [h (fn [y] (+ y 1))] (h x)))
			""")
			let before = clj_debug_live_objects()
			do {
				let named = try #require(clojureError(rt, "(dc-outer 1)"))
				#expect(named.message == "h")
				#expect(named.trace.map(\.fn) == ["helper", "user/dc-outer"])
				#expect(named.trace[0].line == 1)
				let anon = try #require(clojureError(rt, "(dc-anon 2)"))
				#expect(anon.trace.map(\.fn) == [nil, "user/dc-anon"])
				#expect(clojureError(rt, "(dc-arity \"s\")")?.message == "string cannot be cast to a number")
				#expect(clj_shadow_stack_depth() == 0)
				#expect(try rt.eval("(try (dc-outer 3) (catch ExceptionInfo e (:y (ex-data e))))") == 3)
				// A catch inside the body binds a slot of the direct frame.
				#expect(try rt.eval("(let [f (fn [x] (try (throw (ex-info \"in\" {:x x})) (catch :default e (:x (ex-data e)))))] [(f 1) (f 2)])") == [1, 2])
				// An owned param released on the throw path.
				#expect(try rt.eval("(let [f (fn [v] (throw (ex-info \"v\" {:n (count v)})))] (try (f (vector 1 2)) (catch :default e (:n (ex-data e)))))") == 2)
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("dc-outer", "dc-anon", "dc-arity")
		}

		// [:direct-fn ...] and [:direct-call slot depth ...] round-trip; malformed shapes are refused.
		@Test func serializationRoundTrip() throws {
			let before = clj_debug_live_objects()
			do {
				for source in ["(let [f (fn [x] (inc x))] (f 1))",
				               "(let [k 2 f (fn f [n] (if (pos? n) (f (dec n)) k))] (f 3))",
				               "(let [a 1 f (fn [x] (let [g (fn [y] (+ a x y))] (g 1)))] [(f 2) (let [c (fn [] a)] (c))])",
				               "(let [k 7 f (fn [x] (fn [] (+ k x)))] ((f 1)))",
				               "(loop [i 0 acc 0] (if (< i 3) (let [add (fn [x] (+ acc x))] (recur (inc i) (add i))) acc))"] {
					let tree = try Tree(source)
					let data = try tree.data()
					#expect(data.description.contains(":direct-fn") && data.description.contains(":direct-call"), Comment(rawValue: source))
					let text = try #require(Value(owning: clj_pr_str(data.raw)).string)
					let read = try Tree(data: Value(reading: text))
					#expect(try read.data() == data, Comment(rawValue: source))
					#expect(read.node.pointee.nnodes == tree.node.pointee.nnodes)
					#expect(read.kinds == tree.kinds, Comment(rawValue: source))
					#expect(try read.run() == tree.run(), Comment(rawValue: source))
				}
				#expect(try Tree("(let [x 1 f (fn [] x)] (f))").data() == Value(reading: "[:let [[0 [:const 1 1 1]] [1 [:direct-fn nil [[0 false nil 0 [:outer [1 0] 1 13]]] 1 13]]] [:direct-call [1 0] 1 24] 1 1]"))
				#expect(message { try Tree(data: Value(reading: "[:direct-call [0 0]]")) } == "malformed node data, no direct fn bound at that slot: [:direct-call [0 0]]")
				#expect(message { try Tree(data: Value(reading: "[:direct-fn nil [[0 false nil 0 [:const 1]]]]")) } == "malformed node data, direct fn outside a let/loop binding: [:direct-fn nil [[0 false nil 0 [:const 1]]]]")
				#expect(message { try Tree(data: Value(reading: "[:let [[0 [:direct-fn nil [[0 false nil 0 [:const 1]]]]]] [:direct-call [0 0] [:const 2]]]")) } == "malformed node data, the direct fn has no such arity: [:direct-call [0 0] [:const 2]]")
				#expect(message { try Tree(data: Value(reading: "[:let [[0 [:direct-fn nil [[0 false nil 0 [:const 1]]]]]] [:direct-call [0 1]]]")) } == "malformed node data, no direct fn bound at that slot: [:direct-call [0 1]]")
				#expect(message { try Tree(data: Value(reading: "[:let [[0 [:direct-fn nil [[1 true nil 2 [:const 1]]]]]] [:const 2]]")) } == "malformed node data, a direct fn cannot be variadic: [:direct-fn nil [[1 true nil 2 [:const 1]]]]")
				#expect(message { try Tree(data: Value(reading: "[:let [[0 [:direct-fn nil [[0 false nil 0 [:outer [2 0]]]]]]] [:const 2]]")) } == "malformed node data, no frame 2 links up")
				#expect(message { try Tree(data: Value(reading: "[:fn nil [[0 false nil 1 [:let [[0 [:direct-fn nil [[0 false nil 0 [:outer [1 5]]]]]]] [:const 2]]]] []]")) }?.hasPrefix("malformed node data, slot 5 outside a frame of") == true)
				#expect(message { try Tree(data: Value(reading: "[:fn nil [[0 false nil 0 [:outer [1 0]]]] []]")) } == "malformed node data, no frame 1 links up")
				#expect(message { try Tree(data: Value(reading: "[:fn nil [[0 false nil 0 [:const 1]]] [[:outer [0 0]]]]")) } == "malformed node data, expected [:local slot], [:captured index] or [:outer [depth slot]]: [:outer [0 0]]")
				#expect(message { try Tree(data: Value(reading: "[:outer [0 1]]")) } == "malformed node data, expected [[depth slot]]: [:outer [0 1]]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The let allocates no closure and a call allocates nothing: the live count inside the body is the
		// count before the let.
		@Test func liveObjectsPerCall() throws {
			try declare("dc-live")
			let live = Value(function: "dc-live", arity: 0...0) { _ in Value(Int(clj_debug_live_objects())) }
			withExtendedLifetime(live) { clj_var_bind_root(userVar("dc-live"), live.raw) }
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [base (dc-live) f (fn [x] (- (dc-live) base))] [(f 1) (f 2)])") == [0, 0])
				#expect(try rt.eval("(let [base (dc-live) f (fn f [n] (if (zero? n) (- (dc-live) base) (f (dec n))))] (f 20))") == 0)
				#expect(try rt.eval("(let [base (dc-live) f (fn [x] (- (dc-live) base))] (loop [i 0 m 0] (if (< i 100) (recur (inc i) (+ m (f i))) m)))") == 0)
				// The closure path, for contrast: the let creates the fn.
				#expect(try rt.eval("(let [base (dc-live) f (fn [x] (- (dc-live) base))] (identity f) (f 1))") == 1)
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("dc-live")
		}
	}
}
