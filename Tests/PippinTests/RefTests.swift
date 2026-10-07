// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'ref-tests) " + source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalErrorScoped("(in-ns 'ref-tests) " + source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

// Every expectation outside the contention tests is what JVM Clojure 1.12.6 answers for the same forms.
extension CoreTests {
	@Suite struct RefTests {
		init() throws {
			clj_init()
			try cljTimingSupport()
			for k in ["a", "b", "k", "first", "aborted", "x"] { _ = kw(k) }
			_ = try cljEvalScoped("""
			(ns ref-tests)
			(defn msg [f] (try (f) (catch :default e (ex-message e))))
			(let [r (ref 0 :validator number? :meta {:a 1} :min-history 1 :max-history 2) g (agent nil)]
			  (add-watch r :k (fn [& _])) (dosync (alter r inc) (commute r inc) (ensure r) (send g identity)) (await g)
			  (msg #(dosync (ref-set r 1) (throw (ex-info "x" {})))))
			""")
		}

		@Test func aTransactionReadsAndWritesItsRefs() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [r (ref 1)] [@r (dosync (alter r inc)) @r (dosync (ref-set r 10)) @r (dosync (commute r + 5)) @r (dosync (ensure r))])") == [1, 2, 2, 10, 10, 15, 15, 15])
				// A nested dosync joins the running transaction and sees its writes.
				#expect(try eval("(let [w (ref 0)] (dosync (alter w inc) (dosync (alter w inc)) [@w (dosync @w)]))") == [2, 2])
				#expect(try eval("[(sync nil 1 2) (dosync)]") == [2, nil])
				#expect(try eval("(let [r (ref 1)] [(msg #(alter r inc)) (msg #(ref-set r 1)) (msg #(commute r inc)) (msg #(ensure r))])") == ["No transaction running", "No transaction running", "No transaction running", "No transaction running"])
				#expect(try eval("(let [r (ref 1)] [(msg #(dosync (commute r inc) (alter r inc))) @r (dosync (alter r inc) (commute r inc)) @r])") == ["Can't set after commute", 1, 3, 3])
				// A throw aborts: nothing is written.
				#expect(try eval("(let [r (ref 1)] [(msg #(dosync (ref-set r 100) (throw (ex-info \"abort\" {})))) @r])") == ["abort", 1])
				#expect(try eval("[(msg #(dosync (io! \"nope\" 1))) (io! 2) (msg #(dosync (io! 3)))]") == ["nope", 2, "I/O in transaction"])
				#expect(message("(dosync (alter 1 inc))") == "alter expects a ref, got: long")
			}
			base.check()
		}

		@Test func validatorsWatchesMetaAndHistory() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [v (ref 1 :validator pos?)] [(msg #(dosync (ref-set v -1))) @v (some? (get-validator v))])") == ["Invalid reference state", 1, true])
				#expect(message("(ref -1 :validator pos?)") == "Invalid reference state")
				// A rejected write aborts the whole transaction, the valid writes beside it included.
				#expect(try eval("(let [v (ref 1 :validator pos?) u (ref 1)] [(msg #(dosync (alter u inc) (ref-set v -1))) @u])") == ["Invalid reference state", 1])
				#expect(try eval("(let [w (ref 0 :meta {:a 1} :min-history 2 :max-history 5)] [(meta w) (ref-min-history w) (ref-max-history w) (ref-history-count w) (identical? w (ref-min-history w 3)) (ref-min-history w) (identical? w (ref-max-history w 7)) (ref-max-history w) (alter-meta! w assoc :b 2) (reset-meta! w {}) (meta w)])") == [[kw("a"): 1], 2, 5, 0, true, 3, true, 7, [kw("a"): 1, kw("b"): 2], [:], [:]])
				// Watches see the committed value once per transaction, after the commit and outside it.
				#expect(try eval("(let [log (atom []) w (ref 0)] [(identical? w (add-watch w :k (fn [k rf o n] (swap! log conj [k (identical? rf w) o n (msg #(alter w inc))])))) (do (dosync (alter w inc) (alter w inc)) @log) (identical? w (remove-watch w :k))])") == [true, [[kw("k"), true, 0, 2, "No transaction running"]], true])
			}
			base.check()
		}

		@Test func transactionsAreAtomicUnderContention() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [a (ref 0) b (ref 0) fs (doall (for [i (range 8)] (future (dotimes [_ 200] (dosync (alter a inc) (alter b dec))))))] (run! deref fs) [@a @b])") == [1600, -1600])
				// Half the transactions take the refs against id order: theirs retry with both taken in order.
				#expect(try eval("(let [c (ref 0) d (ref 0) fs (doall (for [i (range 8)] (future (dotimes [_ 200] (if (even? i) (dosync (alter c inc) (alter d inc)) (dosync (alter d inc) (alter c inc)))))))] (run! deref fs) [@c @d])") == [1600, 1600])
				#expect(try eval("(let [accts (vec (for [_ (range 5)] (ref 100))) fs (doall (for [i (range 8)] (future (dotimes [j 300] (let [x (rand-int 5) y (rand-int 5) n (rand-int 10)] (dosync (when (>= @(accts x) n) (alter (accts x) - n) (alter (accts y) + n))))))))] (run! deref fs) (reduce + (map deref accts)))") == 500)
				// A body that swallows the retry it was refused with still retries.
				#expect(try eval("(let [c (ref 0) d (ref 0) fs (doall (for [i (range 4)] (future (dotimes [_ 100] (dosync (try (if (even? i) (do (alter c inc) (alter d inc)) (do (alter d inc) (alter c inc))) (catch :default _ nil)))))))] (run! deref fs) [@c @d])") == [400, 400])
			}
			base.check()
		}

		@Test func agentSendsWaitForTheCommit() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [ag (agent []) e (ref 0)] (dosync (send ag conj :first) (alter e inc)) (msg #(dosync (send ag conj :aborted) (throw (ex-info \"x\" {})))) (await ag) @ag)") == [kw("first")])
				#expect(try eval("(let [ag (agent [])] [(msg #(dosync (await ag))) (msg #(dosync (await-for 10 ag)))])") == ["await in transaction", "await-for in transaction"])
				// A future started inside dosync runs outside the transaction.
				#expect(try eval("(let [e (ref 0)] (dosync @(future (msg #(alter e inc)))))") == "No transaction running")
			}
			base.check()
		}
	}
}
