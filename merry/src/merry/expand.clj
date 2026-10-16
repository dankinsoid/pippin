;; @ai-generated(guided)
;; `$` is a direct argument only (docs/merry.md «Макросы»): nested, it would be ambiguous, as in magrittr.
(ns merry.expand
  (:require [merry.types :as t]))

(def ^:dynamic *n* nil)

(defn- fail [form msg] (t/fail msg :line (:line (meta form))))

(defn- fresh-name [] (symbol (str "$" (swap! *n* inc))))

(def ^:private threading '#{-> ->> some-> some->> cond-> cond->>})

(defn- nested-$?
  "Whether `$` occurs in x; an inner threading form owns the `$` of its steps, not of its value."
  [x]
  (boolean
    (cond
      (= '$ x) true
      (and (seq? x) (threading (first x))) (nested-$? (second x))
      (map? x) (some nested-$? (mapcat identity x))
      (coll? x) (some nested-$? x)
      :else false)))

(defn- keep-meta [form x]
  (if (instance? clojure.lang.IObj x) (vary-meta x #(merge (meta form) %)) x))

(defn- thread-step [form x step pos]
  (cond
    (= '$ step) (fail form "`$` alone is not a step")
    (or (symbol? step) (keyword? step)) (keep-meta form (list step x))
    (seq? step)
    (let [[h & args] step]
      (when (= '$ h) (fail step "`$` cannot be the head of a step"))
      (when (some #(and (not= '$ %) (nested-$? %)) args)
        (fail step "`$` must be a direct argument of the step; inside a nested form it is ambiguous"))
      (let [n (count (filter #(= '$ %) args))]
        (keep-meta step
          (cond
            (zero? n) (if (= pos :first) (list* h x args) (apply list h (concat args [x])))
            (or (= 1 n) (symbol? x) (not (coll? x))) (apply list h (replace {'$ x} args))
            :else (let [g (fresh-name)]
                    (list 'let [g x] (apply list h (replace {'$ g} args))))))))
    :else (fail form (str "a threading step is a call, a symbol or a keyword: " (pr-str step)))))

(defn- thread [pos]
  (fn [form x & steps]
    (reduce #(thread-step form %1 %2 pos) x steps)))

(defn- some-thread [pos]
  (fn some-thread [form x & steps]
    (if (empty? steps)
      x
      (let [g (fresh-name)
            nxt (thread-step form g (first steps) pos)]
        (keep-meta form (list 'merry/some-let [g x]
                              (if (next steps) (apply some-thread form nxt (rest steps)) nxt)))))))

(defn- cond-thread [pos]
  (fn [form x & clauses]
    (when (odd? (count clauses)) (fail form "cond-> takes test/step pairs"))
    (when (some (comp nested-$? first) (partition 2 clauses))
      (fail form "`$` in a cond-> test is an idea under discussion (docs/merry.md), not supported"))
    (let [g (fresh-name)]
      (keep-meta form
        (list 'let (into [g x] (mapcat (fn [[test step]] [g (list 'if test (thread-step form g step pos) g)])
                                       (partition 2 clauses)))
              g)))))

(defn- cond* [form & clauses]
  (when (odd? (count clauses)) (fail form "cond takes test/expression pairs"))
  (let [pairs (partition 2 clauses)
        n (count pairs)]
    (doseq [[i [test _]] (map-indexed vector pairs)]
      (when (= :else test) (fail form "the default branch is `_`, not :else"))
      (when (and (= '_ test) (< i (dec n))) (fail form "`_` must be the last test")))
    (reduce (fn [else [test then]]
              (if (= '_ test) then (keep-meta form (list 'if test then else))))
            nil
            (reverse pairs))))

(def ^:private macros
  {'-> (thread :first)
   '->> (thread :last)
   'some-> (some-thread :first)
   'some->> (some-thread :last)
   'cond-> (cond-thread :first)
   'cond->> (cond-thread :last)
   'cond cond*})

(declare expand)

(defn- expand-fn-like
  "fn, fn* and defn: only the body expands, the header and :- annotations stay data."
  [form]
  (let [[before after] (split-with (complement vector?) form)
        [params & tail] after
        [ret body] (if (= :- (first tail)) [(take 2 tail) (drop 2 tail)] [nil tail])]
    (when-not (vector? params) (fail form (str (first form) " takes a parameter vector")))
    (keep-meta form (apply list (concat before [params] ret (mapv expand body))))))

(defn expand [form]
  (cond
    (seq? form)
    (let [h (first form)]
      (cond
        (and (symbol? h) (contains? macros h)) (expand (keep-meta form (apply (macros h) form (rest form))))
        ('#{quote comment ns} h) form
        ('#{fn fn* defn} h) (expand-fn-like form)
        ('#{let loop} h) (let [[_ bs & body] form]
                           (when-not (and (vector? bs) (even? (count bs)))
                             (fail form (str h " takes a vector of name/value pairs")))
                           (keep-meta form (list* h
                                                  (vec (mapcat (fn [[s v]] [s (expand v)]) (partition 2 bs)))
                                                  (mapv expand body))))
        :else (keep-meta form (apply list (mapv expand form)))))
    (vector? form) (mapv expand form)
    (map? form) (into {} (map (fn [[k v]] [(expand k) (expand v)])) form)
    :else form))

(defn expand-top [form]
  (binding [*n* (atom 0)]
    (expand form)))
