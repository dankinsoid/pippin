;; INTRINSIC and FUSED nodes: the table's C functions, their guards under a rebind, consuming forms, fusion drivers.
(ns fixture.intrinsics)

(defn arith [a b] [(+ a b) (- a b) (* a b) (/ a b) (inc a) (dec b) (< a b) (<= a b) (> a b) (>= a b) (= a b) (not= a b) (identical? a a)])
(defn preds [x] [(not x) (nil? x) (number? x) (string? x) (keyword? x) (symbol? x) (fn? x) (vector? x) (map? x) (set? x) (list? x) (seq? x) (seqable? x) (sequential? x) (coll? x) (counted? x) (ifn? x) (associative? x) (indexed? x) (char? x) (integer? x)])
(defn colls [c] [(first c) (rest c) (next c) (seq c) (count c) (empty? c) (cons 0 c) (conj c 9) (get c 0) (get c 0 :nf) (nth c 0) (nth c 5 :nf) (try (contains? c 0) (catch :default e (ex-message e)))])
(defn maps [m] [(assoc m :k 1) (dissoc m :a) (get m :a) (contains? m :a) (with-meta m {:x 1}) (meta (with-meta m {:x 1}))])
(defn grow [n] (loop [i 0 v []] (if (< i n) (recur (inc i) (conj v i)) v)))
(defn grow-map [n] (loop [i 0 m {}] (if (< i n) (recur (inc i) (assoc m i (* i i))) (count m))))
(defn bad [] [(try (+ 1 "a") (catch :default e (ex-message e))) (try (zero? "x") (catch :default e (ex-message e))) (try (nth [] 1) (catch :default e (ex-message e)))])

(println (arith 7 2) (arith 1.5 0.5) (preds nil) (preds [1]) (preds {:a 1}) (preds "s") (colls [1 2 3]) (colls '(1 2)) (maps {:a 1}) (grow 5) (grow-map 100) (bad))
(println (with-redefs [+ -] [(+ 5 3) (arith 5 3)]) (with-redefs [even? (fn [_] :redef)] [(even? 2) (preds 2)]) (+ 5 3) (even? 2))
(defn pipeline [n] [(reduce + (map inc (range n))) (reduce + 100 (filter odd? (range n))) (into [] (map (fn [x] (* x x)) (filter even? (range n)))) (vec (take 3 (map str (range n)))) (count (remove nil? [1 nil 2 nil]))])
(println (pipeline 10) (with-redefs [map (fn [f c] (list :not-map))] (pipeline 3)) (pipeline 3))
(println (reduce (fn [a x] (if (> x 2) (reduced a) (+ a x))) 0 (map inc (range 10))) (into #{} (map dec) [1 2 3]) (transduce (map inc) + (range 4)) (sequence (map inc) [1 2]))
