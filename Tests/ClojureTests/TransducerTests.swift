// @ai-generated(guided)
import CljCore
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
private func kw(_ s: String) -> Value { Value(keyword: s) }

extension CoreTests {
	// reduce through the IReduceInit slots and the generic seq path, reduced, volatiles, transducers.
	@Suite struct TransducerTests {
		private func declare(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func reducedBox() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(reduced? (reduced 1)) (reduced? 1) (reduced? nil) @(reduced 1) (deref (reduced [1]))]") == [true, false, false, 1, [1]])
				#expect(try eval("[(unreduced (reduced 1)) (unreduced 2) (reduced? (ensure-reduced 1)) (reduced? (ensure-reduced (reduced 1))) @(ensure-reduced (reduced 3))]") == [1, 2, true, true, 3])
				#expect(try eval("(let [r (reduced 1)] (identical? r (ensure-reduced r)))") == true)
				#expect(try eval("[(= (reduced 1) (reduced 1)) (let [r (reduced 1)] (= r r)) (pr-str (reduced 1)) (instance? Reduced (reduced 1))]") == [false, true, "#object[reduced]", true])
				#expect(message("(deref 1)") == "deref not supported on this type: fixnum")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func volatiles() throws {
			clj_init()
			try declare("tx-shared")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [v (volatile! 1)] [(volatile? v) @v (vreset! v 2) @v (vswap! v + 10) (deref v)])") == [true, 1, 2, 2, 12, 12])
				#expect(try eval("[(volatile? 1) (pr-str (volatile! nil)) (instance? Volatile (volatile! 1))]") == [false, "#object[volatile]", true])
				#expect(try eval("(macroexpand-1 '(vswap! v f a b))").description == "(clojure.core/vreset! v (f (clojure.core/deref v) a b))")
				#expect(message("(vreset! 1 2)") == "vreset! expects a volatile, got: fixnum")
				// A value stored into a published volatile joins the shared graph.
				_ = try eval("(def tx-shared (volatile! nil)) (vreset! tx-shared [1 [2]])")
				let stored = try eval("@tx-shared")
				#expect(withExtendedLifetime(stored) { clj_debug_all_shared(stored.raw) })
				try unbind("tx-shared")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func reduceMatchesClojure() throws {
			clj_init()
			for k in ["a", "b", "empty"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(reduce + [1 2 3]) (reduce + 10 [1 2 3]) (reduce + []) (reduce + 5 nil) (reduce + nil) (reduce conj [] '(1 2))]") == [6, 16, 0, 5, 0, [1, 2]])
				#expect(try eval("[(reduce + (range 5)) (reduce + 1 (range 5)) (reduce + (range 0 10 3)) (reduce + (range 5 0 -2)) (reduce + 7 (range 0))]") == [10, 11, 18, 9, 7])
				#expect(try eval("[(reduce + (seq [1 2 3])) (reduce + 0 (next [1 2 3])) (reduce + '(1 2 3)) (reduce + 1 ()) (reduce + ())]") == [6, 5, 6, 1, 0])
				#expect(try eval("[(reduce str \"\" \"aλb\") (reduce str \"aλ\") (reduce str \"\" (seq \"aλ\")) (reduce str \"\" (next \"aλ\")) (reduce str \"\" \"\")]") == ["aλb", "aλ", "aλ", "λ", ""])
				#expect(try eval("[(reduce + (map inc (range 4))) (reduce + 0 (filter even? (range 10))) (reduce + (lazy-seq nil)) (reduce + 3 (lazy-seq [1]))]") == [10, 20, 0, 4])
				#expect(try eval("(reduce (fn [acc [k v]] (+ acc v)) 0 {:a 1 :b 2})") == 3)
				#expect(try eval("(reduce (fn [[k1 v1] [k2 v2]] [k1 (+ v1 v2)]) {:a 1})") == [kw("a"), 1])
				#expect(try eval("[(reduce (fn [] :empty) []) (reduce (fn [] :empty) nil) (reduce (fn [] :empty) ()) (reduce (fn [] :empty) (range 0))]") == [kw("empty"), kw("empty"), kw("empty"), kw("empty")])
				#expect(try eval("[(reduce + [5]) (reduce (fn [a b] (throw (ex-info \"never\" {}))) [5])]") == [5, 5])
				// Only a step's result is checked for reduced: a reduced init or first element is an ordinary value to f
				// and comes back as is over an empty coll.
				#expect(try eval("[(reduced? (reduce + (reduced 7) nil)) (reduced? (reduce + (reduced 7) [])) (reduced? (reduce + [(reduced 5)])) @(reduce + [(reduced 5)])]") == [true, true, true, 5])
				#expect(try eval("(reduce (fn [a x] (if (reduced? a) (+ @a x) (+ a x))) (reduced 7) [1 2])") == 10)
				#expect(try eval("(reduce (fn [a x] [a x]) [(reduced 5) 1])").description == "[#object[reduced] 1]")
				#expect(message("(reduce + (reduced 7) [1 2])") == "reduced cannot be cast to a number")
				#expect(message("(reduce conj (reduced [0]) (range))") == "conj not supported on this type: reduced")
				#expect(try eval("(apply reduce + [[1 2]])") == 3)
				#expect(message("(reduce + 5)") == "Don't know how to create ISeq from: fixnum")
				#expect(message("(reduce + 0 :a)") == "Don't know how to create ISeq from: keyword")
				#expect(message("(reduce)") == "Wrong number of args (0) passed to: clojure.core/reduce")
				#expect(message("(reduce (fn [a x] (throw (ex-info \"in f\" {}))) 0 [1])") == "in f")
				#expect(message("(reduce (fn [a x] (+ a x)) 0 [1 :a])") == "keyword cannot be cast to a number")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func reduceKv() throws {
			clj_init()
			for k in ["a", "b"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(reduce-kv (fn [m k v] (assoc m v k)) {} {:a 1 :b 2})") == Value(reading: "{1 :a 2 :b}"))
				#expect(try eval("(reduce-kv (fn [acc i x] (conj acc [i x])) [] [:a :b])") == [[0, kw("a")], [1, kw("b")]])
				#expect(try eval("[(reduce-kv + 0 nil) (reduce-kv + 0 {}) (reduce-kv + 0 [])]") == [0, 0, 0])
				#expect(try eval("(reduce-kv (fn [acc i x] (if (= i 2) (reduced acc) (+ acc x))) 0 [10 20 30 40])") == 30)
				#expect(try eval("(reduce-kv (fn [acc k v] (reduced k)) nil {:a 1})") == kw("a"))
				#expect(try eval("(reduce-kv (fn [acc i x] (+ acc (* i x))) 0 (vec (range 40)))") == 20540)
				#expect(message("(reduce-kv + 0 '(1))") == "reduce-kv not supported on this type: cons")
				#expect(message("(reduce-kv (fn [a k v] (throw (ex-info \"kv\" {}))) 0 {:a 1})") == "kv")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every slot type and the generic path stop at a reduced result without touching what follows.
		@Test func earlyStopThroughEveryPath() throws {
			clj_init()
			_ = kw("k")
			try declare("tx-ones", "tx-from", "TxTri", "->TxTri", "TxBox", "->TxBox")
			_ = try eval("""
			(defn tx-ones [n]
			  (reify ISeq
			    (seq [this] this)
			    (first [_] 1)
			    (next [_] (when (> n 1) (tx-ones (dec n))))))
			(defn tx-from [n steps]
			  (reify ISeq
			    (seq [this] this)
			    (first [_] n)
			    (next [_] (vswap! steps inc) (when (< n 100) (tx-from (inc n) steps)))))
			(deftype TxTri [n]
			  IReduceInit
			  (reduce [_ f init]
			    (loop [i 1 acc init]
			      (if (> i n)
			        acc
			        (let [r (f acc i)]
			          (if (reduced? r) r (recur (inc i) r)))))))
			(deftype TxBox [items] Seqable (seq [_] (seq items)))
			[(tx-ones 1) (tx-from 0 (volatile! 0))]
			""")
			let before = clj_debug_live_objects()
			do {
				let stop = "(fn [acc x] (if (>= acc 3) (reduced acc) (+ acc x)))"
				#expect(try eval("[(reduce \(stop) 0 [1 1 1 1 1]) (reduce \(stop) 0 (seq [1 1 1 1 1])) (reduce \(stop) 0 (range 1 100)) (reduce \(stop) 0 '(1 1 1 1 1)) (reduce \(stop) 0 (map identity (repeat 1)))]") == [3, 3, 3, 3, 3])
				#expect(try eval("[(reduce \(stop) 0 (tx-ones 100)) (reduce \(stop) 0 (->TxTri 100)) (reduce \(stop) 0 (->TxBox [1 1 1 1 1])) (reduce \(stop) 0 (iterate inc 1))]") == [3, 3, 3, 3])
				#expect(try eval("(reduce (fn [acc c] (if (= c \\b) (reduced acc) (str acc c))) \"\" \"abc\")") == "a")
				#expect(try eval("(reduce (fn [acc c] (if (= c \\b) (reduced acc) (str acc c))) \"\" (seq \"abc\"))") == "a")
				#expect(try eval("(reduce (fn [acc e] (reduced e)) nil {:k 1})") == [kw("k"), 1])
				#expect(try eval("[(reduce + (->TxTri 4)) (reduce + 5 (->TxTri 4)) (reduce + (->TxTri 0)) (satisfies? IReduceInit (->TxTri 1)) (satisfies? IReduceInit [1])]") == [10, 15, 0, true, true])
				#expect(try eval("(reduce (fn [a x] (if (= x 2) (reduced a) (+ a x))) 0 (->TxTri 4))") == 1)
				// A stop realizes nothing past it: four thunk runs for a stop at the third element.
				#expect(try eval("(let [n (volatile! 0)] [(reduce (fn [a x] (if (= x 2) (reduced a) (+ a x))) 0 (map (fn [x] (vswap! n inc) x) (range 100))) @n])") == [1, 3])
				#expect(try eval("(let [n (volatile! 0)] [(reduce (fn [a x] (if (= x 2) (reduced a) (+ a x))) 0 (tx-from 0 n)) @n])") == [1, 2])
				#expect(try eval("(let [n (volatile! 0)] (reduce (fn [a x] (vswap! n inc) (if (= x 5) (reduced a) a)) nil (range)) @n)") == 6)
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("tx-ones", "tx-from", "TxTri", "->TxTri", "TxBox", "->TxBox")
		}

		@Test func lazySeqThatThrowsPropagates() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(message("(reduce + (lazy-seq (throw (ex-info \"boom\" {}))))") == "boom")
				#expect(message("(reduce + 0 (concat [1 2] (lazy-seq (throw (ex-info \"mid\" {})))))") == "mid")
				#expect(message("(reduce + 0 (map (fn [x] (if (= x 3) (throw (ex-info \"at 3\" {})) x)) (range)))") == "at 3")
				#expect(message("(transduce (map inc) + (lazy-seq (throw (ex-info \"tx\" {}))))") == "tx")
				#expect(message("(into [] (map inc) (lazy-seq (throw (ex-info \"into\" {}))))") == "into")
				#expect(message("(doall (sequence (map inc) (lazy-seq (throw (ex-info \"seq\" {})))))") == "seq")
				#expect(message("(reduce + (eduction (map inc) (lazy-seq (throw (ex-info \"ed\" {})))))") == "ed")
				#expect(message("(into [] (map (fn [x] (throw (ex-info \"in xf\" {})))) [1])") == "in xf")
				#expect(message("(into [] (map inc) 5)") == "Don't know how to create ISeq from: fixnum")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func compositionHelpers() throws {
			clj_init()
			_ = kw("k")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[((comp) 5) ((comp inc) 1) ((comp inc inc) 1) ((comp str inc) 1) ((comp inc inc inc inc) 0) ((comp first rest) [1 2 3])]") == [5, 2, 3, "2", 4, 2])
				#expect(try eval("[((comp inc +) 1 2) ((comp inc +) 1 2 3) ((comp inc +) 1 2 3 4) ((comp inc +))]") == [4, 7, 11, 1])
				#expect(try eval("[((partial +) 1) ((partial + 1) 2) ((partial + 1 2) 3) ((partial + 1 2 3) 4) ((partial + 1 2 3 4) 5) ((partial vector 1 2 3 4))]") == [1, 3, 6, 10, 15, [1, 2, 3, 4]])
				#expect(try eval("[((constantly :k)) ((constantly :k) 1 2) (map (constantly 0) [1 2])]") == [kw("k"), kw("k"), list([0, 0])])
				#expect(try eval("(let [rf (completing conj (fn [v] (count v)))] [(rf) (rf [1 2]) (rf [1] 2)])") == [[], 2, [1, 2]])
				#expect(try eval("(let [rf (completing +)] [(rf) (rf 5) (rf 1 2)])") == [0, 5, 3])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func transduceInitAndCompletion() throws {
			clj_init()
			for k in ["init", "done", "a"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(transduce (map inc) + (range 5)) (transduce (map inc) + 100 (range 5)) (transduce (map inc) + []) (transduce (map inc) + nil)]") == [15, 115, 0, 0])
				#expect(try eval("(transduce (map inc) (completing conj (fn [v] (count v))) [] [1 2 3])") == 3)
				#expect(try eval("(let [rf ((map inc) +) rf3 ((map +) +)] [(rf) (rf 5) (rf 1 2) (rf3 1 2 3)])") == [0, 5, 4, 6])
				#expect(try eval("(let [rf ((filter odd?) conj)] [(rf) (rf [1]) (rf [] 1) (rf [] 2)])") == [[], [1], [1], []])
				// (f) seeds only when no init is given; the completion arity runs once, after the reduce.
				#expect(try eval("(let [calls (volatile! []) rf (fn ([] (vswap! calls conj :init) 0) ([acc] (vswap! calls conj :done) acc) ([acc x] (+ acc x)))] [(transduce (map inc) rf [1 2]) (transduce (map inc) rf 10 [1 2]) @calls])") == [5, 15, list([kw("init"), kw("done"), kw("done")])])
				#expect(try eval("(let [calls (volatile! []) rf (fn ([] (vswap! calls conj :init) 0) ([acc] (vswap! calls conj :done) acc) ([acc x] (+ acc x)))] [(transduce (map inc) rf [1 2]) @calls])") == [5, list([kw("init"), kw("done")])])
				#expect(try eval("(transduce (comp (map inc) (filter even?) (take 2)) conj (range))") == [2, 4])
				#expect(try eval("(transduce (map (fn [[k v]] v)) + {:a 1})") == 1)
				#expect(try eval("(transduce cat conj [[1 2] [3]])") == [1, 2, 3])
				#expect(try eval("(transduce (comp cat (take 2)) conj [[1 2] (range)])") == [1, 2])
				#expect(message("(transduce (map inc) (fn [a b] (+ a b)) [1])") == "Wrong number of args (0) passed to: fn")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func statefulTransducersFlushOnCompletion() throws {
			clj_init()
			for k in ["a", "b", "c", "d"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(into [] (take 3) (range)) (into [] (take 0) (range)) (into [] (take 5) [1 2]) (into [] (take -1) [1])]") == [[0, 1, 2], [], [1, 2], []])
				#expect(try eval("[(into [] (drop 2) [1 2 3 4]) (into [] (drop 0) [1]) (into [] (drop 5) [1])]") == [[3, 4], [1], []])
				#expect(try eval("[(into [] (take-while pos?) [1 2 -1 3]) (into [] (drop-while pos?) [1 2 -1 3]) (into [] (take-while neg?) (range 1 100000000))]") == [[1, 2], [-1, 3], []])
				#expect(try eval("(count (into [] (take-while (fn [x] (< x 3))) (range)))") == 3)
				#expect(try eval("[(into [] (partition-all 2) (range 5)) (into [] (partition-all 2) []) (into [] (partition-all 2) [1 2]) (into [] (partition-all 3) (range 3))]") == [[[0, 1], [2, 3], [4]], [], [[1, 2]], [[0, 1, 2]]])
				// A reduced result inside partition-all's completion is unwrapped, so the flush still completes.
				#expect(try eval("[(into [] (comp (take 3) (partition-all 2)) (range)) (into [] (comp (partition-all 2) (take 1)) (range))]") == [[[0, 1], [2]], [[0, 1]]])
				#expect(try eval("(transduce (partition-all 2) (completing conj (fn [v] (count v))) [] (range 5))") == 3)
				#expect(try eval("[(into [] (dedupe) [1 1 2 2 2 3 1]) (into [] (dedupe) []) (into [] (dedupe) [nil nil 1]) (dedupe [1 1 2 1 1])]") == [[1, 2, 3, 1], [], [nil, 1], list([1, 2, 1])])
				#expect(try eval("[(into [] (interpose 0) [1 2 3]) (into [] (interpose 0) []) (into [] (interpose 0) [1]) (into [] (comp (interpose 0) (take 2)) [1 2 3])]") == [[1, 0, 2, 0, 3], [], [1], [1, 0]])
				#expect(try eval("[(into [] (map-indexed vector) [:a :b]) (into [] (keep-indexed (fn [i x] (when (odd? i) x))) [:a :b :c :d]) (map-indexed vector [:a :b]) (keep-indexed (fn [i x] (when (odd? i) x)) [:a :b :c :d])]") == [[[0, kw("a")], [1, kw("b")]], [kw("b"), kw("d")], list([[0, kw("a")], [1, kw("b")]]), list([kw("b"), kw("d")])])
				#expect(try eval("[(into [] (mapcat (fn [x] [x x])) [1 2]) (into [] cat [[1 2] [3]]) (into [] (comp cat (map inc)) [[1] [2]]) (into [] (comp (map inc) (filter even?)) (range 10))]") == [[1, 1, 2, 2], [1, 2, 3], [2, 3], [2, 4, 6, 8, 10]])
				#expect(try eval("[(into [] (keep (fn [x] (when (odd? x) (* x x)))) (range 5)) (into [] (remove odd?) (range 5)) (into [] (map +) [1 2])]") == [[1, 9], [0, 2, 4], [1, 2]])
				#expect(try eval("[(partition-all 2 [1 2 3]) (partition-all 2 1 [1 2 3]) (partition-all 2 []) (take 2 (partition-all 2 (range)))]") == [list([list([1, 2]), list([3])]), list([list([1, 2]), list([2, 3]), list([3])]), list([]), list([list([0, 1]), list([2, 3])])])
				// Each transducer application has its own state.
				#expect(try eval("(let [xf (take 2)] [(into [] xf [1 2 3]) (into [] xf [4 5 6])])") == [[1, 2], [4, 5]])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func intoWithTransducer() throws {
			clj_init()
			for k in ["a", "b"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(into [] (map inc) (range 3)) (into [1] (take 2) [2 3 4]) (into () (map inc) [1 2]) (into {} (map (fn [[k v]] [v k])) {:a 1}) (into [] (map inc) nil)]") == [[1, 2, 3], [1, 2, 3], list([3, 2]), Value(reading: "{1 :a}"), []])
				#expect(try eval("[(into [] (range 3)) (into () [1 2]) (into {} [[:a 1] [:b 2]]) (into [] nil) (into nil [1 2]) (into [0] (map inc (range 3)))]") == [[0, 1, 2], list([2, 1]), Value(reading: "{:a 1 :b 2}"), [], list([2, 1]), [0, 1, 2, 3]])
				#expect(try eval("(into [] (take 2) (eduction (map inc) (range)))") == [1, 2])
				#expect(message("(into [] (map inc) (lazy-seq (throw (ex-info \"x\" {}))))") == "x")
				#expect(message("(into [] (lazy-seq (throw (ex-info \"y\" {}))))") == "y")
				#expect(message("(into 1 [2])") == "conj not supported on this type: fixnum")
				#expect(message("(into)") == "Wrong number of args (0) passed to: clojure.core/into")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func sequenceAndEduction() throws {
			clj_init()
			for k in ["a"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(sequence [1 2]) (sequence nil) (sequence ()) (sequence \"ab\") (seq? (sequence [1]))]") == [list([1, 2]), list([]), list([]), list([Value("a" as Unicode.Scalar), Value("b" as Unicode.Scalar)]), true])
				#expect(try eval("[(sequence (map inc) [1 2]) (sequence (map inc) []) (sequence (map inc) nil) (seq? (sequence (map inc) [1]))]") == [list([2, 3]), list([]), list([]), true])
				#expect(try eval("[(take 3 (sequence (map inc) (range))) (sequence (take 2) (range)) (sequence (partition-all 2) [1 2 3]) (sequence (comp (filter odd?) (take 2)) (range))]") == [list([1, 2, 3]), list([0, 1]), list([list([1, 2]), list([3])]), list([1, 3])])
				#expect(try eval("[(sequence (dedupe) [1 1 2]) (sequence cat [[1] [2 3]]) (sequence (mapcat (fn [x] [x x])) [1 2]) (sequence (drop 1) [1 2 3])]") == [list([1, 2]), list([1, 2, 3]), list([1, 1, 2, 2]), list([2, 3])])
				// The source is pulled one item per realization.
				#expect(try eval("(let [n (volatile! 0) s (sequence (map (fn [x] (vswap! n inc) x)) (range 10))] [(first s) @n (second s) @n])") == [0, 1, 1, 2])
				#expect(try eval("(let [n (volatile! 0) s (sequence (filter (fn [x] (vswap! n inc) (> x 2))) (range 10))] [(first s) @n])") == [3, 4])
				#expect(try eval("(let [n (volatile! 0)] [(doall (sequence (take 2) (map (fn [x] (vswap! n inc) x) (range 10)))) @n])") == [list([0, 1]), 2])
				#expect(try eval("[(vec (eduction (map inc) [1 2])) (reduce + (eduction (map inc) (filter odd?) (range 5))) (reduce + 0 (eduction (take 3) (range))) (count (eduction (map inc) [1 2])) (seq (eduction (map inc) []))]") == [[2, 3], 9, 3, 2, nil])
				#expect(try eval("[(into [] (take 2) (eduction (map inc) (range))) (transduce (map inc) + (eduction (map inc) [1 2])) (vec (eduction [1 2])) (satisfies? IReduceInit (eduction [1])) (instance? Eduction (eduction [1]))]") == [[1, 2], 7, [1, 2], true, true])
				#expect(try eval("(let [e (eduction (map inc) [1 2])] [(vec e) (vec e) (reduce + e) (reduce + e)])") == [[2, 3], [2, 3], 5, 5])
				#expect(try eval("(reduce (fn [a x] (if (= x 3) (reduced a) (+ a x))) 0 (eduction (map inc) (range)))") == 3)
				#expect(try eval("(= (eduction (map inc) [1 2]) (eduction (map inc) [1 2]))") == false)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// (into [] (xf ...) coll) agrees with the lazy arity over every seqable kind, including empty ones.
		@Test func transducerArityMatchesLazyArity() throws {
			clj_init()
			for k in ["map", "filter", "remove", "keep", "take", "drop", "take-while", "drop-while", "mapcat", "interpose", "partition-all", "dedupe", "map-indexed", "keep-indexed", "cat"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				let failures = try eval("""
				(let [colls [[1 2 2 3 4 5] '(1 2 2 3 4 5) (range 6) (map identity [1 2 2 3 4 5]) (seq [1 2 2 3 4 5]) (iterate inc 1) [] () nil (range 0) (map inc nil)]
				      finite (fn [c] (take 6 c))
				      cases [[:map (map inc) (fn [c] (map inc c))]
				             [:filter (filter odd?) (fn [c] (filter odd? c))]
				             [:remove (remove odd?) (fn [c] (remove odd? c))]
				             [:keep (keep (fn [x] (when (odd? x) (* x x)))) (fn [c] (keep (fn [x] (when (odd? x) (* x x))) c))]
				             [:take (take 3) (fn [c] (take 3 c))]
				             [:drop (drop 2) (fn [c] (drop 2 c))]
				             [:take-while (take-while (fn [x] (< x 3))) (fn [c] (take-while (fn [x] (< x 3)) c))]
				             [:drop-while (drop-while (fn [x] (< x 3))) (fn [c] (drop-while (fn [x] (< x 3)) c))]
				             [:mapcat (mapcat (fn [x] [x x])) (fn [c] (mapcat (fn [x] [x x]) c))]
				             [:interpose (interpose 0) (fn [c] (interpose 0 c))]
				             [:partition-all (partition-all 4) (fn [c] (partition-all 4 c))]
				             [:dedupe (dedupe) (fn [c] (dedupe c))]
				             [:map-indexed (map-indexed vector) (fn [c] (map-indexed vector c))]
				             [:keep-indexed (keep-indexed (fn [i x] (when (odd? i) x))) (fn [c] (keep-indexed (fn [i x] (when (odd? i) x)) c))]
				             [:cat cat (fn [c] (mapcat identity c))]]
				      ;; take bounds the infinite iterate; cat gets each coll wrapped as one input.
				      run (fn [[name xf lazy]]
				            (keep (fn [c]
				                    (let [c (if (= name :cat) [(finite c)] (finite c))
				                          a (into [] xf c)
				                          b (vec (lazy c))]
				                      (when-not (= a b) [name (vec c) a b])))
				                  colls))]
				  (vec (mapcat run cases)))
				""")
				#expect(failures == [], "\(failures)")
				let strings = try eval("""
				(let [colls ["abbcde" (seq "abbcde") (next "abbcde") "" (seq "")]
				      cases [[:map (map str) (fn [c] (map str c))]
				             [:filter (filter (fn [ch] (not= ch \\b))) (fn [c] (filter (fn [ch] (not= ch \\b)) c))]
				             [:take (take 2) (fn [c] (take 2 c))]
				             [:drop (drop 1) (fn [c] (drop 1 c))]
				             [:partition-all (partition-all 2) (fn [c] (partition-all 2 c))]
				             [:dedupe (dedupe) (fn [c] (dedupe c))]
				             [:interpose (interpose \\-) (fn [c] (interpose \\- c))]
				             [:map-indexed (map-indexed vector) (fn [c] (map-indexed vector c))]]
				      run (fn [[name xf lazy]]
				            (keep (fn [c] (let [a (into [] xf c) b (vec (lazy c))] (when-not (= a b) [name c a b]))) colls))]
				  (vec (mapcat run cases)))
				""")
				#expect(strings == [], "\(strings)")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
