;; Literal fn arguments copied where an inlined callee calls them (design §10 step 7a): values, the order the
;; arguments run in, captures of the caller's locals, a callee calling an unknown value, traces naming both fns.
(ns fixture.inline-beta)

(defn app [f x] (f x))
(defn app2 [f x y] (f x y))
(defn twice [f x] (f (f x)))
(defn pick [p x y] (if (p x) x y))

(defn bump [m y] (update m :a (fn [x] (+ x y))))
(defn bump-inc [m] (update m :n inc))
(defn bump-fnil [m k v] (update m k (fnil conj #{}) v))
(defn bump4 [m] (update m :a (fn [x y] (* x y)) 3))
(defn first-big [xs t] (some (fn [x] (when (> x t) x)) xs))
(defn all-small [xs t] (every? (fn [x] (< x t)) xs))
(defn all-pos [xs] (every? pos? xs))
(defn locals [y]
  [(app (fn [x] (* x y)) 3) (app2 (fn [a b] (- a b y)) 10 2) (twice (fn [x] (conj x y)) []) (pick (fn [x] (odd? x)) 3 4)
   (app inc 1) (twice inc 1) (app (fn [x] (let [z (* x 2)] (if (> z y) [z y] z))) 2)])
(defn in-loop [xs] (loop [s (seq xs) acc []] (if s (recur (next s) (app (fn [x] (conj acc (* x (count acc)))) (first s))) acc)))
(defn nested [m] (update m :a (fn [x] (update x :b (fn [y] (* y 2))))))
(defn big [m]
  (update m :a (fn [x]
                 (let [a (inc x) b (dec x) c (* a b) d (+ c 1) e (- d 2) f (quot e 3)]
                   (if (pos? f) [a b c d e f] (list f e d c b a))))))
(defn order []
  (let [log (atom [])]
    [(app2 (fn [a b] (+ a b)) (do (swap! log conj :a) 1) (do (swap! log conj :b) 2))
     (try (app2 (fn [a b] (quot a b)) (do (swap! log conj :c) 1) (do (swap! log conj :d) 0)) (catch :default _ :thrown))
     (app2 (fn [a b] (swap! log conj [:body a b]) a) (do (swap! log conj :e) 5) (do (swap! log conj :f) 6))
     @log]))
(defn names [f] (try (f) (catch :default e (mapv :fn (take 3 (ex-trace e))))))
(defn app-boom [] (app (fn lit [x] (throw (ex-info "lit" {:x x}))) 1))
(defn bump-boom [] (update {:a 1} :a (fn lit2 [x] (throw (ex-info "lit2" {:x x})))))

(println (bump {:a 1} 2) (bump-inc {:n 1}) (bump-fnil {} :k 1) (bump-fnil {:k #{0}} :k 1) (bump4 {:a 2}))
(println (first-big [1 5 9] 4) (first-big [1 2] 4) (all-small [1 2] 3) (all-small [1 5] 3) (all-pos [1 2]) (all-pos []) (all-pos [1 -1]))
(println (locals 2) (locals 10) (in-loop [1 2 3]) (nested {:a {:b 3}}) (big {:a 5}) (big {:a 1}) (order))
(println (names app-boom) (names bump-boom))
