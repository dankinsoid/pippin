;; @ai-generated(guided)
;; HM with rows (Leijen, "Extensible records with scoped labels", 2005): one row machinery serves
;; record fields and effect labels, as in Koka.
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
  "[:maybe t]; a maybe of a maybe or of :nil collapses (docs/merry.md «nil и умолчания»)."
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

(defn zonk [t]
  (let [t (prune t)]
    (case (:tag t)
      (:var :con) t
      :app (update t :args #(mapv zonk %))
      :fn (-> t
              (update :params #(mapv zonk %))
              (update :rest #(some-> % zonk))
              (update :ret zonk)
              (update :eff zonk))
      :thunk (-> t (update :type zonk) (update :eff zonk))
      :rec (update t :row zonk)
      :row (let [r (resolve-row t)]
             (trow (update-vals (:labels r) #(some-> % zonk)) (:tail r))))))

(defn- walk-vars
  "Calls f on every var of t, repeats included."
  [f t]
  (let [t (prune t)]
    (case (:tag t)
      :var (f t)
      :con nil
      :app (run! #(walk-vars f %) (:args t))
      :fn (do (run! #(walk-vars f %) (:params t))
              (some->> (:rest t) (walk-vars f))
              (walk-vars f (:ret t))
              (walk-vars f (:eff t)))
      :thunk (do (walk-vars f (:type t)) (walk-vars f (:eff t)))
      :rec (walk-vars f (:row t))
      :row (let [r (resolve-row t)]
             (run! #(some->> % (walk-vars f)) (vals (:labels r)))
             (some->> (:tail r) (walk-vars f))))))

(defn free-vars [t]
  (let [acc (volatile! {})]
    (walk-vars #(vswap! acc assoc (:id %) %) t)
    @acc))

(defn- occurrences [t]
  (let [acc (volatile! {})]
    (walk-vars #(vswap! acc update (:id %) (fnil inc 0)) t)
    @acc))

(declare show show-pair)

(defn- occurs? [id t]
  (contains? (free-vars t) id))

(defn- bind! [v t]
  (when (occurs? (:id v) t)
    (fail (str "infinite type: " (show v) " occurs in " (show t))))
  (swap! *state* assoc-in [:subst (:id v)] t))

(defn- row-fail [kind labels side]
  (let [ls (str/join ", " (sort (map str labels)))]
    (fail (case [kind side]
            [:field :missing] (str "missing field " ls)
            [:field :extra] (str "unexpected field " ls)
            [:effect :missing] (str "effect " ls " is not allowed here")
            [:effect :extra] (str "effect " ls " is not allowed here")))))

(declare unify)

(defn unify-row
  "r1 is the expected side, r2 the actual one; kind is :field or :effect, for messages."
  [r1 r2 kind]
  (let [{l1 :labels t1 :tail} (resolve-row r1)
        {l2 :labels t2 :tail} (resolve-row r2)
        only1 (apply dissoc l1 (keys l2))
        only2 (apply dissoc l2 (keys l1))]
    (doseq [[k v] l1
            :let [w (get l2 k)]
            :when (and v w)]
      (unify v w))
    (if (and t1 t2 (= (:id t1) (:id t2)))
      (when (or (seq only1) (seq only2))
        (fail "recursive row: a row cannot extend itself"))
      (let [t3 (when (and t1 t2) (rvar))]
        (when (and (seq only1) (nil? t2)) (row-fail kind (keys only1) :missing))
        (when (and (seq only2) (nil? t1)) (row-fail kind (keys only2) :extra))
        (when t2 (bind! t2 (trow only1 t3)))
        (when t1 (bind! t1 (trow only2 t3)))))))

(defn- mismatch [e a]
  (let [[se sa] (show-pair e a)]
    (fail (str "expected " se ", got " sa
               (when (con? (prune a) :any) "; a value of :any goes down only through a check")))))

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
  "expected first, actual second. :any is the top type: anything goes up into it, nothing comes
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
        :row (unify-row e a :effect)))))

(defn mono [t] {:vars #{} :type t})

(defn generalize [env-ids t]
  (let [t (zonk t)]
    {:vars (set (remove env-ids (keys (free-vars t)))) :type t}))

(defn- copy [t m]
  (let [t (prune t)]
    (case (:tag t)
      :var (get m (:id t) t)
      :con t
      :app (update t :args (fn [as] (mapv #(copy % m) as)))
      :fn (-> t
              (update :params (fn [ps] (mapv #(copy % m) ps)))
              (update :rest #(some-> % (copy m)))
              (update :ret #(copy % m))
              (update :eff #(copy % m)))
      :thunk (-> t (update :type #(copy % m)) (update :eff #(copy % m)))
      :rec (update t :row #(copy % m))
      :row (let [r (resolve-row t)]
             (trow (update-vals (:labels r) #(some-> % (copy m)))
                   (some-> (:tail r) (copy m)))))))

(defn instantiate [{:keys [vars type]}]
  (if (empty? vars)
    type
    (let [fv (free-vars type)
          m (into {} (for [id vars]
                       [id (if (= :row (:kind (fv id))) (rvar) (tvar))]))]
      (copy type m))))

;;; Printing, in the schema language of :- annotations

(def ^:private ^:dynamic *names* nil)
(def ^:private ^:dynamic *closed* #{})

(def ^:private letters (map str (remove #{\e \r} "abcdfghijklmnopqstuvwxyz")))

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

(defn- show-t [t]
  (case (:tag t)
    :var (name-of t (if (= :row (:kind t)) "e" "t"))
    :con (str (:name t))
    :app (let [[a] (:args t)]
           (if (= :maybe (:name t))
             (cond (con? a :nil) ":nil"
                   (maybe? a) (show-t a)
                   :else (str "[:maybe " (show-t a) "]"))
             (str "[" (:name t) " " (str/join " " (map show-t (:args t))) "]")))
    :fn (str "[:=> ["
             (str/join " " (concat (map show-t (:params t))
                                   (when (:rest t) ["&" (show-t (:rest t))])))
             "] " (show-t (:ret t)) " {:eff " (show-row (:eff t)) "}]")
    :thunk (str "[:thunk " (show-t (:type t)) " {:eff " (show-row (:eff t)) "}]")
    :rec (let [r (:row t)]
           (str "[:map"
                (apply str (for [[k v] (sort-by (comp str key) (:labels r))]
                             (str " [" k " " (show-t v) "]")))
                (when (:tail r) (str " & " (name-of (:tail r) "r")))
                "]"))
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

;;; Annotations: the schema language after :-

(def ^:private prims #{:int :double :string :bool :nil :keyword :any :regex})

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
          (p [s]
            (cond
              (keyword? s) (if (prims s) (tcon s) (fail (str "unknown type " s)))
              (lower-sym? s) (named-var names s tvar)
              (vector? s)
              (let [[h & args] s]
                (case h
                  :vector (tapp :vector (p (first args)))
                  :maybe (tmaybe (p (first args)))
                  :thunk (tthunk (p (first args)) (eff (second args)))
                  :=> (let [[ps ret props] args
                            [fixed rest] (split-rest ps)]
                        (tfn (mapv p fixed) (some-> rest p) (p ret) (eff props)))
                  :map (let [[fields tail] (split-rest args)]
                         (trec (trow (into {} (for [[k t] fields] [k (p t)]))
                                     (some->> tail (#(named-var names % rvar))))))
                  (fail (str "unknown schema form " (pr-str s)))))
              :else (fail (str "unknown type " (pr-str s)))))]
    (p s)))
