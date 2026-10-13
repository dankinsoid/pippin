;; @ai-generated(solo)
;; A dev unit clj-compile built over the interpreted core copies max-key into pick and mod into the unit once, which m7
;; calls; over a compiled core each copy runs while its var holds the fn compiled from the same tree, and the call runs
;; otherwise.
(ns dev-inline.core)

(defn m7 [x] (mod x 7))
(defn pick [a b] (max-key count a b))

(println (m7 10) (m7 -3) (pick [1] [1 2]))
(println (with-redefs [mod (fn [a b] [:redef a b])] (m7 10)) (m7 10))

(def orig max-key)
(alter-var-root #'max-key (constantly (fn [k x y] [:altered (k x) (k y)])))
(println (pick [1] [1 2]))

(binding [*ns* (the-ns 'clojure.core)]
  (eval '(defn max-key ([k x] x) ([k x y] [:evaled (k x) (k y)]))))
(println (pick [1] [1 2]))

(alter-var-root #'max-key (constantly orig))
(println (pick [1] [1 2]))
