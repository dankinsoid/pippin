// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

// What a call produced: the printed value, or the thrown message. Two calls agree when these agree, or the
// values are `=` (a NaN prints the same and compares unequal to itself).
private enum Outcome: Equatable {
	case value(Value)
	case thrown(String)

	init(_ raw: clj_value) {
		if raw == CLJ_THROWN {
			self = .thrown(Value(owning: clj_take_pending()).description)
		} else {
			self = .value(Value(owning: raw))
		}
	}

	static func == (a: Outcome, b: Outcome) -> Bool {
		switch (a, b) {
		case let (.value(x), .value(y)): return x == y || x.description == y.description
		case let (.thrown(x), .thrown(y)): return x == y
		default: return false
		}
	}
}

// An analyzed tree with its exec table, analyzed in a namespace of choice; releases both.
private final class Tree {
	let node: UnsafeMutablePointer<clj_node>
	let exec: clj_value

	init(_ source: String, ns: clj_value = CLJ_NIL) throws {
		let form = try Value(reading: source)
		var env = clj_env(ns: ns, line: 0, col: 0)
		guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
		self.node = node
		exec = clj_exec_new(node)
	}

	init(data: Value) throws {
		guard let node = withExtendedLifetime(data, { clj_node_from_data(data.raw) }) else { throw ClojureError.takePending() }
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

	// The node kinds of the tree in id order.
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
	@Suite struct IntrinsicsTests {
		init() {
			clj_init()
			// The codec interns its node-kind keywords on first use; a baseline taken before that would drift.
			for k in ["k", "a", "b", "const", "local", "captured", "var", "the-var", "if", "do", "let", "loop", "recur", "fn", "invoke", "intrinsic",
			          "def", "vector", "map", "try", "throw", "all", "error", "fused", "outer", "direct-fn", "direct-call"] { _ = Value(keyword: k) }
		}

		// The optimizer keys on the var: only a call through the core var at a listed arity becomes an intrinsic.
		@Test func rewriteKeyedByVarAndArity() throws {
			let nsName = Value(symbol: "intr-ns"), plus = Value(symbol: "+")
			let ns = withExtendedLifetime(nsName) { clj_ns_find_or_create(nsName.raw) }
			_ = withExtendedLifetime(plus) { clj_ns_intern(ns, plus.raw) }
			let before = clj_debug_live_objects()
			do {
				// Constant arguments fold (FoldingTests); a local argument keeps the intrinsic.
				#expect(try Tree("(+ 1 2)").data() == Value(reading: "[:const 3 1 1]"))
				#expect(try Tree("(let [a 1] (+ a 2))").data() == Value(reading: "[:let [[0 [:const 1 1 1]]] [:intrinsic clojure.core/+ [:local 0 1 12] [:const 2 1 12] 1 12] 1 1]"))
				#expect(try Tree("(fn [a] (clojure.core/+ a 2))").kinds == [CLJ_NODE_FN, CLJ_NODE_INTRINSIC, CLJ_NODE_LOCAL, CLJ_NODE_CONST])
				#expect(try Tree("(+ 1 2)").run() == 3)
				#expect(try Tree("(let [+ -] (+ 1 2))").kinds == [CLJ_NODE_LET, CLJ_NODE_VAR, CLJ_NODE_INVOKE, CLJ_NODE_LOCAL, CLJ_NODE_CONST, CLJ_NODE_CONST])
				#expect(try Tree("(let [+ -] (+ 1 2))").run() == -1)
				#expect(try Tree("(+ 1 2)", ns: ns).kinds == [CLJ_NODE_INVOKE, CLJ_NODE_VAR, CLJ_NODE_CONST, CLJ_NODE_CONST])
				#expect(try Tree("(fn [a] (clojure.core/+ a 2))", ns: ns).kinds == [CLJ_NODE_FN, CLJ_NODE_INTRINSIC, CLJ_NODE_LOCAL, CLJ_NODE_CONST])
				#expect(try Tree("(+ 1 2 3)").kinds == [CLJ_NODE_INVOKE, CLJ_NODE_VAR, CLJ_NODE_CONST, CLJ_NODE_CONST, CLJ_NODE_CONST])
				#expect(try Tree("(+ 1 2 3)").run() == 6)
				#expect(try Tree("(apply + [1 2])").kinds == [CLJ_NODE_INVOKE, CLJ_NODE_VAR, CLJ_NODE_VAR, CLJ_NODE_CONST])
				#expect(try Tree("(fn [m] (get m :a))").kinds == [CLJ_NODE_FN, CLJ_NODE_INTRINSIC, CLJ_NODE_LOCAL, CLJ_NODE_CONST])
				#expect(try Tree("(fn [m] (get m :b 2))").kinds == [CLJ_NODE_FN, CLJ_NODE_INTRINSIC, CLJ_NODE_LOCAL, CLJ_NODE_CONST, CLJ_NODE_CONST])
				#expect(try Tree("(get {:a 1} :b 2)").run() == 2)
				#expect(try Tree("(fn [x] (if (< x 1) (inc x) (dec x)))").kinds == [CLJ_NODE_FN, CLJ_NODE_IF, CLJ_NODE_INTRINSIC, CLJ_NODE_LOCAL, CLJ_NODE_CONST,
				                                                                     CLJ_NODE_INTRINSIC, CLJ_NODE_LOCAL, CLJ_NODE_INTRINSIC, CLJ_NODE_LOCAL])
				// An intrinsic throws like the builtin, and a throw releases the borrowed arguments cleanly.
				#expect(message { try Tree("(let [x \"s\"] (+ x 1))").run() } == "string cannot be cast to a number")
				#expect(message { try Tree("(nth [1] 5)").run() } == "Index 5 out of bounds for length 1")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A rebound core var takes the generic path until the boot fn is bound again: (def + ...) in clojure.core
		// and a host rebind alike.
		@Test func guardFallsBackWhenTheVarIsRebound() throws {
			let plus = coreVar("+")
			let boot = clj_var_root(plus)
			let tree = try Tree("(fn [a b] (+ a b))")
			let f = try tree.run()
			let before = clj_debug_live_objects()
			do {
				#expect(try f(1, 2) == 3)
				do {
					let minus = try cljEval("(fn [a b] (- a b))")
					withExtendedLifetime(minus) { clj_var_bind_root(plus, minus.raw) }
				}
				#expect(try f(1, 2) == -1)
				#expect(try cljEval("(+ 1 2)") == -1)
				clj_var_bind_root(plus, boot)
				#expect(try f(1, 2) == 3)
				#expect(clj_var_root(plus) == boot)

				clj_ns_set_current(clj_ns_core())
				_ = try cljEval("(def + (fn [a b] (* a b)))")
				clj_ns_set_current(clj_ns_user())
				#expect(try f(3, 4) == 12)
				#expect(try cljEval("(+ 3 4)") == 12)
				clj_var_bind_root(plus, boot)
				clj_var_set_meta(plus, CLJ_NIL)
				#expect(try f(3, 4) == 7)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every root boot bound is immortal, so a call through a core var retains nothing: the rc of a native
		// and of a core.clj closure stays put across 1000 calls each, while a user fn's root is ordinary.
		@Test func coreRootsAreImmortal() throws {
			func rc(_ v: clj_value) -> UInt32 { UnsafeRawPointer(clj_header_of(v)).load(as: UInt32.self) }
			func immortal(_ v: clj_value) -> Bool { clj_header_of(v).pointee.flags & UInt32(CLJ_FLAG_IMMORTAL) != 0 }
			let second = clj_var_root(coreVar("second")), mapFn = clj_var_root(coreVar("map")), plus = clj_var_root(coreVar("+"))
			#expect(immortal(second) && immortal(mapFn) && immortal(plus))
			#expect(!clj_is_unique(mapFn))
			let rcs = [rc(second), rc(mapFn), rc(plus)]
			_ = try cljEval("(def imm-f (fn [x] (inc x)))")
			let userSym = Value(symbol: "imm-f")
			let user = withExtendedLifetime(userSym) { clj_var_root(clj_ns_resolve(clj_ns_user(), userSym.raw)) }
			#expect(!immortal(user) && clj_is_shared(user))
			let before = clj_debug_live_objects()
			do {
				let sum: Int = 1000 * 5 + 999 * 1000 / 2
				#expect(try cljEval("(loop [i 0 acc 0] (if (< i 1000) (recur (inc i) (+ acc (second [1 2]) (first (map inc [1])) (imm-f i))) acc))") == Value(sum))
				#expect([rc(second), rc(mapFn), rc(plus)] == rcs)
				#expect(rc(user) == 1)
				// A var not from boot is retained around its call and released after.
				#expect(try cljEval("[(imm-f 1) (imm-f 2)]") == [2, 3])
				#expect(rc(user) == 1)
			}
			#expect(clj_debug_live_objects() == before)
			_ = try cljEval("(def imm-f nil)")
		}

		// [:intrinsic ns/name args*] round-trips; a name or arity the table lacks is refused on read.
		@Test func serializationRoundTrip() throws {
			let before = clj_debug_live_objects()
			do {
				for source in ["(let [v [1 2 3]] [(first v) (count v) (nth v 1) (get v 5 :k) (conj v 4) (assoc v 0 :a) (contains? v 2)])",
				               "(loop [i 0 acc 0] (if (< i 5) (recur (inc i) (+ acc i)) acc))",
				               "((fn [s] (if (seq s) (cons (first s) (rest s)) (empty? s))) [1 2])"] {
					let tree = try Tree(source)
					let data = try tree.data()
					#expect(data.description.contains(":intrinsic"))
					let text = try #require(Value(owning: clj_pr_str(data.raw)).string)
					let read = try Tree(data: Value(reading: text))
					#expect(try read.data() == data, Comment(rawValue: source))
					#expect(read.node.pointee.nnodes == tree.node.pointee.nnodes)
					#expect(try read.run() == tree.run(), Comment(rawValue: source))
				}
				#expect(try Tree(data: Value(reading: "[:intrinsic clojure.core/+ [:const 1] [:const 2]]")).run() == 3)
				#expect(message { try Tree(data: Value(reading: "[:intrinsic clojure.core/+ [:const 1]]")) } == "malformed node data, unknown intrinsic: [:intrinsic clojure.core/+ [:const 1]]")
				#expect(message { try Tree(data: Value(reading: "[:intrinsic clojure.core/nope [:const 1]]")) } == "malformed node data, unknown intrinsic: [:intrinsic clojure.core/nope [:const 1]]")
				#expect(message { try Tree(data: Value(reading: "[:intrinsic user/+ [:const 1] [:const 2]]")) } == "malformed node data, unknown intrinsic: [:intrinsic user/+ [:const 1] [:const 2]]")
				#expect(message { try Tree(data: Value(reading: "[:intrinsic clojure.core/+]")) } == "malformed node data, expected [ns/name args*]: [:intrinsic clojure.core/+]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every table entry crossed with sample values of every type, by arity: the C function called directly
		// must agree with the var's fn through clj_invoke, in value or in thrown message.
		@Test func intrinsicsMatchTheirBuiltins() throws {
			_ = try cljEval("(def DiffBox) (def ->DiffBox) (deftype DiffBox [v])")
			let samples = try cljEval("""
			[nil true false 0 1 -1 7 \(Int.max >> 1) \(Int.min >> 1) 1.5 -0.5 0.0 1e300 \\a "" "str" :k :ns/k 'sym 'ns/sym
			 [] [1 2 3] {} {:a 1 :b 2} '() '(1 2) (let [l (lazy-seq [1 2])] (seq l) l) (range 3) inc (->DiffBox 1)]
			""")
			let before = clj_debug_live_objects()
			do {
				var n = 0
				let table = try #require(clj_intrinsic_table(&n))
				#expect(n >= 40)
				let count = Int(clj_vector_count(samples.raw))
				let items = (0..<count).map { clj_vector_nth(samples.raw, UInt32($0)) }
				var calls = 0
				for i in 0..<n {
					let op = table + i
					let arity = Int(op.pointee.arity)
					let name = String(cString: op.pointee.name)
					#expect(arity == Int(op.pointee.kind.rawValue), Comment(rawValue: name))
					let fn = Value(owning: clj_var_deref(clj_intrinsic_var(op)))
					#expect(fn.raw == clj_intrinsic_builtin(op), Comment(rawValue: "\(name): core.clj rebinds the var"))
					var args = [clj_value](repeating: CLJ_NIL, count: arity)
					var index = [Int](repeating: 0, count: arity)
					tuples: while true {
						for k in 0..<arity { args[k] = items[index[k]] }
						let direct = Outcome(args.withUnsafeBufferPointer { clj_intrinsic_call(op, $0.baseAddress) })
						let generic = Outcome(args.withUnsafeBufferPointer { clj_invoke(fn.raw, $0.baseAddress, arity) })
						#expect(direct == generic, Comment(rawValue: "\(name) on \(args.map { Value(borrowing: $0).description })"))
						calls += 1
						var k = arity - 1
						while k >= 0 {
							index[k] += 1
							if index[k] < count { continue tuples }
							index[k] = 0
							k -= 1
						}
						break
					}
				}
				#expect(calls > 30_000)
			}
			#expect(clj_debug_live_objects() == before)
			_ = try cljEval("(def DiffBox nil) (def ->DiffBox nil)")
		}
	}
}
