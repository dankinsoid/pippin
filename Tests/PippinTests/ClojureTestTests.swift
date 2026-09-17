// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

// The suite under test, in its own namespace so the vars exist before each baseline.
private let suite = """
(ns ct-suite (:require [clojure.test :refer [deftest is are testing use-fixtures]]))
(def log (atom []))
(use-fixtures :each (fn [f] (swap! log conj :each-in) (f) (swap! log conj :each-out)))
(use-fixtures :once (fn [f] (swap! log conj :once-in) (f) (swap! log conj :once-out)))
(deftest passing
  (is (= 1 1))
  (is (= 2 2) "with a message")
  (testing "nested" (testing "contexts" (is (pos? 1))))
  (are [x y] (= x y) 1 1 2 2))
(deftest failing
  (is (= 1 2))
  (is (= 1 2) "custom message")
  (testing "in a context" (is nil))
  (is (thrown? ExceptionInfo (throw (ex-info "boom" {}))))
  (is (thrown? ExceptionInfo (inc 1)))
  (is (thrown-with-msg? ExceptionInfo #"o+" (throw (ex-info "boom" {}))))
  (is (thrown-with-msg? ExceptionInfo #"xx" (throw (ex-info "boom" {})))))
(deftest erroring
  (is (= 1 (throw (ex-info "inside is" {}))))
  (throw (ex-info "outside is" {})))
(defn not-a-test [] :plain)
"""

extension CoreTests {
	@Suite struct ClojureTestTests {
		private static func load() throws {
			defer { clj_ns_set_current(clj_ns_user()) }
			_ = try eval(suite)
			_ = try eval("(require 'clojure.test) [:test :pass :fail :error :type :summary :each-in :each-out :once-in :once-out :begin-test-var :end-test-var :file :line :column :var :expected :actual :message :begin-test-ns :end-test-ns]")
			// One run before the baseline: the thread's parked-root list and the like are allocated on first use.
			_ = try capturingOutput { _ = try eval("(clojure.test/run-tests 'ct-suite)") }
		}

		@Test func runTestsSummarizesAndReports() throws {
			clj_init()
			try Self.load()
			let before = clj_debug_live_objects()
			do {
				var summary: Value = nil
				let out = try capturingOutput { summary = try eval("(reset! ct-suite/log []) (clojure.test/run-tests 'ct-suite)") }
				#expect(summary == Value([kw("test"): 3, kw("pass"): 7, kw("fail"): 5, kw("error"): 2, kw("type"): kw("summary")]))
				#expect(out.contains("Testing ct-suite"))
				#expect(out.contains("FAIL in (failing) (NO_SOURCE_PATH:11)"))
				#expect(out.contains("expected: (= 1 2)\n  actual: (not (= 1 2))"))
				#expect(out.contains("custom message"))
				#expect(out.contains("in a context\nexpected: nil\n  actual: nil"))
				#expect(out.contains("expected: (thrown? ExceptionInfo (inc 1))\n  actual: nil"))
				#expect(out.contains("expected: (thrown-with-msg? ExceptionInfo #\"xx\" (throw (ex-info \"boom\" {})))\n  actual: #error {:message \"boom\""))
				#expect(out.contains("ERROR in (erroring) (NO_SOURCE_PATH:19)"))
				#expect(out.contains("ERROR in (erroring) (NO_SOURCE_PATH:18)"))
				#expect(out.contains("Uncaught exception, not in assertion."))
				#expect(out.contains("Ran 3 tests containing 14 assertions.\n5 failures, 2 errors."))
				// :once wraps the namespace run, :each every test var, in file order.
				#expect(try eval("@ct-suite/log") == [kw("once-in"), kw("each-in"), kw("each-out"), kw("each-in"), kw("each-out"), kw("each-in"), kw("each-out"), kw("once-out")])
				#expect(try eval("(clojure.test/successful? {:fail 0 :error 0})") == true)
				#expect(try eval("(clojure.test/successful? {:fail 1 :error 0})") == false)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func reportIsRebindableAndCarriesPositions() throws {
			clj_init()
			try Self.load()
			let before = clj_debug_live_objects()
			do {
				let events = try eval("""
				(let [events (atom [])]
				  (binding [clojure.test/report (fn [m] (swap! events conj m))]
				    (clojure.test/test-var #'ct-suite/failing))
				  (mapv (fn [m] [(:type m) (:line m) (:actual m)]) @events))
				""")
				let rows = events.array!
				#expect(rows.count == 9)
				#expect(rows.first == [kw("begin-test-var"), nil, nil])
				#expect(rows.last == [kw("end-test-var"), nil, nil])
				#expect(rows[1].description == "[:fail 11 (not (= 1 2))]")
				#expect(rows[2].description == "[:fail 12 (not (= 1 2))]")
				#expect(rows[3].description == "[:fail 13 nil]")
				#expect(rows[4].array![0] == kw("pass"))
				#expect(rows[5].description == "[:fail 15 nil]")
				#expect(rows[6].array![0] == kw("pass"))
				#expect(rows[7].array![0] == kw("fail"))
				// The position of a test var is its def's; an is form inside it reports its own line.
				#expect(try eval("(:line (meta #'ct-suite/failing))") == 10)
				#expect(try eval("(:file (meta #'ct-suite/failing))") == nil)
				// test-var runs nothing for a var without :test; run-test-var returns its own summary.
				#expect(try eval("(clojure.test/test-var #'ct-suite/not-a-test)") == nil)
				#expect(try capturingOutput { _ = try eval("(clojure.test/run-test-var #'ct-suite/passing)") }.contains("Ran 1 tests containing 5 assertions.\n0 failures, 0 errors."))
				// The counters are kept by the report methods, so a reporter that drops events counts nothing but :test.
				#expect(try eval("(binding [clojure.test/report (fn [m] nil)] (clojure.test/run-test-var #'ct-suite/passing))") == Value([kw("test"): 1, kw("pass"): 0, kw("fail"): 0, kw("error"): 0, kw("type"): kw("summary")]))
				#expect(try eval("(binding [clojure.test/report (fn [m] nil)] (clojure.test/test-vars [#'ct-suite/passing #'ct-suite/not-a-test]))") == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func assertionsOutsideATest() throws {
			clj_init()
			try Self.load()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(clojure.test/is (= 1 1))") == true)
				#expect(try capturingOutput { _ = try eval("(clojure.test/is (= 1 2) \"msg\")") }.contains("FAIL in () (NO_SOURCE_PATH:1)\nmsg\nexpected: (= 1 2)\n  actual: (not (= 1 2))"))
				#expect(try eval("(clojure.test/is (thrown? ExceptionInfo (throw (ex-info \"x\" {}))))").isException)
				#expect(try capturingOutput { _ = try eval("(clojure.test/are [a b] (= a b) 1 1 2 3)") }.contains("expected: (= 2 3)\n  actual: (not (= 2 3))"))
				#expect(message("(clojure.test/are [a b] (= a b) 1)") == "The number of args doesn't match are's argv or testing doesn't have any args")
				#expect(try eval("(clojure.test/testing \"ctx\" (clojure.test/testing-contexts-str))") == "ctx")
				#expect(try eval("(clojure.test/testing \"a\" (clojure.test/testing \"b\" (clojure.test/testing-contexts-str)))") == "a b")
				#expect(try eval("(clojure.test/function? 'inc)") == true)
				#expect(try eval("(clojure.test/function? 'when)") == false)
				#expect(try eval("(clojure.test/function? 'no-such-thing)") == nil)
				#expect(try eval("(binding [clojure.test/*load-tests* false] (clojure.test/deftest never-defined (is true)))") == nil)
				#expect(try eval("(resolve 'never-defined)") == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
