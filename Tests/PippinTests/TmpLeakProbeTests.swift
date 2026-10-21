import CljCore
import Foundation
import Testing
@testable import Pippin

extension CoreTests {
	@Suite struct TmpLeakProbeTests {
		@Test func probe() throws {
			clj_init()
			let forms = [
				"(pr-str (let [calls (atom [])] [(sort-by (fn [x] (swap! calls conj x) (- x)) [3 1 2 5 4]) @calls]))",
				"(pr-str (let [calls (atom [])] [(sort-by (fn [x] (swap! calls conj x) x) > [2 9 2 7]) @calls]))",
				"(pr-str (let [calls (atom 0)] [(sort-by (fn [x] (swap! calls inc) x) [:only]) (sort-by (fn [x] (swap! calls inc) x) []) @calls]))",
				"(try (sort-by (fn [x] (if (even? x) (throw (ex-info (str \"k\" x) {})) x)) [1 4 3 2]) (catch :default e (ex-message e)))",
				"(pr-str (sort-by first > [[1 :a] [2 :b] [1 :c] [2 :d]]))",
				"(pr-str (sort-by :k (fn [a b] (compare b a)) [{:k 1} {:k 3} {:k 2}]))",
			]
			var lines: [String] = []
			let census = LiveCensus()
			for round in 0..<3 {
				for f in forms {
					let before = settledLiveObjects("before")
					_ = try? cljEvalScoped(f)
					let after = settledLiveObjects("after")
					lines.append("PROBE round \(round) delta \(after - before): \(f)")
				}
			}
			lines.append("PROBE moves: " + LiveCensus().moves(since: census).joined(separator: ", "))
			Issue.record(Comment(rawValue: lines.joined(separator: "\n")))
		}
	}
}
