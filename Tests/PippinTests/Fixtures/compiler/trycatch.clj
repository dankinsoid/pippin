;; TRY and THROW: catch kinds, finally on both paths, rethrow, nested, throw of non-errors, throw inside finally.
(ns fixture.trycatch)

(def log (atom []))
(defn note [x] (swap! log conj x) x)

(defn t1 [] (try (note :body) (catch :default e :caught) (finally (note :finally))))
(defn t2 [] (try (throw (ex-info "boom" {:k 1})) (catch ExceptionInfo e [(ex-message e) (ex-data e)]) (finally (note :f2))))
(defn t3 [] (try (throw "a string") (catch ExceptionInfo e :wrong) (catch :default e [:default e])))
(defn t4 [] (try (try (throw (ex-info "inner" {})) (finally (note :inner-finally))) (catch :default e [:outer (ex-message e)])))
(defn t5 [] (try (try (throw (ex-info "x" {})) (catch :default e (throw (ex-info "rethrown" {:cause (ex-message e)})))) (catch :default e (ex-data e))))
(defn t6 [] (try (try 1 (finally (throw (ex-info "from finally" {})))) (catch :default e (ex-message e))))
(defn t7 [] (try (throw (ex-info "lost" {})) (catch :default e (throw (ex-info "replaced" {}))) (catch :default e2 :never)))
(defn t8 [] (try (t7) (catch :default e (ex-message e))))
(defn t9 [] (try (throw 42) (catch :default e (+ e 1))))
(defn t10 [] (let [r (try (throw (ex-info "e" {:n 1})) (catch :default e (:n (ex-data e))))] (* r 100)))
(defn t11 [] (try (throw (ex-info "outer" {} (ex-info "cause" {}))) (catch :default e (ex-message (ex-cause e)))))
(defn t12 [] (try (loop [i 0] (if (< i 3) (recur (inc i)) (throw (ex-info (str "at " i) {})))) (catch :default e (ex-message e))))
(defn t13 [x] (try (if x (throw (ex-info "thrown" {})) :no-throw) (catch :default e :thrown) (finally (note [:finally x]))))

(println (t1) (t2) (t3) (t4) (t5) (t6) (t8) (t9) (t10) (t11) (t12) (t13 true) (t13 false))
(println @log)
(println (try (/ 1 0) (catch :default e (ex-message e))) (try (nth [1] 5) (catch :default e (ex-message e))) (try (throw nil) (catch :default e e)))
(println (map (fn [x] (try (if (odd? x) (throw (ex-info "odd" {:x x})) x) (catch :default e (:x (ex-data e))))) (range 5)))
