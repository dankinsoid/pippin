;; LOCAL, LET, LOOP, RECUR, IF, DO: slots, shadowing, recur in loops and fn bodies, last-use hand-over.
(ns fixture.locals)

(defn sum-to [n]
  (loop [i 0 acc 0]
    (if (< i n) (recur (inc i) (+ acc i)) acc)))

(defn build [n]
  (loop [i 0 v []]
    (if (= i n) v (recur (inc i) (conj v (* i i))))))

(defn shadow [x]
  (let [x (inc x) y (let [x (* x 10)] x) x (+ x y)]
    [x y]))

(defn count-down [n]
  (if (pos? n) (recur (dec n)) :done))

(defn nested [n]
  (loop [i 0 out []]
    (if (< i n)
      (recur (inc i) (conj out (loop [j 0 s 0] (if (<= j i) (recur (inc j) (+ s j)) s))))
      out)))

(defn swap-pair [a b]
  (loop [a a b b k 0]
    (if (< k 3) (recur b a (inc k)) [a b])))

(defn side-effects []
  (let [log (atom [])]
    (do (swap! log conj 1) (swap! log conj 2) nil)
    (if (do (swap! log conj :test) false) :then (do (swap! log conj :else) @log))))

(println (sum-to 10) (build 5) (shadow 1) (count-down 100000) (nested 4) (swap-pair 1 2) (side-effects))
(println (let [] 1) (let [a 1 b 2] (let [c (+ a b)] (* c c))) (loop [] 7) (if nil 1) (if false 1 2) (do) (do 1 2 3))
(println (let [v [1 2] w (conj v 3) v (conj v 4)] [v w]) (loop [xs (range 5) acc ()] (if (seq xs) (recur (rest xs) (cons (first xs) acc)) acc)))
