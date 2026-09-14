// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

extension CoreTests {
	@Suite struct EpochTests {
		let rt = Runtime()

		// One counter for every definition: a def, an extend and a deftype each move it.
		@Test func definitionsBumpTheEpoch() throws {
			_ = try rt.eval("(def ep-x) (def EpP) (def ep-m) (def EpT) (def ->EpT)")
			let e0 = clj_epoch()
			_ = try rt.eval("(def ep-x 1)")
			#expect(clj_epoch() == e0 + 1)
			_ = try rt.eval("(+ ep-x 1)")
			#expect(clj_epoch() == e0 + 1)
			_ = try rt.eval("(defprotocol EpP (ep-m [this]))")
			let e1 = clj_epoch()
			#expect(e1 > e0 + 1)
			_ = try rt.eval("(extend-type Long EpP (ep-m [this] :long))")
			#expect(clj_epoch() == e1 + 1)
			#expect(try rt.eval("(protocol-epoch*)") == Value(Int(e1 + 1)))
			_ = try rt.eval("(deftype EpT [] EpP (ep-m [this] :t))")
			#expect(clj_epoch() > e1 + 1)
			_ = try rt.eval("(alter-meta! (var ep-x) assoc :doc \"d\")")
			let e2 = clj_epoch()
			_ = try rt.eval("(alter-meta! (var ep-x) assoc :doc \"e\")")
			#expect(clj_epoch() == e2)
			_ = try rt.eval("(def ep-x nil) (def EpP nil) (def ep-m nil) (def EpT nil) (def ->EpT nil)")
		}
	}
}
