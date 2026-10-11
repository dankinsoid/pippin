;; Inlined calls and fused stages over literal fns (design §10 step 7a): the traces still name every inlined fn.
(ns fixture.inline)

(defn sq [x] (* x x))
(defn use-sq [y] (sq (inc y)))
(defn pick [m k] (get m k))
(defn use-pick [m] [(pick m :a) (pick m :b)])
(defn boom [x] (if (pos? x) (throw (ex-info "boom" {:x x})) x))
(defn use-boom [x] (boom x))
(defn trace-names [] (try (use-boom 1) (catch :default e [(= "boom" (ex-message e)) (subvec (mapv :fn (ex-trace e)) 0 3)])))
(defn pair [a b] [a b])
(defn use-pair [] (let [log (atom [])] [(pair (do (swap! log conj 1) 1) (do (swap! log conj 2) 2)) @log]))
(defn second-of [v] (first (next v)))
(defn use-second [] (second-of [1 2 3]))
(defn fused [xs]
  [(reduce + 0 (map (fn [x] (* x x)) (filter (fn [x] (odd? x)) xs)))
   (into [] (remove (fn [x] (= x 2)) (map (fn [x] (inc x)) xs)))
   (vec (map (fn [x] (* 2 x)) xs))
   (count (filter (fn [x] (even? x)) xs))])
(defn fused-trace [] (try (reduce + (map (fn stage [x] (if (= x 3) (throw (ex-info "s" {})) x)) [1 2 3])) (catch :default e (first (mapv :fn (ex-trace e))))))

(println (use-sq 2) (use-pick {:a 1 :b 2}) (trace-names) (use-pair) (use-boom 0) (use-second))
(println (fused [1 2 3 4 5]) (fused-trace))
