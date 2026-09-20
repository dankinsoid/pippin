// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'future-tests) " + source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalErrorScoped("(in-ns 'future-tests) " + source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

// future and promise as promise-buffered channels of the coroutine runtime (NOTES.md, "Futures and scopes").
extension CoreTests {
	@Suite struct FutureTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("(ns future-tests (:require [clojure.core.async :refer [chan <! >! <!! >!! close! timeout go alts! alts!! alt! alt!! thread cancel! promise-chan poll!]]))")
			for k in ["a", "b", "blocked", "bound", "cancelled", "caught", "default", "done", "finally", "got", "k", "late", "none", "p", "ran", "root", "slept", "timed-out", "v", "x", "yes"] { _ = kw(k) }
			_ = try cljEvalScoped("(in-ns 'future-tests) (def ^:dynamic *d* :root) (defn thrower [] (throw (ex-info \"t\" {}))) (defn spawner [] (future (thrower))) (def parked (atom nil))")
		}

		@Test func futureRunsOnThePoolAndDerefParks() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("@(future (+ 1 2))") == 3)
				#expect(try eval("(let [f (future 1)] [@f @f (future-done? f) (realized? f) (future? f) (future? 1)])") == [1, 1, true, true, true, false])
				// A deref from a go block parks the coroutine, not the carrier.
				#expect(try eval("(let [c (chan) f (future (<!! c))] (<!! (go (>! c :v) @f)))") == kw("v"))
				// nil is a value too.
				#expect(try eval("(let [f (future nil)] [@f (realized? f)])") == [nil, true])
				// Bindings are conveyed.
				#expect(try eval("(binding [*d* :bound] @(future *d*))") == kw("bound"))
				#expect(try eval("@(future-call (fn [] :ran))") == kw("ran"))
			}
			base.check()
		}

		@Test func derefRethrowsTheCachedException() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [f (future (throw (ex-info \"boom\" {:k 1})))] [(try @f (catch :default e (ex-message e))) (try @f (catch :default e (:k (ex-data e)))) (future-done? f)])") == ["boom", 1, true])
				// The trace is the future's own: the throwing fn, then the spawner's frames.
				#expect(try eval("(let [f (spawner)] (try @f (catch :default e (mapv (comp name :fn) (filter :fn (ex-trace e))))))") == ["thrower", "future-call", "spawner"])
			}
			base.check()
		}

		@Test func derefWithTimeout() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [c (chan) f (future (<!! c))] (let [r (deref f 10 :timed-out)] (>!! c 1) [r @f]))") == [kw("timed-out"), 1])
				#expect(try eval("(deref (future 5) 30 :timed-out)") == 5)
				#expect(try eval("(let [p (promise)] (deref p 10 :none))") == kw("none"))
				#expect(message("(deref (atom 1) 10 :x)") == "deref with a timeout is not supported on this type: atom")
				_ = try eval("(<!! (timeout 50))")
			}
			base.check()
		}

		@Test func promiseDeliverAndRealized() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [p (promise)] [(realized? p) (some? (deliver p :v)) (realized? p) @p (deliver p :late) @p])") == [false, true, true, kw("v"), nil, kw("v")])
				// deref parks until delivered; a delivery of nil is a value.
				#expect(try eval("(let [p (promise) g (go @p)] (<!! (timeout 5)) (deliver p :v) (<!! g))") == kw("v"))
				#expect(try eval("(let [p (promise)] (deliver p nil) [@p (realized? p)])") == [nil, true])
				// Several parked derefs all get the value.
				#expect(try eval("(let [p (promise) gs (vec (repeatedly 5 #(go @p)))] (<!! (timeout 5)) (deliver p :x) (mapv <!! gs))") == [kw("x"), kw("x"), kw("x"), kw("x"), kw("x")])
			}
			base.check()
		}

		@Test func futuresAndPromisesAreAltsPorts() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [p (promise) c (chan)] (deliver p :p) (let [[v port] (alts!! [c p])] [v (= port p)]))") == [kw("p"), true])
				#expect(try eval("(let [f (future :done) c (chan)] (<!! (timeout 5)) (alt!! c :none f ([v] v)))") == kw("done"))
				// A promise channel behaves as a promise: takes keep returning the value.
				#expect(try eval("(let [c (promise-chan)] (>!! c :a) (>!! c :b) [(<!! c) (<!! c) (poll! c)])") == [kw("a"), kw("a"), kw("a")])
			}
			base.check()
		}

		@Test func futureCancel() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [c (chan) f (future (<!! c))] (<!! (timeout 5)) [(future-cancel f) (future-cancelled? f) (try @f (catch :default e (ex-message e))) (future-done? f) (future-cancel f)])") == [true, true, "Coroutine cancelled", true, false])
				#expect(try eval("(let [f (future 1)] @f [(future-cancel f) (future-cancelled? f)])") == [false, false])
				// cancel! reaches a thread body parked on a channel, and one not yet started.
				#expect(try eval("(let [c (chan) t (thread (try (<!! c) (catch :default e (ex-message e))))] (<!! (timeout 5)) [(cancel! t) (<!! t)])") == [true, "Coroutine cancelled"])
				#expect(try eval("(let [ts (vec (repeatedly 70 #(thread (try (<!! (timeout 200)) :slept (catch :default e (ex-message e))))))] (doseq [t ts] (cancel! t)) (frequencies (mapv <!! ts)))") == ["Coroutine cancelled": 70])
				// A cancelled future is done at once, as on the JVM; its body lands a moment later with the cancellation.
				#expect(try eval("(let [f (future (Thread/sleep 10000))] (<!! (timeout 5)) [(realized? f) (future-cancel f) (realized? f) (future-done? f) (try @f (catch :default e (ex-message e)))])") == [false, true, true, true, "Coroutine cancelled"])
				#expect(try eval("(let [f (future (Thread/sleep 1))] [(realized? f) (do @f (realized? f))])") == [false, true])
				// A coroutine parked in a deref is woken by its cancellation; the promise stays undelivered.
				#expect(try eval("(let [p (promise) g (go (try @p (catch :default e (ex-message e))))] (<!! (timeout 5)) (cancel! g) [(<!! g) (realized? p)])") == ["Coroutine cancelled", false])
				// The blocking thread's next job starts clean.
				#expect(try eval("(<!! (thread :ran))") == kw("ran"))
				_ = try eval("(<!! (timeout 250))")
			}
			base.check()
		}

		@Test func pmapAndFriends() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(pmap inc [1 2 3])") == [2, 3, 4])
				#expect(try eval("(pmap + [1 2 3] [10 20 30])") == [11, 22, 33])
				#expect(try eval("(vec (pmap inc (range 100)))") == (try eval("(vec (map inc (range 100)))")))
				#expect(try eval("(pcalls (fn [] 1) (fn [] 2))") == [1, 2])
				#expect(try eval("(pvalues (+ 1 1) (+ 2 2))") == [2, 4])
				// Parks inside pmap's f are parks, not blocked carriers.
				#expect(try eval("(vec (pmap (fn [i] (<!! (timeout 5)) i) (range 20)))") == (try eval("(vec (range 20))")))
			}
			base.check()
		}

		@Test func threadSleepParks() throws {
			let base = CoroBaseline()
			do {
				// Twenty sleeps of 20 ms on the pool finish together: nobody held a carrier.
				let t0 = Date()
				#expect(try eval("(let [gs (vec (repeatedly 20 #(go (Thread/sleep 20) :slept)))] (frequencies (mapv <!! gs)))") == [kw("slept"): 20])
				#expect(Date().timeIntervalSince(t0) < 0.3)
				#expect(try eval("(do (Thread/sleep 1) :ran)") == kw("ran"))
				// A sleep is a park point: cancel! wakes it.
				#expect(try eval("(let [g (go (try (Thread/sleep 5000) (catch :default e (ex-message e))))] (<!! (timeout 5)) (cancel! g) (<!! g))") == "Coroutine cancelled")
				_ = try eval("(<!! (timeout 20))")
			}
			base.check()
		}

		// A deadline is a cancellation by timer: it wakes a coroutine parked past it, and clearing it lifts the flag.
		@Test func deadlineWakesAParkedCoroutine() throws {
			let base = CoroBaseline()
			do {
				defer { clj_deadline_set_ms(0) }
				// The child inherits the spawner's deadline; the spawner's own is cleared before it joins.
				clj_deadline_set_ms(50)
				_ = try eval("(reset! parked (let [c (chan)] (go (try (<! c) (catch :default e (ex-message e))))))")
				clj_deadline_set_ms(0)
				#expect(try eval("(let [v (<!! @parked)] (reset! parked nil) v)") == "Execution timed out")
				clj_deadline_set_ms(50)
				// The bare thread's own park past the deadline is woken as well, and the cleared deadline lifts it.
				#expect(message("(<!! (chan))") == "Execution timed out")
				clj_deadline_set_ms(0)
				#expect(try eval("(<!! (go :ran))") == kw("ran"))
			}
			base.check()
		}

		@Test func withOutStrIsConveyedToAGoBlock() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(with-out-str (<!! (go (print \"inside\"))))") == "inside")
				#expect(try eval("(with-out-str (<!! (thread (print \"thread\"))))") == "thread")
				#expect(try eval("(with-out-str (print \"a\") @(future (print \"b\")) (print \"c\"))") == "abc")
			}
			base.check()
		}

		@Test func setOfAConveyedBindingIsRefused() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(binding [*d* 1] (<!! (go (try (set! *d* 2) (catch :default e (ex-message e))))))") == "Can't set!: future-tests/*d* from non-binding thread")
				// The child's own binding is its to set.
				#expect(try eval("(binding [*d* 1] (<!! (go (binding [*d* 3] (set! *d* 4) *d*))))") == 4)
				#expect(try eval("(binding [*d* 1] (set! *d* 5) *d*)") == 5)
			}
			base.check()
		}
	}
}
