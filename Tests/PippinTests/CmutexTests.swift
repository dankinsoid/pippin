// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval("(in-ns 'cmutex-tests) " + source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError("(in-ns 'cmutex-tests) " + source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

extension CoreTests {
	@Suite struct CmutexTests {
		init() throws {
			clj_init()
			_ = try cljEval("(ns cmutex-tests (:require [clojure.core.async :refer [chan <! >! <!! >!! close! timeout go thread]]))")
			for k in ["twice", "a", "b", "again", "go", "open", "x"] { _ = kw(k) }
			_ = try cljEval("(in-ns 'cmutex-tests) (declare cm-self)")
		}

		@Test func lockingIsReentrant() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [o (atom nil)] (locking o (locking o :twice)))") == kw("twice"))
				#expect(try eval("(let [o (atom nil) p (atom nil)] (locking o (locking p (locking o 3))))") == 3)
				#expect(message("(monitor-exit* (atom nil))") == "monitor-exit of an object this execution does not hold")
				// A throw inside the body releases the monitor.
				#expect(try eval("(let [o (atom nil)] (try (locking o (throw (ex-info \"x\" {}))) (catch :default e nil)) (locking o :again))") == kw("again"))
				#expect(clj_debug_live_monitors() == 0)
			}
			base.check()
		}

		// Four carriers contend for one monitor and park inside the held section; nothing is lost or reordered.
		@Test func lockingContendedFromFourCarriers() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("""
				(let [o (atom nil) n (atom 0) shared (volatile! 0) done (chan)]
				  (dotimes [t 4] (go (dotimes [i 1000] (locking o (vswap! shared inc) (when (zero? (mod i 100)) (<! (timeout 0))) (swap! n inc))) (>! done t)))
				  (dotimes [t 4] (<!! done))
				  [@shared @n])
				""") == [4000, 4000])
				// The same from bare threads (the hybrid: a contended lock blocks the thread).
				#expect(try eval("""
				(let [o (atom nil) shared (volatile! 0) done (chan)]
				  (dotimes [t 4] (thread (dotimes [i 1000] (locking o (vswap! shared inc))) (>!! done t)))
				  (dotimes [t 4] (<!! done))
				  @shared)
				""") == 4000)
				#expect(clj_debug_live_monitors() == 0)
			}
			base.check()
		}

		// A lazy seq forced by one coroutine while three wait for it: the waiters park, the thunk runs once.
		@Test func lazySeqForcingWaitsOnTheLot() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("""
				(let [calls (atom 0) gate (chan) s (lazy-seq (swap! calls inc) (<! gate) [:a :b]) done (chan)]
				  (dotimes [t 4] (go (>! done (first s))))
				  (<!! (timeout 20))
				  (>!! gate :open)
				  [(vec (repeatedly 4 #(<!! done))) @calls])
				""") == [[kw("a"), kw("a"), kw("a"), kw("a")], 1])
				// A thunk reaching its own seq throws: waiting for itself would be a deadlock.
				#expect(message("(def cm-self (lazy-seq (cons (first cm-self) nil))) (first cm-self)") == "Recursive realization of a lazy seq")
				_ = try eval("(def cm-self nil)")
			}
			base.check()
		}

		// Every f runs once even when it parks inside; a deref during f reads the value f was given, without waiting.
		@Test func swapContendedAndDerefWhileFRuns() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("""
				(let [a (atom 0) done (chan)]
				  (dotimes [t 4] (go (dotimes [i 250] (swap! a (fn [v] (when (zero? (mod i 50)) (<! (timeout 0))) (inc v)))) (>! done t)))
				  (dotimes [t 4] (<!! done))
				  @a)
				""") == 1000)
				#expect(try eval("""
				(let [a (atom 0) gate (chan) seen (chan 1)]
				  (go (swap! a (fn [v] (<! gate) (+ v 10))))
				  (<!! (timeout 10))
				  (>!! seen @a)
				  (>!! gate :go)
				  (<!! (timeout 10))
				  [(<!! seen) @a])
				""") == [0, 10])
			}
			base.check()
		}
	}
}
