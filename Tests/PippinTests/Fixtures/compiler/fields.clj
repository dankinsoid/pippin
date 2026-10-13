;; The JVM spellings corpus/datascript writes against its own types: .-field, new, clojure.lang.* names, statics.
(ns fixture.fields)

(defprotocol Label
  (-label [x])
  (area [x]))

(deftype Pt [x max-y]
  clojure.lang.ILookup
  (valAt [_ k] (if (= k :x) x max-y))
  (valAt [_ k nf] (if (= k :x) x nf))
  Label
  (-label [_] "pt")
  (area [_] (* x max-y)))

(defrecord Box [w h])

(defn fields [] (let [p (->Pt 2 3) b (->Box 4 5)] [(.-x p) (.-max-y p) (.-w b) (.-h (assoc b :h 6)) (:x p) (get p :y :nf)]))
;; a dashed name with no such field is the protocol method
(defn methods-still [] (let [p (new Pt 2 3)] [(.-label p) (.label p) (.area p)]))
;; a reify's slots are its method closures, named after the methods: they are no fields to read
(defn reified [] (let [r (reify Label (-label [_] "r") (area [_] 1))] [(.-label r) (try (.-area r) (catch :default _ :no-field))]))
(defn constructed [] [(area (new Pt 5 6)) (= (new Box 1 2) (->Box 1 2)) (special-symbol? 'new)])
(defn interfaces [] [(instance? clojure.lang.ILookup (->Pt 1 1)) (instance? clojure.lang.IPersistentMap {}) (instance? clojure.lang.Seqable [])])
(defn statics [] [(Integer/compare 3 7) (Integer/compare 7 7) (Integer/compare 9 2) (Math/sqrt 16) (Math/sqrt 2.25)])

;; into reduces its source, as the JVM's does: a reify of IReduceInit alone has no seq
(defn reducible [] (let [r (reify clojure.lang.IReduceInit (reduce [_ f start] (reduce f start (range 4))))] [(into [] r) (vec r) (count (into #{} r))]))
;; syntax-quote keeps a name with a dot past its first character, a class's or a namespace's
(defmacro dotted [] `'[a.b.C clojure.lang.ILookup a.b.C. ~'x])

(println (fields) (methods-still) (reified))
(println (constructed) (interfaces) (statics))
(println (reducible) (dotted))
