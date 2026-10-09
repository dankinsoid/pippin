;; @ai-generated(solo)
;; com.stuartsierra.dependency: a random DAG through its protocols, sorted and queried.
;; Input: n = 1500 nodes; node i >= 1 depends on (a mod i) and (b mod i), a and b the next two util/lcg states
;; (seed 7, continuing). Work: topo-sort, a check of every edge against the order, sort by topo-comparator of every
;; third node, transitive-dependencies of the last 40 nodes, transitive-dependents of node 0, depends? of every
;; 41st node on 0, remove-all of every fifth node. Output: a vector of 8 counts, `expected` below.
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

(defn run [] (run* 1500))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '[1500 2995 500 1500 3238 1499 37 1200])
