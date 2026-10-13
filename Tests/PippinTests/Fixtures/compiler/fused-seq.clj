;; Fused sequence, mapcat, dedupe, distinct and multi-coll map: values, call order against the lazy form, realization, throws.
(ns fixture.fused-seq)

(defn values [v]
  [(count (sequence (comp (mapcat (fn [x] [x x])) (dedupe)) v))
   (count (sequence (dedupe) v))
   (vec (sequence (comp (map (fn [x] (* x 2))) (filter (fn [x] (pos? x)))) v))
   (reduce (fn [a x] (+ a x)) 0 (sequence (comp (mapcat (fn [x] [x (- x 1)])) (take 5)) v))
   (reduce (fn ([] :none) ([a x] (conj (if (vector? a) a [a]) x))) (sequence (map (fn [x] (inc x))) v))
   (into [] (sequence (comp (distinct) (map (fn [x] (* x x)))) v))
   (vec (sort (into #{} (sequence (mapcat (fn [x] (repeat (mod x 3) x))) v))))
   (count (distinct (map (fn [x] (mod x 3)) v)))
   (vec (dedupe (map (fn [x] (quot x 2)) v)))
   (reduce + (mapcat (fn [x] (repeat (mod x 3) x)) v))
   (vec (take 4 (mapcat (fn [x] [x x x]) v)))
   (reduce + 0 (map (fn [a b] (* a b)) v (reverse v)))
   (count (filter (fn [x] (pos? x)) (map - (rest v) v)))
   (mapv (fn [i x] [i x]) (range) v)
   (into [] (map (fn [a b c] (+ a b c)) v (rest v) (drop 2 v)))
   (vec (map (fn [a b] [a b]) v [:a :b]))
   (mapv (fn [x] (inc x)) (filter (fn [x] (odd? x)) v))
   (count (map (fn [a b] a) v (range 3)))
   (vec (sequence (comp (take 2) (mapcat (fn [x] [x x]))) v))])

(println (values [3 1 1 2 5 -4 2]))
(println (values '(3 1 1 2 5 -4 2)))
(println (values []))
(println (values (vec (range 40))))
(println (reduce + (map (fn [a b] (* a b)) [1 2 3] (rseq [1 2 3]))) (vec (map (fn [a b] (- a b)) (rseq [5 6 7]) [1 1])))

;; The same pipeline fused and as a let-bound lazy seq, which nothing fuses: the calls each makes, in order.
(defn trace [f]
  (let [log (volatile! [])]
    [(f (fn [k x] (vswap! log conj [k x]) x)) @log]))

(defn seq-reduce [note v] (reduce (fn [a x] (note :f x) (+ a x)) 0 (sequence (comp (mapcat (fn [x] (note :m x) [x x])) (map (fn [y] (note :g y)))) v)))
(defn seq-reduce-lazy [note v] (let [s (sequence (comp (mapcat (fn [x] (note :m x) [x x])) (map (fn [y] (note :g y)))) v)] (reduce (fn [a x] (note :f x) (+ a x)) 0 s)))
(defn seq-halt [note v] (reduce (fn [a x] (note :f x) (if (> a 4) (reduced a) (+ a x))) (sequence (comp (map (fn [x] (note :m x))) (dedupe)) v)))
(defn seq-halt-lazy [note v] (let [s (sequence (comp (map (fn [x] (note :m x))) (dedupe)) v)] (reduce (fn [a x] (note :f x) (if (> a 4) (reduced a) (+ a x))) s)))
(defn seq-take [note v] (into [] (sequence (comp (mapcat (fn [x] (note :m x) [x x])) (take 3)) v)))
(defn seq-take-lazy [note v] (let [s (sequence (comp (mapcat (fn [x] (note :m x) [x x])) (take 3)) v)] (into [] s)))
(defn multi [note a b] (reduce (fn [acc x] (note :f x) (+ acc x)) 0 (map (fn [x y] (note :m [x y]) (* x y)) a b)))
(defn multi-lazy [note a b] (let [s (map (fn [x y] (note :m [x y]) (* x y)) a b)] (reduce (fn [acc x] (note :f x) (+ acc x)) 0 s)))
(defn cat-lazy-inner [note v] (count (mapcat (fn [x] (note :m x) (map (fn [y] (note :i y)) (repeat (mod x 3) x))) v)))
(defn cat-lazy-inner-lazy [note v] (let [s (mapcat (fn [x] (note :m x) (map (fn [y] (note :i y)) (repeat (mod x 3) x))) v)] (count s)))

(println (= (trace #(seq-reduce % [1 2 3])) (trace #(seq-reduce-lazy % [1 2 3]))) (first (trace #(seq-reduce % [1 2 3])))
         (= (trace #(seq-halt % [1 1 2 3 3 4 5])) (trace #(seq-halt-lazy % [1 1 2 3 3 4 5]))) (first (trace #(seq-halt % [1 1 2 3 3 4 5])))
         (= (trace #(seq-take % [1 2 3])) (trace #(seq-take-lazy % [1 2 3]))) (first (trace #(seq-take % [1 2 3])))
         (= (trace #(multi % [1 2 3] '(4 5))) (trace #(multi-lazy % [1 2 3] '(4 5)))) (first (trace #(multi % [1 2 3] '(4 5))))
         (= (trace #(cat-lazy-inner % [1 2 3 4 5])) (trace #(cat-lazy-inner-lazy % [1 2 3 4 5]))) (first (trace #(cat-lazy-inner % [1 2 3 4 5]))))

;; A lazy source counts the items it realizes: the walk stops where the lazy form stops pulling.
(defn counted [n cnt] ((fn src [i] (lazy-seq (when (< i n) (vswap! cnt inc) (cons i (src (inc i)))))) 0))
(defn realized [f] (let [c1 (volatile! 0) c2 (volatile! 0)] [(f c1 c2) @c1 @c2]))
(println (realized (fn [c1 c2] (reduce + 0 (map (fn [a b] (+ a b)) (counted 3 c1) (counted 10 c2)))))
         (realized (fn [c1 c2] (let [s (map (fn [a b] (+ a b)) (counted 3 c1) (counted 10 c2))] (reduce + 0 s))))
         (realized (fn [c1 c2] (count (map (fn [a b] (+ a b)) (counted 10 c1) (counted 2 c2)))))
         (realized (fn [c1 c2] (count (sequence (comp (filter (fn [x] (odd? x))) (take 2)) (counted 10 c1)))))
         (realized (fn [c1 c2] (let [s (sequence (comp (filter (fn [x] (odd? x))) (take 2)) (counted 10 c1))] (count s))))
         (realized (fn [c1 c2] (reduce (fn [a x] (if (= x 2) (reduced a) (+ a x))) 0 (sequence (mapcat (fn [x] [x x])) (counted 10 c1))))))

;; A throw mid-walk propagates; the consumer's `to` is as it was.
(defn caught [f] (try (f) (catch :default e [(ex-message e) (ex-data e)])))
(defn boom [x] (if (= x 3) (throw (ex-info "boom" {:x x})) x))
(let [to [:to]]
  (println (caught #(count (sequence (comp (mapcat (fn [x] [(boom x) x])) (dedupe)) [1 2 3 4])))
           (caught #(into to (sequence (map (fn [x] (boom x))) [1 2 3 4])))
           (caught #(reduce + (map (fn [a b] (boom (+ a b))) [1 2 3] [0 0 0])))
           (caught #(mapv (fn [a b] (boom b)) [1 2 3] [1 2 3]))
           (caught #(count (distinct (map (fn [x] (boom x)) [1 2 3]))))
           (caught #(reduce + (map (fn [a b] (+ a b)) [1 2] (lazy-seq (throw (ex-info "source" {}))))))
           to))
