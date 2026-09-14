// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

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
	return withExtendedLifetime(sym) { clj_ns_resolve(clj_ns_user(), sym.raw) }
}

private func rc(_ v: clj_value) -> UInt32 { UnsafeRawPointer(clj_header_of(v)).load(as: UInt32.self) }

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
}
