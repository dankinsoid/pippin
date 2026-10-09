;; @ai-generated(solo)
;; Aggregation over records and words: group-by, frequencies, sort-by, merge-with, update-vals.
(ns workloads.group-freq
  (:require [workloads.util :refer [lcg-vec]]))

(def ^:private syllables ["ka" "lo" "mi" "ne" "ru" "sa" "ti" "vo" "ze" "pa" "qu" "do"])

(defn- word [x]
  (let [n (inc (mod x 3))]
    (apply str (map #(nth syllables (mod (quot x (inc (* % 12))) 12)) (range n)))))

(defn- people [xs]
  (mapv (fn [i x]
          {:id i
           :dept (keyword (str "d" (mod x 23)))
           :city (nth ["Yerevan" "Gyumri" "Vanadzor" "Dilijan" "Goris"] (mod x 5))
           :age (+ 20 (mod x 45))
           :salary (* 100 (mod x 997))})
        (range)
        xs))

(defn run* [n]
  (let [xs (lcg-vec 3 n)
        words (mapv word xs)
        freqs (frequencies words)
        top (take 10 (sort-by (juxt (comp - val) key) freqs))
        by-len (update-vals (group-by count words) count)
        ps (people xs)
        by-dept (group-by :dept ps)
        payroll (update-vals by-dept #(reduce + (map :salary %)))
        ;; reduce, not apply: apply of a compiled variadic past 21 arguments (docs/notes/compiler.md).
        oldest (update-vals by-dept #(:id (reduce (fn [a b] (max-key (fn [p] (+ (* 1000000 (:age p)) (:id p))) a b)) %)))
        by-city-age (frequencies (map (juxt :city #(quot (:age %) 10)) ps))
        merged (apply merge-with + (map (fn [p] {(:city p) (:salary p)}) ps))
        ranked (take 100 (sort-by (juxt (comp - :salary) :id) ps))
        buckets (reduce (fn [m p] (update m (:dept p) (fnil conj #{}) (:city p))) {} ps)]
    [(count freqs) (vec top) (into (sorted-map) by-len)
     (reduce + (vals payroll)) (reduce + (vals oldest)) (count by-city-age)
     (into (sorted-map) merged) (mapv :id ranked) (reduce + (map count (vals buckets)))]))

(defn run [] (run* 50000))
