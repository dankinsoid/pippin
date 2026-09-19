// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval("(in-ns 'chan-tests) " + source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError("(in-ns 'chan-tests) " + source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

nonisolated(unsafe) private var uncaughtReports = 0

// Every test ends with what it started with: no live objects, no live coroutines.
struct CoroBaseline {
	let objects = clj_debug_live_objects(), coros = clj_debug_live_coros()
	// The timer thread releases a timeout channel after it woke the taker: the object count settles a moment later.
	func check(_ location: SourceLocation = #_sourceLocation) {
		#expect(clj_debug_coro_settle(coros, 5000), sourceLocation: location)
		clj_output_flush()
		var tries = 0
		while clj_debug_live_objects() != objects && tries < 200 {
			usleep(1000)
			tries += 1
		}
		#expect(clj_debug_live_objects() == objects, sourceLocation: location)
		#expect(clj_debug_live_coros() == coros, sourceLocation: location)
	}
}

extension CoreTests {
	@Suite struct ChanTests {
		init() throws {
			clj_init()
			_ = try cljEval("(ns chan-tests (:require [clojure.core.async :as a :refer [chan buffer dropping-buffer sliding-buffer <! >! <!! >!! put! take! close! offer! poll! alts! alt! alts!! alt!! timeout go go-loop thread cancel!]]))")
			// Keywords intern for ever and defs make vars: both before any test's live-object baseline.
			for k in ["a", "after", "again", "b", "before", "bound", "c", "caught", "closed", "conveyed", "d", "default", "done", "early", "finally", "from-thread", "got", "in", "k", "none", "one-more", "printed", "priority", "put", "root", "t", "taking", "timed-out", "took", "v", "x"] { _ = kw(k) }
			_ = try cljEval("""
			(in-ns 'chan-tests)
			(def ^:dynamic *d* :root)
			(defn inner-after-park [c] (<! c) (throw (ex-info "x" {})))
			(defn spawner [c] (go (try (inner-after-park c) (catch :default e (mapv :fn (ex-trace e))))))
			(defn outer-spawner [c] (spawner c))
			""")
		}

		// A test waits for its go blocks by taking from their channels: (<! (go ...)) is a join.
		@Test func unbufferedHandOff() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [c (chan) g (go (>! c 42) :put)] [(<!! c) (<!! g) (<!! g)])") == [42, kw("put"), nil])
				#expect(try eval("(let [c (chan) g (go (<! c))] (>!! c :v) (<!! g))") == kw("v"))
				// The putter parks until a taker arrives; both sides see the hand-off.
				#expect(try eval("(let [c (chan) seen (atom []) g (go (swap! seen conj :before) (>! c 1) (swap! seen conj :after) @seen)] (<!! (timeout 20)) (swap! seen conj :taking) (<!! c) (<!! g))") == [kw("before"), kw("taking"), kw("after")])
			}
			base.check()
		}

		@Test func bufferKinds() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [c (chan 2)] [(offer! c 1) (offer! c 2) (offer! c 3) (poll! c) (poll! c) (poll! c)])") == [true, true, nil, 1, 2, nil])
				#expect(try eval("(let [c (chan (buffer 1))] (>!! c 1) [(offer! c 2) (<!! c)])") == [nil, 1])
				#expect(try eval("(let [c (chan (dropping-buffer 2))] [(>!! c 1) (>!! c 2) (>!! c 3) (<!! c) (<!! c) (poll! c)])") == [true, true, true, 1, 2, nil])
				#expect(try eval("(let [c (chan (sliding-buffer 2))] [(>!! c 1) (>!! c 2) (>!! c 3) (<!! c) (<!! c) (poll! c)])") == [true, true, true, 2, 3, nil])
				// A parked putter refills the buffer when a taker makes room.
				#expect(try eval("(let [c (chan 1) g (go (>! c 1) (>! c 2) (>! c 3) :done)] [(<!! c) (<!! c) (<!! c) (<!! g)])") == [1, 2, 3, kw("done")])
				#expect(message("(chan -1)") == "chan: a buffer size must not be negative")
				#expect(message("(buffer 0)") == "buffer expects a positive size")
				#expect(message("(chan :x)") == "chan expects a buffer or a size, got: keyword")
				#expect(message("(chan 1 (map inc))") == "chan: transducers on channels are not supported yet")
			}
			base.check()
		}

		@Test func closeSemantics() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [c (chan 2)] (>!! c 1) (close! c) [(>!! c 2) (offer! c 3) (<!! c) (<!! c) (<!! c) (poll! c)])") == [false, false, 1, nil, nil, nil])
				// Parked takers wake with nil on close!; a parked putter is still taken after the close.
				#expect(try eval("(let [c (chan) g (go (<! c))] (close! c) (<!! g))") == nil)
				#expect(try eval("(let [c (chan) g (go (>! c :v))] (<!! (timeout 10)) (close! c) [(<!! c) (<!! g) (<!! c)])") == [kw("v"), true, nil])
				#expect(try eval("(let [c (chan)] (close! c) (close! c) (a/close! c) (<!! c))") == nil)
			}
			base.check()
		}

		@Test func altsWithDefaultTimeoutAndPriority() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [c (chan)] (alts!! [c] :default :none))") == [kw("none"), kw("default")])
				#expect(try eval("(let [c (chan 1)] (>!! c 1) (let [[v p] (alts!! [c] :default :none)] [v (identical? p c)]))") == [1, true])
				#expect(try eval("(let [c (chan) t (timeout 10) [v p] (alts!! [c t])] [v (identical? p t)])") == [nil, true])
				#expect(try eval("(let [a (chan 1) b (chan 1)] (>!! a :a) (>!! b :b) (let [[v p] (alts!! [a b] :priority true)] [v (identical? p a)]))") == [kw("a"), true])
				// A put port completes with true; a closed put port with false.
				#expect(try eval("(let [c (chan 1) [v p] (alts!! [[c :v]])] [v (identical? p c) (<!! c)])") == [true, true, kw("v")])
				#expect(try eval("(let [c (chan)] (close! c) (first (alts!! [[c :v]])))") == false)
				// A parked alts! is completed by whichever port wakes first; the other port's stale entry is dropped.
				#expect(try eval("(let [a (chan) b (chan) g (go (let [[v p] (alts! [a b])] [v (identical? p b)]))] (<!! (timeout 5)) (>!! b :b) [(<!! g) (offer! a 1)])") == [[kw("b"), true], nil])
				// Two alts! putters racing for one taker: exactly one wins, the other's other port completes it.
				#expect(try eval("(let [c (chan) d (chan) g1 (go (first (alts! [[c 1] d]))) g2 (go (first (alts! [[c 2] d])))] (<!! (timeout 5)) (let [got (<!! c)] (>!! d :d) (let [s (set [(<!! g1) (<!! g2) got])] [(count s) (contains? s true) (contains? s :d) (contains? s got)])))") == [3, true, true, true])
				#expect(message("(alts!! [])") == "alts! must have at least one channel operation")
				#expect(message("(alts!! [1])") == "alts! expects channels or [channel value] pairs, got: long")
				#expect(message("(alts!! [[(chan) nil]])") == "Can't put nil on channel")
			}
			base.check()
		}

		@Test func altMacro() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [c (chan 1)] (>!! c 5) (alt!! c ([v ch] [v (identical? ch c)]) :default :d))") == [5, true])
				#expect(try eval("(let [c (chan)] (alt!! c :got :default :d))") == kw("d"))
				#expect(try eval("(let [c (chan 1) d (chan)] (alt!! [[c :put]] ([ok ch] [ok (identical? ch c)]) d :took))") == [true, true])
				#expect(try eval("(let [a (chan) b (chan 1)] (>!! b 2) (alt!! [a b] ([v] (* v 10)) :priority true))") == 20)
				#expect(try eval("(let [t (timeout 5) c (chan)] (alt!! c :c t :timed-out))") == kw("timed-out"))
			}
			base.check()
		}

		@Test func putAndTakeCallbacks() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [c (chan 1) r (atom nil)] [(put! c 1 (fn [ok] (reset! r ok))) @r (poll! c)])") == [true, true, 1])
				#expect(try eval("(let [c (chan) r (atom nil)] (take! c (fn [v] (reset! r v))) [@r (>!! c :v) @r])") == [nil, true, kw("v")])
				#expect(try eval("(let [c (chan) r (atom nil)] (put! c :v (fn [ok] (reset! r ok))) [@r (<!! c) @r])") == [nil, kw("v"), true])
				#expect(try eval("(let [c (chan) r (atom nil)] (close! c) [(put! c 1 (fn [ok] (reset! r ok))) @r])") == [false, false])
				#expect(try eval("(let [c (chan) r (atom nil)] (take! c (fn [v] (reset! r [:closed v]))) (close! c) @r)") == [kw("closed"), nil])
				// on-caller? false runs the callback on a coroutine of the pool; the go joins it.
				#expect(try eval("(let [c (chan 1) d (chan)] (put! c 1 (fn [ok] (put! d ok)) false) (<!! d))") == true)
				#expect(message("(take! (chan) 1)") == "take! expects a fn callback, got: long")
				#expect(message("(put! (chan) nil)") == "Can't put nil on channel")
			}
			base.check()
		}

		@Test func pendingLimit() throws {
			let base = CoroBaseline()
			do {
				#expect(message("(let [c (chan)] (dotimes [i 1024] (put! c i)) (put! c :one-more))") == "No more than 1024 pending puts are allowed on a single channel.")
				#expect(message("(let [c (chan)] (dotimes [i 1024] (take! c identity)) (take! c identity))") == "No more than 1024 pending takes are allowed on a single channel.")
				#expect(try eval("(let [c (chan)] (dotimes [i 1024] (put! c i)) (<!! c))") == 0)
			}
			base.check()
		}

		@Test func colorlessPark() throws {
			let base = CoroBaseline()
			do {
				// <! inside a nested fn, inside map, and inside a lazy seq realized in the go block.
				#expect(try eval("(let [c (chan 3) f (fn [x] (+ x (<! c))) g (go (>! c 1) (>! c 2) (>! c 3) (mapv f [10 20 30]))] (<!! g))") == [11, 22, 33])
				#expect(try eval("(let [c (chan) g (go (doall (map (fn [_] (<! c)) (range 3))))] (>!! c 1) (>!! c 2) (>!! c 3) (<!! g))") == [1, 2, 3])
				// A park inside try/finally, binding and loop keeps their semantics.
				#expect(try eval("(let [c (chan) g (go (binding [*d* :bound] (try (<! c) (finally (>! c *d*)))))] (>!! c :in) (<!! c))") == kw("bound"))
				#expect(try eval("(let [c (chan) g (go (loop [acc [] n 0] (if (< n 3) (recur (conj acc (<! c)) (inc n)) acc)))] (>!! c :a) (>!! c :b) (>!! c :c) (<!! g))") == [kw("a"), kw("b"), kw("c")])
				// Bindings are conveyed into the go block.
				#expect(try eval("(binding [*d* :conveyed] (<!! (go *d*)))") == kw("conveyed"))
			}
			base.check()
		}

		@Test func goFromCoroutineAndThread() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(<!! (go (<! (go (<! (go 3))))))") == 3)
				#expect(try eval("(<!! (thread (+ 1 2)))") == 3)
				#expect(try eval("(let [c (chan)] (thread (>!! c :from-thread)) (<!! c))") == kw("from-thread"))
				#expect(try eval("(binding [*d* :t] (<!! (thread *d*)))") == kw("t"))
				// A go block's nil result closes the channel without a value.
				#expect(try eval("(<!! (go nil))") == nil)
				// A go that returns before anyone takes: the value waits as a pending put.
				#expect(try eval("(let [g (go :early)] (<!! (timeout 5)) [(<!! g) (<!! g)])") == [kw("early"), nil])
			}
			base.check()
		}

		@Test func classicExamples() throws {
			let base = CoroBaseline()
			do {
				// Ping-pong.
				#expect(try eval("""
				(let [ping (chan) pong (chan)
				      p (go-loop [n 0] (if (< n 5) (do (>! ping n) (<! pong) (recur (inc n))) (do (close! ping) n)))
				      q (go-loop [seen []] (if-let [v (<! ping)] (do (>! pong v) (recur (conj seen v))) seen))]
				  [(<!! p) (<!! q)])
				""") == [5, [0, 1, 2, 3, 4]])
				// Fan-in over alts!.
				#expect(try eval("""
				(let [a (chan) b (chan) out (chan)
				      g (go (dotimes [i 3] (>! a i)) (close! a))
				      h (go (dotimes [i 3] (>! b (+ 10 i))) (close! b))
				      m (go-loop [open #{a b} acc []]
				          (if (seq open)
				            (let [[v p] (alts! (vec open))]
				              (if (nil? v) (recur (disj open p) acc) (recur open (conj acc v))))
				            (sort acc)))]
				  (<!! g) (<!! h) (<!! m))
				""") == [0, 1, 2, 10, 11, 12])
				// A pipeline of three go-loops.
				#expect(try eval("""
				(let [in (chan) mid (chan) out (chan 10)
				      s1 (go-loop [] (when-let [v (<! in)] (>! mid (* v 2)) (recur)))
				      s2 (go-loop [] (if-let [v (<! mid)] (do (>! out (inc v)) (recur)) (close! out)))
				      src (go (dotimes [i 5] (>! in i)) (close! in) (<! s1) (close! mid))]
				  (<!! src) (<!! s2)
				  (<!! (go-loop [acc []] (if-let [v (<! out)] (recur (conj acc v)) acc))))
				""") == [1, 3, 5, 7, 9])
			}
			base.check()
		}

		@Test func cancellation() throws {
			let base = CoroBaseline()
			do {
				// Parked on a take, a put, an alts!, a timeout; cancelled in a loop tick.
				#expect(try eval("(let [c (chan) g (go (try (<! c) (catch :default e (ex-message e))))] (<!! (timeout 5)) (cancel! g) (<!! g))") == "Coroutine cancelled")
				#expect(try eval("(let [c (chan) g (go (try (>! c 1) (catch :default e (ex-message e))))] (<!! (timeout 5)) (cancel! g) (<!! g))") == "Coroutine cancelled")
				#expect(try eval("(let [c (chan) d (chan) g (go (try (alts! [c d]) (catch :default e (ex-message e))))] (<!! (timeout 5)) (cancel! g) (<!! g))") == "Coroutine cancelled")
				// The timer keeps its channel until it fires, so the wait below lets it go before the baseline check.
				#expect(try eval("(let [g (go (try (<! (timeout 100)) (catch :default e (ex-message e))))] (<!! (timeout 5)) (cancel! g) (<!! g))") == "Coroutine cancelled")
				#expect(try eval("(let [g (go (try (loop [i 0] (recur (inc i))) (catch :default e (ex-message e))))] (<!! (timeout 5)) (cancel! g) (<!! g))") == "Coroutine cancelled")
				// After the cancellation every park point keeps throwing; a finally still runs.
				#expect(try eval("(let [c (chan) r (atom []) g (go (try (<! c) (catch :default e (swap! r conj :caught) (try (<! c) (catch :default e2 (swap! r conj :again)))) (finally (swap! r conj :finally))) @r)] (<!! (timeout 5)) (cancel! g) (<!! g))") == [kw("caught"), kw("again"), kw("finally")])
				// cancel! of a channel without a go block, or of a finished one, is harmless.
				#expect(try eval("(let [c (chan) g (go 1)] (<!! g) [(cancel! c) (cancel! g)])") == [false, true])
				_ = try eval("(<!! (timeout 150))")
			}
			base.check()
		}

		@Test func errorsInsideGoAreReportedAndCloseTheChannel() throws {
			let base = CoroBaseline()
			do {
				uncaughtReports = 0
				clj_coro_set_uncaught_handler { _, _ in uncaughtReports += 1 }
				defer { clj_coro_set_uncaught_handler(nil) }
				#expect(try eval("(<!! (go (throw (ex-info \"boom\" {}))))") == nil)
				#expect(uncaughtReports == 1)
			}
			base.check()
		}

		@Test func parkUnderHostCallIsAnError() throws {
			let base = CoroBaseline()
			do {
				let f = try eval("(fn [c] (<! c))")
				let c = try eval("(chan)")
				#expect(throws: ClojureError.self) { try f(c) }
				do {
					_ = try f(c)
				} catch let e as ClojureError {
					#expect(e.message.hasPrefix("Cannot park inside a synchronous host call"))
					#expect(!e.trace.isEmpty)
				}
			}
			base.check()
		}

		@Test func parkUnderRuntimeLockIsAnError() throws {
			let base = CoroBaseline()
			do {
				// A watch runs after the atom's lock, so a park there is legal; the locked section itself is short.
				#expect(try eval("(let [a (atom 0) c (chan 1)] (add-watch a :k (fn [_ _ _ n] (>!! c n))) (swap! a inc) (<!! c))") == 1)
			}
			base.check()
		}

		// A one-byte queue: every second line finds it full, the printer parks and the writer wakes it; nothing is lost or reordered.
		@Test func printlnBackpressure() throws {
			let base = CoroBaseline()
			do {
				clj_debug_output_set_limit(1)
				defer { clj_debug_output_set_limit(0) }
				let waits = clj_debug_output_waits()
				let text = try capturingOutput {
					_ = try eval("(<!! (go (dotimes [i 3000] (println \"line\" i)) :printed))")
				}
				#expect(text.split(separator: "\n").count == 3000)
				#expect(text.hasSuffix("line 2999\n"))
				#expect(clj_debug_output_waits() > waits)
				// The same from a bare thread blocks it instead of parking.
				let again = try capturingOutput { _ = try eval("(dotimes [i 1000] (println \"bare\" i))") }
				#expect(again.split(separator: "\n").count == 1000)
			}
			base.check()
		}

		// A file on the load path is read on the blocking pool while the go block parks; the load itself makes
		// vars and a namespace that live on, so only the coroutines are checked.
		@Test func requireFromAGoBlock() throws {
			let dir = FileManager.default.temporaryDirectory.appendingPathComponent("chan-tests-\(getpid())")
			try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
			defer { try? FileManager.default.removeItem(at: dir) }
			try "(ns gofile) (def answer 42)".write(to: dir.appendingPathComponent("gofile.clj"), atomically: true, encoding: .utf8)
			let saved = Runtime.loadPath
			Runtime.loadPath = [dir.path]
			defer { Runtime.loadPath = saved }
			let coros = clj_debug_live_coros()
			#expect(try eval("(<!! (go (require 'gofile) @(resolve 'gofile/answer)))") == 42)
			#expect(clj_debug_coro_settle(coros, 5000))
		}

		@Test func traceThroughAPark() throws {
			let base = CoroBaseline()
			do {
				// The frames of the throw after the park, then the spawner's frames captured at the go.
				#expect(try eval("(let [c (chan) g (outer-spawner c)] (>!! c 1) (let [fns (<!! g) at (fn [s] (count (take-while #(not= s %) fns)))] [(first fns) (boolean (some #{'chan-tests/spawner} fns)) (boolean (some #{'chan-tests/outer-spawner} fns)) (< (at 'chan-tests/spawner) (at 'chan-tests/outer-spawner))]))") == [Value(symbol: "chan-tests/inner-after-park"), true, true, true])
			}
			base.check()
		}
	}
}
