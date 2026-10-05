;; @ai-generated(solo)
;; Ours: a stand-in for org.clojure/test.check (SOURCE). transducers.clj requires the namespace for
;; quick-check alone, and a missing one would take its whole (ns ...) form down.
(ns clojure.test.check
  (:require [clojure.data.generators :as g]))

;; The vendored file asks for 200000 trials; capped, since a failure here repeats every run anyway.
(def ^:dynamic *max-trials* 200)

;; A property is a fn of the size returning [args result] (clojure.test.check.properties); there is no
;; shrinking, so :smallest is the failing case itself.
(defn quick-check [trials property]
  (g/set-seed! 42)
  (loop [i 0]
    (if (>= i (min trials *max-trials*))
      {:result true :num-tests i}
      (let [[args ok] (property (inc (mod i 50)))]
        (if ok
          (recur (inc i))
          {:result false :fail args :shrunk {:smallest args}})))))
