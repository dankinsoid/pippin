(ns ^:no-doc me.tonsky.persistent-sorted-set.arrays
  (:require
    [clojure.string :as str])
  (:refer-clojure :exclude [make-array into-array array amap aget aset alength array? aclone])
  #?(:cljs (:require-macros me.tonsky.persistent-sorted-set.arrays))
  #?(:clj  (:import [java.util Arrays])))

;; pippin: the :clj branches below are this runtime's: object arrays and loops where the JVM's are Java
;; (Arrays, System/arraycopy, RT); corpus/datascript/SOURCE.


(defn- if-cljs [env then else]
  (if (:ns env) then else))


#?(:cljs
   (defn ^array make-array [size] (js/Array. size))
   :clj 
   (defn make-array [size]
     (clojure.core/object-array size)))


#?(:cljs
   (defn ^array into-array [aseq]
     (reduce (fn [a x] (.push a x) a) (js/Array.) aseq))
   :clj
   (defn into-array [aseq]
     (clojure.core/object-array aseq)))


#?(:clj
  (defmacro aget [arr i]
    (if-cljs &env
      (list 'js* "(~{}[~{}])" arr i)
     `(clojure.core/aget ~arr ~i))))


#?(:clj
  (defmacro alength [arr]
    (if-cljs &env
      (-> (list 'js* "~{}.length" arr)
          (vary-meta assoc :tag 'number))
     `(clojure.core/alength ~arr))))


#?(:clj
  (defmacro aset [arr i v]
    (if-cljs &env
      (list 'js* "(~{}[~{}] = ~{})" arr i v)
     `(clojure.core/aset ~arr ~i ~v))))


#?(:clj
  (defmacro array [& args]
    (if-cljs &env
      (->
        (list* 'js* (str "[" (str/join "," (repeat (count args) "~{}")) "]") args)
        (vary-meta assoc :tag 'array))
      (let [len (count args)]
        `(let [arr# (clojure.core/object-array ~len)]
           (doto arr#
             ~@(map #(list 'clojure.core/aset % (nth args %)) (range len))))))))


#?(:clj
  (defmacro acopy [from from-start from-end to to-start]
    (if-cljs &env
     `(let [l# (- ~from-end ~from-start)]
        (dotimes [i# l#]
          (aset ~to (+ i# ~to-start) (aget ~from (+ i# ~from-start)))))
     `(let [l# (- ~from-end ~from-start)]
        (dotimes [i# l#]
          (clojure.core/aset ~to (+ i# ~to-start) (clojure.core/aget ~from (+ i# ~from-start))))))))


(defn aclone [from]
  #?(:clj  (clojure.core/aclone from)
     :cljs (.slice from 0)))


(defn aconcat [a b]
  #?(:cljs (.concat a b)
     :clj  (let [al  (alength a)
                 bl  (alength b)
                 res (clojure.core/object-array (+ al bl))]
             (acopy a 0 al res 0)
             (acopy b 0 bl res al)
             res)))


#?(:cljs
   (defn amap [f arr]
     (.map arr f))
   :clj
   (defn amap 
     ([f arr]
      (amap f :object arr))
     ([f type arr] ;; TODO check if faster in Java
      (let [res (clojure.core/make-array type (alength arr))]
        (dotimes [i (alength arr)]
          (aset res i (f (aget arr i))))
        res))))


(defn asort [arr cmp]
  #?(:cljs (.sort arr cmp)
     :clj  (let [sorted (vec (sort cmp arr))]
             (dotimes [i (alength arr)]
               (aset arr i (nth sorted i)))
             arr)))


#?(:cljs
   (defn ^boolean array? [x]
     (if (identical? *target* "nodejs")
       (.isArray js/Array x)
       (instance? js/Array x)))
   :clj
   (do
     (def ^:private object-array-type (type (clojure.core/object-array 0)))
     (defn array? [x]
       (identical? object-array-type (type x)))))


#?(:clj
   (defmacro alast [arr]
     `(let [arr# ~arr]
        (aget arr# (dec (alength arr#))))))


#?(:clj
   (defmacro half [x]
     `(unsigned-bit-shift-right ~x 1)))


#?(:clj
  (def array-type
    (memoize
      (fn [type]
        (clojure.core/type (clojure.core/make-array type 0))))))
