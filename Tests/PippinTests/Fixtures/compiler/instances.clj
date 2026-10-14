;; (field* x i) with a constant i and (new* T ...) of a deftype read and build in place, behind the builtin's checks.
(ns fixture.instances)

(defprotocol Sum
  (sum [x]))

(deftype P [a b]
  Sum
  (sum [_] (+ a b)))
(defrecord R [a b]
  Sum
  (sum [_] (* a b)))

(defn fields-of [x] [(field* x 0) (field* x 1)])
(defn built [] [(sum (->P 1 2)) (sum (new* P 3 4)) (sum (->R 2 5)) (= (new* R 2 5) (->R 2 5)) (:a (new* R 7 8))])
(defn read-back [] [(fields-of (->P :a :b)) (fields-of (->R :c :d)) (fields-of (assoc (->R :e :f) :g 1))])
(defn refused []
  (mapv (fn [f] (try (f) (catch :default e (ex-message e))))
        [#(field* (->P 1 2) 2) #(field* {:a 1} 0) #(field* nil 0) #(new* P 1) #(new* R 1) #(new* 5 1) #(new* nil)]))

(println (built) (read-back))
(println (refused))

;; a redefinition with another field count: old instances keep their type, new ones get the new layout
(def old (->P 1 2))
(deftype P [a b c]
  Sum
  (sum [_] (+ a b c)))
(println (sum old) (fields-of old) (sum (->P 1 2 3)) (fields-of (->P 4 5 6)) (try (new* P 1 2) (catch :default e (ex-message e))))
