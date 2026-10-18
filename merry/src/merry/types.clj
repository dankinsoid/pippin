;; @ai-generated(guided)
;; HM with rows (Leijen 2005): one row machinery for record fields, effect labels and variant tags.
(ns merry.types
  (:require [clojure.string :as str]))

(def ^:dynamic *state* nil)

(defn new-state [] (atom {:n 0 :subst {}}))

(defn fail [msg & {:as data}]
  (throw (ex-info msg (assoc data ::error true))))

(defn- next-id! [] (:n (swap! *state* update :n inc)))

(defn tvar [] {:tag :var :kind :type :id (next-id!)})
(defn rvar [] {:tag :var :kind :row :id (next-id!)})
(defn tcon [k] {:tag :con :name k})
(defn tapp [k & args] {:tag :app :name k :args (vec args)})
(defn trow [labels tail] {:tag :row :labels labels :tail tail})
(defn tfn [params rest ret eff] {:tag :fn :params (vec params) :rest rest :ret ret :eff eff})
(defn tthunk [t eff] {:tag :thunk :type t :eff eff})
(defn trec [row] {:tag :rec :row row})
(defn ttuple [elems] {:tag :tuple :elems (vec elems)})

(defn tvariant
  "Row labels are tags: a tuple for `[:tag x y]`, nil for a bare keyword `:tag`."
  [row]
  {:tag :variant :row row})

(defn open-row [] (trow {} (rvar)))
(def closed-row (trow {} nil))

(defn tv? [t] (= :var (:tag t)))
(defn con? [t k] (and (= :con (:tag t)) (= k (:name t))))

(defn prune [t]
  (if (tv? t)
    (if-let [b (get-in @*state* [:subst (:id t)])]
      (prune b)
      t)
    t))

(defn maybe? [t]
  (let [t (prune t)]
    (and (= :app (:tag t)) (= :maybe (:name t)))))

(defn tmaybe
  "(or nil t); a maybe of a maybe or of nil collapses (docs/merry.md «Запись объединения»)."
  [t]
  (let [p (prune t)]
    (if (or (maybe? p) (con? p :nil)) p (tapp :maybe t))))

(defn resolve-row
  "r with its tail chain followed: every label, and the unbound tail var or nil when closed."
  [r]
  (loop [labels (:labels r) tail (:tail r)]
    (if (nil? tail)
      (trow labels nil)
      (let [t (prune tail)]
        (if (tv? t)
          (trow labels t)
          (recur (merge (:labels t) labels) (:tail t)))))))

(defn open-up
  "A closed row made open: a callee's effects are a subset of the caller's, not equal to them."
  [row]
  (let [r (resolve-row row)]
    (if (:tail r) r (trow (:labels r) (rvar)))))

(defn- map-children
  "t with f applied to every direct child type; row labels without a payload stay nil."
  [f t]
  (case (:tag t)
    (:var :con) t
    :app (update t :args #(mapv f %))
    :fn (-> t
            (update :params #(mapv f %))
            (update :rest #(some-> % f))
            (update :ret f)
            (update :eff f))
    :thunk (-> t (update :type f) (update :eff f))
    (:rec :variant) (update t :row f)
    :tuple (update t :elems #(mapv f %))
    :row (let [r (resolve-row t)]
           (trow (update-vals (:labels r) #(some-> % f)) (some-> (:tail r) f)))))

(defn zonk [t] (map-children zonk (prune t)))

(defn- walk-vars
  "Calls f on every var of t, repeats included."
  [f t]
  (let [t (prune t)]
    (if (tv? t)
      (f t)
      (map-children (fn [c] (walk-vars f c) c) t))))

(defn free-vars [t]
  (let [acc (volatile! {})]
    (walk-vars #(vswap! acc assoc (:id %) %) t)
    @acc))

(defn- occurrences [t]
  (let [acc (volatile! {})]
    (walk-vars #(vswap! acc update (:id %) (fnil inc 0)) t)
    @acc))

(declare show show-pair show-label)

(defn- occurs? [id t]
  (contains? (free-vars t) id))

(defn- bind! [v t]
  (when (occurs? (:id v) t)
    (fail (str "infinite type: " (show v) " occurs in " (show t))))
  (swap! *state* assoc-in [:subst (:id v)] t))

(defn- row-fail [kind labels side other]
  (let [ls (str/join ", " (sort (map str (keys labels))))
        vs (str/join ", " (sort (map (fn [[k v]] (show-label k v)) labels)))
        handled (str/join ", " (sort (map (fn [[k v]] (show-label k v)) other)))]
    (fail (case [kind side]
            [:field :missing] (str "missing field " ls)
            [:field :extra] (str "unexpected field " ls)
            [:effect :missing] (str "effect " ls " is not allowed here")
            [:effect :extra] (str "effect " ls " is not allowed here")
            [:variant :missing] (str "the value's type has no " vs)
            [:variant :extra] (str vs " is not handled: the match covers only "
                                   (if (seq other) handled "nothing"))))))

(declare unify)

(defn unify-row
  "r1 is the expected side, r2 the actual one; kind is :field, :effect or :variant, for messages."
  [r1 r2 kind]
  (let [{l1 :labels t1 :tail} (resolve-row r1)
        {l2 :labels t2 :tail} (resolve-row r2)
        only1 (apply dissoc l1 (keys l2))
        only2 (apply dissoc l2 (keys l1))]
    (doseq [[k v] l1
            :when (contains? l2 k)
            :let [w (get l2 k)]]
      (cond
        (and v w) (unify v w)
        ;; `:paid` and `[:paid]` are different values
        (and (= kind :variant) (not= (nil? v) (nil? w)))
        (fail (str "expected " (show-label k v) ", got " (show-label k w)))))
    (if (and t1 t2 (= (:id t1) (:id t2)))
      (when (or (seq only1) (seq only2))
        (fail "recursive row: a row cannot extend itself"))
      (let [t3 (when (and t1 t2) (rvar))]
        (when (and (seq only1) (nil? t2)) (row-fail kind only1 :missing l2))
        (when (and (seq only2) (nil? t1)) (row-fail kind only2 :extra l1))
        (when t2 (bind! t2 (trow only1 t3)))
        (when t1 (bind! t1 (trow only2 t3)))))))

(defn- mismatch [e a]
  (let [[se sa] (show-pair e a)]
    (fail (str "expected " se ", got " sa
               (when (con? (prune a) :any) "; a value of Any goes down only through a check")))))

(defn- unify-fn [e a]
  (let [pe (:params e) pa (:params a)]
    (doseq [i (range (max (count pe) (count pa)))]
      (let [x (or (get pe i) (:rest e))
            y (or (get pa i) (:rest a))]
        (when (or (nil? x) (nil? y)) (mismatch e a))
        (unify x y)))
    ;; a variadic function stands in for a fixed one, not the other way round
    (cond
      (and (:rest e) (:rest a)) (unify (:rest e) (:rest a))
      (:rest e) (mismatch e a))
    (unify (:ret e) (:ret a))
    (unify-row (:eff e) (:eff a) :effect)))

(defn unify
  "expected first, actual second. Any is the top type: anything goes up into it, nothing comes
  down without a check. :error stands for a definition that failed and unifies with everything."
  [expected actual]
  (let [e (prune expected) a (prune actual)]
    (cond
      (and (tv? e) (tv? a) (= (:id e) (:id a))) nil
      (or (con? e :error) (con? a :error)) nil
      (con? e :any) nil
      (tv? e) (bind! e a)
      (tv? a) (bind! a e)
      (not= (:tag e) (:tag a)) (mismatch e a)
      :else
      (case (:tag e)
        :con (when-not (= (:name e) (:name a)) (mismatch e a))
        :app (if (and (= (:name e) (:name a)) (= (count (:args e)) (count (:args a))))
               (run! (fn [[x y]] (unify x y)) (map vector (:args e) (:args a)))
               (mismatch e a))
        :fn (unify-fn e a)
        :thunk (do (unify (:type e) (:type a)) (unify-row (:eff e) (:eff a) :effect))
        :rec (unify-row (:row e) (:row a) :field)
        :variant (unify-row (:row e) (:row a) :variant)
        :tuple (if (= (count (:elems e)) (count (:elems a)))
                 (run! (fn [[x y]] (unify x y)) (map vector (:elems e) (:elems a)))
                 (mismatch e a))
        :row (unify-row e a :effect)))))

(defn mono [t] {:vars #{} :type t})

(defn generalize [env-ids t]
  (let [t (zonk t)]
    {:vars (set (remove env-ids (keys (free-vars t)))) :type t}))

(defn- copy [t m]
  (let [t (prune t)]
    (if (tv? t)
      (get m (:id t) t)
      (map-children #(copy % m) t))))

(defn instantiate [{:keys [vars type]}]
  (if (empty? vars)
    type
    (let [fv (free-vars type)
          m (into {} (for [id vars]
                       [id (if (= :row (:kind (fv id))) (rvar) (tvar))]))]
      (copy type m))))

;;; Printing, in the notation of :- annotations (docs/merry.md «Тип функции — (Fn [A B] C {...})»)

(def ^:private ^:dynamic *names* nil)
(def ^:private ^:dynamic *closed* #{})

(def ^:private letters (map str (remove #{\e \r} "abcdfghijklmnopqstuvwxyz")))

(def ^:private prim-names
  {:int "Int" :double "Double" :string "String" :bool "Bool" :keyword "Keyword" :any "Any"
   :regex "Regex" :nil "nil" :error "error"})

(defn- name-of [v prefix]
  (or (get-in @*names* [:by-id (:id v)])
      (let [n (get-in @*names* [:count prefix] 0)
            nm (cond
                 (= prefix "t") (or (nth letters n nil) (str "t" n))
                 (zero? n) prefix
                 :else (str prefix (inc n)))]
        (swap! *names* #(-> %
                            (assoc-in [:by-id (:id v)] nm)
                            (assoc-in [:count prefix] (inc n))))
        nm)))

(declare show-t)

(defn- show-row [r]
  (let [labels (sort (map str (keys (:labels r))))
        tail (:tail r)
        open? (and tail (not (*closed* (:id tail))))]
    (cond
      (and open? (empty? labels)) (name-of tail "e")
      open? (str "#{" (str/join " " labels) " & " (name-of tail "e") "}")
      :else (str "#{" (str/join " " labels) "}"))))

(defn- show-label* [k payload]
  (if (nil? payload)
    (str k)
    (str "[" (str/join " " (cons (str k) (map show-t (:elems (prune payload))))) "]")))

(defn show-label
  "One variant as it is written: `:red`, `[:paid Int]`."
  [k payload]
  (binding [*names* (or *names* (atom {}))]
    (show-label* k (some-> payload zonk))))

(defn- show-t [t]
  (case (:tag t)
    :var (name-of t (if (= :row (:kind t)) "e" "t"))
    :con (get prim-names (:name t) (str (:name t)))
    :app (let [[a] (:args t)]
           (case (:name t)
             :maybe (cond (con? a :nil) "nil"
                          (maybe? a) (show-t a)
                          :else (str "(or nil " (show-t a) ")"))
             :vector (str "[" (show-t a) "]")
             (str "(" (name (:name t)) " " (str/join " " (map show-t (:args t))) ")")))
    :fn (str "(Fn ["
             (str/join " " (concat (map show-t (:params t))
                                   (when (:rest t) ["&" (show-t (:rest t))])))
             "] " (show-t (:ret t)) " {:eff " (show-row (:eff t)) "})")
    :thunk (str "(Thunk " (show-t (:type t)) " {:eff " (show-row (:eff t)) "})")
    :rec (let [r (:row t)]
           (str "{"
                (str/join " " (concat (for [[k v] (sort-by (comp str key) (:labels r))]
                                        (str k " " (show-t v)))
                                      (when (:tail r) [(str "& " (name-of (:tail r) "r"))])))
                "}"))
    :variant (let [r (:row t)
                   alts (map (fn [[k v]] (show-label* k v)) (sort-by (comp str key) (:labels r)))]
               (str "(or " (str/join " " (concat alts (when (:tail r) [(str "& " (name-of (:tail r) "r"))])))
                    ")"))
    :tuple (str "[" (str/join " " (map show-t (:elems t))) "]")
    :row (show-row t)))

(defn show [t]
  (binding [*names* (or *names* (atom {}))]
    (show-t (zonk t))))

(defn show-pair
  "Two types named in one context, so that `a` means the same var in both."
  [x y]
  (binding [*names* (atom {})]
    [(show x) (show y)]))

(defn show-contract
  "A contract as in docs: the function's own effect tail is hidden when nothing else mentions it."
  [t]
  (let [z (zonk t)
        tail (when (= :fn (:tag z)) (:tail (:eff z)))
        closed (if (and tail (= 1 ((occurrences z) (:id tail)))) #{(:id tail)} #{})]
    (binding [*names* (atom {}) *closed* closed]
      (show-t z))))

;;; Annotations: the type notation after :-

(def ^:private prims
  {'Int :int 'Double :double 'String :string 'Bool :bool 'Keyword :keyword 'Any :any 'Regex :regex})

(defn- lower-sym? [x]
  (and (symbol? x) (nil? (namespace x)) (Character/isLowerCase (.charAt (name x) 0))))

(defn- split-rest [xs]
  (let [[fixed more] (split-with #(not= '& %) xs)]
    (when (and (seq more) (not= 2 (count more)))
      (fail "`&` takes exactly one form after it"))
    [(vec fixed) (second more)]))

(defn- named-var [names sym mk]
  (or (get @names sym)
      (let [v (mk)] (swap! names assoc sym v) v)))

(defn parse-eff
  "An effect row: #{Console Net}, #{Log & e}, a var e; nil is open (inferred) or closed (pure) by default."
  [e names default]
  (cond
    (nil? e) (if (= default :closed) closed-row (open-row))
    (lower-sym? e) (trow {} (named-var names e rvar))
    (set? e) (let [tails (filter lower-sym? e)
                   labels (remove #(or (= '& %) (lower-sym? %)) e)]
               (when (or (> (count tails) 1) (not= (boolean (seq tails)) (contains? e '&)))
                 (fail (str "an effect set holds labels and at most one `& tail`: " (pr-str e))))
               (doseq [l labels]
                 (when-not (symbol? l) (fail (str "an effect label is a symbol: " (pr-str l)))))
               (trow (zipmap labels (repeat nil)) (some->> (first tails) (#(named-var names % rvar)))))
    :else (fail (str "not an effect row: " (pr-str e)))))

(defn parse-schema
  "names: an atom of symbol → var, shared across one signature."
  [s names default-eff]
  (letfn [(eff [props] (parse-eff (:eff props) names default-eff))
          (tail [sym] (when sym
                        (when-not (lower-sym? sym) (fail (str "a row tail is a lowercase name: " (pr-str sym))))
                        (named-var names sym rvar)))
          (alternative [x]
            (cond
              (keyword? x) [x nil]
              (and (vector? x) (keyword? (first x))) [(first x) (ttuple (mapv p (rest x)))]
              :else (fail (str "the prototype's unions are (or nil T) and variants (:tag, [:tag T]): "
                               (pr-str x)))))
          (p [s]
            (cond
              (nil? s) (tcon :nil)
              (contains? prims s) (tcon (prims s))
              (lower-sym? s) (named-var names s tvar)
              (vector? s) (if (= 1 (count s))
                            (tapp :vector (p (first s)))
                            (fail (str "[T] is a vector; tuples arrive later: " (pr-str s))))
              (map? s) (let [fields (dissoc s '&)]
                         (doseq [k (keys fields)]
                           (when-not (keyword? k) (fail (str "a record key is a keyword: " (pr-str k)))))
                         (trec (trow (update-vals fields p) (tail (get s '&)))))
              (seq? s)
              (let [[h & args] s]
                (case h
                  Vector (tapp :vector (p (first args)))
                  (Thunk Delay) (tthunk (p (first args)) (eff (second args)))
                  Fn (let [[ps ret props] args
                           [fixed rest] (split-rest ps)]
                       (tfn (mapv p fixed) (some-> rest p) (p ret) (eff props)))
                  or (let [[alts more] (split-rest args)
                           with-nil (some nil? alts)
                           alts (remove nil? alts)]
                       (cond
                         (and with-nil (= 1 (count alts)) (nil? more)) (tmaybe (p (first alts)))
                         (empty? alts) (fail "(or) needs an alternative besides nil")
                         :else (cond-> (tvariant (trow (into {} (map alternative) alts) (tail more)))
                                 with-nil tmaybe)))
                  (fail (str "unknown type form " (pr-str s)))))
              :else (fail (str "unknown type " (pr-str s)))))]
    (p s)))
