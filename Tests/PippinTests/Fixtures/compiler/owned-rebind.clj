;; A dev call of an owned entry with the var rebound: the guard fails and the +0 call releases what was taken.
(ns fixture.owned-rebind)

(defn bump [m k] (if (neg? k) (bump m 0) (assoc m k (count m))))
(defn walk [m n] (loop [i 0 m m] (if (< i n) (recur (inc i) (bump m i)) m)))

(println (into (sorted-map) (walk {} 2)))
(println (into (sorted-map) (with-redefs [bump (fn [m k] (assoc m k :redef))] (walk {} 2))))
(println (into (sorted-map) (walk {} 3)))
