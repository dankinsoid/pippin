// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

// Owned; the tests release what they take.
private func sym(_ s: String) -> clj_value { clj_symbol_from_cstr(s) }
private func syms(_ names: String...) -> [clj_value] { names.map(sym) }

extension CoreTests {
	@Suite struct VarTests {
		@Test func exceptionsArePendingPerThread() {
			for k in ["message", "data", "cause"] { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				#expect(clj_is_nil(clj_pending()))
				let boom = Value("boom 7")
				#expect(withExtendedLifetime(boom) { clj_throw(clj_ex_info(boom.raw, CLJ_NIL)) } == CLJ_THROWN)
				let ex = clj_pending()
				#expect(clj_is_exception(ex))
				#expect(Value(borrowing: clj_exception_message(ex)) == "boom 7")
				#expect(clj_is_nil(clj_exception_data(ex)))
				#expect(Value(borrowing: ex).description == "#error {:message \"boom 7\", :data nil}")
				let taken = clj_take_pending()
				#expect(taken == ex)
				#expect(clj_is_nil(clj_pending()))
				let outerMessage = Value("outer")
				let outer = withExtendedLifetime(outerMessage) { clj_ex_info_cause(outerMessage.raw, clj_map_empty(), taken) }
				#expect(Value(borrowing: outer).description == "#error {:message \"outer\", :data {}, :cause #error {:message \"boom 7\", :data nil}}")
				clj_release(taken)
				// Throwing over a pending exception drops the old one.
				_ = clj_throw(outer)
				let second = Value("second")
				withExtendedLifetime(second) { _ = clj_throw(clj_ex_info(second.raw, CLJ_NIL)) }
				#expect(Value(borrowing: clj_exception_message(clj_pending())) == "second")
				clj_release(clj_take_pending())
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func internResolveAndBind() {
			let names = syms("var-tests", "x", "nope", "var-tests/x", "other/x", "var-tests-core", "clojure.core/var-tests-core", "y", "var-tests/y")
			defer { names.forEach(clj_release) }
			let ns = clj_ns_find_or_create(names[0])
			let var_ = clj_ns_intern(ns, names[1])
			#expect(clj_ns_find_or_create(names[0]) == ns)
			#expect(clj_ns_find(names[2]) == CLJ_NIL)
			#expect(clj_ns_intern(ns, names[1]) == var_)
			#expect(clj_ns_resolve(ns, names[1]) == var_)
			#expect(clj_ns_resolve(ns, names[3]) == var_)
			#expect(clj_ns_resolve(ns, names[4]) == CLJ_NIL)
			#expect(clj_ns_resolve(clj_ns_user(), names[1]) == CLJ_NIL)
			#expect(Value(borrowing: var_).description == "#'var-tests/x")
			#expect(!clj_var_is_bound(var_))
			#expect(clj_var_deref(var_) == CLJ_THROWN)
			#expect(Value(owning: clj_take_pending()).description == "#error {:message \"Unbound var: #'var-tests/x\", :data nil}")
			let v = Value([1, 2])
			withExtendedLifetime(v) {
				clj_var_bind_root(var_, v.raw)
				#expect(clj_var_root(var_) == v.raw)
				#expect(clj_is_shared(v.raw))
				let d = clj_var_deref(var_)
				#expect(d == v.raw)
				clj_release(d)
			}
			clj_var_bind_root(var_, CLJ_NIL)
			#expect(clj_var_is_bound(var_))
			// Core vars are visible from every namespace; a referred var only from its own.
			let core = clj_ns_intern(clj_ns_core(), names[5])
			#expect(clj_ns_resolve(ns, names[5]) == core)
			#expect(clj_ns_resolve(ns, names[6]) == core)
			clj_ns_refer(ns, names[7], core)
			#expect(clj_ns_resolve(ns, names[7]) == core)
			#expect(clj_ns_resolve(ns, names[8]) == CLJ_NIL)
		}

		@Test func currentNamespaceDefaultsToUser() {
			#expect(clj_ns_current() == clj_ns_user())
			#expect(Value(borrowing: clj_ns_name(clj_ns_current())) == Value(symbol: "user"))
			let name = sym("var-tests")
			defer { clj_release(name) }
			let ns = clj_ns_find_or_create(name)
			clj_ns_set_current(ns)
			#expect(clj_ns_current() == ns)
			clj_ns_set_current(clj_ns_user())
		}
	}
}
