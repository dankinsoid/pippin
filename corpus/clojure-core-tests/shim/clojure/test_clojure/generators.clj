;; @ai-generated(solo)
;; Ours: stands in for Clojure's test_clojure/generators.clj, whose pools bind *rnd* to a java.util.Random.
(ns clojure.test-clojure.generators
  (:require [clojure.data.generators :as gen]))

;; No gen/symbol or gen/keyword: data_structures picks from this vector with core's rand-nth, and interning a
;; fresh name is permanent, so a differing pick would move the corpus's second-run live count (NOTES "Corpus").
(def ednable-scalars
  [(constantly nil) gen/byte gen/long gen/boolean gen/printable-ascii-char gen/string
   gen/uuid gen/date gen/ratio gen/bigint gen/bigdec])

(defn ednable-scalar [] (gen/call-through (gen/pick ednable-scalars)))

(def ednable-collections
  [[gen/vec [ednable-scalars]] [gen/set [ednable-scalars]] [gen/hash-map [ednable-scalars ednable-scalars]]])

(defn ednable-collection []
  (let [[coll args] (gen/pick ednable-collections)]
    (apply coll (map gen/pick args))))

(defn ednable [] (gen/one-of ednable-scalar ednable-collection))
