;; @ai-generated(solo)
;; reduce, transduce and lazy-seq pipelines over ranges and vectors, integer and double.
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
