;; @ai-generated(solo)
;; Ours: a stand-in for org.clojure/test.check (SOURCE).
(ns clojure.test.check.properties)

;; A property is a fn of the size returning [args result], which is all clojure-test/defspec reports.
(defmacro for-all [bindings & body]
  (let [pairs (partition 2 bindings)
        names (mapv first pairs)
        size 'size]
    `(fn [~size]
       (let [~@(mapcat (fn [[n g]] [n (list g size)]) pairs)]
         [~names (do ~@body)]))))
