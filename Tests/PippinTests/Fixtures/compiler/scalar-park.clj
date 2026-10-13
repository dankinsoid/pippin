;; A replaced value's fields held across a park, read and materialized after it.
(ns fixture.scalar-park
  (:require [clojure.core.async :as a]))

(defn parked [ch x] (a/go (let [[p q] [x (inc x)] v (a/<! ch)] (+ p q v))))
(defn parked-map [ch x] (a/go (let [m {:a x :b 2} v (a/<! ch)] (if (pos? v) (assoc m :c v) (:a m)))))
(let [ch (a/chan) r (parked ch 1)] (a/>!! ch 10) (println (a/<!! r)))
(let [ch (a/chan) r (parked-map ch 3)] (a/>!! ch 5) (println (a/<!! r)))
(let [ch (a/chan) r (parked-map ch 3)] (a/>!! ch -1) (println (a/<!! r)))
