;; A lowered call runs the native only while its var holds the boot root; a rebinding runs the call as written.
(ns fixture.lowered-rebind)

(defn total [xs] (reduce (fn [a x] (+ a x)) 0 xs))
(defn bump [a] (swap! a (fn [v] (inc v))))
(defn flip [m] (reduce-kv (fn [acc k v] (assoc acc v k)) {} m))

(println (total [1 2 3]) (with-redefs [reduce (fn [f init coll] [:redef (f init (first coll))])] (total [1 2 3])) (total [4]))
(println (bump (atom 1)) (with-redefs [swap! (fn [a f] [:redef (f @a)])] (bump (atom 1))) (bump (atom 2)))
(println (flip {:a 1}) (with-redefs [reduce-kv (fn [f init m] :redef)] (flip {:a 1})))
