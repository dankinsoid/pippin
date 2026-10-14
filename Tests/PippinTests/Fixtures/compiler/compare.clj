;; compare, ==, long, int and Integer/compare: the fast arms, the unboxed forms under --closed, the generic path.
(ns fixture.compare)

(defn msg [f] (try (f) (catch :default e (ex-message e))))
(defn cmp [a b] (compare a b))
(defn icmp [a b] (Integer/compare a b))
(defn numeq [a b] (== a b))
(defn lng [x] (long x))
(defn i32 [x] (int x))
(def nan (/ 0.0 0.0))

(println (mapv (fn [[a b]] (cmp a b))
               [[1 2] [2 1] [3 3] [nil nil] [nil 1] [1 nil] [1 1.0] [1.5 1] [nan 1] [1 nan] [nan nan] [1N 2] [3 2N] [1/2 1]
                [9223372036854775807 1] [:a :b] [:b :a] [:a :a] [:a :a/b] [:b/a :a] [:a/x :a/y] [:b/a :a/y] ["a" "b"] ["é" "z"]
                ["z" "é"] ["ab" "a"] ["" ""] ["日本" "日"] ['a 'b] [\a \b] [[1 2] [1 3]] [false true]]))
(println (mapv (fn [[a b]] (msg #(cmp a b))) [[1 "a"] ["a" 1] [:a "a"] [:a 'a] [{:a 1} {:b 2}]]))
(println (mapv (fn [[a b]] (msg #(icmp a b))) [[1 2] [2 1] [5 5] [-3 4] [9223372036854775807 -9223372036854775808] [1 1.5] [nil 1] ["a" 1]]))
(println (mapv (fn [[a b]] (msg #(numeq a b)))
               [[1 1] [1 2] [1 1.0] [1.0 1] [nan nan] [nan 1] [1N 1] [1/2 0.5] [9223372036854775807 9223372036854775807] [1 "a"] [nil 1]])
         (== 1 1 1) (== 1 1 2) (msg #(== 1 1 "a")))
(println (mapv (fn [x] [(msg #(lng x)) (msg #(i32 x))]) [7 -7 1.9 -1.9 \a 3000000000 9223372036854775807 1N 5/2 nan "s" nil]))

;; under --closed these run on int64 and double C values
(defn chain [a b c d] (let [x (long (compare a b))] (if (== 0 x) (long (Integer/compare c d)) x)))
(defn loop-cmp [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (* i (compare i 2)) (Integer/compare i 3))) acc)))
(defn dloop [n] (loop [x 0.0 acc 0] (if (< x n) (recur (+ x 0.5) (+ acc (compare x 1.0))) acc)))
(defn mixed [n] (loop [i 0 acc []] (if (< i n) (recur (inc i) (conj acc (compare i 1.5) (== i 1.0))) acc)))
(defn nan-cmp [] (let [d (/ 0.0 0.0) e 1.0] [(compare d e) (compare e d) (== d d)]))
(defn ints [lo n] (loop [i lo out []] (if (< i (+ lo n)) (recur (inc i) (conj out (try (int i) (catch :default e (ex-message e))))) out)))
(defn longs-of [n] (loop [i 0 out []] (if (< i n) (recur (inc i) (conj out (long i) (long (* i 1.5)))) out)))
(println (chain 1 2 0 0) (chain 2 2 3 1) (chain :a :b "x" 1) (msg #(chain 1 1 "x" 1)) (msg #(chain 1 "a" 0 0))
         (loop-cmp 5) (dloop 3) (mixed 3) (nan-cmp) (ints 2147483646 3) (ints -2147483649 2) (longs-of 3))
