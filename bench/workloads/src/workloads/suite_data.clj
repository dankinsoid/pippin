;; @ai-generated(solo)
;; The breadth clojure-test-suite exercises, as work: records, protocols, multimethods, sets, sorted and transient
;; collections, sequence functions.
(ns workloads.suite-data
  (:require [clojure.set :as set]
            [clojure.walk :as walk]))

(defprotocol Shape
  (area [s])
  (scale [s k]))

(defrecord Circle [r]
  Shape
  (area [_] (* 3 r r))
  (scale [_ k] (->Circle (* r k))))

(defrecord Rect [w h]
  Shape
  (area [_] (* w h))
  (scale [_ k] (->Rect (* w k) (* h k))))

(defrecord Tri [b h]
  Shape
  (area [_] (quot (* b h) 2))
  (scale [_ k] (->Tri (* b k) (* h k))))

(defmulti describe :kind)
(defmethod describe :point [p] (+ (:x p) (:y p)))
(defmethod describe :segment [s] (- (:x2 s) (:x1 s)))
(defmethod describe :default [_] 1)

(defn run* [n]
  (let [shapes (mapv (fn [i] (case (mod i 3)
                               0 (->Circle (mod i 10))
                               1 (->Rect (mod i 7) (mod i 5))
                               2 (->Tri (mod i 9) (mod i 4))))
                     (range n))
        areas (reduce + (map area shapes))
        scaled (reduce + (map #(area (scale % 2)) shapes))
        fields (reduce + (map #(or (:r %) (:w %) (:b %)) shapes))
        records (count (filter #(and (record? %) (satisfies? Shape %)) shapes))
        figures (mapv (fn [i] (case (mod i 3)
                                0 {:kind :point :x i :y (* 2 i)}
                                1 {:kind :segment :x1 i :x2 (+ i 5)}
                                2 {:kind :other}))
                      (range n))
        described (reduce + (map describe figures))
        s1 (set (range 0 n 2))
        s2 (set (range 0 n 3))
        sets [(count (set/union s1 s2)) (count (set/intersection s1 s2)) (count (set/difference s1 s2))
              (count (set/select even? s2)) (count (set/index (take 300 figures) [:kind]))]
        merged (merge-with + (frequencies (map #(mod % 13) (range n))) (frequencies (map #(mod % 17) (range n))))
        zm (zipmap (range 1000) (map str (range 1000)))
        parts (count (partition 3 1 (range 3000)))
        inter (reduce + (interleave (range 3000) (range 3000 6000)))
        sorted (take 5 (sort > (map #(mod (* % 7919) 10007) (range n))))
        sm (reduce (fn [m i] (assoc m (mod (* i 31) 1009) i)) (sorted-map) (range n))
        stack (reduce (fn [v i] (if (zero? (mod i 3)) (if (seq v) (pop v) v) (conj v i))) [] (range n))
        tr (persistent! (reduce conj! (transient []) (range n)))
        trm (persistent! (reduce (fn [m i] (assoc! m (mod i 777) i)) (transient {}) (range n)))
        walked (walk/postwalk #(if (number? %) (inc %) %) (vec (take 200 figures)))
        nested (reduce + (flatten (map (fn [i] [i [i [i]]]) (range 3000))))]
    [areas scaled fields records described sets (reduce + (vals merged)) (count zm) parts inter
     (vec sorted) (first sm) (last sm) (count stack) (count tr) (count trm)
     (reduce + (map #(or (:x %) 0) walked)) nested]))

(defn run [] (run* 30000))
