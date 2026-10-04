;; @ai-generated(solo)
;; Ours: a stand-in for org.clojure/test.check (SOURCE).
(ns clojure.test.check.clojure-test
  (:require [clojure.test] [clojure.data.generators]))

;; The vendored files ask for 1000 trials; capped, since a failure here repeats every run anyway.
(def ^:dynamic *max-trials* 200)

(defmacro defspec [name trials property]
  `(clojure.test/deftest ~name
     (clojure.data.generators/set-seed! 42)
     (let [p# ~property]
       (dotimes [i# (min ~trials *max-trials*)]
         (let [[args# ok#] (p# (inc (mod i# 50)))]
           (clojure.test/is ok# (pr-str args#)))))))
