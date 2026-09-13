// @ai-generated(solo)
import Testing
@testable import Clojure

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

	@Test func tagsAreDisjoint() {
		let samples: [Value] = [nil, false, true, 0, -1, Value("a" as Unicode.Scalar)]
		let kinds = samples.map { v in
			[v.isNil, v.bool != nil, v.int != nil, v.scalar != nil].filter { $0 }.count
		}
		#expect(kinds.allSatisfy { $0 == 1 })
	}
}
