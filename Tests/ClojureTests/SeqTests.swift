// @ai-generated(guided)
import CljCore
import Dispatch
import Testing
@testable import Clojure

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func list(_ items: [Value]) -> Value { Value(list: items) }

extension CoreTests {
	@Suite struct SeqTests {
		// Vars are immortal: declare them before the baseline and unbind after.
		private func declare(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func seqFirstNextRestOnEverySeqable() throws {
			clj_init()
			for k in ["a", "b"] { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(seq nil) (seq ()) (seq []) (seq {}) (seq \"\")]") == [nil, nil, nil, nil, nil])
				#expect(try eval("(seq \"aλb\")") == list([Value("a" as Unicode.Scalar), Value("λ" as Unicode.Scalar), Value("b" as Unicode.Scalar)]))
				#expect(try eval("[(first \"aλ\") (next \"aλ\") (rest \"a\") (next \"a\")]") == [Value("a" as Unicode.Scalar), list([Value("λ" as Unicode.Scalar)]), list([]), nil])
				#expect(try eval("[(first {:a 1}) (next {:a 1}) (rest {:a 1})]") == [Value(reading: "[:a 1]"), nil, list([])])
				#expect(try eval("[(first (range 3)) (next (range 3)) (rest (range 1)) (next (range 1))]") == [0, list([1, 2]), list([]), nil])
				#expect(try eval("[(first [1 2]) (next [1 2]) (next [1]) (rest [1])]") == [1, list([2]), nil, list([])])
				#expect(try eval("(let [s (seq [1 2 3])] [(first s) (second s) (first (next (next s))) (next (next (next s)))])") == [1, 2, 3, nil])
				// cons onto any seqable keeps the tail as a seq; onto a seq keeps it as is.
				#expect(try eval("[(cons 1 nil) (cons 1 [2]) (cons 1 \"a\") (cons 1 {:a 1}) (cons 1 (range 2))]") == [list([1]), list([1, 2]), list([1, Value("a" as Unicode.Scalar)]), list([1, Value(reading: "[:a 1]")]), list([1, 0, 1])])
				#expect(try eval("(rest (cons 1 [2 3]))") == list([2, 3]))
				#expect(try eval("(seq? (rest (cons 1 [2 3])))") == true)
				#expect(try eval("[(conj nil 1) (conj (range 2) 5) (conj (seq [1]) 0) (conj (map inc [1]) 0)]") == [list([1]), list([5, 0, 1]), list([0, 1]), list([0, 2])])
				#expect(try eval("(empty? (map inc []))") == true)
				#expect(message("(first 1)") == "Don't know how to create ISeq from: fixnum")
				#expect(message("(seq :a)") == "Don't know how to create ISeq from: keyword")
				#expect(message("(next 1.5)") == "Don't know how to create ISeq from: double")
				#expect(message("(cons 1 2)") == "Don't know how to create ISeq from: fixnum")
				#expect(message("(doall (map inc 1))") == "Don't know how to create ISeq from: fixnum")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func libraryMatchesClojure() throws {
			clj_init()
			for k in ["a", "b", "c", "d"] { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(map inc [1 2 3])") == list([2, 3, 4]))
				#expect(try eval("(map + [1 2 3] [10 20])") == list([11, 22]))
				#expect(try eval("(map + [1 2] [10 20] [100 200 300])") == list([111, 222]))
				#expect(try eval("(map + [1 2] [10 20] [100 200] [1000 2000])") == list([1111, 2222]))
				#expect(try eval("(map vector [1 2] \"ab\")") == list([[1, Value("a" as Unicode.Scalar)], [2, Value("b" as Unicode.Scalar)]]))
				#expect(try eval("(map (fn [[k v]] v) {:a 1})") == list([1]))
				#expect(try eval("(filter even? (range 10))") == list([0, 2, 4, 6, 8]))
				#expect(try eval("(remove even? (range 10))") == list([1, 3, 5, 7, 9]))
				#expect(try eval("(keep (fn [x] (when (even? x) (* x x))) (range 6))") == list([0, 4, 16]))
				#expect(try eval("[(reduce + [1 2 3]) (reduce + 10 [1 2 3]) (reduce + []) (reduce + 5 nil) (reduce conj [] '(1 2))]") == [6, 16, 0, 5, [1, 2]])
				#expect(try eval("(reduce (fn [acc [k v]] (+ acc v)) 0 {:a 1 :b 2})") == 3)
				#expect(try eval("[(take 2 [1 2 3]) (take 0 [1]) (take 5 [1 2]) (take -1 [1])]") == [list([1, 2]), list([]), list([1, 2]), list([])])
				#expect(try eval("[(drop 1 [1 2 3]) (drop 5 [1 2]) (drop 0 [1]) (drop -1 [1])]") == [list([2, 3]), list([]), list([1]), list([1])])
				#expect(try eval("[(take-while pos? [1 2 -1 3]) (drop-while pos? [1 2 -1 3]) (take-while pos? nil) (drop-while pos? [1])]") == [list([1, 2]), list([-1, 3]), list([]), list([])])
				#expect(try eval("[(range 3) (range 1 4) (range 0 10 3) (range 5 0 -2) (range 0) (range 3 1) (range 0 -3)]") == [list([0, 1, 2]), list([1, 2, 3]), list([0, 3, 6, 9]), list([5, 3, 1]), list([]), list([]), list([])])
				#expect(try eval("(range 0 1 0.25)") == list([0, 0.25, 0.5, 0.75]))
				#expect(try eval("[(take 3 (range 1 10 0)) (range 10 1 0)]") == [list([1, 1, 1]), list([])])
				#expect(try eval("[(count (range 10)) (count (range 0 10 3)) (count (range 5 0 -2)) (count (range 0 7 7)) (nth (range 0 10 2) 3)]") == [10, 4, 3, 1, 6])
				#expect(try eval("(take 4 (iterate (fn [x] (* 2 x)) 1))") == list([1, 2, 4, 8]))
				#expect(try eval("[(take 2 (repeat :a)) (repeat 3 1) (repeat 0 1)]") == [list([Value(keyword: "a"), Value(keyword: "a")]), list([1, 1, 1]), list([])])
				#expect(try eval("[(interleave [1 2 3] [:a :b]) (interleave [1] [2] [3]) (interleave) (interleave [1 2])]") == [list([1, Value(keyword: "a"), 2, Value(keyword: "b")]), list([1, 2, 3]), list([]), list([1, 2])])
				#expect(try eval("[(interpose 0 [1 2 3]) (interpose 0 []) (interpose 0 [1])]") == [list([1, 0, 2, 0, 3]), list([]), list([1])])
				#expect(try eval("[(concat) (concat [1]) (concat [1] '(2)) (concat [1] nil [2 3] {:a 1} \"x\")]") == [list([]), list([1]), list([1, 2]), list([1, 2, 3, Value(reading: "[:a 1]"), Value("x" as Unicode.Scalar)])])
				#expect(try eval("(mapcat (fn [x] [x x]) [1 2])") == list([1, 1, 2, 2]))
				#expect(try eval("(mapcat list [1 2] [3 4])") == list([1, 3, 2, 4]))
				#expect(try eval("[(some even? [1 2 3]) (some even? [1 3]) (some #'identity [nil false 3]) (some identity [nil false 3]) (some even? nil)]") == [true, nil, 3, 3, nil])
				#expect(try eval("[(every? even? [2 4]) (every? even? [2 3]) (every? even? nil) (not-any? even? [1 3]) (not-every? even? [1 2])]") == [true, false, true, true, true])
				#expect(try eval("[(doall (map inc [1 2])) (dorun (map inc [1 2])) (doall 1 (map inc [1 2]))]") == [list([2, 3]), nil, list([2, 3])])
				#expect(try eval("[(vec '(1 2)) (vec nil) (vec \"ab\") (vec (range 3))]") == [[1, 2], [], [Value("a" as Unicode.Scalar), Value("b" as Unicode.Scalar)], [0, 1, 2]])
				#expect(try eval("[(into [] (range 3)) (into () [1 2]) (into {} [[:a 1] [:b 2]])]") == [[0, 1, 2], list([2, 1]), Value(reading: "{:a 1 :b 2}")])
				#expect(try eval("[(partition 2 [1 2 3 4 5]) (partition 2 []) (partition 3 1 (range 5))]") == [list([list([1, 2]), list([3, 4])]), list([]), list([list([0, 1, 2]), list([1, 2, 3]), list([2, 3, 4])])])
				#expect(try eval("[(zipmap [:a :b] [1 2 3]) (zipmap nil nil)]") == [Value(reading: "{:a 1 :b 2}"), Value(reading: "{}")])
				#expect(try eval("[(count (map inc [1 2 3])) (count (filter even? (range 10))) (nth (map inc [1 2 3]) 2) (nth (map inc [1 2 3]) 5 :d) (last (map inc [1 2 3]))]") == [3, 5, 4, Value(keyword: "d"), 4])
				#expect(try eval("[(second (map inc [1 2 3])) (butlast (map inc [1 2 3])) (reverse (map inc [1 2 3]))]") == [3, list([2, 3]), list([4, 3, 2])])
				#expect(try eval("(apply + (map inc (range 4)))") == 10)
				#expect(try eval("(apply str (interpose \", \" (map str (range 3))))") == "0, 1, 2")
				#expect(try eval("(let [[a b & more] (map inc (range 5))] [a b more])") == [1, 2, list([3, 4, 5])])
				#expect(message("(nth (map inc [1 2 3]) 5)") == "Index 5 out of bounds for length 3")
				#expect(message("(nth (range 3) 3)") == "Index 3 out of bounds for length 3")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func lazinessAndInfiniteSources() throws {
			clj_init()
			for k in ["x", "ok"] { _ = Value(keyword: k) }
			try declare("lz-count", "lz-s", "lz-boom", "lz-tries", "lz-rec")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(take 5 (map inc (range)))") == list([1, 2, 3, 4, 5]))
				#expect(try eval("(take 3 (filter even? (iterate inc 0)))") == list([0, 2, 4]))
				#expect(try eval("(take 3 (map + (range) (repeat 10)))") == list([10, 11, 12]))
				#expect(try eval("(first (drop 1000 (range)))") == 1000)
				#expect(try eval("(take 2 (drop-while (fn [x] (< x 5)) (range)))") == list([5, 6]))
				#expect(try eval("(take 4 (concat [1] (range)))") == list([1, 0, 1, 2]))
				#expect(try eval("(take 3 (mapcat (fn [x] [x x]) (range)))") == list([0, 0, 1]))
				#expect(try eval("(take 4 (interleave (range) (repeat :x)))") == list([0, Value(keyword: "x"), 1, Value(keyword: "x")]))
				#expect(try eval("(take 2 (partition 2 (range)))") == list([list([0, 1]), list([2, 3])]))
				#expect(try eval("(some (fn [x] (when (> x 10) x)) (range))") == 11)
				// The thunk runs once; first/count/print all read the cached seq.
				_ = try eval("(def lz-count 0) (def lz-s (lazy-seq (def lz-count (inc lz-count)) [1 2 3]))")
				#expect(try eval("(realized? lz-s)") == false)
				#expect(try eval("[(first lz-s) (count lz-s) (pr-str lz-s) (rest lz-s) lz-count (realized? lz-s)]") == [1, 3, "(1 2 3)", list([2, 3]), 1, true])
				// map calls f once per element however often the seq is walked.
				_ = try eval("(def lz-count 0) (def lz-s (map (fn [x] (def lz-count (inc lz-count)) x) (range 4)))")
				#expect(try eval("[(count lz-s) (reduce + lz-s) (doall lz-s) lz-count]") == [4, 6, list([0, 1, 2, 3]), 4])
				// rest does not realize the tail; next does.
				_ = try eval("(def lz-count 0) (def lz-s (map (fn [x] (def lz-count (inc lz-count)) x) (range 4)))")
				#expect(try eval("[(first lz-s) lz-count (do (rest lz-s) lz-count) (do (next lz-s) lz-count)]") == [0, 1, 1, 2])
				// A thunk that throws leaves the seq unrealized; the next force runs it again.
				_ = try eval("(def lz-tries 0) (def lz-boom (lazy-seq (def lz-tries (inc lz-tries)) (if (< lz-tries 2) (throw (ex-info \"boom\" {})) [:ok])))")
				#expect(message("(first lz-boom)") == "boom")
				#expect(try eval("[(realized? lz-boom) (first lz-boom) lz-tries]") == [false, Value(keyword: "ok"), 2])
				#expect(message("(pr-str (lazy-seq (throw (ex-info \"print\" {}))))") == "print")
				#expect(message("(str (lazy-seq (throw (ex-info \"str\" {}))))") == "str")
				#expect(message("(count (lazy-seq (throw (ex-info \"count\" {}))))") == "count")
				#expect(message("(= [1] (lazy-seq (throw (ex-info \"eq\" {}))))") == nil)
				// A thunk that forces its own object is an error, not a hang, shared or not.
				_ = try eval("(def lz-rec (lazy-seq (seq lz-rec)))")
				#expect(message("(first lz-rec)") == "Recursive realization of a lazy seq")
				try unbind("lz-count", "lz-s", "lz-boom", "lz-tries", "lz-rec")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func seqOnVectorIsAView() throws {
			clj_init()
			try declare("sv-v", "sv-s")
			_ = try eval("(def sv-v (vec (range 1000)))")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("(def sv-s (seq sv-v))")
				#expect(clj_debug_live_objects() == before + 1)
				_ = try eval("(def sv-s (next (next (next sv-s))))")
				#expect(clj_debug_live_objects() == before + 1)
				#expect(try eval("[(first sv-s) (count sv-s) (nth sv-s 2)]") == [3, 997, 5])
				// Walking to the end allocates one view per step and frees the previous one.
				#expect(try eval("(loop [s sv-s n 0] (if s (recur (next s) (+ n (first s))) n))") == 499497)
				#expect(clj_debug_live_objects() == before + 1)
				#expect(try eval("[(first (seq \"λx\")) (next (seq \"λx\")) (count (seq \"λxy\")) (count (next \"λxy\"))]") == [Value("λ" as Unicode.Scalar), list([Value("x" as Unicode.Scalar)]), 3, 2])
				try unbind("sv-s")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("sv-v")
		}

		@Test func longSeqsRealizeIteratively() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(count (range 100000))") == 100000)
				#expect(try eval("(count (map inc (range 100000)))") == 100000)
				#expect(try eval("(reduce + (map inc (range 100000)))") == 5000050000)
				#expect(try eval("(count (filter (fn [x] (= x 99999)) (range 100000)))") == 1)
				#expect(try eval("(first (drop 99999 (range 100000)))") == 99999)
				#expect(try eval("(last (map inc (range 100000)))") == 100000)
				#expect(try eval("(nth (map inc (range 100000)) 99999)") == 100000)
				#expect(try eval("(count (concat (range 50000) (range 50000)))") == 100000)
				#expect(try eval("(reduce + (take 100000 (iterate inc 0)))") == 4999950000)
				#expect(try eval("(count (doall (map inc (range 100000))))") == 100000)
				#expect(try eval("(= (map inc (range 100000)) (range 1 100001))") == true)
				let printed: String = (0..<100000).map { String($0) }.joined(separator: " ")
				let printedLength: Int = printed.count + 2
				#expect(try eval("(count (pr-str (range 100000)))") == Value(printedLength))
				#expect(try eval("(count (vec (map inc (range 100000))))") == 100000)
				#expect(try eval("(some (fn [x] (= x 99999)) (range 100000))") == true)
				#expect(try eval("(every? number? (range 100000))") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func equalityHashAndPrinting() throws {
			clj_init()
			for k in ["a", "r"] { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(= (range 3) [0 1 2]) (= [0 1 2] (range 3)) (= '(0 1 2) (range 3)) (= (range 3) (map identity [0 1 2])) (= (seq \"ab\") [\\a \\b])]") == [true, true, true, true, true])
				#expect(try eval("[(= (range 3) (range 4)) (= (range 3) {:a 1}) (= (range 3) nil) (= (range 3) \"012\") (= () (range 0))]") == [false, false, false, false, true])
				#expect(try eval("[(= (hash (range 3)) (hash [0 1 2])) (= (hash (map inc [0 1])) (hash '(1 2))) (= (hash (seq \"ab\")) (hash [\\a \\b]))]") == [true, true, true])
				#expect(try eval("(get {(range 2) :r} [0 1])") == Value(keyword: "r"))
				#expect(try eval("[(pr-str (map inc [1 2])) (pr-str (range 3)) (pr-str (seq [1])) (pr-str (seq \"ab\")) (pr-str (lazy-seq nil)) (str (range 2)) (pr-str (list 1 (range 2)))]") == ["(2 3)", "(0 1 2)", "(1)", "(\\a \\b)", "()", "(0 1)", "(1 (0 1))"])
				#expect(try eval("(pr-str (take 3 (range)))") == "(0 1 2)")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func macrosExpandThroughLazySeqs() throws {
			clj_init()
			try declare("ml-m")
			let before = clj_debug_live_objects()
			do {
				// A macro may return any seq; the analyzer seqs it. Nested lazy forms realize during analysis.
				_ = try eval("(defmacro ml-m [& xs] (cons 'do (map (fn [x] (list 'inc x)) xs)))")
				#expect(try eval("(ml-m 1 2)") == 3)
				_ = try eval("(defmacro ml-m [& xs] (map (fn [x] (list 'inc x)) xs))")
				#expect(message("(ml-m 1 2)") == "2 cannot be invoked")
				_ = try eval("(defmacro ml-m [& xs] (cons 'vector (map inc xs)))")
				#expect(try eval("(ml-m 1 2)") == [2, 3])
				_ = try eval("(defmacro ml-m [& xs] (concat ['vector] xs (map inc xs)))")
				#expect(try eval("(ml-m 1 2)") == [1, 2, 2, 3])
				#expect(try eval("(macroexpand-1 '(ml-m 1))") == Value(reading: "(vector 1 2)"))
				_ = try eval("(defmacro ml-m [] (lazy-seq nil))")
				#expect(try eval("(ml-m)") == list([]))
				try unbind("ml-m")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func sharedLazySeqForcedFromManyThreads() throws {
			clj_init()
			try declare("ts-s")
			let before = clj_debug_live_objects()
			do {
				let s = try eval("(def ts-s (map (fn [x] (* x x)) (range 20000))) ts-s")
				#expect(clj_is_shared(s.raw))
				let threads = 8
				var sums = [Int](repeating: 0, count: threads)
				var counts = [Int](repeating: 0, count: threads)
				sums.withUnsafeMutableBufferPointer { sp in
					counts.withUnsafeMutableBufferPointer { cp in
						DispatchQueue.concurrentPerform(iterations: threads) { i in
							var it = clj_seq_iter_start(s.raw)
							var item: clj_value = CLJ_NIL
							var sum = 0, n = 0
							while clj_seq_iter_next(&it, &item) {
								sum += clj_fixnum_val(item)
								n += 1
							}
							sp[i] = sum
							cp[i] = n
						}
					}
				}
				#expect(counts.allSatisfy { $0 == 20000 })
				#expect(sums.allSatisfy { $0 == 2_666_466_670_000 })
				#expect(try eval("(count ts-s)") == 20000)
				try unbind("ts-s")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
