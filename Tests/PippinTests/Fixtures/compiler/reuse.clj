;; Drop-guided reuse (NOTES "Compiler"): each pairing shape, then values someone else holds, which lend no cell.
(ns fixture.reuse)

;; cons in the cell of the dying vector-seq, string-seq or range
(defn rev-seq [coll]
  (loop [s (seq coll) acc nil]
    (if s
      (recur (next s) (cons (first s) acc))
      acc)))

;; cons over the dying cons: the tail stays in place
(defn bump-heads [n l]
  (loop [i 0 l l]
    (if (< i n)
      (let [x (first l) r (rest l)]
        (recur (inc i) (cons (inc x) r)))
      l)))

(defn double-heads [n l]
  (loop [i 0 l l]
    (if (< i n)
      (recur (inc i) (cons (* 2 (first l)) (rest l)))
      l)))

(defn relist [] (let [s (cons 1 (list 2))] (cons (first s) nil)))
(defn same-cell [] (let [s (cons 1 (list 2))] (cons (first s) (rest s))))
(defn onto-vec [v] (let [s (seq v)] (cons (first s) [9 8])))
(defn onto-nil [v] (let [s (seq v)] (cons (first s) nil)))

;; next and rest handed their dying operand: a unique view steps in its own cell
(defn sum-seq [coll]
  (loop [n 0 s (seq coll)]
    (if s (recur (+ n (first s)) (next s)) n)))

(defn count-seq [coll]
  (loop [n 0 s (seq coll)]
    (if s (recur (inc n) (next s)) n)))

(defn last-of [coll]
  (loop [x nil s (seq coll)]
    (if (seq s) (recur (first s) (rest s)) x)))

(defn split-first [v] (let [[a & more] v] [a more]))

(prn (rev-seq [1 2 3]) (count (rev-seq "héllo")) (= (seq "olléh") (rev-seq "héllo")) (rev-seq (range 3)) (rev-seq []) (rev-seq (list :a :b)))
(prn (bump-heads 3 (list 1 2 3)) (bump-heads 0 (list 1)) (double-heads 3 (list 1 2 3)) (double-heads 2 '(5 6)))
(prn (relist) (list? (relist)) (same-cell) (onto-vec [1 2]) (onto-nil [5]) (list? (onto-nil [5])))
(prn (sum-seq [1 2 3 4]) (sum-seq (range 5)) (sum-seq (list 1 2 3)) (sum-seq nil) (count-seq "héllo") (count-seq []))
(prn (last-of [1 2 3]) (last-of (range 4)) (last-of "xyz") (last-of nil))
(prn (split-first [1 2 3]) (split-first [1]) (split-first (range 3)) (split-first "ab") (split-first nil))

;; held elsewhere: a var, an atom, another local, a closure, a handler; a cell with meta and a lazy seq
(def kept (list 1 2 3))
(def box (atom (list 10 20)))
(defn aliased [l] (let [s (seq (map inc l)) t s] [(cons 0 (rest s)) t]))
(defn captured [] (let [s (list 1 2 3) f (fn [] s)] [(cons 0 (rest s)) (f)]))
(defn guarded [v]
  (let [s (seq v)]
    (try (cons (first s) (rest s))
         (catch :default e (count s)))))
(defn meta-cell [] (let [s (with-meta (list 1 2) {:m 1})] (cons 0 (rest s))))
(defn lazy-cell [] (let [s (lazy-seq (list 1 2))] (cons 0 (rest s))))

(prn (bump-heads 1 kept) kept (bump-heads 2 @box) @box (rev-seq kept) kept)
(prn (aliased [1 2 3]) (captured) (guarded [1 2]) (meta-cell) (meta (meta-cell)) (lazy-cell))
