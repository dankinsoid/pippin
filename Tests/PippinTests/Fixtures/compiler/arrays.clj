;; aget, aset and alength: the object-array arms at the site, every other kind and every error through the builtins.
(ns fixture.arrays)

(defn msg [f] (try (f) (catch :default e (ex-message e))))
(defn get-at [a i] (aget a i))
(defn put-at [a i v] (aset a i v))
(defn len [a] (alength a))

(defn fill [n] (let [a (object-array n)] (dotimes [i n] (aset a i [i (str i)])) a))
(defn total [a] (let [n (alength a)] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (first (aget a i)))) acc))))
(defn swap-ends [a] (let [n (alength a) x (aget a 0)] (aset a 0 (aget a (dec n))) (aset a (dec n) x) (vec a)))
;; an array that holds itself: the store gives it the edge the collector must see
(defn cyclic [] (let [a (object-array 2)] (aset a 0 a) (aset a 1 :end) [(identical? a (aget a 0)) (aget (aget a 0) 1)]))

(let [o (fill 4)
      l (long-array [1 2 3])
      d (double-array [1.5 2.5])
      c (char-array "hi")
      b (boolean-array 2)]
  (println (total o) (swap-ends o) (len o) (get-at o 2) (put-at o 2 :x) (get-at o 2) (put-at o 3 nil) (vec o))
  (println (get-at l 1) (put-at l 1 40) (vec l) (get-at d 0) (put-at d 1 0.25) (vec d) (get-at c 1) (put-at c 0 \H) (apply str c)
           (put-at b 1 true) (vec b) (len l) (len d) (len c))
  (println (cyclic) (msg #(get-at o 4)) (msg #(get-at o -1)) (msg #(put-at o 4 1)) (msg #(put-at o -1 1)) (msg #(get-at o 1.0))
           (msg #(get-at o "1")) (msg #(get-at o nil)) (msg #(put-at o :k 1)) (msg #(get-at [1 2] 0)) (msg #(put-at nil 0 1))
           (msg #(len [1 2])) (msg #(len nil)) (msg #(put-at l 0 "s")) (msg #(put-at l 0 1.5)) (msg #(get-at l 3))
           (msg #(put-at c 0 "x")) (msg #(get-at (object-array 0) 0))))
