;; APPLY at and past 21 spread arguments, where a variadic callee takes its rest as one seq at its rest arity.
(ns fixture.apply)

(defn outcome [thunk] (try (thunk) (catch :default e :threw)))

(defn err [f & args] (try (apply f args) (catch :default e (ex-message e))))

(defn none [] (throw (ex-info "no arity" {})))

(defn apply-split [f all lead]
  (let [[a b c d] all
        s (drop lead all)
        s (if (even? lead) (vec s) s)]
    (case lead
      0 (apply f s)
      1 (apply f a s)
      2 (apply f a b s)
      3 (apply f a b c s)
      4 (apply f a b c d s))))

;; 20 is CLJ_FN_MAX_FIXED.
(defn check [f ref gen]
  (let [bad (for [s [0 1 20 21 22 100]
                  lead (range 5)
                  :let [all (gen (+ lead s))
                        got (outcome #(apply-split f all lead))
                        want (outcome #(ref all))]
                  :when (not= got want)]
              [lead s got want])]
    (if (seq bad) (vec bad) :ok)))

(defn nums [n] (vec (range n)))
(defn scattered [n] (mapv #(mod (* % 37) 101) (range n)))
(defn halves [n] (mapv #(quot % 2) (range n)))
(defn keyed [n] (vec (take n (cons :a (map (fn [i] {:a (mod (* i 7) 101) :i i}) (range))))))
(defn small-maps [n] (mapv (fn [i] {(mod i 7) i}) (range n)))
(defn pairs [n] (mapv (fn [i] [i (- i)]) (range n)))

(defn uv [a & r] [a r])
(defn uv-ref [all] (if (seq all) [(first all) (next all)] (none)))

(defn um ([] [:a0]) ([a] [:a1 a]) ([a b] [:a2 a b]) ([a b c & r] [:v a b c r]))
(defn um-ref [all]
  (case (count all)
    0 [:a0]
    1 [:a1 (all 0)]
    2 [:a2 (all 0) (all 1)]
    (let [[a b c & r] all] [:v a b c r])))

(defn ug ([a] [:g1 a]) ([a b c d & r] [:gv a b c d r]))
(defn ug-ref [all]
  (case (count all)
    1 [:g1 (all 0)]
    (0 2 3) (none)
    (let [[a b c d & r] all] [:gv a b c d r])))

(defn u20
  ([a] [:one a])
  ([a1 a2 a3 a4 a5 a6 a7 a8 a9 a10 a11 a12 a13 a14 a15 a16 a17 a18 a19 a20 & r] [:many a1 a20 r]))
(defn u20-ref [all]
  (cond
    (= 1 (count all)) [:one (all 0)]
    (>= (count all) 20) [:many (all 0) (all 19) (seq (drop 20 all))]
    :else (none)))

(defn captured [k] (fn ([] k) ([a] [k a]) ([a b & r] [k a b r])))
(defn captured-ref [k]
  (fn [all]
    (case (count all)
      0 k
      1 [k (all 0)]
      (let [[a b & r] all] [k a b r]))))

(println 'max (check max #(reduce max %) scattered))
(println 'min (check min #(reduce min %) scattered))
(println 'max-key (check max-key (fn [[k & ms :as all]] (if (seq all) (reduce #(max-key k %1 %2) ms) (none))) keyed))
(println 'min-key (check min-key (fn [[k & ms :as all]] (if (seq all) (reduce #(min-key k %1 %2) ms) (none))) keyed))
(println 'distinct? (check distinct? #(if (seq %) (= (count (set %)) (count %)) (none)) nums)
         (check distinct? #(if (seq %) (= (count (set %)) (count %)) (none)) halves))
(println 'str (check str #(reduce (fn [s x] (str s x)) "" %) nums))
(println '+ (check + #(reduce + 0 %) nums))
(println 'merge (check merge #(when (seq %) (reduce conj {} %)) small-maps))
(println 'concat (check concat #(reduce into [] %) pairs))
(println 'uv (check uv uv-ref nums))
(println 'um (check um um-ref nums) (check #'um um-ref nums) (check (with-meta um {:m 1}) um-ref nums))
(println 'ug (check ug ug-ref nums))
(println 'u20 (check u20 u20-ref nums))
(println 'captured (check (captured :k) (captured-ref :k) nums))
(println 'partial (check (partial max 500) #(reduce max 500 %) scattered)
         (check (partial um 1) #(um-ref (into [1] %)) nums)
         (check (partial um 1 2 3 4) #(um-ref (into [1 2 3 4] %)) nums))

(println (apply max (range 22)) (apply min 5 4 (range 3 100)) (apply distinct? (range 30)) (apply distinct? 1 (range 30))
         (:i (apply max-key :a (map (fn [i] {:a i :i i}) (range 21)))) (apply str (range 25)))
(println (apply um (range 22)) (apply ug 1 2 3 4 (range 5 30)))
(println (um 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22) (ug 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22))
(println (err ug 1 2) "|" (err ug))
