// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

private struct DefineError: Error, Equatable {
	let code: Int
}

private func kw(_ s: String) -> Value { Value(keyword: s) }
private func sym(_ s: String) -> Value { Value(symbol: s) }

private func coreVar(_ name: String) -> clj_value {
	let s = sym(name)
	return withExtendedLifetime(s) { clj_ns_resolve(clj_ns_core(), s.raw) }
}

private func message(_ body: () throws -> Value) -> String? {
	do {
		_ = try body()
		return nil
	} catch let e as ClojureError {
		return e.message
	} catch {
		return String(describing: error)
	}
}

extension CoreTests {
	@Suite struct DefineTests {
		let rt = Runtime()

		init() {
			for k in ["k", "tag", "x"] { _ = kw(k) }
		}

		// A defined fn is the var's root and lives with it: baselines are taken after the define, and a test
		// ends by defining a fn of the same shape again.
		@Test func definesAndRedefines() throws {
			let v = rt.define("df-twice", arity: 1...1) { args in Value(args[0].int! + 1) }
			let before = clj_debug_live_objects()
			do {
				#expect(v.description == "#'user/df-twice")
				#expect(v.typeName == "var")
				#expect(try rt.eval("(df-twice 1)") == 2)
				#expect(try rt.eval("#'df-twice") == v)
				#expect(try rt.eval("(var df-twice)") == v)
				#expect(try rt.eval("(map df-twice [1 2])") == Value(list: [2, 3]))
				let site = try rt.eval("(fn [x] (df-twice x))")
				#expect(try site(1) == 2)
				let again = rt.define("df-twice", arity: 1...1) { args in Value(args[0].int! * 10) }
				#expect(again == v)
				#expect(try site(1) == 10)
				#expect(try rt.eval("(df-twice 1)") == 10)
				// A Clojure def takes the var over, and a define takes it back.
				_ = try rt.eval("(def df-twice (fn [x] (- x)))")
				#expect(try site(1) == -1)
				rt.define("df-twice", arity: 1...1) { args in Value(args[0].int! + 100) }
				#expect(try site(1) == 101)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func arityErrorsNameTheVar() throws {
			rt.define("df-one", arity: 1...1) { _ in nil }
			rt.define("df-some", arity: 1...2) { args in Value(args.count) }
			rt.define("df-any") { args in Value(args.count) }
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(df-one 1)") == nil)
				#expect(message { try rt.eval("(df-one)") } == "Wrong number of args (0) passed to: user/df-one")
				#expect(message { try rt.eval("(df-one 1 2)") } == "Wrong number of args (2) passed to: user/df-one")
				#expect(message { try rt.eval("(apply df-one [1 2 3])") } == "Wrong number of args (3) passed to: user/df-one")
				#expect(try rt.eval("[(df-some 1) (df-some 1 2)]") == [1, 2])
				#expect(message { try rt.eval("(df-some 1 2 3)") } == "Wrong number of args (3) passed to: user/df-some")
				#expect(try rt.eval("[(df-any) (df-any 1 2 3 4 5)]") == [0, 5])
				#expect(try rt.eval("(try (df-one) (catch :default e (ex-message e)))") == "Wrong number of args (0) passed to: user/df-one")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func thrownSwiftErrorsSurface() throws {
			rt.define("df-fail") { args in throw DefineError(code: args.first?.int ?? 0) }
			rt.define("df-clj") { _ in throw ClojureError(thrown: Value(exInfo: "from swift", data: [kw("k"): 1])) }
			_ = try rt.eval("(def df-caller)")
			let before = clj_debug_live_objects()
			do {
				do {
					_ = try rt.eval("(+ 1 (df-fail 3))")
					Issue.record("expected DefineError")
				} catch let e as DefineError {
					#expect(e == DefineError(code: 3))
				}
				#expect(try rt.eval("(try (df-fail 4) (catch :default e [(ex-message e) (ex-cause e)]))") == ["DefineError(code: 4)", nil])
				#expect(try rt.eval("(try (df-fail) (catch ExceptionInfo e (instance? HostError e)))") == true)
				// An ex-info made in Swift is Clojure's own error: its message, its data, its trace.
				#expect(try rt.eval("(try (df-clj) (catch ExceptionInfo e [(ex-message e) (ex-data e)]))") == ["from swift", [kw("k"): 1]])
				do {
					_ = try rt.eval("(defn df-caller [] (df-clj)) (df-caller)")
					Issue.record("expected a ClojureError")
				} catch let e as ClojureError {
					#expect(e.message == "from swift")
					#expect(e.data == [kw("k"): 1])
					#expect(e.trace.map(\.fn) == ["user/df-caller"])
				}
				_ = try rt.eval("(def df-caller nil)")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func metaFollowsDef() throws {
			let v = rt.define("df-doc", in: "df-ns", doc: "Adds one.") { args in Value(args[0].int! + 1) }
			let bare = rt.define("df-bare") { _ in nil }
			let before = clj_debug_live_objects()
			do {
				#expect(v.description == "#'df-ns/df-doc")
				#expect(v.meta == [kw("ns"): sym("df-ns"), kw("name"): sym("df-doc"), kw("doc"): "Adds one."])
				#expect(try rt.eval("(meta #'df-ns/df-doc)") == v.meta)
				#expect(try rt.eval("(df-ns/df-doc 1)") == 2)
				#expect(bare.meta == [kw("ns"): sym("user"), kw("name"): sym("df-bare")])
				#expect(try capturingOutput { _ = try rt.eval("(doc df-ns/df-doc)") }.contains("Adds one."))
				#expect(try rt.eval("(:macro (meta #'df-bare))") == nil)
				// A def afterwards writes its own meta, with the position; a define after that drops it again.
				_ = try rt.eval("(def ^{:tag :x} df-bare 1)")
				#expect(try rt.eval("(let [m (meta #'df-bare)] [(:tag m) (:name m) (integer? (:line m))])") == [kw("x"), sym("df-bare"), true])
				rt.define("df-bare") { _ in nil }
				#expect(try rt.eval("(meta #'df-bare)") == [kw("ns"): sym("user"), kw("name"): sym("df-bare")])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A site warmed on the boot `+` (an INTRINSIC node) sees the Swift root through the guard, and the
		// boot fn again once it is restored.
		@Test func redefiningACoreVarFallsBackFromTheIntrinsic() throws {
			let plus = coreVar("+")
			let boot = clj_var_root(plus)
			let bootMeta = Value(borrowing: clj_var_meta(plus))
			let f = try rt.eval("(fn [a b] (+ a b))")
			#expect(try f(3, 4) == 7)
			let before = clj_debug_live_objects()
			do {
				rt.define("+", in: "clojure.core", arity: 2...2) { args in Value(args[0].int! * args[1].int!) }
				#expect(try f(3, 4) == 12)
				#expect(try rt.eval("(+ 3 4)") == 12)
				#expect(try rt.eval("(reduce + [2 3 4])") == 24)
				#expect(message { try rt.eval("(+ 1 2 3)") } == "Wrong number of args (3) passed to: clojure.core/+")
				#expect(try rt.eval("(meta #'clojure.core/+)") == [kw("ns"): sym("clojure.core"), kw("name"): sym("+")])
				clj_var_bind_root(plus, boot)
				withExtendedLifetime(bootMeta) { clj_var_set_meta(plus, bootMeta.raw) }
				#expect(try f(3, 4) == 7)
				#expect(try rt.eval("(+ 1 2 3)") == 6)
				#expect(clj_var_root(plus) == boot)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
