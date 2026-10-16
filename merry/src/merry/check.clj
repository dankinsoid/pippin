;; @ai-generated(guided)
;; Inference and elaboration: contracts with effect rows, :thunk parameters wrapped at the call site,
;; the chain of calls behind an effect a closed row refuses.
(ns merry.check
  (:require [clojure.string :as str]
            [merry.builtins :as b]
            [merry.types :as t]))

(def ^:dynamic *globals* nil)
(def ^:dynamic *builtins* nil)

(declare infer)

(defn- line-of [form] (:line (meta form)))

(defn- err [form msg] (t/fail msg :line (line-of form)))

(defn- fn-form? [x] (and (seq? x) ('#{fn fn*} (first x))))

(defn- builtin-type [sym]
  (when-let [s (get-in b/table [sym :type])]
    (let [sc (or (get @*builtins* sym)
                 (let [sc (t/generalize #{} (t/parse-schema s (atom {}) :closed))]
                   (swap! *builtins* assoc sym sc)
                   sc))
          ty (t/instantiate sc)]
      (cond-> ty (= :fn (:tag ty)) (update :eff t/open-up)))))

(defn- lookup [ctx sym]
  (cond
    (= '$ sym) (err sym "`$` is a direct argument of a threading step (->, ->>, some->, cond->), not a name")
    (contains? (:locals ctx) sym) (t/instantiate (get-in ctx [:locals sym]))
    (contains? @*globals* sym) (t/instantiate (:scheme (get @*globals* sym)))
    :else (or (builtin-type sym) (err sym (str "unknown name " sym)))))

(defn- env-ids [ctx]
  (into (set (keys (t/free-vars (:eff ctx))))
        (mapcat (fn [{:keys [vars type]}] (remove vars (keys (t/free-vars type)))))
        (vals (:locals ctx))))

(defn- unify-arg
  "A value goes where [:maybe T] is expected without wrapping; elsewhere plain unification."
  [expected actual]
  (let [e (t/prune expected) a (t/prune actual)]
    (if (and (t/maybe? e) (not (t/maybe? a)) (not (t/tv? a)))
      (when-not (t/con? a :nil) (t/unify (first (:args e)) a))
      (t/unify e a))))

(defn- perform! [ctx eff]
  (t/unify-row (t/open-up eff) (:eff ctx) :effect))

(defn- infer-body [ctx body]
  (if (empty? body)
    [(t/tcon :nil) []]
    (let [res (mapv #(infer ctx %) body)]
      [(first (peek res)) (map second res)])))

(defn- take-schema
  "After a parameter: nothing, `:- T`, or `:- {props} T` (docs/merry.md «Аргументы»)."
  [form xs]
  (if (= :- (first xs))
    (let [[_ a & more] xs]
      (if (map? a)
        (let [[s & more] more]
          (when (nil? s) (err form "a schema follows the property map"))
          [{:props a :schema s} more])
        [{:schema a} more]))
    [nil xs]))

(defn- parse-params [form params]
  (loop [xs (seq params) fixed []]
    (if-not xs
      {:fixed fixed :rest nil}
      (let [[x & more] xs]
        (cond
          (= '& x) (let [[sym & more] more]
                     (when-not (symbol? sym) (err form "`&` takes a name"))
                     (let [[schema more] (take-schema form more)]
                       (when (seq more) (err form "nothing follows the `&` parameter"))
                       {:fixed fixed :rest (assoc schema :sym sym)}))
          (symbol? x) (let [[schema more] (take-schema form more)]
                        (recur (seq more) (conj fixed (assoc schema :sym x))))
          :else (err form (str "destructuring in parameters arrives with patterns: " (pr-str x))))))))

(defn- param-type [info names]
  (if-let [s (:schema info)]
    (let [ty (t/parse-schema s names :open)]
      (if (and ((some-fn :thunk :delay) (:props info)) (not= :thunk (:tag (t/prune ty))))
        (t/tthunk ty (t/open-row))
        ty))
    (t/tvar)))

(defn- infer-fn [ctx form]
  (let [[_ & more] form
        [fname more] (if (symbol? (first more)) [(first more) (rest more)] [nil more])
        [params & more] more
        _ (when-not (vector? params) (err form "fn takes a parameter vector"))
        [ret-schema body] (if (= :- (first more)) [(second more) (drop 2 more)] [nil more])
        {:keys [fixed rest]} (parse-params form params)
        names (atom {})
        ptypes (mapv #(param-type % names) fixed)
        rest-elem (when rest
                    (if-let [s (:schema rest)]
                      (let [ty (t/prune (t/parse-schema s names :open))]
                        (if (and (= :app (:tag ty)) (= :vector (:name ty)))
                          (first (:args ty))
                          (err form "the `&` parameter is a [:vector T]")))
                      (t/tvar)))
        ret (if ret-schema (t/parse-schema ret-schema names :open) (t/tvar))
        eff (t/open-row)
        self (t/tfn ptypes rest-elem ret eff)
        locals (cond-> (into (:locals ctx) (map (fn [p ty] [(:sym p) (t/mono ty)]) fixed ptypes))
                 rest (assoc (:sym rest) (t/mono (t/tapp :vector rest-elem)))
                 fname (assoc fname (t/mono self)))
        [tb body'] (infer-body (assoc ctx :locals locals :eff eff :loop ptypes) body)]
    (unify-arg ret tb)
    [self (with-meta (apply list 'fn (concat (when fname [fname])
                                             [(cond-> (mapv :sym fixed) rest (into ['& (:sym rest)]))]
                                             body'))
            (meta form))]))

(defn- infer-if [ctx form]
  (when-not (#{3 4} (count form)) (err form "if takes a test, a then and an optional else"))
  (let [[_ c a b] form
        [tc c'] (infer ctx c)
        _ (t/unify (t/tcon :bool) tc)
        [ta a'] (infer ctx a)
        [tb b'] (infer ctx b)
        ;; only the literal (if c x nil) makes a union (docs/merry.md «nil и умолчания»)
        ty (cond
             (nil? b) (t/tmaybe ta)
             (nil? a) (t/tmaybe tb)
             (t/maybe? ta) (do (unify-arg ta tb) ta)
             (t/maybe? tb) (do (unify-arg tb ta) tb)
             :else (do (t/unify ta tb) ta))]
    [ty (with-meta (list 'if c' a' b') (meta form))]))

(defn- infer-let [ctx form]
  (let [[_ bindings & body] form
        [ctx' out]
        (reduce (fn [[c out] [sym v]]
                  (when-not (symbol? sym) (err form "destructuring in let arrives with patterns"))
                  (let [[tv v'] (infer c v)
                        ;; a syntactic value restriction: only fn values generalize
                        s (if (fn-form? v) (t/generalize (env-ids c) tv) (t/mono tv))]
                    [(assoc-in c [:locals sym] s) (conj out sym v')]))
                [ctx []]
                (partition 2 bindings))
        [tb body'] (infer-body ctx' body)]
    [tb (with-meta (list* 'let out body') (meta form))]))

(defn- infer-loop [ctx form]
  (let [[_ bindings & body] form
        [ctx' out types]
        (reduce (fn [[c out types] [sym v]]
                  (when-not (symbol? sym) (err form "destructuring in loop arrives with patterns"))
                  (let [[tv v'] (infer c v)]
                    [(assoc-in c [:locals sym] (t/mono tv)) (conj out sym v') (conj types tv)]))
                [ctx [] []]
                (partition 2 bindings))
        [tb body'] (infer-body (assoc ctx' :loop types) body)]
    [tb (with-meta (list* 'loop out body') (meta form))]))

(defn- infer-recur [ctx form]
  (let [args (rest form)
        lt (:loop ctx)]
    (when-not lt (err form "recur outside loop or fn"))
    (when-not (= (count args) (count lt))
      (err form (str "recur expects " (count lt) " arguments, got " (count args))))
    (let [args' (mapv (fn [ty a] (let [[ta a'] (infer ctx a)] (unify-arg ty ta) a')) lt args)]
      [(t/tvar) (with-meta (list* 'recur args') (meta form))])))

(defn- infer-deref [ctx form]
  (let [[_ x] form
        [tx x'] (infer ctx x)
        th (t/tthunk (t/tvar) (t/open-row))]
    (t/unify th tx)
    (perform! ctx (:eff th))
    [(:type th) (with-meta (list 'clojure.core/deref x') (meta form))]))

(defn- infer-some-let [ctx form]
  (let [[_ [g x] body] form
        [tx x'] (infer ctx x)
        px (t/prune tx)
        tg (if (t/maybe? px) (first (:args px)) tx)
        [tb body'] (infer (assoc-in ctx [:locals g] (t/mono tg)) body)]
    [(t/tmaybe tb) (with-meta (list 'merry/some-let [g x'] body') (meta form))]))

(defn- infer-kw [ctx form]
  (let [[k m & more] form]
    (when (or (< (count form) 2) (seq more)) (err form (str "(" k " m) takes one record")))
    (let [[tm m'] (infer ctx m)
          ft (t/tvar)]
      (t/unify (t/trec (t/trow {k ft} (t/rvar))) tm)
      [ft (with-meta (list k m') (meta form))])))

(defn- infer-arg [ctx expected arg]
  (let [p (t/prune expected)]
    (cond
      (= :thunk (:tag p))
      (let [eff (t/open-row)
            [ta a'] (infer (assoc ctx :eff eff :loop nil) arg)]
        (t/unify p (t/tthunk ta eff))
        (list 'merry/thunk a'))

      ;; the expected type from above: a keyword where a function goes is its accessor
      (and (keyword? arg) (= :fn (:tag p)))
      (let [a (t/tvar)]
        (t/unify p (t/tfn [(t/trec (t/trow {arg a} (t/rvar)))] nil a (t/open-row)))
        arg)

      :else
      (let [[ta a'] (infer ctx arg)]
        (unify-arg p ta)
        a'))))

(defn- arity-str [f]
  (str (count (:params f)) (when (:rest f) " or more")))

(defn- infer-call [ctx form]
  (let [[head & args] form
        n (count args)
        [tf head'] (infer ctx head)
        f0 (t/prune tf)
        fresh-fn #(t/tfn (repeatedly n t/tvar) nil (t/tvar) (t/open-row))
        f (case (:tag f0)
            :fn f0
            :var (let [g (fresh-fn)] (t/unify f0 g) g)
            (if (t/con? f0 :error)
              (fresh-fn)
              (err form (str "calling a value of type " (t/show f0)))))
        callee (if (symbol? head) head "the function")]
    (when (or (< n (count (:params f))) (and (> n (count (:params f))) (nil? (:rest f))))
      (err form (str callee " expects " (arity-str f) " arguments, got " n)))
    (let [args' (vec (map-indexed (fn [i a] (infer-arg ctx (or (get (:params f) i) (:rest f)) a)) args))]
      (when (symbol? head)
        (swap! (:calls ctx) conj {:callee head
                                  :line (line-of form)
                                  :labels (set (keys (:labels (t/resolve-row (:eff f)))))}))
      (perform! ctx (:eff f))
      [(:ret f) (with-meta (apply list head' args') (meta form))])))

(defn- infer-vector [ctx form]
  (let [el (t/tvar)
        elab (mapv (fn [x] (let [[tx x'] (infer ctx x)] (t/unify el tx) x')) form)]
    [(t/tapp :vector el) elab]))

(defn- infer-map [ctx form]
  (doseq [k (keys form)]
    (when-not (keyword? k) (err form "map keys are keywords: a map literal is a record")))
  (let [entries (mapv (fn [[k v]] (let [[tv v'] (infer ctx v)] [k tv v'])) form)]
    [(t/trec (t/trow (into {} (map (fn [[k tv]] [k tv])) entries) nil))
     (into {} (map (fn [[k _ v']] [k v'])) entries)]))

(defn- infer* [ctx form]
  (cond
    (nil? form) [(t/tcon :nil) nil]
    (boolean? form) [(t/tcon :bool) form]
    (int? form) [(t/tcon :int) form]
    (double? form) [(t/tcon :double) form]
    (string? form) [(t/tcon :string) form]
    (keyword? form) [(t/tcon :keyword) form]
    (instance? java.util.regex.Pattern form) [(t/tcon :regex) form]
    (symbol? form) [(lookup ctx form) form]
    (vector? form) (infer-vector ctx form)
    (map? form) (infer-map ctx form)
    (set? form) (err form "set literals arrive later")
    (seq? form)
    (let [h (first form)]
      (case h
        if (infer-if ctx form)
        do (let [[ty body] (infer-body ctx (rest form))] [ty (with-meta (list* 'do body) (meta form))])
        let (infer-let ctx form)
        (fn fn*) (infer-fn ctx form)
        loop (infer-loop ctx form)
        recur (infer-recur ctx form)
        clojure.core/deref (infer-deref ctx form)
        merry/some-let (infer-some-let ctx form)
        (def defn ns) (err form (str h " is a top-level form"))
        quote (err form "quote is not supported yet")
        (if (keyword? h) (infer-kw ctx form) (infer-call ctx form))))
    :else (err form (str "unsupported literal " (pr-str form)))))

(defn infer
  "[type elaborated-form]; an error without a line takes the line of the nearest list."
  [ctx form]
  (try
    (infer* ctx form)
    (catch clojure.lang.ExceptionInfo e
      (let [d (ex-data e)]
        (if (and (::t/error d) (nil? (:line d)) (line-of form))
          (throw (ex-info (ex-message e) (assoc d :line (line-of form))))
          (throw e))))))

;;; Top level

(defn- new-ctx [] {:locals {} :eff (t/open-row) :loop nil :calls (atom [])})

(defn- origins
  "label → the first call that brought it, from labels each callee had at its call site."
  [calls eff]
  (into {} (for [l (keys (:labels (t/resolve-row eff)))
                 :let [c (first (filter #(contains? (:labels %) l) calls))]
                 :when c]
             [l (select-keys c [:callee :line])])))

(defn- chain [fname own label]
  (loop [o (get own label) seen #{fname} acc [(str fname)]]
    (if (or (nil? o) (seen (:callee o)))
      acc
      (let [c (:callee o)]
        (recur (get-in @*globals* [c :origins label])
               (conj seen c)
               (conj acc (str c " (line " (:line o) ")")))))))

(defn- check-defn [form]
  (let [[_ name & more] form
        _ (when-not (symbol? name) (err form "defn takes a name"))
        more (if (string? (first more)) (rest more) more)
        [props more] (if (map? (first more)) [(first more) (rest more)] [nil more])
        ctx (new-ctx)
        [ty elab] (infer ctx (with-meta (list* 'fn name more) (meta form)))
        own (origins @(:calls ctx) (:eff ty))]
    (when (contains? props :eff)
      (let [declared (t/parse-eff (:eff props) (atom {}) :closed)
            allowed (set (keys (:labels declared)))
            extra (remove allowed (keys (:labels (t/resolve-row (:eff ty)))))]
        (when (seq extra)
          (err form (str/join "\n" (for [l (sort-by str extra)]
                                     (str "effect " l " is outside the declared row " (pr-str (:eff props))
                                          ": " (str/join " → " (chain name own l)))))))
        (t/unify-row declared (:eff ty) :effect)))
    (let [scheme (t/generalize #{} ty)]
      (swap! *globals* assoc name {:scheme scheme :origins own})
      {:kind :defn
       :name name
       :type (:type scheme)
       :elab (with-meta (list* 'defn name (drop 2 elab)) (meta form))})))

(defn- check-def [form]
  (let [[_ name v & more] form]
    (when (or (not (symbol? name)) (not= 3 (count form)) (seq more))
      (err form "def takes a name and a value"))
    (let [ctx (new-ctx)
          [ty v'] (infer ctx v)
          scheme (t/generalize (set (keys (t/free-vars (:eff ctx)))) ty)]
      (swap! *globals* assoc name {:scheme scheme :origins (origins @(:calls ctx) (:eff ctx))})
      {:kind :def
       :name name
       :type (:type scheme)
       :load-eff (keys (:labels (t/resolve-row (:eff ctx))))
       :elab (list 'def name v')})))

(defn- def-name [form]
  (when (and (seq? form) ('#{def defn} (first form)) (symbol? (second form)))
    (second form)))

(defn check-top
  "One expanded top-level form → {:kind :defn|:def|:expr|:skip|:error ...}."
  [form]
  (try
    (cond
      (and (seq? form) (= 'defn (first form))) (check-defn form)
      (and (seq? form) (= 'def (first form))) (check-def form)
      (and (seq? form) ('#{ns comment} (first form))) {:kind :skip}
      :else (let [ctx (new-ctx)
                  [ty f'] (infer ctx form)]
              {:kind :expr
               :type ty
               :load-eff (keys (:labels (t/resolve-row (:eff ctx))))
               :elab f'}))
    (catch clojure.lang.ExceptionInfo e
      (if (::t/error (ex-data e))
        (do
          ;; :error keeps one failed definition from cascading into every later use of its name
          (when-let [n (def-name form)]
            (swap! *globals* assoc n {:scheme (t/mono (t/tcon :error)) :origins {}}))
          {:kind :error
           :message (ex-message e)
           :line (or (:line (ex-data e)) (line-of form))})
        (throw e)))))
