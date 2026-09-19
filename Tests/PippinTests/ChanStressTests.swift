// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval("(in-ns 'chan-tests) " + source) }

extension CoreTests {
	@Suite struct ChanStressTests {
		init() throws {
			clj_init()
			_ = try cljEval("(ns chan-tests (:require [clojure.core.async :refer [chan <! >! <!! >!! timeout go thread]]))")
		}

		@Test func stressNested() throws {
			let exprs = ["(<!! (go (<! (go (<! (go 3))))))", "(<!! (thread (+ 1 2)))", "(let [c (chan)] (thread (>!! c :from-thread)) (<!! c))", "(<!! (go nil))", "(let [g (go :early)] (<!! (timeout 5)) [(<!! g) (<!! g)])"]
			for e in exprs {
				let coros = clj_debug_live_coros()
				for _ in 0..<100 { _ = try eval(e) }
				#expect(clj_debug_coro_settle(coros, 3000), "\(e)")
			}
		}
	}
}
