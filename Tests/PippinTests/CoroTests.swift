// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

// Every test ends with what it started with: no live objects, no live coroutines beyond the threads' own.
private struct Baseline {
	let objects = clj_debug_live_objects(), coros = clj_debug_live_coros()
	func check(_ location: SourceLocation = #_sourceLocation) {
		#expect(clj_debug_coro_settle(coros, 5000), sourceLocation: location)
		clj_output_flush()
		#expect(clj_debug_live_objects() == objects, sourceLocation: location)
		#expect(clj_debug_live_coros() == coros, sourceLocation: location)
	}
}

extension CoreTests {
	@Suite struct CoroTests {
		init() {
			clj_init()
			_ = try? cljEval("(require 'clojure.core.async)")
		}

		@Test func smoke() throws {
			let base = Baseline()
			do {
				#expect(try eval("(let [c (chan*)] (go* (fn [] (chan-put* c 42))) (chan-take* c))") == 42)
			}
			base.check()
		}
	}
}
