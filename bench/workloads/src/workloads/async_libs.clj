;; @ai-generated(solo)
;; Libraries written over core.async: parallel-async on go blocks, turbine on threads.
(ns workloads.async-libs
  (:require [clojure.core.async :as a]
            [com.stuartsierra.parallel :as p]
            [turbine.core :as t]))

(defn- parallel-sum [n]
  (let [in (a/to-chan! (range n))
        out (a/chan 128)
        done (p/parallel 4 (fn [x] (* x x)) in out)]
    (a/go (a/<! done) (a/close! out))
    (a/<!! (a/reduce + 0 out))))

(defn- pmax-sum [n]
  (let [in (a/to-chan! (range n))
        out (a/chan 128)
        done (p/pmax 8 (fn [x] (a/go (inc x))) in out)]
    (a/go (a/<! done) (a/close! out))
    (a/<!! (a/reduce + 0 out))))

;; The sink cannot tell the last value apart, so it counts to what the routes let through.
(defn- turbine-sum [n]
  (let [expected (+ n (quot n 2))
        total (atom 0)
        seen (atom 0)
        done (a/promise-chan)
        [in] (t/make-topology
              [[:in :in1]
               [:scatter :in1 [[:a (map inc)] [:b (map #(* 2 %))]]]
               [:union [:a :b] [:u (filter even?)]]
               [:sink :u (fn [v]
                           (swap! total + v)
                           (when (= expected (swap! seen inc))
                             (a/put! done true)))]])]
    (dotimes [i n] (in i))
    (a/<!! done)
    (t/close-topology [in])
    @total))

(defn run* [n]
  [(parallel-sum n) (pmax-sum (quot n 2)) (turbine-sum (quot n 4))])

(defn run [] (run* 20000))
