// @ai-generated(solo)
import CljCore
import Testing
@testable import Clojure

private func text(_ s: clj_value) -> String? {
	guard clj_is_string(s) else { return nil }
	return String(cString: clj_string_bytes(s))
}

extension CoreTests {
	@Suite struct SymbolTests {
		@Test(arguments: [
			("a/b", "a", "b"),
			("a", nil, "a"),
			("/", nil, "/"),
			("a/b/c", "a", "b/c"),
			("a/", "a", ""),
			("/a", "", "a"),
			("", nil, ""),
		] as [(String, String?, String)])
		func splitRules(input: String, ns: String?, name: String) {
			let before = clj_debug_live_objects()
			let s = clj_symbol_from_cstr(input)
			#expect(clj_is_symbol(s))
			#expect(String(cString: clj_type_name(s)) == "symbol")
			#expect(text(clj_symbol_ns(s)) == ns)
			#expect(text(clj_symbol_name(s)) == name)
			if ns == nil { #expect(clj_is_nil(clj_symbol_ns(s))) }
			clj_release(s)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func constructorRetainsStrings() {
			let before = clj_debug_live_objects()
			let ns = clj_string_from_cstr("ns")
			let name = clj_string_from_cstr("name")
			let s = clj_symbol_new(ns, name)
			#expect(!clj_is_unique(ns) && !clj_is_unique(name))
			#expect(clj_symbol_ns(s) == ns && clj_symbol_name(s) == name)
			clj_release(ns)
			clj_release(name)
			#expect(clj_debug_live_objects() == before + 3)
			clj_release(s)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func equalityAndHash() {
			let before = clj_debug_live_objects()
			let a = clj_symbol_from_cstr("ns/x")
			let b = clj_symbol_new(clj_string_from_cstr("ns"), clj_string_from_cstr("x"))
			clj_release(clj_symbol_ns(b))
			clj_release(clj_symbol_name(b))
			let plain = clj_symbol_from_cstr("x")
			let other = clj_symbol_from_cstr("ns/y")
			#expect(clj_equals(a, b))
			#expect(clj_hash(a) == clj_hash(b))
			#expect(!clj_equals(a, plain))
			#expect(!clj_equals(plain, a))
			#expect(clj_hash(a) != clj_hash(plain))
			#expect(!clj_equals(a, other))
			let x = clj_string_from_cstr("x")
			#expect(!clj_equals(plain, x))
			#expect(!clj_equals(x, plain))
			for s in [a, b, plain, other, x] { clj_release(s) }
			#expect(clj_debug_live_objects() == before)
		}

		@Test func hashIsCachedOnce() {
			let before = clj_debug_live_objects()
			let s = clj_symbol_from_cstr("a/b")
			#expect(clj_debug_cached_hash(s) == 0)
			#expect(clj_debug_cached_hash(clj_symbol_name(s)) == 0)
			let h = clj_hash(s)
			#expect(clj_debug_cached_hash(s) == h)
			#expect(clj_debug_cached_hash(clj_symbol_name(s)) == clj_hash(clj_symbol_name(s)))
			#expect(clj_hash(s) == h)
			clj_release(s)
			#expect(clj_debug_live_objects() == before)
		}
	}
}
