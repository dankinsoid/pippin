;; A redefined or rebound callee: the inlined body's guard fails and the call runs the var's new root.
(ns fixture.inline-rebind)

(defn sq [x] (* x x))
(defn use-sq [y] (sq y))
(println (use-sq 3) (with-redefs [sq (fn [x] (- x))] (use-sq 3)) (use-sq 3))
(defn sq [x] (+ x x))
(println (use-sq 3))
(alter-var-root #'sq (fn [_] (fn [x] (* 10 x))))
(println (use-sq 3))
