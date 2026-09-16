;; @ai-generated(guided)
;; Clojure's clojure.walk; map entries are two-element vectors here, so they walk as vectors.
(ns clojure.walk)

(defn walk
  "Traverses form, applying inner to each element of a collection and outer to the result; outer to a leaf."
  [inner outer form]
  (cond
    (list? form) (outer (apply list (map inner form)))
    (seq? form) (outer (doall (map inner form)))
    (coll? form) (outer (into (empty form) (map inner form)))
    :else (outer form)))

(defn postwalk
  "Performs a depth-first, post-order traversal of form, replacing each sub-form with (f sub-form)."
  [f form]
  (walk (partial postwalk f) f form))

(defn prewalk
  "Like postwalk, but does pre-order traversal."
  [f form]
  (walk (partial prewalk f) identity (f form)))

(defn keywordize-keys
  "Recursively transforms all map keys from strings to keywords."
  [m]
  (let [f (fn [[k v]] (if (string? k) [(keyword k) v] [k v]))]
    (postwalk (fn [x] (if (map? x) (into {} (map f x)) x)) m)))

(defn stringify-keys
  "Recursively transforms all map keys from keywords to strings."
  [m]
  (let [f (fn [[k v]] (if (keyword? k) [(name k) v] [k v]))]
    (postwalk (fn [x] (if (map? x) (into {} (map f x)) x)) m)))

(defn prewalk-replace
  "Recursively transforms form by replacing keys in smap with their values, pre-order."
  [smap form]
  (prewalk (fn [x] (if (contains? smap x) (smap x) x)) form))

(defn postwalk-replace
  "Recursively transforms form by replacing keys in smap with their values, post-order."
  [smap form]
  (postwalk (fn [x] (if (contains? smap x) (smap x) x)) form))

(defn macroexpand-all
  "Recursively performs all possible macroexpansions in form."
  [form]
  (prewalk (fn [x] (if (seq? x) (macroexpand x) x)) form))
