;; Drop-guided reuse under a rebound cons, next and rest: the site calls the root and the slot keeps its value.
(ns fixture.reuse-rebind)

(defn rev-seq [coll] (loop [s (seq coll) acc nil] (if s (recur (next s) (cons (first s) acc)) acc)))
(defn count-seq [coll] (loop [n 0 s (seq coll)] (if s (recur (inc n) (next s)) n)))
(defn last-of [coll] (loop [x nil s (seq coll)] (if (seq s) (recur (first s) (rest s)) x)))
(defn bump-heads [n l] (loop [i 0 l l] (if (< i n) (let [x (first l) r (rest l)] (recur (inc i) (cons (inc x) r))) l)))

(def orig-cons cons)
(def orig-next next)
(def orig-rest rest)

(prn (with-redefs [cons (fn [x s] (orig-cons x s))
                   next (fn [s] (orig-next s))
                   rest (fn [s] (orig-rest s))]
       [(rev-seq [1 2 3]) (count-seq "abc") (last-of (range 4)) (bump-heads 2 (list 1 2))])
     (rev-seq [1 2 3]) (count-seq "abc") (last-of (range 4)) (bump-heads 2 (list 1 2)))
(prn (with-redefs [cons (fn [x s] (orig-cons (* 10 x) s))] (rev-seq [1 2 3])) (with-redefs [next (fn [s] (orig-next (orig-next s)))] (count-seq [1 2 3 4 5])))
