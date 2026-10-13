;; Scalar replacement: a let-bound literal in slots, materialized where it leaves the frame.
(ns fixture.scalar)

;; reads the layout answers: destructuring, keyword lookups, the value as a fn, count and predicates
(defn swap-pair [a b] (let [[x y] [b a]] (+ (* 10 x) y)))
(defn area [w h] (let [r {:w w :h h}] (* (:w r) (:h r))))
(defn greet [n] (let [{:keys [name age] :or {age 0}} {:name n :age 3}] (str name "/" age)))
(defn as-fn [a] (let [t [a 7] m {:a a}] [(t 1) (m :a) (m :zz 9)]))
(defn kw-tuple [a] (let [t [a]] (:k t :none)))
(defn not-found [a] (let [t [a]] [(nth t 3 :nope) (get t 5) (get t 0)]))
(defn preds [a] (let [t [a a] m {:a a}] [(count t) (vector? t) (map? m) (empty? m) (contains? m :a) (contains? t 2) (if t :yes :no)]))
(println (swap-pair 1 2) (area 3 4) (greet "x") (as-fn 5) (kw-tuple 1) (not-found 4) (preds 2))

;; an if of two literals, an assoc of a key the map has, a value held in a field, a loop
(defn order [a b] (let [[lo hi] (if (< a b) [a b] [b a])] (- hi lo)))
(defn bump [n] (let [m {:n n :k 1} m2 (assoc m :n (inc n))] [(:n m) (:n m2) (:k m2)]))
(defn nested [a] (let [p [a 1] q [p 2]] (+ (nth (nth q 0) 0) (nth q 1))))
(defn loop-sum [n] (loop [i 0 acc 0] (if (< i n) (let [[x y] [i (* 2 i)]] (recur (inc i) (+ acc x y))) acc)))
(defn norm2 [[x y]] (+ (* x x) (* y y)))
(defn use-norm [a b] (norm2 [a b]))
(println (order 5 2) (order 1 9) (bump 5) (nested 5) (loop-sum 4) (use-norm 3 4))

;; items run once, in order, in either arm
(defn order-fx [] (let [log (atom []) t [(swap! log conj 1) (swap! log conj 2)]] [(count (nth t 0)) @log]))
(defn join-fx [c] (let [log (atom []) [x y] (if c [(swap! log conj :a) (swap! log conj :b)] [(swap! log conj :c) (swap! log conj :d)])] [(count y) @log]))
(println (order-fx) (join-fx true) (join-fx false))

;; materialization points: a return, a store, an unknown call, a capture, recur, metadata, =, hash, seq
(defn ret [a] (let [t [a (inc a)]] (if (pos? a) (nth t 1) t)))
(defn store [a] (let [t [a a]] (if (odd? a) (atom t) (nth t 0))))
(defn call-it [f a] (let [t [a 2]] (if f (f t) (count t))))
(defn cap [a] (let [t {:a a :b 2}] (if (pos? a) (fn [] [(:b t) t]) (:a t))))
(defn rec [n] (loop [i 0 acc nil] (if (< i n) (let [p [i (* i i)]] (recur (inc i) (if (= i (dec n)) p (nth p 1)))) acc)))
(defn meta-it [a] (let [t [a]] (if (pos? a) (meta (with-meta t {:x a})) (nth t 0))))
(defn eq1 [a] (let [t [a 1]] (if (pos? a) (= t [a 1]) (nth t 0))))
(defn h1 [a] (let [m {:a a :b [a]}] (if (pos? a) (hash m) (:a m))))
(defn seq1 [a] (let [m {:z a :a 1}] (if (pos? a) (seq m) (:z m))))
(println (ret 1) (ret -1) (vector? (ret -1)) @(store 3) (store 2) (call-it nil 1) (call-it pr-str 1))
(println ((cap 1)) (cap -1) (rec 4) (meta-it 2) (meta-it -2) (eq1 3) (= (h1 3) (hash {:a 3 :b [3]})) (seq1 5) (seq1 -5))

;; one object per binding: a value that would be materialized twice keeps its allocation
(defn same [a] (let [t [a 1] box (atom nil)] (reset! box t) (identical? @box t)))
(defn twice [a] (let [t [a 1]] (if (pos? a) [t t] (nth t 0))))
(defn in-loop [a] (let [t [a 1]] (loop [i 0 acc []] (if (< i 3) (recur (inc i) (conj acc t)) (identical? (nth acc 0) (nth acc 2))))))
(println (same 1) (let [[u v] (twice 2)] (identical? u v)) (twice -2) (in-loop 1))

;; a throw between the fields and their reads, a handler reading them, a throw while the items run
(defn handled [a] (let [t [a (inc a)]] (try (if (neg? a) (throw (ex-info "neg" {})) (nth t 0)) (catch Exception e (nth t 1)))))
(defn handled-mat [a] (let [t {:a a}] (try (if (neg? a) (throw (ex-info "neg" {})) (:a t)) (catch Exception e t))))
(defn item-throws [a b] (try (let [[x y] [a (quot a b)]] (+ x y)) (catch Exception e (ex-message e))))
(println (handled 2) (handled -5) (handled-mat 1) (handled-mat -1) (item-throws 6 3) (item-throws 6 0))

;; a reduce over a map runs its fn over the key and value; any other source, the 2-arity's seed and the entry kept see it
(defn without [amap x] (reduce (fn [m [k vs]] (assoc m k (disj vs x))) {} (dissoc amap x)))
(defn odd-entries [coll] (reduce (fn [acc [k v :as e]] (if (odd? v) (conj acc e) acc)) [] coll))
(defn first-key [coll] (reduce (fn [acc [k v]] (if (= v 2) (reduced k) acc)) nil coll))
(defn seeded [coll] (reduce (fn [a [k v]] (if (vector? a) (+ (nth a 1) v) (+ a v))) coll))
(defn step-throws [coll] (try (reduce (fn [acc [k v]] (+ acc (quot 10 v))) 0 coll) (catch Exception e (ex-message e))))
(println (into (sorted-map) (without {:a #{1 2} :b #{2 3} :c #{2}} 2)) (sort (odd-entries {:a 1 :b 2 :c 3})) (odd-entries [[:x 1] [:y 2]])
         (first-key {:a 1 :b 2}) (first-key (sorted-map :a 2 :b 2)) (seeded {:a 1}) (seeded (sorted-map :a 1 :b 2)) (step-throws {:a 5 :b 0}))
