;; Protocols, types, records, reify, multimethods and hierarchies: registration order across top-level forms.
(ns fixture.types (:require [clojure.string :as str]))

(defprotocol Shape (area [s]) (label [s] [s prefix]))
(deftype Circle [r] Shape (area [_] (* 3 r r)) (label [_] "circle") (label [_ p] (str p "circle")))
(defrecord Rect [w h] Shape (area [_] (* w h)) (label [_] "rect") (label [_ p] (str p "rect")))
(extend-type nil Shape (area [_] 0) (label [_] "nothing") (label [_ p] (str p "nothing")))
(extend-protocol Shape Long (area [n] n) (label [n] (str "long " n)) (label [n p] (str p n)))

(defmulti speak (fn [x] (:kind x)))
(defmethod speak :dog [_] "woof")
(defmethod speak :default [x] (str "?" (:kind x)))
(derive ::puppy ::dog)
(defmulti tagged (fn [x] x))
(defmethod tagged ::dog [_] :dog-method)

(defn shapes [] [(area (->Circle 2)) (label (->Circle 1) "a ") (area (->Rect 2 3)) (:w (map->Rect {:w 4 :h 5})) (label nil) (area 7) (label 7 "n=") (satisfies? Shape 1) (satisfies? Shape "s")])
(defn multis [] [(speak {:kind :dog}) (speak {:kind :cat}) (tagged ::puppy) (isa? ::puppy ::dog) (parents ::puppy)])
(defn reified [] (let [r (reify Shape (area [_] 99) (label [_] "reified") (label [_ p] (str p "reified")))] [(area r) (label r) (label r ">")]))
(defn record-ops [] (let [r (->Rect 1 2)] [(= r (->Rect 1 2)) (assoc r :w 9) (dissoc r :w) (:h r) (record? r) (instance? Rect r) (pr-str r) (str/upper-case (label r))]))

(println (shapes) (multis) (reified) (record-ops))
(println (let [c (->Circle 3)] [(area c)]) (type (->Circle 1)) (map area [1 2 nil]) (methods speak) (str/join "," (map label [nil 1])))
;; The global hierarchy is process state other tests baseline against.
(underive ::puppy ::dog)
