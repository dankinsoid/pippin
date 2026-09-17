;; DIRECT_FN, DIRECT_CALL, OUTER: let-bound helpers only ever called, free variables through the static link.
(ns fixture.direct)

(defn helper-in-loop [n]
  (let [square (fn [x] (* x x))]
    (loop [i 0 acc 0]
      (if (< i n) (recur (inc i) (+ acc (square i))) acc))))

(defn free-vars [a b]
  (let [c 10
        f (fn [x] (+ a b c x))
        g (fn [y] (f (f y)))]
    [(f 1) (g 1)]))

(defn nested-direct [k]
  (let [outer (fn [x]
                (let [inner (fn [y] (+ x y k))]
                  (inner (inner x))))]
    (outer 1)))

(defn recursive-direct [n]
  (let [fib (fn fib [i] (if (< i 2) i (+ (fib (- i 1)) (fib (- i 2)))))]
    (fib n)))

(defn closure-inside-direct [xs]
  (let [scale (fn [k] (map (fn [x] (* k x)) xs))]
    [(scale 2) (scale 3)]))

(defn direct-throws [x]
  (let [check (fn [v] (if (neg? v) (throw (ex-info "negative" {:v v})) v))]
    (try (check x) (catch :default e (ex-message e)))))

(defn multi-arity-direct []
  (let [f (fn ([] 0) ([a] a) ([a b] (+ a b)))]
    [(f) (f 1) (f 1 2)]))

(defn escaping [x]
  (let [f (fn [y] (+ x y))]
    [(f 1) (map f [1 2]) f]))

(println (helper-in-loop 5) (free-vars 1 2) (nested-direct 100) (recursive-direct 15) (closure-inside-direct [1 2 3]) (direct-throws -1) (direct-throws 4) (multi-arity-direct) (take 2 (escaping 10)))
(println (let [f (fn [] :unused)] :ok) (let [a 1 f (fn [] a)] (let [a 2] [(f) a])) (loop [i 0] (let [step (fn [x] (inc x))] (if (< i 3) (recur (step i)) i))))
