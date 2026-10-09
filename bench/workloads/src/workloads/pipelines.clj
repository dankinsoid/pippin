;; @ai-generated(solo)
;; reduce, transduce and lazy-seq pipelines over ranges and vectors, integer and double.
;; Input: n = 300000, v = (vec (range n)). Work: 18 folds — transduce with map/filter, reduce over lazy map/filter,
;; partition-all, a map built by reduce, maps of maps, iterate, mapcat + dedupe, reductions, repeat, keep, distinct,
;; two double sums, a two-sequence map over v and (rseq v), a first-difference, a hand loop over seq/next,
;; reduce-kv, group-by. Output: a vector of 18 numbers, `expected` below.
(ns workloads.pipelines)

(defn run* [n]
  (let [v (vec (range n))]
    [(transduce (comp (map #(* % %)) (filter even?) (map #(mod % 1000))) + (range n))
     (reduce + (map inc (filter odd? v)))
     (count (into [] (comp (partition-all 16) (map #(reduce + %)) (remove zero?)) v))
     (reduce + (vals (reduce (fn [m x] (let [k (mod x 100)] (assoc m k (+ (get m k 0) x)))) {} v)))
     (->> v (map (fn [x] {:k (mod x 10) :v x})) (filter #(odd? (:v %))) (map :v) (reduce +))
     (reduce + (take 5000 (iterate #(mod (* % 7) 1000003) 1)))
     (count (sequence (comp (mapcat (fn [x] [x x])) (dedupe)) v))
     (reduce max (reductions + (take (quot n 4) v)))
     (reduce + (mapcat (fn [x] (repeat (mod x 3) x)) (take (quot n 4) v)))
     (reduce + (keep #(when (zero? (mod % 3)) %) v))
     (count (distinct (map #(mod % 1000) v)))
     (reduce + (map #(* 0.5 %) v))
     (transduce (map #(/ (double %) 3.0)) + 0.0 v)
     (reduce + (map (fn [a b] (* a b)) v (rseq v)))
     (count (filter pos? (map - (rest v) v)))
     (loop [s (seq v) acc 0] (if s (recur (next s) (+ acc (first s))) acc))
     (reduce-kv (fn [acc i x] (+ acc (* i (mod x 7)))) 0 v)
     (count (group-by #(mod % 7) (take (quot n 4) v)))]))

(defn run [] (run* 300000))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '[73500000 22500150000 18750 44999850000 22500000000 2499150080 300000 2812462500 2812512500 14999850000 1000 2.2499925E10 1.499995E10 4499955000100000 299999 44999850000 134999849999 7])
