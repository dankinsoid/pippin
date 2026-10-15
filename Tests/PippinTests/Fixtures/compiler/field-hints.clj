;; A hinted field is cast or checked at construction; a method body reads it with the hint's fact.
(ns fixture.field-hints)

(defprotocol Reader
  (read-at [r i])
  (size [r])
  (text-count [r]))

(deftype Text [^String s ^long n]
  Reader
  (read-at [_ i] (int (nth s i)))
  (size [_] (+ n 1))
  (text-count [_] (count s)))

(deftype Prims [^int i ^short sh ^byte b ^double d ^float f ^boolean z ^char c])

(defrecord Point [^long x ^double y ^String label])

(defn err [f] (try (f) (catch :default e (ex-message e))))
(defn thrown [f] (try (f) :none (catch :default _ :thrown)))
(defn prims [^Prims p] (pr-str [(field* p 0) (field* p 1) (field* p 2) (field* p 3) (field* p 4) (field* p 5) (field* p 6)]))
(defn point [p] (pr-str ((juxt :x :y :label) p)))

(println (read-at (->Text "héllo" 5) 1) (read-at (->Text "abc" 3) 2) (size (->Text "abc" 3)) (size (Text. nil 2.9))
         (text-count (->Text "héllo" 5)) (text-count (->Text nil 0)))
(println (thrown #(read-at (->Text "ab" 2) 5)) (err #(read-at (->Text nil 0) 0)))
(println (err #(->Text :k 1)) (err #(->Text "a" "b")) (err #(new* Text 'sym 1)))
(println (prims (->Prims 7 300 -5 1 2.5 nil 97)) (prims (->Prims \a 1 1 1.5 1 0 \z)))
(println (err #(->Prims 3000000000 1 1 1 1 true \a)) (err #(->Prims 1 1 1 1 1 true 'x)) (err #(->Prims 1 1 1 "d" 1 true \a)))
(println (point (->Point 1 2 "a")) (point (->Point 2.9 3.0 nil)) (point (map->Point {:x 4 :y 5 :label "m"})))
(println (point (assoc (->Point 1 2 "a") :x 7.5)) (point (conj (->Point 1 2 "a") [:y 4])) (:extra (assoc (->Point 1 2 "a") :extra 1)))
(println (err #(->Point nil 1 "a")) (err #(map->Point {:y 2 :label "q"})) (err #(assoc (->Point 1 2 "a") :label 5))
         (err #(map->Point (sorted-map :x 1 :y 2 :label :k))))
