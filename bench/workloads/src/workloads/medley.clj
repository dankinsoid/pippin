;; @ai-generated(solo)
;; medley.core over a table of records (map-entry, window and partition-between need JVM classes here).
;; Input: n = 20000 records {:id i, :group x mod 37, :score x mod 1000, :tags #{:a :b} if x even else #{:c},
;; :parent x mod i (nil for i = 0)}, x the i-th state of util/lcg from seed 42. Work: index-by, map-vals,
;; filter-vals/-keys, remove-vals, group-by totals, collate-by, deep-merge of 2000 records, distinct-by, dedupe-by,
;; greatest-by, find-first, take-upto, update-existing-in, dissoc-in, assoc-some, interleave-all, indexed.
;; Output: a vector of 18 counts and sums, `expected` below.
(ns workloads.medley
  (:require [medley.core :as m]
            [workloads.util :refer [lcg-vec]]))

(defn- records [n]
  (mapv (fn [i x]
          {:id i
           :group (mod x 37)
           :score (mod x 1000)
           :tags (if (even? x) #{:a :b} #{:c})
           :parent (when (pos? i) (mod x i))})
        (range n)
        (lcg-vec 42 n)))

(defn run* [n]
  (let [rs (records n)
        by-id (m/index-by :id rs)
        scores (m/map-vals :score by-id)
        high (m/filter-vals #(> % 500) scores)
        keyed (m/map-keys str high)
        odd-ids (m/filter-keys odd? scores)
        dropped (m/remove-vals zero? scores)
        totals (m/map-vals #(reduce + (map :score %)) (group-by :group rs))
        collated (m/collate-by :group (fn [acc r] (+ acc (:score r))) :score rs)
        merged (reduce (fn [acc r]
                         (m/deep-merge acc {(:group r) {:n (:id r) :tags {(:score r) true}}}))
                       {}
                       (take 2000 rs))
        distinct-scores (m/distinct-by :score rs)
        deduped (m/dedupe-by #(quot (:score %) 100) rs)
        best (m/greatest-by :score (take 1000 rs))
        found (m/find-first #(= 999 (:score %)) rs)
        upto (m/take-upto #(> (:score %) 990) rs)
        nested (reduce (fn [acc r] (m/update-existing-in acc [(:group r) :n] + (:score r)))
                       (into {} (map (fn [g] [g {:n 0}])) (range 37))
                       rs)
        pruned (reduce (fn [acc r] (m/dissoc-in acc [(:group r) :n]))
                       nested
                       (take 20 rs))
        some-assoc (reduce (fn [acc r] (m/assoc-some acc (:id r) (:parent r))) {} (take 5000 rs))
        interleaved (count (m/interleave-all (range 1000) (range 500) (range 1500)))
        indexed (reduce + (map (fn [[i r]] (* i (:score r))) (m/indexed (take 3000 rs))))]
    [(count high) (count keyed) (count odd-ids) (count dropped) (reduce + (vals totals))
     (reduce + (vals collated)) (count merged) (reduce + (map (comp count :tags) (vals merged)))
     (count distinct-scores) (count deduped) (:score best) (:id found) (count upto)
     (reduce + (map :n (vals nested))) (count pruned) (count some-assoc) interleaved indexed]))

(defn run [] (run* 20000))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '[10115 10115 10000 19981 10035672 10035672 37 1944 1000 18013 nil 460 89 10035672 20 4999 3000 2246395904])
