;; @ai-generated(guided)
;; Clojure's clojure.template: the substitution behind clojure.test/are.
(ns clojure.template
  (:require [clojure.walk :as walk]))

(defn apply-template
  "For use in macros. argv is an argument list as in defn; expr is a quoted expression using the
  symbols in argv; values is a sequence of values for them. Returns expr with the values substituted."
  [argv expr values]
  (assert (vector? argv))
  (assert (every? symbol? argv))
  (walk/postwalk-replace (zipmap argv values) expr))

(defmacro do-template
  "Repeatedly copies expr (in a do block) for each group of arguments in values, substituting them for argv."
  [argv expr & values]
  (let [c (count argv)]
    `(do ~@(map (fn [a] (apply-template argv expr a)) (partition c values)))))
