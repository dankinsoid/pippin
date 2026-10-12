;; Owned parameters (NOTES "Compiler"): each hand-over shape, throws on either side of the call, the +0 calls.
(ns fixture.owned)

(defn sorted [m] (into (sorted-map) m))

;; the collection read again by `count`, taken at the assoc; the recursion keeps it a call
(defn bump [m k] (if (neg? k) (bump m 0) (assoc m k (count m))))

;; the loop's map handed to bump at its last use
(defn walk [m n] (loop [i 0 m m] (if (< i n) (recur (inc i) (bump m i)) m)))

;; owned only because bump owns: the parameter goes on at its death
(defn outer [m] (bump m 9))

;; the argument the call takes is a later operand than a read of it
(defn sw [a m] (assoc m :a a))
(defn later [m] (sw (count m) m))

;; the same value twice in one call: retained, never handed over beside a borrowed alias
(defn pair [a b] [(assoc a :x 1) b])
(defn twice [m] (pair m m))

;; a throw inside the callee releases what it took; a throw before the call leaves it to the caller
(defn boom [m k] (if (= k :boom) (throw (ex-info "boom" {:k k})) (assoc m k 1)))
(defn try-boom [m] (try (boom m :boom) (catch :default e (ex-message e))))
(defn early [m] (try (bump m (throw (ex-info "early" {}))) (catch :default e (ex-message e))))

;; a fn-body recur rebinding owned parameters
(defn drain [s acc] (if (seq s) (recur (rest s) (conj acc (first s))) acc))

;; a cons over the dying cons a caller handed over
(defn step1 [s] (if (empty? s) s (cons (inc (first s)) (rest s))))
(defn steps [n s] (if (zero? n) s (recur (dec n) (step1 s))))

;; a captured parameter stays +0
(defn capt [m] (let [f (fn [] (count m))] (assoc m :z (f))))

(def kept {7 7})

(println (sorted (walk {} 5)) (sorted (outer {1 1})))
(let [m {1 1} m2 (bump m 5)] (println (sorted m) (sorted m2)))
(println (sorted (bump kept 8)) (sorted kept))
(println (:a (later {:b 1})) (:b (later {:b 1})))
(let [[a b] (twice {:y 2})] (println [(:x a) (:x b) (:y a) (:y b)]))
(println (try-boom {1 1}) (early {1 1}))
(println (drain (list 1 2 3) []) (drain [4 5] [3]))
(println (steps 3 (list 1 2 3)) (steps 2 (list 5)))
(println (:z (capt {:a 1})))
;; the +0 entries: clj_invoke from reduce and apply, the host's call
(println (sorted (reduce bump {} [1 2 3])) (sorted (apply bump [{} 4])) (sorted (apply walk [{} 2])))
