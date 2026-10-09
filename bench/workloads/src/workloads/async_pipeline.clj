;; @ai-generated(solo)
;; core.async on go blocks. Input: the integers 0..n-1, n = 20000. Work: (1) a producer go-loop into a 64-slot
;; channel, piped into a channel with (comp (map inc) (filter odd?)), through (pipeline 4 (map #(* 3 %))), summed
;; by a/reduce; (2) 500 go blocks each putting n/100 values (+ block i) on one 128-slot channel, summed; (3) n
;; round trips of ping-pong between two go-loops over unbuffered channels, summing (inc i). Output: the three sums.
(ns workloads.async-pipeline
  (:require [clojure.core.async :as a]))

(defn- staged [n]
  (let [src (a/chan 64)
        mid (a/chan 64 (comp (map inc) (filter odd?)))
        out (a/chan 64)]
    (a/go-loop [i 0]
      (if (< i n)
        (do (a/>! src i) (recur (inc i)))
        (a/close! src)))
    (a/pipe src mid)
    (a/pipeline 4 out (map #(* 3 %)) mid)
    (a/<!! (a/reduce + 0 out))))

(defn- fan-in [blocks per]
  (let [out (a/chan 128)
        done (a/chan blocks)]
    (dotimes [b blocks]
      (a/go
        (dotimes [i per]
          (a/>! out (+ b i)))
        (a/>! done b)))
    (a/go
      (dotimes [_ blocks] (a/<! done))
      (a/close! out))
    (a/<!! (a/reduce + 0 out))))

(defn- ping-pong [n]
  (let [ping (a/chan)
        pong (a/chan)]
    (a/go-loop []
      (when-let [v (a/<! ping)]
        (a/>! pong (inc v))
        (recur)))
    (let [r (a/<!! (a/go-loop [i 0 acc 0]
                     (if (< i n)
                       (do (a/>! ping i)
                           (recur (inc i) (+ acc (a/<! pong))))
                       acc)))]
      (a/close! ping)
      r)))

(defn run* [n]
  [(staged n) (fan-in 500 (quot n 100)) (ping-pong n)])

(defn run [] (run* 20000))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '[300000000 34900000 200010000])
