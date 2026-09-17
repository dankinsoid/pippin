// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private func kw(_ s: String) -> Value { Value(keyword: s) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

extension CoreTests {
	// clojure.lang.PersistentQueue (queue.c): FIFO over a front seq and a rear vector, reached through the JVM's spelling.
	@Suite struct QueueTests {
		init() {
			clj_init()
			_ = try? cljEval("(def q-empty clojure.lang.PersistentQueue/EMPTY) (defn q [& xs] (into q-empty xs)) (def q-shared)")
			for k in ["a", "b"] { _ = kw(k) }
		}

		@Test func fifoSemantics() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("[(count q-empty) (empty? q-empty) (peek q-empty) (seq q-empty) (identical? (pop q-empty) q-empty)]") == [0, true, nil, nil, true])
				#expect(try cljEval("(let [q (q 1 2 3)] [(count q) (peek q) (first q) (seq q) (vec q)])") == [3, 1, 1, Value(list: [1, 2, 3]), [1, 2, 3]])
				#expect(try cljEval("(let [q (pop (q 1 2 3))] [(count q) (peek q) (seq q)])") == [2, 2, Value(list: [2, 3])])
				#expect(try cljEval("(let [q (pop (pop (q 1 2 3)))] [(count q) (peek q) (seq (pop q)) (count (pop q))])") == [1, 3, nil, 0])
				// Popping past the rear and conj'ing again keeps the order: the rear becomes the front, then a new rear grows.
				#expect(try cljEval("(seq (-> (q 1 2 3) pop pop (conj 4) (conj 5)))") == Value(list: [3, 4, 5]))
				#expect(try cljEval("(seq (-> (q 1 2 3) pop pop (conj 4) (conj 5) pop))") == Value(list: [4, 5]))
				#expect(try cljEval("(vec (reduce conj q-empty (range 10)))") == Value((0..<10).map { Value($0) }))
				#expect(try cljEval("(loop [q (q 1 2 3 4 5) out []] (if (seq q) (recur (pop q) (conj out (peek q))) out))") == [1, 2, 3, 4, 5])
				#expect(try cljEval("(reduce + (q 1 2 3 4))") == 10)
				#expect(try cljEval("(reduce + 10 (pop (q 1 2 3 4)))") == 19)
				#expect(try cljEval("(reduce (fn [acc x] (if (= x 3) (reduced acc) (+ acc x))) 0 (q 1 2 3 4))") == 3)
				#expect(try cljEval("(reduce + (pop (pop (q 1 2 3))))") == 3)
				#expect(try cljEval("(reduce + q-empty)") == 0)
				#expect(try cljEval("(into [] (map inc) (q 1 2 3))") == [2, 3, 4])
				#expect(try cljEval("(rest (q 1 2))") == Value(list: [2]))
				#expect(try cljEval("(last (q 1 2 3))") == 3)
				#expect(try cljEval("(nth (q 1 2 3) 1)") == 2)
				#expect(try cljEval("(map inc (q 1 2 3))") == Value(list: [2, 3, 4]))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func identityEqualityAndPrinting() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("[(instance? clojure.lang.PersistentQueue q-empty) (instance? PersistentQueue (q 1)) (instance? clojure.lang.PersistentQueue []) (type (q 1))]").description == "[true true false PersistentQueue]")
				#expect(try cljEval("[(coll? (q 1)) (sequential? (q 1)) (seq? (q 1)) (list? (q 1)) (counted? (q 1)) (vector? (q 1)) (map? (q 1)) (ifn? (q 1))]") == [true, true, false, true, true, false, false, false])
				#expect(try cljEval("[(= (q 1 2) (q 1 2)) (= (q 1 2) [1 2]) (= [1 2] (q 1 2)) (= (q 1 2) '(1 2)) (= (q 1 2) (q 2 1)) (= (q 1) #{1}) (= q-empty [])]") == [true, true, true, true, false, false, true])
				#expect(try cljEval("[(= (hash (q 1 2)) (hash [1 2])) (= (hash (pop (q 0 1 2))) (hash '(1 2))) (= (hash q-empty) (hash []))]") == [true, true, true])
				#expect(try cljEval("(get {(q 1 2) :a} [1 2])") == kw("a"))
				#expect(try cljEval("(pr-str (q 1 \"a\" :b))") == "#queue [1 \"a\" :b]")
				#expect(try cljEval("(pr-str q-empty)") == "#queue []")
				#expect(try cljEval("(str (q 1 2))") == "#queue [1 2]")
				#expect(try cljEval("(pr-str (pop (q 1 2 3)))") == "#queue [2 3]")
				#expect(try cljEval("[(empty (q 1 2)) (identical? (empty (q 1 2)) q-empty) (empty? (empty (q 1)))]").description == "[#queue [] true true]")
				#expect(try cljEval("(meta (with-meta (q 1) {:a 1}))") == [kw("a"): 1])
				#expect(try cljEval("(seq (with-meta (q 1 2) {:a 1}))") == Value(list: [1, 2]))
				#expect(try cljEval("(meta (conj (with-meta (q 1) {:a 1}) 2))") == [kw("a"): 1])
				#expect(try cljEval("(meta (pop (with-meta (q 1 2) {:a 1})))") == [kw("a"): 1])
				#expect(try cljEval("(meta (empty (with-meta (q 1 2) {:a 1})))") == [kw("a"): 1])
				#expect(try cljEval("(= (q 1 2) (with-meta (q 1 2) {:a 1}))") == true)
				#expect(message("(queue-pop* [])") == "queue-pop* expects a queue, got: vector")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A unique queue is updated in place; a shared one is copied and the original keeps its items.
		@Test func persistence() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("(let [a (q 1 2 3) b (conj a 4) c (pop a)] [(vec a) (vec b) (vec c)])") == [[1, 2, 3], [1, 2, 3, 4], [2, 3]])
				#expect(try cljEval("(let [a (pop (q 1 2 3)) b (conj a 4) c (pop a)] [(vec a) (vec b) (vec c)])") == [[2, 3], [2, 3, 4], [3]])
				#expect(try cljEval("(let [a (q 1 2 3)] (dotimes [_ 3] (conj a 9)) (vec a))") == [1, 2, 3])
				#expect(try cljEval("(def q-shared (q 1 2 3)) (vec (conj q-shared 4))") == [1, 2, 3, 4])
				#expect(try cljEval("(vec q-shared)") == [1, 2, 3])
				#expect(try cljEval("(vec (pop q-shared))") == [2, 3])
				_ = try cljEval("(def q-shared nil)")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
