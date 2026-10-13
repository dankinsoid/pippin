;; @ai-generated(solo)
;; src/workloads/async_broadcast.clj without <!!, which one thread lacks: run* returns the sum's channel.
(ns workloads.async-broadcast
  (:require [clojure.core.async :as a]))

(defn- broadcast [n]
  (let [src (a/chan)
        m (a/mult src)
        evens (a/chan 32 (filter even?))
        squares (a/chan 32 (map #(* % %)))]
    (a/tap m evens)
    (a/tap m squares)
    ;; A mult drops what arrives before its first tap, so the source fills only now.
    (a/onto-chan! src (range n))
    (a/reduce + 0 (a/merge [evens squares]))))

(defn run* [n] (broadcast n))

(defn run [] (run* 20000))

(def expected '2666566660000)
