// @ai-generated(solo)
import CljCore
import Testing
@testable import Clojure

// Nested in CoreTests: heap-backed values would skew the live-object baselines of a parallel suite.
extension CoreTests {
	@Suite struct ValueTests {
		@Test func nilIsZeroWord() {
			let v: Value = nil
			#expect(v.raw == 0)
			#expect(v.isNil)
			#expect(!v.isTruthy)
			#expect(v.typeName == "nil")
		}

		@Test func fixnumRoundTrip() {
			for n in [0, 1, -1, 42, Value.fixnumRange.upperBound, Value.fixnumRange.lowerBound] {
				let v = Value(n)
				#expect(v.int == n)
				#expect(v.typeName == "fixnum")
				#expect(v.isTruthy)
			}
		}

		@Test func booleans() {
			let t: Value = true
			let f: Value = false
			#expect(t.bool == true)
			#expect(f.bool == false)
			#expect(t.isTruthy)
			#expect(!f.isTruthy)
			#expect(f.int == nil)
			#expect(t.typeName == "boolean")
		}

		@Test func chars() {
			let v = Value("λ" as Unicode.Scalar)
			#expect(v.scalar == "λ")
			#expect(v.typeName == "char")
			#expect(v.int == nil)
		}

		// `raw` is borrowed from the Value: a Value that is not used afterwards may already be released
		// by the time the C call runs, so the values are held with withExtendedLifetime.
		@Test func strings() {
			let before = clj_debug_live_objects()
			do {
				let v = Value("héllo\u{0}🙂")
				let lit: Value = "héllo\u{0}🙂"
				let empty: Value = ""
				withExtendedLifetime((v, lit, empty)) {
					#expect(v.string == "héllo\u{0}🙂")
					#expect(lit.string == v.string)
					#expect(empty.string == "")
					#expect(v.typeName == "string")
					#expect(v.int == nil && v.double == nil)
					#expect(clj_equals(v.raw, lit.raw))
					#expect(clj_is_shared(v.raw))
					#expect(Value(1).string == nil)
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func doubles() {
			let before = clj_debug_live_objects()
			do {
				let v = Value(2.5)
				let lit: Value = 2.5
				let int: Value = 2
				let one = Value(1.0)
				withExtendedLifetime((v, lit, one)) {
					#expect(v.double == 2.5)
					#expect(lit.double == 2.5)
					#expect(v.typeName == "double")
					#expect(int.double == nil && int.int == 2)
					#expect(v.int == nil)
					#expect(clj_equals(v.raw, lit.raw))
					#expect(!clj_equals(one.raw, int.raw))
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func keywordsAndSymbols() {
			let warm = Value(keyword: "user/name")
			let before = clj_debug_live_objects()
			do {
				let k = Value(keyword: "user/name")
				let s = Value(symbol: "user/name")
				withExtendedLifetime((k, s)) {
					#expect(k.raw == warm.raw)
					#expect(k.typeName == "keyword")
					#expect(s.typeName == "symbol")
					#expect(k.string == nil && s.string == nil)
					#expect(!clj_equals(k.raw, s.raw))
					#expect(String(cString: clj_string_bytes(clj_symbol_ns(s.raw))) == "user")
					#expect(String(cString: clj_string_bytes(clj_keyword_name(k.raw))) == "name")
					#expect(clj_debug_all_shared(s.raw))
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func tagsAreDisjoint() {
			let samples: [Value] = [nil, false, true, 0, -1, Value("a" as Unicode.Scalar), "a", 1.0]
			let kinds = samples.map { v in
				[v.isNil, v.bool != nil, v.int != nil, v.scalar != nil, v.string != nil, v.double != nil]
					.filter { $0 }.count
			}
			#expect(kinds.allSatisfy { $0 == 1 })
		}
	}
}
