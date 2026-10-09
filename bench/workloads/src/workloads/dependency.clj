;; @ai-generated(solo)
;; com.stuartsierra.dependency: a random DAG through its protocols, sorted and queried.
(ns workloads.dependency
  (:require [com.stuartsierra.dependency :as dep]
            [workloads.util :refer [lcg]]))

(defn- build [n]
  (loop [i 1 x 7 g (dep/graph)]
    (if (< i n)
      (let [a (lcg x)
            b (lcg a)]
        (recur (inc i) b (-> g (dep/depend i (mod a i)) (dep/depend i (mod b i)))))
      g)))

(defn run* [n]
  (let [g (build n)
        order (dep/topo-sort g)
        pos (zipmap order (range))
        ;; A set's order is the runtime's, so the sort is checked against every edge rather than printed.
        forward (count (for [i (range n)
                             d (dep/immediate-dependencies g i)
                             :when (< (pos d) (pos i))]
                         d))
        sorted (sort (dep/topo-comparator g) (range 0 n 3))
        trans (reduce + (map #(count (dep/transitive-dependencies g %)) (range (- n 40) n)))
        dependents (count (dep/transitive-dependents g 0))
        hits (count (filter #(dep/depends? g % 0) (range 1 n 41)))
        pruned (reduce dep/remove-all g (range 0 n 5))]
    [(count order) forward (count sorted) (count (dep/nodes g)) trans dependents hits
     (count (dep/nodes pruned))]))

(defn run [] (run* 3000))
