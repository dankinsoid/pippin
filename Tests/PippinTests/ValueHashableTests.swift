// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

extension CoreTests {
	@Suite struct ValueHashableTests {
		@Test func equalityFollowsClojure() {
			#expect(Value(1) == Value(1))
			#expect(Value(1) != Value(1.0))
			#expect(Value("a") == Value("a"))
			#expect(Value("a") != Value(keyword: "a"))
			#expect(Value(keyword: "a") == Value(keyword: "a"))
			#expect(Value(.nan) != Value(.nan))
			#expect(Value(0.0) == Value(-0.0))
		}

		@Test func worksAsDictionaryKey() {
			_ = Value(keyword: "k") // interning allocates immortals; take the baseline after
			let before = clj_debug_live_objects()
			do {
				var d: [Value: Int] = [:]
				d[Value("k")] = 1
				d[Value("k")] = 2
				d[Value(keyword: "k")] = 3
				d[Value(1)] = 4
				d[Value(1.0)] = 5
				#expect(d.count == 4)
				#expect(d[Value("k")] == 2)
				#expect(Set([Value(1), Value(1), Value(2)]).count == 2)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
