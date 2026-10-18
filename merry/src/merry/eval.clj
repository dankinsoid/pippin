;; @ai-generated(guided)
;; A tree-walking interpreter over elaborated forms; values are plain Clojure data.
(ns merry.eval
  (:require [merry.builtins :as b]))

(def ^:dynamic *globals* nil)

(deftype Thunk [f]
  clojure.lang.IDeref
  (deref [_] (f)))

(defrecord Recur [args])

(declare ev)

(defn- ev-body [env body]
  (reduce (fn [_ x] (ev env x)) nil body))

(defn- lookup [env sym]
  (if-let [e (find env sym)]
    (val e)
    (if-let [e (find @*globals* sym)]
      (val e)
      (or (b/impl sym) (throw (ex-info (str "unbound name " sym) {}))))))

(defn- make-fn [env form]
  (let [[_ & more] form
        [fname more] (if (symbol? (first more)) [(first more) (rest more)] [nil more])
        [params & body] more
        [fixed tail] (split-with #(not= '& %) params)
        fixed (vec fixed)
        rest-sym (second tail)]
    (letfn [(self [& args]
              (loop [args args]
                (let [env' (cond-> (into env (map vector fixed args))
                             rest-sym (assoc rest-sym (vec (drop (count fixed) args)))
                             fname (assoc fname self))
                      r (ev-body env' body)]
                  (if (instance? Recur r) (recur (:args r)) r))))]
      self)))

(defn- match-pattern
  "The bindings of pattern p against value v, or nil when it does not match."
  [p v]
  (letfn [(all [pairs]
            (reduce (fn [env [sp sv]]
                      (if-let [e (match-pattern sp sv)] (merge env e) (reduced nil)))
                    {}
                    pairs))]
    (cond
      (= '_ p) {}
      (symbol? p) {p v}
      (vector? p) (when (and (vector? v) (= (count p) (count v)) (= (first p) (first v)))
                    (all (map vector (rest p) (rest v))))
      (map? p) (when (and (map? v) (every? #(contains? v %) (keys p)))
                 (all (map (fn [[k sp]] [sp (get v k)]) p)))
      :else (when (= p v) {}))))

(defn- bind-pairs [env pairs]
  (reduce (fn [e [s v]] (assoc e s (ev e v))) env pairs))

(defn ev [env form]
  (cond
    (symbol? form) (lookup env form)
    (vector? form) (mapv #(ev env %) form)
    (map? form) (into {} (map (fn [[k v]] [k (ev env v)])) form)
    (seq? form)
    (let [[h & args] form]
      (case h
        if (if (ev env (first args)) (ev env (second args)) (ev env (nth args 2 nil)))
        do (ev-body env args)
        let (let [[bs & body] args] (ev-body (bind-pairs env (partition 2 bs)) body))
        (fn fn*) (make-fn env form)
        loop (let [[bs & body] args
                   pairs (partition 2 bs)
                   syms (mapv first pairs)]
               (loop [env' (bind-pairs env pairs)]
                 (let [r (ev-body env' body)]
                   (if (instance? Recur r) (recur (into env' (map vector syms (:args r)))) r))))
        recur (->Recur (mapv #(ev env %) args))
        merry/thunk (let [x (first args)] (->Thunk (fn [] (ev env x))))
        merry/some-let (let [[[g x] body] args
                             v (ev env x)]
                         (when (some? v) (ev (assoc env g v) body)))
        clojure.core/deref (deref (ev env (first args)))
        merry/match (let [[x & branches] args
                          v (ev env x)]
                      ;; a one-element vector, so that a branch returning nil or false still counts as matched
                      (if-let [[r] (some (fn [[p body]]
                                           (when-let [binds (match-pattern p v)]
                                             [(ev (merge env binds) body)]))
                                         branches)]
                        r
                        (throw (ex-info (str "no branch matched " (pr-str v)) {}))))
        quote (first args)
        (if (keyword? h)
          (get (ev env (first args)) h)
          (apply (ev env h) (mapv #(ev env %) args)))))
    :else form))

(defn eval-top [form]
  (if (and (seq? form) (= 'defn (first form)))
    (let [[_ name & more] form]
      (swap! *globals* assoc name (make-fn {} (list* 'fn name more))))
    (if (and (seq? form) (= 'def (first form)))
      (let [[_ name v] form]
        (swap! *globals* assoc name (ev {} v)))
      (ev {} form))))

(defn global [sym] (get @*globals* sym))
