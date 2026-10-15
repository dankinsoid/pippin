;; The arms a known type picks (a string's nth and count, a char's int, an array kind's aget and aset) answer as the generic ones.
(ns fixture.known-types)

(defn err [f] (try (f) (catch :default e (ex-message e))))
(defn thrown [f] (try (f) :none (catch :default _ :thrown)))

(defn walk [x]
  (let [s (str x "é!")]
    (loop [i 0 acc []]
      (if (< i (count s)) (recur (inc i) (conj acc (int (nth s i)))) acc))))
(defn maybe-nth [flag i] (let [s (when flag "héllo")] [(nth s i nil) (get s i) (count s)]))
(defn codes [s] (let [t (str s)] [(long (nth t 0)) (nth t 1 :none) (nth t 9 :none) (get t 9)]))

(defn longs-of [n]
  (let [a (long-array n)]
    (dotimes [i n] (aset a i (* i 3000000000)))
    [(aget a (dec n)) (alength a) (thrown #(aget a n)) (err #(aset a 0 "x")) (aset a 0 2.5) (aget a 0)]))
(defn doubles-of []
  (let [a (double-array 2) s (short-array 2) c (char-array 2)]
    (aset a 1 2.5)
    (aset a 0 3)
    (aset s 1 (short 7))
    (aset c 0 \q)
    [(aget a 0) (aget a 1) (aget s 1) (aget c 0) (err #(aset s 0 70000))]))

(prn (walk "ab") (walk 12) (maybe-nth true 1) (maybe-nth false 1) (thrown #(walk nil)))
(prn (codes "xyz") (codes "é") (thrown #(codes "")))
(prn (longs-of 3) (doubles-of))
