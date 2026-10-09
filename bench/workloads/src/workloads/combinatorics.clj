;; @ai-generated(solo)
;; clojure.math.combinatorics: lazy generation of permutations, combinations, subsets and partitions.
(ns workloads.combinatorics
  (:require [clojure.math.combinatorics :as c]))

(defn- weigh [xs] (reduce + (map * xs (range 1 100))))

(defn run* [k]
  [(reduce + (map weigh (c/permutations (range k))))
   (reduce + (map weigh (c/permutations [1 1 2 2 3 3 4])))
   (reduce + (map weigh (c/combinations (range (* 3 k)) 4)))
   (reduce + (map count (c/subsets (range (+ k 6)))))
   (count (c/partitions (range (inc k))))
   (count (c/partitions (range k) :min 2 :max 3))
   (reduce + (map weigh (c/cartesian-product (range 9) (range 9) (range 9) (range 9))))
   (count (c/selections [:a :b :c] k))
   (c/count-permutations (range (+ k 4)))
   (reduce + (map #(weigh (c/nth-permutation (range 10) %)) (range 0 3628800 9973)))
   ;; 20 items, not more: all-different? applies distinct? to them (docs/notes/compiler.md, apply over 21).
   (reduce + (map #(weigh (c/nth-combination (range 20) 5 %)) (range 0 15504 23)))])

(defn run [] (run* 8))
