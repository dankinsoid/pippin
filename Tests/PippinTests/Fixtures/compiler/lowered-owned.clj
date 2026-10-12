;; The accumulator handed to a lowered literal at +1 (NOTES "Lowered natives"): every other holder keeps what it saw.
(ns fixture.lowered-owned)

(defn freq [xs] (reduce (fn [m x] (assoc m x (inc (get m x 0)))) {} xs))
(defn uniq [xs] (reduce (fn [s x] (if (contains? s x) s (conj s x))) #{} xs))
(defn invert [m] (reduce-kv (fn [acc k v] (assoc acc v k)) {} m))
(defn onto [init xs] (reduce (fn [v x] (conj v x)) init xs))

;; the 2-arity seeds with the first item, which the collection still holds
(defn joined [vs] (reduce (fn [a b] (into a b)) vs))
;; the init a caller keeps, and the same map as init and as the walked collection
(defn bumped [m] (reduce (fn [acc [k v]] (assoc acc k (inc v))) m m))
;; the accumulator as two operands of one call, and kept by an atom across steps
(defn self-nest [xs] (reduce (fn [a x] (conj a a)) [] xs))
(def seen (atom []))
(defn logged [xs] (reduce (fn [a x] (swap! seen conj a) (conj a x)) [] xs))
;; read again after the consuming call's operands, bound to another local, returned unchanged
(defn later [xs] (reduce (fn [m x] (assoc m x (count m))) {} xs))
(defn rebound [xs] (reduce (fn [m x] (let [n m] (assoc n x 1))) {} xs))
(defn kept [xs] (reduce (fn [m x] (if (odd? x) m (assoc m x x))) {} xs))
;; a capture of the accumulator keeps it +0
(defn captured [xs] (reduce (fn [m x] (let [f (fn [] (count m))] (assoc m x (f)))) {} xs))
;; reduced, an early throw and one after the consuming call, the empty coll's (f)
(defn upto [xs t] (reduce (fn [v x] (if (> x t) (reduced v) (conj v x))) [] xs))
(defn boom-before [xs] (try (reduce (fn [m x] (if (= x 3) (throw (ex-info "before" {:m m})) (assoc m x x))) {} xs)
                            (catch :default e [(ex-message e) (ex-data e)])))
(defn boom-after [xs] (try (reduce (fn [m x] (let [r (assoc m x x)] (if (= x 3) (throw (ex-info "after" {:r r})) r))) {} xs)
                           (catch :default e [(ex-message e) (ex-data e)])))
(defn empty-or [xs] (reduce (fn ([] {:empty true}) ([m x] (assoc m x x))) xs))
;; handed on to a def'd fn that owns its parameter
(defn add-key [m k] (assoc m k true))
(defn keyed [ks] (reduce (fn [m k] (add-key m k)) {} ks))
;; a nested lowered reduce inside the literal
(defn grid [rows] (reduce (fn [m row] (reduce (fn [n x] (update n x (fnil inc 0))) m row)) {} rows))
;; a fn-body recur rebinding the accumulator
(defn pad [xs] (reduce (fn [v x] (if (< (count v) x) (recur (conj v :_) x) (conj v x))) [] xs))

(def shared {:a 1 :b 2})
(def base [1 2])
(println (into (sorted-map) (freq [1 2 1 3 1])) (sort (uniq [3 1 3 2])) (into (sorted-map) (invert {:a 1 :b 2})))
(println (onto base [3 4]) base (onto [] (range 5)) (onto nil [1]))
(let [vs [[1] [2] [3]]] (println (joined vs) vs))
(println (into (sorted-map) (bumped shared)) shared)
(println (self-nest [1 2 3]))
(println (logged [1 2 3]) @seen)
(println (into (sorted-map) (later [:a :b :c])) (into (sorted-map) (rebound [1 2])) (into (sorted-map) (kept (range 6))))
(println (into (sorted-map) (captured [:x :y])))
(println (upto [1 2 3 4] 2) (upto (range) 3) (upto [] 0))
(println (boom-before [1 2 3 4]) (boom-after [1 2 3 4]))
(println (empty-or []) (empty-or nil) (into (sorted-map) (empty-or [{} 1 2])) (empty-or [7]))
(println (into (sorted-map) (keyed [:p :q])))
(println (into (sorted-map) (grid [[1 2] [2 3] [3]])))
(println (pad [2 5]))
