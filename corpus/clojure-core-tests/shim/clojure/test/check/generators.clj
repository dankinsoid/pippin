;; @ai-generated(solo)
;; Ours: a stand-in for org.clojure/test.check (SOURCE), with no shrinking and only the generators used.
(ns clojure.test.check.generators
  (:refer-clojure :exclude [boolean int])
  (:require [clojure.data.generators :as g]))

;; A generator is a fn of the size, so for-all needs nothing but a call.
(def int (fn [size] (g/uniform (- size) (inc size))))
(def nat (fn [size] (g/uniform 0 (inc size))))
(def s-pos-int (fn [size] (g/uniform 1 (inc size))))
(def pos-int (fn [size] (g/uniform 1 (inc size))))
(def boolean (fn [_] (g/boolean)))
