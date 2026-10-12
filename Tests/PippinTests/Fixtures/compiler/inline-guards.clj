;; The guards of the 7a shapes under a rebind: a callee taking a literal fn, a known path and each core var its
;; unrolling took away, the helpers a fused step stands for. A failed guard runs the program as written.
(ns fixture.inline-guards)

(defn app [f x] (f x))
(defn use-app [y] (app (fn [x] (+ x y)) 1))
(defn bump [m] (update m :a (fn [x] (* x 10))))
(defn ai [m] (assoc-in m [:a :b] 1))
(defn ui [m] (update-in m [:a] inc))
(defn gi [m] (get-in m [:a :b]))
(defn gi3 [m] (get-in m [:a :b] :nf))
(defn tk [xs] (into [] (take 3 (map (fn [x] (inc x)) xs))))
(defn kp [xs] (vec (keep (fn [x] (when (odd? x) x)) xs)))

(println (use-app 2) (with-redefs [app (fn [f x] [:redef (f x)])] (use-app 2)) (use-app 2))
(println (bump {:a 1}) (with-redefs [update (fn [m k f] [:redef (f 2)])] (bump {:a 1})) (bump {:a 1}))
(println (ai {}) (with-redefs [assoc-in (fn [m ks v] :redef)] (ai {})) (with-redefs [next (fn [_] nil)] (ai {})) (ai {}))
(println (ui {:a 1}) (with-redefs [apply (fn [f & _] :redef)] (ui {:a 1})) (ui {:a 1}))
(println (gi {:a {:b 2}}) (with-redefs [reduce (fn [f init coll] :redef)] (gi {:a {:b 2}})) (gi {:a {:b 2}}))
;; with-redefs restores its roots with doseq, so only a var it can still walk with is rebound here
(println (gi3 {:a {:b 2}}) (with-redefs [next (fn [_] nil)] (gi3 {:a {:b 2}})) (gi3 {:a {:b 2}}))
(println (tk [1 2 3 4 5]) (with-redefs [dec (fn [x] (- x 2))] (tk [1 2 3 4 5])) (with-redefs [pos? (fn [n] (> n 1))] (tk [1 2 3 4 5]))
         (tk [1 2 3 4 5]))
(println (kp [1 2 3]))
(defn app [f x] (f (f x)))
(println (use-app 2))
