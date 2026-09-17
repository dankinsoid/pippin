;; FN and INVOKE: arities, variadics, self reference, captures at every depth, closures as values, apply, arity errors.
(ns fixture.fns)

(defn arities
  ([] :zero)
  ([a] [:one a])
  ([a b] [:two a b])
  ([a b & more] [:many a b more]))

(defn make-adder [n] (fn [x] (+ n x)))

(defn make-counter []
  (let [state (atom 0)]
    {:inc (fn [] (swap! state inc)) :get (fn [] @state)}))

(defn deep [a]
  (fn [b]
    (fn [c]
      (fn [d] [a b c d]))))

(defn fact [n] ((fn f [k] (if (<= k 1) 1 (* k (f (dec k))))) n))

(defn closures-in-loop [n]
  (loop [i 0 fs []]
    (if (< i n) (recur (inc i) (conj fs (fn [] (* i 10)))) (mapv (fn [f] (f)) fs))))

(defn err [f & args] (try (apply f args) (catch :default e (ex-message e))))

(println (arities) (arities 1) (arities 1 2) (arities 1 2 3 4) ((make-adder 5) 10) (fact 10))
(let [c (make-counter)] ((:inc c)) ((:inc c)) (println ((:get c)) ((((deep 1) 2) 3) 4) (closures-in-loop 3)))
(println (err arities) (err (fn [a b] a) 1) (err (fn named [a] a)) (err (fn [& r] r)) (err 42 1) (err :k {:k 1}) (err {:k 2} :k) (err [10 20] 1) (err #{3} 3))
(println (apply arities [1 2 3]) (apply + 1 2 [3 4]) (map (make-adder 1) [1 2 3]) (fn? make-adder) (ifn? :k) (meta (with-meta (fn []) {:m 1})))
(println (let [f (fn [x] (fn [y] (fn [z] (+ x y z))))] (((f 1) 2) 3)) ((fn [& {:keys [a b]}] [a b]) :a 1 :b 2) ((fn [[x y] {z :z}] [x y z]) [1 2] {:z 3}))
