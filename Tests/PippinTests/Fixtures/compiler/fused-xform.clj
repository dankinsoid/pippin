;; Fused (into to XF coll) and (transduce XF f [init] coll), partition-all's buffer and completion, core fns and keywords as stage fns.
(ns fixture.fused-xform)

(defn values [v]
  [(into [] (comp (partition-all 3) (map (fn [p] (reduce + p))) (remove zero?)) v)
   (count (into [] (partition-all 2) v))
   (transduce (comp (map (fn [x] (* x x))) (filter even?)) + v)
   (transduce (map inc) + 100 v)
   (transduce (comp (partition-all 2) (map count)) conj v)
   (into [:a] (comp (take 3) (partition-all 2)) v)
   (into [] (comp (partition-all 2) (take 1)) v)
   (into [] (comp (partition-all 2) (partition-all 2)) v)
   (into [] (comp (mapcat (fn [x] [x x])) (partition-all 3)) v)
   (reduce + (map inc (filter odd? v)))
   (vec (map :k (filter :k (map (fn [x] {:k (when (odd? x) x)}) v))))
   (reduce + 0 (map - (rest v) v))
   (count (filter pos? (map - (rest v) v)))
   (vec (keep :a (map (fn [x] (if (odd? x) {:a x} {:b x})) v)))
   (vec (sequence (comp (partition-all 2) (map (fn [p] (apply + p)))) v))
   (transduce (comp (map inc) (partition-all 2)) (completing (fn [a p] (+ a (count p))) (fn [a] [:done a])) 0 v)
   (sort (into #{} (comp (partition-all 4) (map count)) v))])

(println (values [3 1 1 2 5 -4 2]))
(println (values '(3 1 1 2 5 -4 2)))
(println (values []))
(println (values (vec (range 70))))

;; The fused form against the same XF bound to a local, which nothing fuses: the calls each makes, in order.
(defn trace [f]
  (let [log (volatile! [])]
    [(f (fn [k x] (vswap! log conj [k x]) x)) @log]))

(defn into-pa [note v] (into [] (comp (map (fn [x] (note :m x))) (partition-all 2) (map (fn [p] (note :p p)))) v))
(defn into-pa-lazy [note v] (let [xf (comp (map (fn [x] (note :m x))) (partition-all 2) (map (fn [p] (note :p p))))] (into [] xf v)))
(defn td [note v] (transduce (comp (map (fn [x] (note :m x))) (partition-all 2)) (completing (fn [a p] (note :f p) (+ a (count p))) (fn [a] (note :c a) a)) 0 v))
(defn td-lazy [note v] (let [xf (comp (map (fn [x] (note :m x))) (partition-all 2))] (transduce xf (completing (fn [a p] (note :f p) (+ a (count p))) (fn [a] (note :c a) a)) 0 v)))
(defn td-halt [note v] (transduce (comp (partition-all 2) (take 2)) (fn ([a] (note :c a) a) ([a p] (note :f p) (conj a p))) [] v))
(defn td-halt-lazy [note v] (let [xf (comp (partition-all 2) (take 2))] (transduce xf (fn ([a] (note :c a) a) ([a p] (note :f p) (conj a p))) [] v)))
(defn td-init [note v] (transduce (map (fn [x] (note :m x))) (fn ([] (note :i 0) 0) ([a] (note :c a) a) ([a x] (+ a x))) v))
(defn td-init-lazy [note v] (let [xf (map (fn [x] (note :m x)))] (transduce xf (fn ([] (note :i 0) 0) ([a] (note :c a) a) ([a x] (+ a x))) v)))

(println (= (trace #(into-pa % [1 2 3 4 5])) (trace #(into-pa-lazy % [1 2 3 4 5]))) (first (trace #(into-pa % [1 2 3 4 5])))
         (= (trace #(td % [1 2 3])) (trace #(td-lazy % [1 2 3]))) (first (trace #(td % [1 2 3])))
         (= (trace #(td-halt % [1 2 3 4 5 6])) (trace #(td-halt-lazy % [1 2 3 4 5 6]))) (first (trace #(td-halt % [1 2 3 4 5 6])))
         (= (trace #(td-init % [1 2])) (trace #(td-init-lazy % [1 2]))) (first (trace #(td-init % [1 2]))))

(defn caught [f] (try (f) (catch :default e [(ex-message e) (ex-data e)])))
(defn boom [x] (if (= x 3) (throw (ex-info "boom" {:x x})) x))
(let [to [:to]]
  (println (caught #(into to (comp (map (fn [x] (boom x))) (partition-all 2)) [1 2 3 4]))
           (caught #(transduce (comp (partition-all 3) (map (fn [p] (boom (count p))))) + [1 2 3 4 5 6 7]))
           (caught #(transduce (map inc) (fn ([a] (boom 3)) ([a x] (+ a x))) 0 [1 2]))
           (caught #(reduce + (map inc [1 :k])))
           to))
