;; Promoted slots (C variables) beside the ones the frame array must keep: captures, OUTER reads, handlers, hand-over.
(ns fixture.promoted)

(defn capture-one [n]
  (let [a (* n 2) b (+ n 1) c (vector n)]
    (let [f (fn [] b)]
      [(f) a c (f)])))

(defn outer-read [n]
  (let [base (* n 10) extra (vector n) helper (fn [x] (+ base x))]
    (loop [i 0 acc []]
      (if (< i 3) (recur (inc i) (conj acc (helper i))) [acc extra]))))

(defn handler-reads [x]
  (let [before (vector x x) tag (str "t" x)]
    (try
      (let [inner (conj before :inner)]
        (if (pos? x) (throw (ex-info "mid" {:inner inner})) inner))
      (catch :default e [before tag (:inner (ex-data e)) (ex-message e)]))))

(defn catch-slot [x]
  (try (throw (ex-info "boom" {:x x}))
       (catch :default e (let [d (ex-data e)] [(:x d) (ex-message e)]))))

(defn hand-over [n]
  (loop [i 0 v [] s #{}]
    (if (< i n) (recur (inc i) (conj v i) (conj s (* i i))) [v s])))

(defn swap-vars [a b n]
  (loop [x a y b k 0]
    (if (< k n) (recur y x (inc k)) [x y])))

(defn fused-inside [n]
  (let [k 3 tag (vector :k k) total (reduce + 0 (map (fn [x] (* x k)) (range n)))]
    [tag total (reduce + 0 (filter even? (range n)))]))

(defn throw-mid [n]
  (let [a (vec (range n)) b (str "s" n) c (list n n) d (hash-map :n n)]
    (if (pos? n) (throw (ex-info "live" {:a a :b b :c c :d d})) [a b c d])))

(defn rest-param [x & more]
  (let [n (count more)]
    (if (seq more) [x n (first more)] [x n])))

(defn direct-with-let [n]
  (let [scale (* n 2)
        f (fn [x] (let [y (+ x scale) z (vector y)] (conj z x)))]
    [(f 1) (f 2)]))

(defn loop-in-try [n]
  (try
    (loop [i 0 acc []]
      (if (< i n) (recur (inc i) (conj acc i)) (throw (ex-info "done" {:acc acc}))))
    (catch :default e (:acc (ex-data e)))))

(println (capture-one 5) (outer-read 2) (handler-reads 1) (handler-reads 0) (catch-slot 7))
(println (hand-over 4) (swap-vars :a :b 3) (swap-vars :a :b 4) (fused-inside 5))
(println (try (throw-mid 3) (catch :default e (ex-data e))) (throw-mid 0) (rest-param 1) (rest-param 1 2 3))
(println (direct-with-let 10) (loop-in-try 3))
(let [top-a (vector 1) top-b (str "b") top-c (fn [] top-b)]
  (println top-a (top-c) (let [x (conj top-a 2)] x)))
