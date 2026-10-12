;; A lazy-seq thunk lets its dead captures go at its tail cons (NOTES "Owned parameters"): every holder keeps what it saw.
(ns fixture.lazy-let-go)

(defn lmap [f coll] (lazy-seq (when-let [s (seq coll)] (cons (f (first s)) (lmap f (rest s))))))
(defn lcat [x y] (lazy-seq (let [s (seq x)] (if s (cons (first s) (lcat (rest s) y)) y))))
;; a capture read directly as an operand of the cons stays in the cell
(defn lead [x coll] (lazy-seq (let [s (seq coll)] (when s (cons x (lead x (rest s)))))))
;; the tail cons inside a loop
(defn lkeep [p coll] (lazy-seq (loop [s (seq coll)] (when s (if (p (first s)) (cons (first s) (lkeep p (rest s))) (recur (next s)))))))
;; a throw before the cons leaves the captures for the next force
(def tries (atom 0))
(defn flaky [coll] (lazy-seq (let [s (seq coll)] (when (= 1 (swap! tries inc)) (throw (ex-info "flaky" {}))) (cons (first s) (rest s)))))
;; a tail the cons has to seq, which throws: no capture goes either
(def tail (atom 5))
(defn odd-tail [coll] (lazy-seq (let [s (seq coll)] (cons (first s) @tail))))

(def kept (list 1 2 3))
(def v [1 2 3 4])
(println (lmap inc (lmap inc v)) v)
(println (lcat (lcat kept [4 5]) (lmap inc kept)) kept)
(println (lead :x (lcat kept kept)) (lead :y nil))
(println (lkeep odd? (lmap inc (range 10))) (lkeep odd? kept))
(let [s (flaky kept)]
  (println (try (first s) (catch :default e (ex-message e))) (realized? s))
  (println (doall s) (realized? s) @tries))
(let [s (odd-tail kept)]
  (println (try (first s) (catch :default e :thrown)) (realized? s))
  (reset! tail [9])
  (println (doall s) (realized? s)))
;; held by a let: forcing one stage leaves the other walkable
(let [inner (lmap inc kept) outer (lmap inc inner)]
  (println (first outer) inner outer))
;; a rebound lazy-seq* gets the closure, which may be called twice: the closure never lets its captures go
(let [orig lazy-seq*]
  (println (with-redefs [lazy-seq* (fn [f] (f) (orig f))] (doall (lcat (lmap inc kept) [:end])))))
