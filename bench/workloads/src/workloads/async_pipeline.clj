;; @ai-generated(solo)
;; core.async: a producer, a transducing channel, pipeline, mult/tap, merge and a fan-in of many go blocks.
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

(defn- broadcast [n]
  (let [src (a/chan)
        m (a/mult src)
        evens (a/chan 32 (filter even?))
        squares (a/chan 32 (map #(* % %)))]
    (a/tap m evens)
    (a/tap m squares)
    ;; A mult drops what arrives before its first tap, so the source fills only now.
    (a/onto-chan! src (range n))
    (a/<!! (a/reduce + 0 (a/merge [evens squares])))))

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
  [(staged n) (broadcast n) (fan-in 500 (quot n 100)) (ping-pong n)])

(defn run [] (run* 20000))
