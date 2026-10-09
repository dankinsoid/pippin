;; @ai-generated(solo)
;; An application state as nested maps and vectors, driven by a stream of events.
(ns workloads.nested-update
  (:require [workloads.util :refer [lcg]]))

(defn- step [st x]
  (let [uid (mod x 500)
        kind (mod (quot x 7) 6)]
    (case kind
      0 (assoc-in st [:users uid :name] (str "user-" uid))
      1 (update-in st [:users uid :visits] (fnil inc 0))
      2 (update-in st [:users uid :tags] (fnil conj []) (mod x 10))
      3 (-> st
            (update-in [:counters (if (even? x) :ok :err)] inc)
            (update :log (fn [log] (if (< (count log) 100) (conj log uid) (conj (subvec log 1) uid)))))
      4 (update-in st [:users uid] (fn [u] (-> u (assoc :seen x) (update :score (fnil + 0) (mod x 13)))))
      5 (assoc-in st [:index (mod x 50) (mod x 7)] uid))))

(defn run* [n]
  (loop [i 0 x 1 reads 0 st {:users {} :log [] :counters {:ok 0 :err 0} :index {}}]
    (if (< i n)
      (let [x (lcg x)]
        (recur (inc i) x
               (+ reads (get-in st [:users (mod x 500) :visits] 0) (count (get-in st [:index (mod x 50)])))
               (step st x)))
      (let [users (vals (:users st))]
        [(count users)
         (reduce + (keep :visits users))
         (reduce + (map (comp count :tags) users))
         (reduce + (keep :score users))
         (get-in st [:counters :ok])
         (get-in st [:counters :err])
         (reduce + (:log st))
         (reduce + (map count (vals (:index st))))
         reads]))))

(defn run [] (run* 200000))
