// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

extension CoreTests {
	@Suite struct PrimitiveTests {
		let rt = Runtime()

		init() {
			// A host error interns its ex-type keyword on the first error of that Swift type (design §4).
			_ = Value(keyword: "Pippin/ReaderError")
		}

		@Test func differentialReportsDivergences() throws {
			let before = clj_debug_live_objects()
			do {
				let inc = Value(function: "inc-spec") { args in Value(args[0].int! + 1) }
				let wrong = Value(function: "inc-wrong") { args in args[0].int! == 2 ? 0 : Value(args[0].int! + 1) }
				#expect(Runtime.differential(primitive: inc, spec: inc, samples: [[1], [2]]).isEmpty)
				let d = Runtime.differential(primitive: wrong, spec: inc, samples: [[1], [2], [3]])
				#expect(d.count == 1)
				#expect(d.first?.args == [2])
				#expect(d.first?.description == "(2): primitive returned 0, spec returned 3")
				let throwsA = Value(function: "throws-a") { _ in throw ClojureError(thrown: Value(exInfo: "a")) }
				let throwsB = Value(function: "throws-b") { _ in throw ClojureError(thrown: Value(exInfo: "b")) }
				#expect(Runtime.differential(primitive: throwsA, spec: throwsB, samples: [[1]]).isEmpty)
				#expect(Runtime.differential(primitive: throwsA, spec: throwsA, samples: [[1]], messages: true).isEmpty)
				let byMessage = Runtime.differential(primitive: throwsA, spec: throwsB, samples: [[1]], messages: true)
				#expect(byMessage.map(\.description) == ["(1): primitive threw \"a\", spec threw \"b\""])
				#expect(Runtime.differential(primitive: throwsA, spec: inc, samples: [[1]]).map(\.description) == ["(1): primitive threw \"a\", spec returned 2"])
				// A Swift error counts as a throw, with its description as the message.
				let host = Value(function: "throws-host") { _ in throw ReaderError(message: "m", line: 1, column: 2) }
				#expect(Runtime.differential(primitive: host, spec: throwsA, samples: [[1]]).isEmpty)
				#expect(Runtime.differential(primitive: host, spec: throwsA, samples: [[1]], messages: true).map(\.description) == ["(1): primitive threw \"1:2: m\", spec threw \"a\""])
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
