;; The guards of the 7a shapes under a rebind: a callee taking a literal fn, a known path, a fused step's helpers.
;; The fixture runs beside other suites, so it rebinds no core var they lean on; VerifierTests' exit tests do.
(ns fixture.inline-guards)

(defn app [f x] (f x))
(defn use-app [y] (app (fn [x] (+ x y)) 1))
(defn gi [m] (get-in m [:a :b]))
(defn rm [xs] (vec (remove (fn [x] (odd? x)) xs)))
(defn tk [xs] (into [] (take 3 (map (fn [x] (inc x)) xs))))

(println (use-app 2) (with-redefs [app (fn [f x] [:redef (f x)])] (use-app 2)) (use-app 2))
(println (gi {:a {:b 2}}) (with-redefs [unreduced (fn [_] :redef)] (gi {:a {:b 2}})) (gi {:a {:b 2}}))
(println (rm [1 2 3]) (with-redefs [complement (fn [f] (fn [x] true))] (rm [1 2 3])) (rm [1 2 3]))
(println (tk [1 2 3 4 5]) (with-redefs [ensure-reduced (fn [x] x)] (tk [1 2 3 4 5])) (tk [1 2 3 4 5]))
(defn app [f x] (f (f x)))
(println (use-app 2))
