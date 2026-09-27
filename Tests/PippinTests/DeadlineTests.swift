// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

// The cooperative deadline of eval.h and the bounded printing an error message quotes a value with.
extension CoreTests {
	@Suite struct DeadlineTests {
		private func withDeadline<T>(ms: UInt64, _ body: () throws -> T) rethrows -> T {
			clj_deadline_set_ms(ms)
			defer { clj_deadline_set_ms(0) }
			return try body()
		}

		@Test func loopWithoutCallsIsInterrupted() throws {
			let message = withDeadline(ms: 100) { cljEvalError("(loop [i 0] (recur (inc i)))") }
			#expect(message?.contains("Execution timed out") == true)
		}

		@Test func anInfiniteLazySeqIsInterrupted() throws {
			let message = withDeadline(ms: 100) { cljEvalError("(count (iterate inc 0))") }
			#expect(message?.contains("Execution timed out") == true)
		}

		@Test func aLoopThatCatchesTheTimeoutStillStops() throws {
			// The unwind budgets run out, after which every check throws and the catching loop cannot resume.
			let message = withDeadline(ms: 100) {
				cljEvalError("(loop [i 0] (try (loop [j 0] (recur (inc j))) (catch :cancelled e nil)) (recur (inc i)))")
			}
			#expect(message?.contains("Execution timed out") == true)
		}

		@Test func noDeadlineLetsWorkFinish() throws {
			clj_deadline_set_ms(0)
			#expect(try cljEval("(reduce + (range 100000))") == 4999950000)
			#expect(clj_deadline_expired() == false)
		}

		@Test func theDeadlineIsClearedByZero() throws {
			clj_deadline_set_ms(1)
			clj_deadline_set_ms(0)
			#expect(clj_deadline_get() == 0)
			#expect(try cljEval("(loop [i 0] (if (< i 100000) (recur (inc i)) i))") == 100000)
		}

		@Test func theDeadlineIsHeldAndRestored() throws {
			clj_deadline_set_ms(60000)
			let held = clj_deadline_get()
			#expect(held != 0)
			clj_deadline_restore(0)
			#expect(clj_deadline_get() == 0)
			clj_deadline_restore(held)
			#expect(clj_deadline_get() == held)
			clj_deadline_set_ms(0)
		}

		// ---- with-deadline (core.clj): the construct that imposes a deadline, and the :timeout it throws

		@Test func withDeadlineThrowsTimeout() throws {
			#expect(try cljEval("(try (with-deadline 50 (loop [i 0] (recur (inc i)))) (catch :timeout e [(ex-type e) (:timeout/ms (ex-data e))]))")
				== [Value(keyword: "timeout"), 50])
		}

		// The deadline is the code's own, so it is an ordinary failure: the :cancelled exclusion does not apply.
		@Test func aTimeoutIsCaughtByDefault() throws {
			#expect(try cljEval("(try (with-deadline 50 (loop [i 0] (recur (inc i)))) (catch :default e (ex-type e)))") == Value(keyword: "timeout"))
		}

		// The cancel flag is sticky, so a timeout that did not clear it would throw again at the next call.
		@Test func aTimeoutLeavesTheCoroutineRunnable() throws {
			#expect(try cljEval("""
			(let [a (atom nil)
			      r (try (with-deadline 50 (try (loop [i 0] (recur (inc i))) (finally (reset! a :ran))))
			             (catch :timeout e :caught))]
			  [r @a (cancelled?*) (reduce + (range 100000))])
			""") == [Value(keyword: "caught"), Value(keyword: "ran"), false, 4999950000])
		}

		// The ring holds one deadline, so a firing one names no owner: being stopped from outside is not my timeout.
		@Test func anOuterDeadlinePassesThroughTheInnerOne() throws {
			#expect(try cljEval("""
			(let [saw (atom nil)
			      out (try (with-deadline 50
			                 (try (with-deadline 60000 (loop [i 0] (recur (inc i))))
			                      (catch :timeout e (reset! saw :timeout))
			                      (catch :cancelled e (reset! saw :cancelled))))
			               (catch :timeout e :outer))]
			  [@saw out])
			""") == [Value(keyword: "cancelled"), Value(keyword: "outer")])
		}

		@Test func anInnerDeadlineTimesOutAndRestoresTheOuterOne() throws {
			#expect(try cljEval("""
			(with-deadline 60000
			  [(try (with-deadline 50 (loop [i 0] (recur (inc i)))) (catch :timeout e :inner))
			   (reduce + (range 100000))])
			""") == [Value(keyword: "inner"), 4999950000])
		}

		// Zero is no time at all, not the "clear it" of clj_deadline_set_ms.
		@Test func aBodyThatNeverMetTheExpiryStillTimesOut() throws {
			#expect(try cljEval("(try (with-deadline 0 :done) (catch :timeout e :timed-out))") == Value(keyword: "timed-out"))
		}

		@Test func aNegativeDeadlineIsAnArgumentError() throws {
			#expect(cljEvalError("(with-deadline -1 :x)")?.contains("non-negative") == true)
		}

		// The message quotes at most CLJ_ERROR_PRINT_MAX bytes of the value, so an unbounded seq at the head
		// of a call reports instead of printing for ever.
		@Test func invokingAnInfiniteSeqReports() throws {
			let message = cljEvalError("((iterate inc 0))")
			#expect(message?.contains("cannot be invoked") == true)
			#expect(message?.contains("...") == true)
		}

		@Test func boundedPrintingClosesWhatItOpened() throws {
			clj_init()
			let long = try cljEval("(vec (range 100))")
			let text = withExtendedLifetime(long) { Value(owning: clj_pr_str_max(long.raw, 20)) }
			#expect(text.string?.hasPrefix("[0 1 2 3 4 5 6 7 8 9") == true)
			#expect(text.string?.hasSuffix(" ...]") == true)
		}

		@Test func boundedPrintingLeavesShortValuesAlone() throws {
			clj_init()
			let v = try cljEval("{:a [1 2] :b \"x\"}")
			let bounded = withExtendedLifetime(v) { Value(owning: clj_pr_str_max(v.raw, 64)) }
			let full = withExtendedLifetime(v) { Value(owning: clj_pr_str(v.raw)) }
			#expect(bounded.string == full.string)
		}
	}
}
