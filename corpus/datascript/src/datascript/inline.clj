(ns datascript.inline
  (:refer-clojure :exclude [assoc update]))

(defn assoc
  {:inline
   (fn
     ([m k v]
      `(clojure.lang.RT/assoc ~m ~k ~v))
     ([m k v & kvs]
      (assert (even? (count kvs)))
      `(assoc (assoc ~m ~k ~v) ~@kvs)))}
  ;; pippin: clojure.core/assoc is RT.assoc; ex-info for the JVM exception constructor.
  ([map key val] (clojure.core/assoc map key val))
  ([map key val & kvs]
   (let [ret (clojure.core/assoc map key val)]
     (if kvs
       (if (next kvs)
         (recur ret (first kvs) (second kvs) (nnext kvs))
         (throw (ex-info
                  "assoc expects even number of arguments after map/vector, found odd number" {})))
       ret))))

(defn update
  {:inline
   (fn
     ([m k f & more]
      `(let [m# ~m k# ~k]
         (assoc m# k# (~f (get m# k#) ~@more)))))}
  ([m k f]
   (assoc m k (f (get m k))))
  ([m k f x]
   (assoc m k (f (get m k) x)))
  ([m k f x y]
   (assoc m k (f (get m k) x y)))
  ([m k f x y z]
   (assoc m k (f (get m k) x y z)))
  ([m k f x y z & more]
   (assoc m k (apply f (get m k) x y z more))))
