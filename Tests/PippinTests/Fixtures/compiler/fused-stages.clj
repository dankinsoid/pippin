;; Fused stages that keep state or end the walk, compiled as one step (design §10 step 7a): keep, take, drop,
;; take-while, drop-while and map-indexed over vectors (a tuple, a trie past one leaf), seqs and endless ranges.
(ns fixture.fused-stages)

(defn over [xs]
  [(reduce + 0 (keep (fn [x] (when (odd? x) (* x x))) xs))
   (into [] (take 3 (map (fn [x] (inc x)) xs)))
   (vec (drop 2 (filter (fn [x] (even? x)) xs)))
   (count (take-while (fn [x] (< x 4)) xs))
   (into [:start] (drop-while (fn [x] (< x 3)) xs))
   (vec (map-indexed (fn [i x] [i x]) xs))
   (reduce + (take 2 (drop 1 xs)))
   (into [] (take 0 (map (fn [x] (inc x)) xs)))
   (vec (take 10 xs))
   (vec (drop 10 xs))
   (count (keep (fn [x] (when (> x 2) false)) xs))
   (reduce (fn [a x] (if (> x 4) (reduced a) (+ a x))) 0 (take 5 (map (fn [x] (* 2 x)) xs)))
   (vec (take-while (fn [x] true) (drop-while (fn [x] false) xs)))
   (reduce + (map-indexed (fn [i x] (* i x)) (take 4 xs)))
   (reduce + (take 3 xs))
   (count (remove (fn [x] (odd? x)) (take 5 xs)))])
(defn endless [] [(vec (take 3 (map (fn [x] (* x x)) (range)))) (count (take-while (fn [x] (< x 5)) (range))) (vec (take 2 (drop 3 (range))))])
(defn stage-throws [xs] (try (vec (map-indexed (fn [i x] (if (= i 2) (throw (ex-info "at 2" {:x x})) x)) xs)) (catch :default e [(ex-message e) (ex-data e)])))
(defn counts [] [(vec (take 2.5 (map (fn [x] x) [1 2 3 4]))) (vec (drop 1.5 (filter (fn [x] true) [1 2 3 4])))
                 (try (vec (take :k (map (fn [x] x) [1 2]))) (catch :default _ :not-a-number))])

(println (over [1 2 3 4 5 6]) (over (range 1 7)) (over '(1 2 3 4 5 6)) (over []))
(println (over (vec (range 1 40))))
(println (endless) (stage-throws [1 2 3 4]) (stage-throws '(1 2 3)) (counts))
