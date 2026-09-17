;; VECTOR, MAP, SET nodes with non-constant items, duplicate keys, nesting, metadata on literals.
(ns fixture.collections)

(defn lits [a b]
  [[a b] {a b b a} #{a b} [{:k [a #{b}]}] {:nested {a [b {b a}]}}])

(defn dup [k] (try {k 1 (identity k) 2} (catch :default e (ex-message e))))
(defn dup-set [k] (try #{k (identity k)} (catch :default e (ex-message e))))

(println (lits 1 2) (lits :x "y") (dup :a) (dup-set 3) (meta ^{:m 1} [1 2]) (meta ^:flag {:a 1}))
(println (let [x 1] [x (inc x) (let [x 5] x) x]) (let [m {:a 1}] {:m m :n (assoc m :b 2)}) (let [s #{1}] #{s (conj s 2)}))
(println (vector) (hash-map) (hash-set) (list 1 2) (vec (range 3)) (into {} [[1 2]]) (set [1 1 2]) (sorted-map :b 1 :a 2) (sorted-set 3 1 2))
