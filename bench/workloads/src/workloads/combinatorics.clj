;; @ai-generated(solo)
;; clojure.math.combinatorics: lazy permutations, combinations, subsets, partitions, products, selections.
;; Input: k = 8 and integer ranges. Work: weigh (sum of x_i * (i+1)) every permutation of 0..7 and of the multiset
;; [1 1 2 2 3 3 4], every 4-combination of 0..23, every 4-tuple of 0..8; count the subsets of 0..13 by size, the
;; partitions of 0..8 and those of 0..7 into 2-3 parts, the 3-letter selections of length 8; count-permutations of
;; 0..11; nth-permutation of 0..9 at every 9973rd index, nth-combination 5 of 0..29 at every 211th.
;; Output: a vector of 11 sums and counts, `expected` below.
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
   (reduce + (map #(weigh (c/nth-combination (range 30) 5 %)) (range 0 142506 211)))])

(defn run [] (run* 8))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '[5080320 40320 1487640 114688 21147 1093 262440 6561 479001600 90039 182523])
