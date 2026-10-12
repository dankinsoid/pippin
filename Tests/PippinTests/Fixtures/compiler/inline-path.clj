;; get-in, assoc-in and update-in over a literal path, unrolled at the site (design §10 step 7a): every length, a
;; missing step, not-found, a reduced value get-in's reduce unwraps, f and its arguments, the order, the traces.
(ns fixture.inline-path)

(def m1 {:a {:b {:c {:d 4}}} :r (reduced {:s 9}) :v [10 20] :n nil})
(defn gets [m k]
  [(get-in m [:a]) (get-in m [:a :b]) (get-in m [:a k :c]) (get-in m [:a :b :c :d]) (get-in m [:x :y] :nf) (get-in m [:a :b] :nf)
   (get-in m [k] :nf) (get-in nil [:a]) (get-in m [:r :s]) (get-in m [:v 1]) (get-in m [:n] :nf) (get-in m [:n :x] :nf)
   (get-in m [:a :b :c :d :e]) (get-in m [:a :b :c :d :e] :deep)])
(defn assocs [m k]
  [(assoc-in m [:a] 1) (assoc-in m [:a :b] 2) (assoc-in {} [:x k :z] 3) (assoc-in m [:v 0] :zero) (assoc-in nil [:a :b] 1)
   (assoc-in [] [0 :a] 1) (assoc-in {} [:a :b :c :d] 4) (assoc-in {} [:a :b :c :d :e] 5)])
(defn updates [m k]
  [(update-in m [:a :b] inc) (update-in m [:a k] + 10 20) (update-in m [:n] (fnil inc 0)) (update-in m [:a :b] (fn [x] (* x 100)))
   (update-in {} [:p :q :r :s] (fnil conj []) 1) (update-in m [:a] assoc :c 3) (update-in m [:a :b] - 1 2 3 4)
   (update-in m [:a :b] - 1 2 3 4 5) (update-in m [:a :b] (fn [x y] (+ x y 100)) 7) (update-in m [:a :b] :not-a-fn)])
(defn order []
  (let [log (atom [])
        note (fn [x v] (swap! log conj x) v)]
    [(update-in (note :m {:a {:b 1}}) [(note :k1 :a) (note :k2 :b)] (note :f +) (note :x 10))
     (assoc-in (note :m2 {}) [(note :k3 :c)] (note :v 5))
     (get-in (note :m3 {:c 1}) [(note :k4 :c)] (note :nf 0))
     (get-in (note :m4 {:c 2}) [(note :k5 :c)])
     @log]))
(defn names [f] (try (f) (catch :default e [(ex-message e) (mapv :fn (take 5 (ex-trace e)))])))
(defn assoc-boom [] (assoc-in {:a {:b 5}} [:a :b :c] 1))
(defn update-boom [] (update-in {} [:a :b] (fn boom [x] (throw (ex-info "boom" {:x x})))))
(defn update-nil [] (update-in {:a {:b nil}} [:a :b] inc))

(println (gets m1 :b) (gets {} :a))
(println (assocs {:a {:b 0} :v [1 2]} :y) (updates {:a {:b 1}} :b))
(println (order))
(println (names assoc-boom) (first (names update-boom)) (second (names update-boom)) (first (names update-nil)))
