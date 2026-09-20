// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

private func kw(_ s: String) -> Value { Value(keyword: s) }

private func message(_ rt: Runtime, _ source: String) -> String? {
	do {
		_ = try rt.eval(source)
		return nil
	} catch let e as ClojureError {
		return e.message
	} catch {
		return nil
	}
}

private func userVar(_ name: String) -> clj_value {
	let sym = Value(symbol: name)
	let v = withExtendedLifetime(sym) { clj_ns_resolve(clj_ns_user(), sym.raw) }
	precondition(clj_is_var(v), "user/\(name) is not interned: another suite moved the root *ns* during this eval")
	return v
}

private func rc(_ v: clj_value) -> UInt32 { UnsafeRawPointer(clj_header_of(v)).load(as: UInt32.self) }

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

	// Counters of the tree's invoke node number `site`, in source order.
	func hits(_ site: UInt32 = 0) -> Int64 { clj_debug_exec_ic_hits(exec, clj_debug_exec_invoke_id(exec, site)) }
	func misses(_ site: UInt32 = 0) -> Int64 { clj_debug_exec_ic_misses(exec, clj_debug_exec_invoke_id(exec, site)) }
	func receivers(_ site: UInt32 = 0) -> UInt32 { clj_debug_exec_ic_proto_entries(exec, clj_debug_exec_invoke_id(exec, site)) }
}

extension CoreTests {
	// A fn root is read at +0 by a call through its var; a rebind parks the old fn until the thread is idle.
	@Suite struct FnRootTests {
		let rt = Runtime()

		init() {
			for k in ["after", "new", "old", "b"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		private func define(_ name: String, _ value: Value) {
			withExtendedLifetime(value) { clj_var_bind_root(userVar(name), value.raw) }
		}

		@Test func fnRootReadWithoutRetainDataRootFreedOnRebind() throws {
			try declare("fr-f", "fr-d", "fr-g")
			_ = try rt.eval("(def fr-f (fn [x] x))")
			let f = clj_var_root(userVar("fr-f"))
			#expect(rc(f) == 1)
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(loop [i 0] (if (< i 1000) (recur (inc (fr-f i))) i))") == 1000)
				#expect(rc(f) == 1)
				#expect(try rt.eval("[(fr-f 1) (fr-f 2)]") == [1, 2])
				#expect(rc(f) == 1)
				_ = try rt.eval("(def fr-d (vector 1 2 3))")
				_ = try rt.eval("(def fr-d (vector 4 5 6))")
				try unbind("fr-d")
				_ = try rt.eval("(def fr-g (fn [] 1))")
				_ = try rt.eval("(def fr-g (fn [] 2))")
				try unbind("fr-g")
			}
			#expect(clj_debug_live_objects() == before)
			#expect(clj_debug_retired_roots() == 0)
			try unbind("fr-f")
		}

		// The old body finishes after replacing itself; the next call through the same site reaches the new fn.
		@Test func redefinitionInsideItsOwnBody() throws {
			try declare("fr-self", "fr-call")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defn fr-self [] (def fr-self (fn [] 2)) 1) (defn fr-call [] (fr-self))")
				#expect(try rt.eval("(fr-call)") == 1)
				#expect(try rt.eval("(fr-call)") == 2)
				#expect(try rt.eval("(fr-call)") == 2)
				_ = try rt.eval("(defn fr-self [] (def fr-self (fn [] 2)) 1)")
				#expect(try rt.eval("[(fr-call) (fr-call)]") == [1, 2])
				#expect(clj_debug_retired_roots() == 0)
				try unbind("fr-self", "fr-call")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func redefinitionUnderAThrowingTryAndFinally() throws {
			try declare("fr-t", "fr-u", "fr-call-t")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("""
				(defn fr-t [] (try (throw (ex-info "boom" {})) (finally (def fr-t (fn [] :after)))))
				(defn fr-u [] (def fr-u (fn [] :after)) (throw (ex-info "bang" {})))
				(defn fr-call-t [f] (f))
				""")
				#expect(message(rt, "(fr-call-t fr-t)") == "boom")
				#expect(try rt.eval("(fr-call-t fr-t)") == kw("after"))
				#expect(message(rt, "(fr-call-t fr-u)") == "bang")
				#expect(try rt.eval("(fr-call-t fr-u)") == kw("after"))
				#expect(clj_debug_retired_roots() == 0)
				try unbind("fr-t", "fr-u", "fr-call-t")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A rebind from a callback a native runs, and one from the host inside a callback: both park the old fn.
		@Test func redefinitionFromANativeCallback() throws {
			try declare("fr-g", "fr-h", "fr-host", "fr-host-bind")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defn fr-g [] (doall (map (fn [_] (def fr-g (fn [] :new))) [1 2])) :old)")
				#expect(try rt.eval("(fr-g)") == kw("old"))
				#expect(try rt.eval("(fr-g)") == kw("new"))
				_ = try rt.eval("(defn fr-h [] (apply (fn [_] (def fr-h (fn [] :new))) [1]) :old)")
				#expect(try rt.eval("(fr-h)") == kw("old"))
				#expect(try rt.eval("(fr-h)") == kw("new"))
				define("fr-host", Value(function: "fr-host", arity: 1...1) { args in try args[0]() })
				_ = try rt.eval("(defn fr-g [] (fr-host (fn [] (def fr-g (fn [] :new)))) :old)")
				#expect(try rt.eval("(fr-g)") == kw("old"))
				#expect(try rt.eval("(fr-g)") == kw("new"))
				define("fr-host-bind", Value(function: "fr-host-bind", arity: 0...0) { [rt] _ in
					let replacement = try rt.eval("(fn [] :b)")
					withExtendedLifetime(replacement) { clj_var_bind_root(userVar("fr-h"), replacement.raw) }
					return Value(Int(clj_debug_retired_roots()))
				})
				_ = try rt.eval("(defn fr-h [] (fr-host-bind))")
				#expect(try rt.eval("(fr-h)") == 1)
				#expect(try rt.eval("(fr-h)") == kw("b"))
				#expect(clj_debug_retired_roots() == 0)
				try unbind("fr-g", "fr-h", "fr-host", "fr-host-bind")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The head is read before the arguments run: a rebind in an argument at top level must not free it.
		@Test func headReadBeforeARebindInAnArgument() throws {
			try declare("fr-a", "fr-retired")
			define("fr-retired", Value(function: "fr-retired", arity: 0...0) { _ in Value(Int(clj_debug_retired_roots())) })
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(def fr-a (fn [x] x))")
				#expect(try rt.eval("(fr-a (do (def fr-a (fn [x] :new)) 1))") == 1)
				#expect(try rt.eval("(fr-a 1)") == kw("new"))
				#expect(try rt.eval("(let [] (def fr-a (fn [x] x)) (fr-retired))") == 1)
				#expect(clj_debug_retired_roots() == 0)
				#expect(try rt.eval("(do (def fr-a (fn [x] x)) (fr-retired))") == 0)
				try unbind("fr-a")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("fr-retired")
		}
	}
	// The call-site fast paths: a closure body entered without clj_invoke, a protocol method dispatched from
	// its cache; every site counts its hits and misses in debug builds.
	@Suite struct CallSiteTests {
		let rt = Runtime()

		init() {
			// Method names become interned keywords in the impl maps; values used in expectations too.
			for k in ["a", "b", "long", "string", "nil", "changed", "bottom", "done", "x", "none", "one", "two",
			          "cs-m", "cs-q", "cs-d", "cs-ap", "cs-tp", "cs-r"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		// Two closures of different code through one site both take the direct path, a native is called from
		// the site; a variadic closure and a closure whose frame is past the stack buffer go through the generic invoke.
		@Test func closureSitesHitNativesAndVariadicsMiss() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("""
				(let [f (fn [x] x) g (fn [y] (inc y))]
				  (loop [i 0 acc []] (if (< i 4) (recur (inc i) (conj acc ((if (even? i) f g) i))) acc)))
				""")
				#expect(try tree.run() == [0, 2, 2, 4])
				#expect(tree.hits() == 4 && tree.misses() == 0)
				#expect(try tree.run() == [0, 2, 2, 4])
				#expect(tree.hits() == 8 && tree.misses() == 0)
				let generic = try Tree("""
				[(identity 1) ((fn [& xs] xs) 1 2) ((fn [a b c d e f g h i j k l m n o p q] q) 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17)
				 ((fn ([] :none) ([x] x)) 5) ((fn [x] (let [a 1 b 2] (+ x a b))) 1)]
				""")
				#expect(try generic.run() == [1, Value(list: [1, 2]), 17, 5, 4])
				#expect(generic.hits(0) == 1 && generic.misses(0) == 0)
				#expect(generic.hits(1) == 0 && generic.misses(1) == 1)
				#expect(generic.hits(2) == 0 && generic.misses(2) == 1)
				#expect(generic.hits(3) == 1 && generic.misses(3) == 0)
				#expect(generic.hits(4) == 1 && generic.misses(4) == 0)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func varHeadRedefinedToOtherCodeAndBack() throws {
			try declare("cs-v")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(def cs-v (fn [x] x))")
				let tree = try Tree("(cs-v 1)")
				#expect(try tree.run() == 1)
				_ = try rt.eval("(def cs-v (fn [x] (inc x)))")
				#expect(try tree.run() == 2)
				_ = try rt.eval("(def cs-v (fn [x] x))")
				#expect(try tree.run() == 1)
				_ = try rt.eval("(def cs-v identity)")
				#expect(try tree.run() == 1)
				#expect(tree.hits() == 4 && tree.misses() == 0)
				try unbind("cs-v")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A local head: the site inside `call` sees a closure, another closure, then a native; every one a hit.
		@Test func localHeadWithDifferentFns() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("(let [call (fn [f x] (f x))] [(call (fn [x] (* x 2)) 2) (call (fn [y] (- y)) 3) (call inc 1)])")
				#expect(try tree.run() == [4, -3, 2])
				#expect(tree.hits(0) == 3 && tree.misses(0) == 0)
				#expect(try rt.eval("[(map (fn [x] (* x 2)) [1 2]) (map (fn [x] (- x)) [1 2]) (map inc [1 2])]") == [Value(list: [2, 4]), Value(list: [-1, -2]), Value(list: [2, 3])])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func protocolSiteMonoPolyAndEviction() throws {
			try declare("CsP", "cs-m", "CsA", "->CsA", "CsB", "->CsB")
			_ = try rt.eval("""
			(defprotocol CsP (cs-m [x]))
			(deftype CsA [] CsP (cs-m [x] :a))
			(deftype CsB [] CsP (cs-m [x] :b))
			(extend-type Long CsP (cs-m [x] :long))
			(extend-type String CsP (cs-m [x] :string))
			(extend-type nil CsP (cs-m [x] :nil))
			""")
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("(fn [xs] (loop [s (seq xs) acc []] (if s (recur (next s) (conj acc (cs-m (first s)))) acc)))")
				let walk = try tree.run()
				let a = try rt.eval("(->CsA)"), b = try rt.eval("(->CsB)")
				#expect(try walk([a, a, a, a]) == [kw("a"), kw("a"), kw("a"), kw("a")])
				#expect(tree.hits() == 3 && tree.misses() == 1 && tree.receivers() == 1)
				#expect(try walk([a, b, 1, "s"]) == [kw("a"), kw("b"), kw("long"), kw("string")])
				#expect(tree.hits() == 4 && tree.misses() == 4 && tree.receivers() == 4)
				#expect(try walk([a, b, 1, "s"]) == [kw("a"), kw("b"), kw("long"), kw("string")])
				#expect(tree.hits() == 8 && tree.misses() == 4)
				// A fifth receiver evicts the oldest; every dispatch stays right.
				#expect(try walk([nil, a, b, 1, "s", nil, a]) == [kw("nil"), kw("a"), kw("b"), kw("long"), kw("string"), kw("nil"), kw("a")])
				#expect(tree.receivers() == 4)
				#expect(tree.hits() + tree.misses() == 19)
				#expect(try walk([a, b, 1, "s", nil, a, b, 1, "s", nil]) == [kw("a"), kw("b"), kw("long"), kw("string"), kw("nil"), kw("a"), kw("b"), kw("long"), kw("string"), kw("nil")])
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("CsP", "cs-m", "CsA", "->CsA", "CsB", "->CsB")
		}

		// Only deftypes here: an entry in a builtin type's side table outlives a redefined protocol.
		@Test func extendAndRedefinitionAfterWarmUp() throws {
			try declare("CsQ", "cs-q", "CsC", "->CsC", "CsC2", "->CsC2")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defprotocol CsQ (cs-q [x])) (deftype CsC [] CsQ (cs-q [x] :a)) (deftype CsC2 [] CsQ (cs-q [x] :b))")
				let tree = try Tree("(fn [x] (cs-q x))")
				let call = try tree.run()
				let c = try rt.eval("(->CsC)"), c2 = try rt.eval("(->CsC2)")
				#expect(try call(c) == kw("a"))
				#expect(try call(c) == kw("a"))
				#expect(try call(c2) == kw("b"))
				#expect(tree.hits() == 1 && tree.misses() == 2)
				_ = try rt.eval("(extend-type CsC CsQ (cs-q [x] :changed))")
				#expect(try call(c) == kw("changed"))
				#expect(try call(c2) == kw("b"))
				#expect(tree.misses() == 4)
				#expect(try call(c) == kw("changed"))
				#expect(tree.hits() == 2)
				_ = try rt.eval("(extend-type CsC2 CsQ (cs-q [x] :x))")
				#expect(try call(c2) == kw("x"))
				#expect(try call(c) == kw("changed"))
				// A new protocol object behind the same names: the site sees another method fn.
				_ = try rt.eval("(defprotocol CsQ (cs-q [x])) (extend-type CsC CsQ (cs-q [x] :b))")
				#expect(try call(c) == kw("b"))
				#expect(message(rt, "(cs-q (->CsC2))") == "No implementation of method: :cs-q of protocol: #'user/CsQ found for type: user.CsC2")
				try unbind("CsQ", "cs-q", "CsC", "->CsC", "CsC2", "->CsC2")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A dying deftype descriptor retires its tables: the epoch moves so no site can hit a borrowed impl.
		@Test func typeDeathBumpsTheEpoch() throws {
			try declare("CsD", "->CsD", "CsDP", "cs-d")
			_ = try rt.eval("(defprotocol CsDP (cs-d [x]))")
			let e0 = clj_epoch()
			_ = try rt.eval("(deftype CsD [] CsDP (cs-d [x] :a))")
			let e1 = clj_epoch()
			#expect(e1 > e0)
			// The def and the descriptor's death: nothing but the var held it.
			_ = try rt.eval("(def CsD nil)")
			#expect(clj_epoch() == e1 + 2)
			_ = try rt.eval("(def ->CsD nil)")
			#expect(clj_epoch() == e1 + 3)
			try unbind("CsDP", "cs-d")
		}

		@Test func arityErrorsAndVariadicsThroughTheDirectPath() throws {
			try declare("cs-named", "cs-rest", "CsAP", "cs-ap", "CsE", "->CsE")
			_ = try rt.eval("""
			(defn cs-named [x] x)
			(defn cs-rest ([x] [x]) ([x & more] [x more]))
			(defprotocol CsAP (cs-ap [x] [x y]))
			(deftype CsE [] CsAP (cs-ap [x] :one) (cs-ap [x y] [:two y]))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(message(rt, "(cs-named)") == "Wrong number of args (0) passed to: user/cs-named")
				#expect(message(rt, "(cs-named 1 2)") == "Wrong number of args (2) passed to: user/cs-named")
				#expect(message(rt, "((fn [x] x))") == "Wrong number of args (0) passed to: fn")
				#expect(message(rt, "(let [f (fn [x] x)] (f 1 2))") == "Wrong number of args (2) passed to: fn")
				#expect(try rt.eval("[(cs-rest 1) (cs-rest 1 2 3) (apply cs-rest [4 5])]") == [[1], [1, Value(list: [2, 3])], [4, Value(list: [5])]])
				#expect(message(rt, "(cs-rest)") == "Wrong number of args (0) passed to: user/cs-rest")
				#expect(try rt.eval("(let [e (->CsE)] [(cs-ap e) (cs-ap e 2)])") == [kw("one"), [kw("two"), 2]])
				#expect(message(rt, "(cs-ap (->CsE) 1 2)") == "Wrong number of args (3) passed to: user/cs-ap")
				#expect(message(rt, "(cs-ap)") == "Wrong number of args (0) passed to: user/cs-ap")
				#expect(message(rt, "(let [e (->CsE)] (cs-ap e) (cs-ap e 1 2))") == "Wrong number of args (3) passed to: user/cs-ap")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("cs-named", "cs-rest", "CsAP", "cs-ap", "CsE", "->CsE")
		}

		// A throw unwinds the direct path like any other: the trace names every frame, the shadow depth is
		// back to zero, the frame's owned slots are released.
		@Test func exceptionsThroughTheDirectPath() throws {
			try declare("cs-thrower", "cs-caller", "cs-wide", "CsTP", "cs-tp", "CsT", "->CsT")
			_ = try rt.eval("""
			(defn cs-thrower [x] (throw (ex-info "t" {:x x})))
			(defn cs-caller [x] (let [v (vector x x)] (cs-thrower v)))
			(defn cs-wide [a b c d e f g h i j k l m n o p q] (cs-caller q))
			(defprotocol CsTP (cs-tp [x]))
			(deftype CsT [] CsTP (cs-tp [x] (cs-caller x)))
			""")
			let before = clj_debug_live_objects()
			do {
				for source in ["(cs-caller 1)", "(cs-wide 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17)", "(cs-tp (->CsT))", "(doall (map cs-caller [1]))"] {
					do {
						_ = try rt.eval(source)
						Issue.record("\(source) did not throw")
					} catch let e as ClojureError {
						#expect(e.message == "t")
						#expect(e.trace.map(\.fn).prefix(2) == ["user/cs-thrower", "user/cs-caller"], "\(source)")
					}
					#expect(clj_shadow_stack_depth() == 0)
				}
				#expect(try rt.eval("(try (cs-caller 1) (catch ExceptionInfo e (:x (ex-data e))))") == [1, 1])
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("cs-thrower", "cs-caller", "cs-wide", "CsTP", "cs-tp", "CsT", "->CsT")
		}

		@Test func recurInsideADirectlyCalledBody() throws {
			try declare("cs-count", "cs-down", "cs-self")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("""
				(defn cs-count [n] (loop [i 0] (if (< i n) (recur (inc i)) i)))
				(defn cs-down [n acc] (if (pos? n) (recur (dec n) (conj acc n)) acc))
				(defn cs-self [n] (if (pos? n) (cs-self (dec n)) :done))
				""")
				#expect(try rt.eval("[(cs-count 1000) (cs-down 3 []) (cs-self 40)]") == [1000, [3, 2, 1], kw("done")])
				try unbind("cs-count", "cs-down", "cs-self")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A recursive protocol method reaches its own site; releasing the execs frees everything.
		@Test func liveObjectsAtBaselineAfterExecsAreReleased() throws {
			try declare("CsRP", "cs-r", "CsR", "->CsR")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("""
				(defprotocol CsRP (cs-r [x]))
				(deftype CsR [n] CsRP (cs-r [x] (if (pos? n) (cs-r (->CsR (dec n))) :bottom)))
				""")
				#expect(try rt.eval("(cs-r (->CsR 5))") == kw("bottom"))
				let tree = try Tree("[(cs-r (->CsR 2)) (cs-r (->CsR 0))]")
				#expect(try tree.run() == [kw("bottom"), kw("bottom")])
				#expect(tree.receivers(0) == 1)
				try unbind("CsRP", "cs-r", "CsR", "->CsR")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
