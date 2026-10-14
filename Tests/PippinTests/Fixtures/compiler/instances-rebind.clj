;; A rebound field* or new* takes the call at a site that would read or build the instance in place.
(ns fixture.instances-rebind)

(deftype P [a b])

(defn second-of [x] (field* x 1))
(defn make [a b] (new* P a b))

(defn rebound []
  [(with-redefs [field* (fn [x i] [:field i])] (second-of (->P 1 2)))
   (with-redefs [new* (fn [t & xs] (vec xs))] (make 3 4))
   (second-of (make 5 6))])

(println (rebound))
