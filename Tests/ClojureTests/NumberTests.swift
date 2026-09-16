// @ai-generated(solo)
import CljCore
import Testing
@testable import Clojure

extension CoreTests {
	@Suite struct NumberTests {
		@Test func boxRoundTrip() {
			let before = clj_debug_live_objects()
			for d in [0.0, 1.5, -2.25, .greatestFiniteMagnitude, .leastNonzeroMagnitude, .infinity, -.infinity] {
				let v = clj_double_new(d)
				#expect(clj_is_double(v))
				#expect(String(cString: clj_type_name(v)) == "double")
				#expect(clj_double_val(v) == d)
				#expect(clj_is_unique(v) == clj_reuse_enabled())
				clj_release(v)
			}
			let nan = clj_double_new(.nan)
			#expect(clj_double_val(nan).isNaN)
			clj_release(nan)
			#expect(!clj_is_double(clj_fixnum(1)))
			#expect(!clj_is_double(CLJ_NIL))
			#expect(clj_debug_live_objects() == before)
		}

		@Test func zeroSigns() {
			let before = clj_debug_live_objects()
			let pos = clj_double_new(0.0)
			let neg = clj_double_new(-0.0)
			#expect(clj_double_val(neg).sign == .minus)
			#expect(clj_equals(pos, neg))
			#expect(clj_equals(neg, pos))
			#expect(clj_hash(pos) == clj_hash(neg))
			clj_release(pos)
			clj_release(neg)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nanNeverEqualsAnotherBox() {
			let before = clj_debug_live_objects()
			let a = clj_double_new(.nan)
			let b = clj_double_new(.nan)
			#expect(!clj_equals(a, b))
			#expect(!clj_equals(b, a))
			let one = clj_double_new(1.0)
			#expect(!clj_equals(a, one))
			clj_release(a)
			clj_release(b)
			clj_release(one)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func typeStrictAgainstFixnum() {
			let before = clj_debug_live_objects()
			let one = clj_double_new(1.0)
			#expect(!clj_equals(one, clj_fixnum(1)))
			#expect(!clj_equals(clj_fixnum(1), one))
			#expect(clj_hash(one) != clj_hash(clj_fixnum(1)))
			#expect(!clj_equals(one, CLJ_TRUE))
			let str = clj_string_from_cstr("1.0")
			#expect(!clj_equals(one, str) && !clj_equals(str, one))
			clj_release(one)
			clj_release(str)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func equalValuesHashEqual() {
			let before = clj_debug_live_objects()
			for d in [3.25, -1e300, 6.02e23, .infinity] {
				let a = clj_double_new(d)
				let b = clj_double_new(d)
				#expect(a != b)
				#expect(clj_equals(a, b))
				#expect(clj_hash(a) == clj_hash(b))
				clj_release(a)
				clj_release(b)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
