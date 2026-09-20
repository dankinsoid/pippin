// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'async-lib-tests) " + source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalErrorScoped("(in-ns 'async-lib-tests) " + source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

nonisolated(unsafe) private var uncaughtReports = 0

// The library layer of core.async over the primitives, transducers on channels, and go-scoped (NOTES.md,
// "Channels" and "Futures and scopes").
extension CoreTests {
	@Suite struct AsyncLibTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("(ns async-lib-tests (:require [clojure.core.async :as a :refer [chan buffer dropping-buffer sliding-buffer <! >! <!! >!! put! take! close! offer! poll! alts! alt! alts!! alt!! timeout go go-loop thread cancel! go-scoped plet promise-chan pipe mult tap untap untap-all pub sub unsub unsub-all mix admix unmix unmix-all toggle solo-mode merge onto-chan! to-chan! onto-chan to-chan pipeline pipeline-blocking pipeline-async split unblocking-buffer?]]))")
			for k in ["a", "b", "body", "cancelled", "caught", "child", "closed", "done", "err", "even", "finally", "int", "odd", "one", "ran", "second", "put", "string", "take", "two", "unreached", "x", "y", "z"] { _ = kw(k) }
			_ = try cljEvalScoped("""
			(in-ns 'async-lib-tests)
			(defn mapping [f] (fn [f1] (fn ([] (f1)) ([result] (f1 result)) ([result input] (f1 result (f input))))))
			(defn xerox [n] (fn [f1] (fn ([] (f1)) ([result] (f1 result)) ([result input] (loop [res result i n] (if (pos? i) (let [a (f1 result input)] (if (reduced? a) a (recur a (dec i)))) res))))))
			(defn pipeline-tester [pipeline-fn n inputs xf] (let [cin (to-chan! inputs) cout (chan 1)] (pipeline-fn n cout xf cin) (<!! (go-loop [acc []] (let [val (<! cout)] (if (not (nil? val)) (recur (conj acc val)) acc))))))
			;; (f) under a scope whose children (a mix's loop, which never ends by itself) are cancelled once f returned.
			(defn helper [r] (go (<! (timeout 10)) (reset! r :done)))
			(defn until-done [f] (let [p (promise) g (go (try (go-scoped (deliver p (try [(f)] (catch :default e [nil e]))) (<! (chan))) (catch :default e nil)))] (let [[v e] @p] (cancel! g) (<!! g) (when e (throw e)) v)))
			;; A reify site makes its type on first use and every macro site its nodes: warmed here, before any baseline.
			(until-done (fn [] (let [src (chan) out (chan 1)] (tap (mult src) out) (sub (pub src :k) :k out) (admix (mix out) src) (alt!! [[out 1]] :put src :take) nil)))
			""")
		}

		@Test func transducersOnChannels() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [c (chan 1 (map inc))] (>!! c 1) (<!! c))") == 2)
				#expect(try eval("(<!! (a/into [] (a/to-chan! [1 2 3])))") == [1, 2, 3])
				// An expanding transducer overfills the buffer; a filtering one completes puts without a transfer.
				#expect(try eval("(let [c (chan 1 (mapcat identity))] [(>!! c [1 2 3]) (<!! c) (offer! c [4 5 6]) (<!! c) (<!! c) (offer! c [4 5 6])])") == [true, 1, nil, 2, 3, true])
				#expect(try eval("(let [c (chan 1 (filter even?))] [(>!! c 1) (>!! c 2) (poll! c) (poll! c)])") == [true, true, 2, nil])
				// The completion arity flushes at close (partition-all's tail).
				#expect(try eval("(let [c (chan 10 (partition-all 2))] (>!! c 1) (>!! c 2) (>!! c 3) (close! c) [(<!! c) (<!! c) (<!! c)])") == [[1, 2], [3], nil])
				// reduced closes the channel and completes parked puts.
				#expect(try eval("(let [c (chan 1 (take 2))] (>!! c 1) (let [g (go (>! c 2) (>! c 3))] [(<!! c) (<!! c) (<!! g) (<!! c) (chan-closed?* c)]))") == [1, 2, false, nil, true])
				// A parked putter refills through the step when a taker makes room.
				#expect(try eval("(let [c (chan 1 (map inc)) g (go (>! c 1) (>! c 2) (>! c 3) :done)] [(<!! c) (<!! c) (<!! c) (<!! g)])") == [2, 3, 4, kw("done")])
				// The step runs under the coroutine mutex: it may park; ex-handler gets the exception, its answer goes in.
				#expect(try eval("(let [in (chan 1) c (chan 1 (map (fn [x] (+ x (<!! in)))))] (go (>! in 10)) (>!! c 1) (<!! c))") == 11)
				#expect(try eval("(let [c (chan 10 (map (fn [x] (if (= x 2) (throw (ex-info \"bad\" {:x x})) x))) (fn [e] (:x (ex-data e))))] (>!! c 1) (>!! c 2) (>!! c 3) [(<!! c) (<!! c) (<!! c)])") == [1, 2, 3])
				uncaughtReports = 0
				clj_coro_set_uncaught_handler { _, _ in uncaughtReports += 1 }
				defer { clj_coro_set_uncaught_handler(nil) }
				#expect(try eval("(let [c (chan 1 (map (fn [x] (throw (ex-info \"bad\" {})))))] (>!! c 1) (poll! c))") == nil)
				#expect(uncaughtReports == 1)
				// A step touching its own channel is refused rather than deadlocked.
				#expect(try eval("(let [a (atom nil) c (chan 1 (map (fn [x] (poll! @a) x)) (fn [e] (ex-message e)))] (reset! a c) (let [r [(>!! c 1) (poll! c)]] (reset! a nil) r))") == [true, "poll! on a channel from inside its own transducer step"])
				#expect(message("(chan (buffer 1) 1)") == "chan expects a transducer fn, got: long")
				// alts! with an xform channel takes the step under the paired claim.
				#expect(try eval("(let [c (chan 1 (map inc)) d (chan)] (alt!! [[c 1]] :put d :take))") == kw("put"))
				#expect(try eval("(let [c (chan 1 (map inc))] (>!! c 1) (alt!! c ([v] v)))") == 2)
				#expect(try eval("(let [c (promise-chan (map inc))] (>!! c 1) (>!! c 5) [(<!! c) (<!! c)])") == [2, 2])
				// The expanding transducer of core.async's own test: every taker reports, in every buffer size.
				#expect(try eval("(let [c (chan 3 (xerox 2)) res (atom []) counter (atom 0)] (dotimes [_ 4] (take! c (fn [v] (when (some? v) (swap! res conj v)) (swap! counter inc)))) (onto-chan! c (range 3)) (while (< @counter 4) (Thread/sleep 5)) (sort @res))") == [0, 0, 1, 1])
			}
			base.check()
		}

		@Test func bufferPredicatesAndPromiseChan() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("[(unblocking-buffer? (buffer 1)) (unblocking-buffer? (dropping-buffer 1)) (unblocking-buffer? (sliding-buffer 1)) (unblocking-buffer? (promise-buffer*))]") == [false, true, true, true])
				#expect(try eval("(let [c (promise-chan) t1 (thread (<!! c)) t2 (thread (<!! c))] (>!! c :x) [(<!! t1) (<!! t2) (>!! c :y) (<!! c) (do (close! c) (<!! c))])") == [kw("x"), kw("x"), true, kw("x"), kw("x")])
				#expect(try eval("(let [c (promise-chan) t1 (thread (<!! c))] (close! c) [(<!! t1) (<!! c)])") == [nil, nil])
			}
			base.check()
		}

		@Test func pipeSplitReduceIntoTakeMerge() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [out (chan)] (pipe (to-chan! [1 2 3 4 5]) out) (<!! (a/into [] out)))") == [1, 2, 3, 4, 5])
				#expect(try eval("(let [[even odd] (split even? (to-chan! [1 2 3 4 5 6]) 5 5)] [(<!! (a/into [] even)) (<!! (a/into [] odd))])") == [[2, 4, 6], [1, 3, 5]])
				#expect(try eval("[(<!! (a/reduce + 0 (to-chan! []))) (<!! (a/reduce + 0 (to-chan! (range 10)))) (<!! (a/reduce #(if (= %2 2) (reduced :x) %1) 0 (to-chan! (range 10))))]") == [0, 45, kw("x")])
				#expect(try eval("(<!! (a/transduce (mapping inc) conj [] (to-chan! (range 5))))") == [1, 2, 3, 4, 5])
				#expect(try eval("(<!! (a/into #{} (a/take 3 (to-chan! (range 10)))))") == (try eval("#{0 1 2}")))
				#expect(try eval("(frequencies (<!! (a/into [] (merge [(to-chan! (range 3)) (to-chan! (range 3))]))))") == (try eval("{0 2 1 2 2 2}")))
				#expect(try eval("[(<!! (a/into [] (a/map + [(to-chan! (range 4)) (to-chan! (range 4))]))) (<!! (a/map + []))]") == [[0, 2, 4, 6], nil])
				#expect(try eval("(let [ch (chan 10)] (onto-chan ch (range 3)) (<!! (a/into [] ch)))") == [0, 1, 2])
				#expect(try eval("(<!! (a/into [] (to-chan (range 3))))") == [0, 1, 2])
				#expect(try eval("(<!! (a/into [] (a/to-chan!! (range 3))))") == [0, 1, 2])
				#expect(try eval("(<!! (a/into [] (a/unique (to-chan! [1 1 2 2 3 3 3 3 4]))))") == [1, 2, 3, 4])
				#expect(try eval("(<!! (a/into [] (a/partition 2 (to-chan! [1 2 2 3]))))") == [[1, 2], [2, 3]])
				#expect(try eval("(<!! (a/into [] (a/partition-by string? (to-chan! [\"a\" \"b\" 1 :two 3 \"c\"]))))") == [["a", "b"], [1, kw("two"), 3], ["c"]])
				#expect(try eval("(let [out (chan) in (a/map> inc out)] (onto-chan! in [1 2 3]) (<!! (a/into [] out)))") == [2, 3, 4])
				#expect(try eval("(<!! (a/into [] (a/mapcat< range (to-chan! [1 2 3]))))") == [0, 0, 1, 0, 1, 2])
			}
			base.check()
		}

		@Test func multPubMix() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [a (chan 4) b (chan 4) src (chan) m (mult src)] (tap m a) (tap m b) (pipe (to-chan! (range 4)) src) [(<!! (a/into [] a)) (<!! (a/into [] b))])") == [[0, 1, 2, 3], [0, 1, 2, 3]])
				// ASYNC-127: a closed tap is dropped, the others still pace the mult. The source is fed after the taps: a
				// value taken while there is no tap is dropped, and here the go block runs before the next line does.
				#expect(try eval("(until-done (fn [] (let [ch (chan) m (mult ch) t-1 (chan) t-2 (chan) t-3 (chan)] (tap m t-1) (tap m t-2) (tap m t-3) (close! t-3) (onto-chan! ch [1 2 3]) [(<!! t-1) (poll! t-1) (<!! t-2) (<!! t-1) (poll! t-1)])))") == (try eval("[1 nil 1 2 nil]")))
				#expect(try eval("(let [a-ints (chan 5) a-strs (chan 5) src (chan) p (pub src (fn [x] (if (string? x) :string :int)))] (sub p :string a-strs) (sub p :int a-ints) (pipe (to-chan! [1 \"a\" 2 \"b\" 3 \"c\"]) src) [(<!! (a/into [] a-ints)) (<!! (a/into [] a-strs))])") == [[1, 2, 3], ["a", "b", "c"]])
				#expect(try eval("(until-done (fn [] (let [out (chan) mx (mix out)] (admix mx (to-chan! [1 2 3])) (admix mx (to-chan! [4 5 6])) (<!! (a/into #{} (a/take 6 out))))))") == (try eval("#{1 2 3 4 5 6}")))
				// ASYNC-145: 2048 inputs on one mix.
				#expect(try eval("(until-done (fn [] (let [out (chan 2500) mx (mix out)] (dotimes [i 2048] (let [c (chan)] (admix mx c) (put! c i))) (= (set (range 2048)) (<!! (a/into #{} (a/take 2048 out)))))))") == true)
				// toggle before admix adds the input in that state, so what the loop reads from it is never in doubt:
				// a muted input is consumed and dropped, a paused one is not consumed, solo keeps only the soloed.
				#expect(try eval("(until-done (fn [] (let [out (chan 10) mx (mix out) a (chan 10) b (chan 10)] (admix mx a) (toggle mx {b {:mute true}}) (>!! a 1) (>!! b 2) (>!! a 3) (<!! (timeout 20)) (unmix-all mx) (close! out) (<!! (a/into [] out)))))") == [1, 3])
				#expect(try eval("(until-done (fn [] (let [out (chan 10) mx (mix out) a (chan 10) b (chan 10)] (admix mx a) (toggle mx {b {:pause true}}) (>!! a 1) (>!! b 2) (<!! (timeout 20)) [(poll! b) (do (unmix-all mx) (close! out) (<!! (a/into [] out)))])))") == [2, [1]])
				#expect(try eval("(until-done (fn [] (let [out (chan 10) mx (mix out) a (chan 10) b (chan 10)] (solo-mode mx :pause) (toggle mx {a {:solo true} b {}}) (>!! a 1) (>!! b 2) (<!! (timeout 20)) [(poll! b) (do (unmix-all mx) (close! out) (<!! (a/into [] out)))])))") == [2, [1]])
			}
			base.check()
		}

		@Test func pipelines() throws {
			let base = CoroBaseline()
			do {
				let r = try eval("(vec (for [[n size] [[1 0] [1 10] [10 10] [20 10] [5 1000]]] (let [r (range size)] (and (= r (pipeline-tester pipeline n r (mapping identity))) (= r (pipeline-tester pipeline-blocking n r (mapping identity))) (= r (pipeline-tester pipeline-async n r (fn [v ch] (thread (>!! ch v) (close! ch)))))))))")
				#expect(r == [true, true, true, true, true])
				#expect(try eval("(let [cout (chan 1)] (pipeline 5 cout (mapping identity) (to-chan! [1]) false) [(<!! cout) (>!! cout :x) (<!! cout)])") == [1, true, kw("x")])
				#expect(try eval("(let [cout (chan 1) chex (chan 1) xf (mapping (fn [x] (if (= x 3) (throw (ex-info \"err\" {:data x})) x)))] (pipeline 5 cout xf (to-chan! [1 2 3 4]) true (fn [e] (>!! chex e) :err)) [(<!! cout) (<!! cout) (<!! cout) (<!! cout) (ex-data (<!! chex))])") == [1, 2, kw("err"), 4, [kw("data"): 3]])
				#expect(try eval("(pipeline-tester pipeline-async 2 (range 1 5) (fn [v ch] (thread (dotimes [i v] (>!! ch i)) (close! ch))))") == [0, 0, 1, 0, 1, 2, 0, 1, 2, 3])
				#expect(try eval("(pipeline-tester pipeline-async 1 (range 100) (fn [v ch] (future (>!! ch (inc v)) (close! ch))))") == (try eval("(vec (range 1 101))")))
			}
			base.check()
		}

		@Test func goScoped() throws {
			let base = CoroBaseline()
			do {
				// The scope joins its children; a child's value is taken as usual.
				#expect(try eval("(let [r (atom [])] (go-scoped (go (swap! r conj :a)) (go (<! (timeout 10)) (swap! r conj :b))) (set @r))") == (try eval("#{:a :b}")))
				#expect(try eval("(go-scoped (let [c (go 41)] (inc (<! c))))") == 42)
				// A child's error cancels the siblings and the body, and is rethrown from the scope.
				#expect(try eval("(let [r (atom []) c (chan)] [(try (go-scoped (go (try (<! c) (catch :default e (swap! r conj :cancelled)))) (go (<! (timeout 5)) (throw (ex-info \"child\" {}))) (try (<! c) (catch :default e (swap! r conj :body))) :unreached) (catch :default e (ex-message e))) (sort @r)])") == ["child", [kw("body"), kw("cancelled")]])
				// The body's error cancels the children first; the join completes before the throw.
				#expect(try eval("(let [r (atom 0) c (chan)] [(try (go-scoped (dotimes [_ 5] (go (try (<! c) (catch :default e (swap! r inc))))) (throw (ex-info \"body\" {}))) (catch :default e (ex-message e))) @r])") == ["body", 5])
				// Cancelling the coroutine running the scope cancels the children transitively, nested scopes included.
				#expect(try eval("(let [r (atom 0) c (chan) g (go (try (go-scoped (go (go-scoped (go (try (<! c) (catch :default e (swap! r inc)))) (<! c))) (<! c)) (catch :default e (ex-message e))))] (<!! (timeout 20)) (cancel! g) [(<!! g) @r])") == ["Coroutine cancelled", 1])
				// After a scope's own cancellation the caller's coroutine is usable again.
				#expect(try eval("(<!! (go (try (go-scoped (go (throw (ex-info \"x\" {}))) (<! (chan))) (catch :default e nil)) (<! (go :ran))))") == kw("ran"))
				// A go outside any scope is unstructured; a go in a function called from the scope is a child.
				#expect(try eval("(let [r (atom nil)] (go-scoped (helper r)) @r)") == kw("done"))
				#expect(try eval("(let [r (atom nil) c (chan)] (go (<! c) (reset! r :done)) (go-scoped nil) (let [before @r] (>!! c 1) (<!! (timeout 10)) [before @r]))") == [nil, kw("done")])
				// Many children, and a child that spawns grandchildren into the same scope.
				#expect(try eval("(let [r (atom 0)] (go-scoped (dotimes [_ 200] (go (swap! r inc)))) @r)") == 200)
				#expect(try eval("(let [r (atom 0)] (go-scoped (go (dotimes [_ 10] (go (<! (timeout 1)) (swap! r inc))))) @r)") == 10)
				// plet is async let over a scope.
				#expect(try eval("(plet [a (do (<! (timeout 5)) 1) b 2] (+ a b))") == 3)
				#expect(try eval("(try (plet [a (throw (ex-info \"a\" {})) b (<! (chan))] (+ a b)) (catch :default e (ex-message e)))") == "a")
				_ = try eval("(<!! (timeout 20))")
			}
			base.check()
		}
	}
}
