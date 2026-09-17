// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

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

// What a call produced: the value, or the thrown message.
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

// An analyzed tree with its exec, so a test can read the call-site counters of its invoke nodes.
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

	deinit {
		clj_release(exec)
		clj_release(clj_from_ptr(node))
	}

	func run() throws -> Value {
		let raw = clj_exec_run(exec)
		if raw == CLJ_THROWN { throw ClojureError.takePending() }
		return Value(owning: raw)
	}

	func hits(_ site: UInt32 = 0) -> Int64 { clj_debug_exec_ic_hits(exec, clj_debug_exec_invoke_id(exec, site)) }
	func misses(_ site: UInt32 = 0) -> Int64 { clj_debug_exec_ic_misses(exec, clj_debug_exec_invoke_id(exec, site)) }
}

extension CoreTests {
	// A plain native at the head of an invoke is called from the site (eval.c, call_native).
	@Suite struct NativeCallTests {
		let rt = Runtime()

		init() {
			for k in ["x", "k", "a"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		// Through a let local, a captured slot, a param and a var: every site counts a hit, nothing is allocated.
		@Test func nativesThroughLocalsCapturesParamsAndVars() throws {
			try declare("nc-f")
			_ = try rt.eval("(def nc-f count)")
			let before = clj_debug_live_objects()
			do {
				let local = try Tree("(let [f inc] (f 1))")
				#expect(try local.run() == 2)
				#expect(local.hits() == 1 && local.misses() == 0)
				let captured = try Tree("(let [f conj g (fn [x] (f [] x))] [(g 1) (g 2)])")
				#expect(try captured.run() == [[1], [2]])
				#expect(captured.hits(0) == 2 && captured.misses(0) == 0)
				let param = try Tree("((fn [f x] (f x)) count [1 2 3])")
				#expect(try param.run() == 3)
				#expect(param.hits(0) == 1 && param.misses(0) == 0)
				let user = try Tree("(nc-f [1 2])")
				#expect(try user.run() == 2)
				#expect(user.hits() == 1 && user.misses() == 0)
				#expect(try rt.eval("(let [f str] (loop [i 0 acc \"\"] (if (< i 3) (recur (inc i) (f acc i)) acc)))") == "012")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("nc-f")
		}

		// The message names the fn as clj_invoke's does, at zero and at too many arguments.
		@Test func arityErrors() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(clojureError(rt, "(let [f inc] (f 1 2))")?.message == "Wrong number of args (2) passed to: clojure.core/inc")
				#expect(clojureError(rt, "(let [f inc] (f))")?.message == "Wrong number of args (0) passed to: clojure.core/inc")
				#expect(clojureError(rt, "((fn [f] (f)) vector?)")?.message == "Wrong number of args (0) passed to: clojure.core/vector?")
				#expect(try rt.eval("(let [f +] [(f) (f 1) (f 1 2 3 4 5 6 7 8 9 10)])") == [0, 1, 55])
				#expect(clj_shadow_stack_depth() == 0)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A native throws as a leaf: the frames are the closures around the site, as through clj_invoke.
		@Test func thrownErrorsKeepTheirTrace() throws {
			try declare("nc-call", "nc-apply", "nc-outer")
			_ = try rt.eval("""
			(defn nc-call [f x] (f x))
			(defn nc-apply [f x] (apply f [x]))
			(defn nc-outer [g f x] (g f x))
			""")
			let before = clj_debug_live_objects()
			do {
				let direct = try #require(clojureError(rt, "(nc-outer nc-call inc \"s\")"))
				let generic = try #require(clojureError(rt, "(nc-outer nc-apply inc \"s\")"))
				#expect(direct.message == "string cannot be cast to a number")
				#expect(direct.message == generic.message)
				#expect(direct.trace.map(\.fn) == ["user/nc-call", "user/nc-outer"])
				#expect(generic.trace.map(\.fn) == ["user/nc-apply", "user/nc-outer"])
				#expect(direct.trace.map(\.line) == generic.trace.map(\.line))
				#expect(clj_shadow_stack_depth() == 0)
				let info = try #require(clojureError(rt, "(nc-call ex-info \"only a message\")"))
				#expect(info.message == "Wrong number of args (1) passed to: clojure.core/ex-info")
				#expect(info.trace.map(\.fn) == ["user/nc-call"])
				#expect(try rt.eval("(try (nc-call inc \"s\") (catch :default e (ex-message e)))") == "string cannot be cast to a number")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("nc-call", "nc-apply", "nc-outer")
		}

		@Test func applyUnaffected() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [f +] [(apply f [1 2 3]) (apply f 1 2 [3 4]) (apply f [])])") == [6, 10, 0])
				#expect(try rt.eval("(let [f inc] (map f [1 2]))") == Value(list: [2, 3]))
				#expect(try rt.eval("(let [f inc g (fn [h] h)] ((g f) 1))") == 2)
				#expect(clojureError(rt, "(let [f inc] (apply f [1 2]))")?.message == "Wrong number of args (2) passed to: clojure.core/inc")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Differential: every intrinsics builtin through a local head against clj_invoke, over sample values.
		@Test func intrinsicBuiltinsByValueMatchInvoke() throws {
			_ = try cljEval("(def NcBox) (def ->NcBox) (deftype NcBox [v])")
			let samples = try cljEval("""
			[nil true false 0 1 -1 7 \(Int.max >> 1) \(Int.min >> 1) 1.5 -0.5 0.0 \\a "" "str" :k :ns/k 'sym 'ns/sym
			 [] [1 2 3] {} {:a 1 :b 2} '() '(1 2) (range 3) inc (->NcBox 1)]
			""")
			let callers = try (1...3).map { arity in
				try Tree("(fn [f \((0..<arity).map { "a\($0)" }.joined(separator: " "))] (f \((0..<arity).map { "a\($0)" }.joined(separator: " "))))")
			}
			let fns = try callers.map { try $0.run() }
			let before = clj_debug_live_objects()
			do {
				var n = 0
				let table = try #require(clj_intrinsic_table(&n))
				let count = Int(clj_vector_count(samples.raw))
				let items = (0..<count).map { clj_vector_nth(samples.raw, UInt32($0)) }
				var calls: Int64 = 0
				for i in 0..<n {
					let op = table + i
					let arity = Int(op.pointee.arity)
					let name = String(cString: op.pointee.name)
					let builtin = clj_intrinsic_builtin(op)
					var args = [clj_value](repeating: CLJ_NIL, count: arity + 1)
					var index = [Int](repeating: 0, count: arity)
					args[0] = builtin
					tuples: while true {
						for k in 0..<arity { args[k + 1] = items[index[k]] }
						let direct = Outcome(args.withUnsafeBufferPointer { clj_invoke(fns[arity - 1].raw, $0.baseAddress, arity + 1) })
						let generic = Outcome(args.withUnsafeBufferPointer { clj_invoke(builtin, $0.baseAddress! + 1, arity) })
						#expect(direct == generic, Comment(rawValue: "\(name) on \(args.dropFirst().map { Value(borrowing: $0).description })"))
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
				#expect(callers.map { $0.hits(0) }.reduce(0, +) == calls)
				#expect(callers.map { $0.misses(0) }.reduce(0, +) == 0)
				#expect(clj_shadow_stack_depth() == 0)
			}
			#expect(clj_debug_live_objects() == before)
			_ = try cljEval("(def NcBox nil) (def ->NcBox nil)")
		}
	}
}
