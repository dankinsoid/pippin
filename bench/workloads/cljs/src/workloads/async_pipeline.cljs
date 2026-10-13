;; @ai-generated(solo)
;; src/workloads/async_pipeline.clj without <!!, which one thread lacks: run* parks on each stage in its order.
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
    (a/reduce + 0 out)))

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
    (a/reduce + 0 out)))

(defn- ping-pong [n]
  (let [ping (a/chan)
        pong (a/chan)]
    (a/go-loop []
      (when-let [v (a/<! ping)]
        (a/>! pong (inc v))
        (recur)))
    (a/go
      (let [r (a/<! (a/go-loop [i 0 acc 0]
                      (if (< i n)
                        (do (a/>! ping i)
                            (recur (inc i) (+ acc (a/<! pong))))
                        acc)))]
        (a/close! ping)
        r))))

(defn run* [n]
  (a/go [(a/<! (staged n)) (a/<! (fan-in 500 (quot n 100))) (a/<! (ping-pong n))]))

(defn run [] (run* 20000))

(def expected '[300000000 34900000 200010000])
