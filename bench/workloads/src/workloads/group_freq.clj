;; @ai-generated(solo)
;; Aggregation over records and words: group-by, frequencies, sort-by, merge-with, update-vals.
;; Input: n = 50000 util/lcg states from seed 3; each makes a word of 1-3 syllables of a 12-syllable alphabet and a
;; person {:id :dept (23 departments) :city (5) :age 20..64 :salary}. Work: word frequencies and the top 10 by count,
;; then by word; words by length; payroll and the oldest person per department; (city, age decade) frequencies;
;; salaries merged per city; the 100 best paid; the set of cities per department.
;; Output: a vector of 9 values, `expected` below.
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
        oldest (update-vals by-dept #(:id (apply max-key (fn [p] (+ (* 1000000 (:age p)) (:id p))) %)))
        by-city-age (frequencies (map (juxt :city #(quot (:age %) 10)) ps))
        merged (apply merge-with + (map (fn [p] {(:city p) (:salary p)}) ps))
        ranked (take 100 (sort-by (juxt (comp - :salary) :id) ps))
        buckets (reduce (fn [m p] (update m (:dept p) (fnil conj #{}) (:city p))) {} ps)]
    [(count freqs) (vec top) (into (sorted-map) by-len)
     (reduce + (vals payroll)) (reduce + (vals oldest)) (count by-city-age)
     (into (sorted-map) merged) (mapv :id ranked) (reduce + (map count (vals buckets)))]))

(defn run [] (run* 50000))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '[628 [["ne" 4291] ["pa" 4265] ["ka" 4135] ["ti" 4105] ["ququ" 697] ["lolo" 692] ["ruru" 664] ["vovo" 645] ["qupa" 355] ["qumi" 349]] {2 16796, 4 16712, 6 16492} 2500375200 1124999 25 {"Dilijan" 491823800, "Goris" 504888500, "Gyumri" 506007900, "Vanadzor" 498841000, "Yerevan" 498814000} [207 1349 2681 4080 5580 6073 7411 7432 10511 13177 16920 17003 18520 18956 21264 22331 22409 23546 23800 27353 29760 30187 32250 32622 33227 33288 34801 36220 37078 39317 39659 40101 40253 40587 40678 40857 41198 42764 43359 43483 46245 46294 48276 49960 1612 2280 2398 2577 7498 8062 12613 13000 13357 13519 13568 18005 18342 19354 19838 20584 22658 24436 26687 27037 29237 29727 29847 31991 32574 33903 33998 34710 37742 38321 39353 42047 42403 42408 42730 43043 43366 44833 49752 2897 4404 6268 9125 9234 9310 10492 10680 11271 11483 17723 19189 19269 21484 22104 22391 27085] 115])
