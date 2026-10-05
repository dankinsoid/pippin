;; @ai-generated(solo)

;; The differential fuzzer of design §10 and §3 «Корректность реализации» item 2 (docs/notes/fuzzing.md).

;; Runs on pinned JVM Clojure, which is also the oracle; every runner is a process over one case file.

(ns fuzz.differential
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.set :as set]
            [clojure.string :as str])
  (:import (java.util.concurrent TimeUnit)))

;; ---------------------------------------------------------------- exclusions

(def ^:private jvm-differences "docs/jvm-differences.md")

(defn load-exclusions [path] (edn/read-string (slurp path)))

;; An exclusion whose citation does not resolve is refused: this list is the only thing between the fuzzer and
;; reporting a deliberate difference forever.
(defn audit-exclusions [ex]
  (let [rows (slurp jvm-differences)]
    (vec (for [e (:exclusions ex)
               :let [bad (cond
                           (:row e) (when-not (str/includes? rows (:row e))
                                      (str "no such row in " jvm-differences ": " (pr-str (:row e))))
                           (:design e) (let [f (io/file (:design e))]
                                         (cond
                                           (not (.exists f)) (str "no such design file: " (:design e))
                                           (not (:cite e)) "a :design without a :cite"
                                           (not (str/includes? (slurp f) (:cite e)))
                                           (str "no such text in " (:design e) ": " (pr-str (:cite e)))))
                           :else "no citation: an exclusion needs :row, or :design with :cite")]
               :when bad]
           (str (:id e) ": " bad)))))

(defn- ids [ex] (set (map :id (:exclusions ex))))

;; ---------------------------------------------------------------- randomness

(def ^:private golden -7046029254386353131)

(defn- mix64 [z]
  (let [z (unchecked-multiply (bit-xor z (unsigned-bit-shift-right z 30)) -4658895280553007687)
        z (unchecked-multiply (bit-xor z (unsigned-bit-shift-right z 27)) -7723592293110705685)]
    (bit-xor z (unsigned-bit-shift-right z 31))))

(defn- new-st [seed] (atom {:s (long seed) :n 0}))

(defn- next-long! [st]
  (mix64 (:s (swap! st update :s (fn [z] (unchecked-add z golden))))))

(defn- rint! [st n]
  (if (<= n 1) 0 (mod (bit-and (next-long! st) Long/MAX_VALUE) n)))

(defn- pick! [st coll]
  (let [v (vec coll)] (nth v (rint! st (count v)))))

(defn- coin! [st n] (zero? (rint! st n)))

(defn- fresh! [st prefix] (symbol (str prefix (:n (swap! st update :n inc)))))

;; ---------------------------------------------------------------- literal pools

(def ^:private int-lits
  [-4611686018427387904 -1000000 -1000 -100 -31 -8 -3 -2 -1 0 1 2 3 5 7 8 16 31 63 100 1000
   123456789 4611686018427387903])

(def ^:private double-lits
  [0.0 0.5 -0.5 1.5 -1.5 0.1 3.14 1.0 2.0 1.0E10 1.0E-10 1.0E300])

(def ^:private str-lits
  ["" "a" "ab" "abc" "Hello" "hello world" "  pad  " "a,b,,c" "0" "-12" "x\ny" "AbC" "~!@" "1.5"])

(def ^:private kw-lits [:a :b :c :d :k :x])
(def ^:private char-lits [\a \Z \0 \space \newline])
(def ^:private small-lits [-4 -2 -1 0 1 2 3 4 5 6 7 8])
(def ^:private idx-lits [0 0 1 1 2 3 4 5 6])
(def ^:private pos-lits [1 1 2 2 3 4 5])
(def ^:private shift-lits [0 1 2 7 31 32 62 63 64 65])

(def ^:private scalar-types [:int :num :str :bool :kw :char :nil])
(def ^:private any-types [:int :num :str :bool :kw :char :nil :vec :seq])

;; ---------------------------------------------------------------- the op tables

;; A template's keyword is a generated argument of that type, [:lit x] passes through, a nested vector is a call.
(def ^:private op-table
  {:int [['+ :int :int] ['+ :int :int] ['- :int :int] ['* :int :int] ['- :int]
         ['quot :int :nzint] ['rem :int :nzint] ['mod :int :nzint]
         ['inc :int] ['dec :int] ['min :int :int] ['max :int :int]
         ['bit-and :int :int] ['bit-or :int :int] ['bit-xor :int :int] ['bit-not :int]
         ['bit-and-not :int :int] ['bit-shift-left :int :shift] ['bit-shift-right :int :shift]
         ['unsigned-bit-shift-right :int :shift] ['bit-flip :int :shift] ['bit-clear :int :shift]
         ['count :coll] ['count :str] ['long :int] ['int :int] ['inc' :int] ['dec' :int]
         ['+' :int :int] ['-' :int :int] ['*' :int :int] ['bigint :int]
         ['apply '+ :seqint] ['apply '* :seqint] ['reduce '+ :seqint]
         ['reduce '+ :int :seqint] ['transduce [:lit '(map inc)] '+ :seqint]
         :op/let :op/if :op/loop]
   :num [['+ :num :num] ['- :num :num] ['* :num :num]
         ['inc :num] ['dec :num] ['min :num :num] ['max :num :num]
         ['double :num] ['quot :num :num] ['rem :num :num] ['mod :num :num]
         ['reduce '+ :num :seqint] ['apply 'max :seqint] ['apply 'min :seqint]
         :op/int :op/let :op/if]
   :bool [['= :any :any] ['= :any :any] ['not= :any :any] ['= :map :map] ['= :set :set]
          ['< :num :num] ['<= :num :num] ['> :num :num] ['>= :num :num] ['== :num :num]
          ['zero? :num] ['pos? :num] ['neg? :num] ['even? :int] ['odd? :int]
          ['nil? :any] ['some? :any] ['empty? :coll] ['seq? :coll] ['vector? :any] ['map? :coll]
          ['set? :coll] ['coll? :any] ['sequential? :any] ['string? :any] ['number? :any]
          ['keyword? :any] ['char? :any] ['int? :any] ['ratio? :any] ['float? :any] ['integer? :any]
          ['contains? :map :key] ['contains? :vec :idx] ['contains? :set :any]
          ['every? :pred1 :seq] ['not-any? :pred1 :seq] ['every? :pred1 :seqint]
          ['not :bool] ['and :bool :bool] ['or :bool :bool] ['bit-test :int :shift]
          ['distinct? :any :any] ['distinct? :any :any :any]
          ['clojure.string/starts-with? :str :str] ['clojure.string/ends-with? :str :str]
          ['clojure.string/includes? :str :str] ['clojure.string/blank? :str]
          ['clojure.set/subset? :set :set] ['clojure.set/superset? :set :set]
          ['boolean :any] ['< :ratio :ratio] ['= :ratio :ratio]
          ['= :sortedmap :sortedmap] ['= :sortedset :sortedset]
          ['contains? :sortedmap :ikey] ['contains? :sortedset :ikey]
          :op/let :op/if]
   :str [['str :scalar :scalar] ['str :scalar] ['str :vec]
         ['subs :str :idx] ['subs :str :idx :idx]
         ['clojure.string/join :seqs] ['clojure.string/join :str :seqs]
         ['clojure.string/upper-case :str] ['clojure.string/lower-case :str]
         ['clojure.string/capitalize :str] ['clojure.string/trim :str]
         ['clojure.string/triml :str] ['clojure.string/trimr :str]
         ['clojure.string/reverse :str] ['clojure.string/replace :str :str :str]
         ['clojure.string/replace-first :str :str :str] ['clojure.string/trim-newline :str]
         ['apply 'str :seqs] ['pr-str :any] ['pr-str :vec] ['name :kw]
         :op/let :op/if]
   :vec [:op/vec-lit :op/vec-lit
         ['conj :vec :any] ['conj :vec :any :any] ['vec :seq] ['vector :any :any]
         ['into :vec :seq] ['into [:lit []] :seq] ['into [:lit []] [:lit '(map inc)] :seqint]
         ['mapv :fn1 :seq] ['filterv :pred1 :seq] ['subvec :vec :idx] ['subvec :vec :idx :idx]
         ['assoc :vec :idx :any] ['update :vec :idx :fn1] ['pop :vec]
         ['apply 'vector :seq]
         :op/let :op/if]
   :seq [:op/vec-lit ['list :any :any] ['list :any]
         ['range :small] ['range :small :small] ['range :small :small :posidx]
         ['cons :any :seq] ['rest :seq] ['next :seq] ['butlast :seq]
         ['take :idx :seq] ['drop :idx :seq] ['take-last :idx :seq] ['drop-last :idx :seq]
         ['take-while :pred1 :seq] ['drop-while :pred1 :seq] ['take-nth :posidx :seq]
         ['map :fn1 :seq] ['map :fn1 :seq] ['map :fn2 :seq :seq] ['filter :pred1 :seq]
         ['remove :pred1 :seq] ['keep :fn1 :seq] ['mapcat 'list :seq]
         ['reduce :fn2 :seq] ['reductions :fn2 :seq] ['reductions :fn2 :any :seq]
         ['concat :seq :seq] ['interleave :seq :seq] ['interpose :any :seq]
         ['partition :posidx :seq] ['partition :posidx :posidx :seq] ['partition-all :posidx :seq]
         ['split-at :idx :seq] ['split-with :pred1 :seq]
         ['reverse :seq] ['distinct :seq] ['dedupe :seq] ['flatten :seq] ['sort :seqint]
         ['sort ['map 'pr-str :seq]]
         ['take :idx ['iterate :fn1 :any]] ['take :idx ['cycle :seq]] ['take :idx ['repeat :any]]
         ['repeat :idx :any] ['lazy-seq :seq] ['seq :vec] ['seq :seqint]
         ['apply 'list :seq] ['sequence [:lit '(map inc)] :seqint]
         ['into [:lit '()] :seq] ['keep-indexed [:lit '(fn [i x] x)] :seq]
         ['map-indexed [:lit '(fn [i x] i)] :seq]
         :op/map-bridge :op/map-bridge :op/set-bridge
         ['seq :sortedmap] ['keys :sortedmap] ['vals :sortedmap] ['vec :sortedmap]
         ['seq :sortedset] ['vec :sortedset] ['take :idx :sortedset] ['map 'inc :sortedset]
         :op/let :op/if :op/loop :op/thread :op/destructure]
   :ratio [['/ :int :nzint] ['/ :int :nzint] ['/ :int :nzint]
           ['+ :ratio :ratio] ['- :ratio :ratio] ['* :ratio :ratio] ['/ :ratio :ratio]
           ['+ :ratio :int] ['* :ratio :int] ['- :int :ratio] ['- :ratio]
           ['inc :ratio] ['dec :ratio] ['min :ratio :ratio] ['max :ratio :ratio]
           ['quot :ratio :ratio] ['rem :ratio :ratio] ['mod :ratio :ratio]]
   :sortedmap [['sorted-map :ikey :any :ikey :any] ['sorted-map :ikey :any]
               ['assoc :sortedmap :ikey :any] ['dissoc :sortedmap :ikey]
               ['merge :sortedmap :sortedmap] ['into [:lit '(sorted-map)] :sortedmap]
               ['into :sortedmap :sortedmap] ['update :sortedmap :ikey :fn1]
               ['empty :sortedmap]]
   :sortedset [['sorted-set :ikey :ikey] ['sorted-set :ikey :ikey :ikey]
               ['conj :sortedset :ikey] ['disj :sortedset :ikey]
               ['into [:lit '(sorted-set)] :sortedset] ['into :sortedset :sortedset]
               ['clojure.set/union :sortedset :sortedset]
               ['clojure.set/intersection :sortedset :sortedset]
               ['clojure.set/difference :sortedset :sortedset] ['empty :sortedset]]
   :seqs [:op/scalar-vec :op/scalar-vec
          ['range :small] ['range :small :small] ['range :small :small :posidx]
          ['map 'inc :seqint] ['take :idx :seqs] ['drop :idx :seqs]
          ['filter 'some? :seqs] ['remove 'nil? :seqs] ['sort :seqint]
          ['reverse :seqs] ['distinct :seqs] ['dedupe :seqs] ['concat :seqs :seqs]
          ['cons :scalar :seqs] ['rest :seqs] ['interpose :scalar :seqs]
          ['seq :seqs] ['vec :seqs] ['sort ['map 'pr-str :seqs]]]
   :seqint [:op/int-vec :op/int-vec
            ['range :small] ['range :small :small] ['range :small :small :posidx]
            ['map 'inc :seqint] ['filter 'even? :seqint] ['take :idx :seqint] ['drop :idx :seqint]
            ['sort :seqint] ['reverse :seqint] ['distinct :seqint] ['concat :seqint :seqint]]
   :map [:op/map-lit :op/map-lit
         ['assoc :map :key :any] ['assoc :map :key :any :key :any] ['dissoc :map :key]
         ['merge :map :map] ['merge-with :fn2 :map :map] ['into :map :map]
         ['select-keys :map :vec] ['update :map :key :fn1] ['zipmap :seq :seq]
         ['frequencies :seq] ['group-by :pred1 :seq] ['group-by :fn1 :seqint]
         ['reduce-kv [:lit '(fn [m k v] (assoc m k v))] [:lit {}] :map]
         ['apply 'hash-map :vec] ['empty :map] ['update-in :map ['vector :key] :fn1]
         :op/let :op/if]
   :set [:op/set-lit :op/set-lit
         ['set :seq] ['conj :set :any] ['disj :set :any] ['into :set :seq]
         ['clojure.set/union :set :set] ['clojure.set/intersection :set :set]
         ['clojure.set/difference :set :set]
         ['set :seqint] ['empty :set]
         :op/let :op/if]
   :any [['first :seq] ['last :seq] ['nth :seq :idx] ['nth :seq :idx :any] ['peek :vec]
         ['first :sortedmap] ['first :sortedset] ['last :sortedset]
         ['get :sortedmap :ikey] ['count :sortedmap] ['count :sortedset]
         ['get :map :key] ['get :map :key :any] ['get :vec :idx] ['get :vec :idx :any]
         ['get-in :map ['vector :key]] :op/kw-get
         ['some :pred1 :seq] ['reduce :fn2 :any :seq]
         ['identity :any] ['when :bool :any] ['or :any :any] ['and :any :any]
         ['max-key 'count :vec :vec] ['min-key 'count :vec :vec]
         :op/int :op/num :op/str :op/bool :op/vec :op/seq
         :op/let :op/if :op/cond :op/try :op/destructure :op/apply-fn]
   :fn1 [:op/fn1 :op/leaf]
   :pred1 [:op/pred1 :op/leaf]
   :fn2 [:op/fn2 :op/leaf]
   :coll [:op/coll]})

;; What the generator emits only while the :map-seq-order exclusion is off: a map's or a set's order, raw.
(def ^:private raw-order-ops
  [['seq :map] ['keys :map] ['vals :map] ['vec :map] ['seq :set] ['vec :set]
   ['into [:lit []] :map] ['into [:lit []] :set] ['first :map] ['map 'identity :set]])

;; ---------------------------------------------------------------- the generator

(declare gen)

(defn- scope-of [env] (set (mapcat val env)))

(defn- splice
  "Replaces every hole symbol by its [form recs], rebasing the recs on the hole's own path."
  [form holes]
  (letfn [(walk [f path]
            (cond
              (and (symbol? f) (contains? holes f))
              (let [[cf crs] (get holes f)]
                [cf (mapv (fn [r] (update r :path #(into path %))) crs)])
              (vector? f) (let [ps (map-indexed (fn [i x] (walk x (conj path i))) f)]
                            [(vec (map first ps)) (vec (mapcat second ps))])
              (seq? f) (let [ps (map-indexed (fn [i x] (walk x (conj path i))) f)]
                         [(apply list (map first ps)) (vec (mapcat second ps))])
              :else [f []]))]
    (walk form [])))

(defn- leaf [st t env]
  (let [locals (get env t)]
    (if (and (seq locals) (not (coin! st 3)))
      (pick! st locals)
      (case t
        :int (pick! st int-lits)
        :num (if (coin! st 2) (pick! st double-lits) (pick! st int-lits))
        :str (pick! st str-lits)
        :bool (pick! st [true false])
        :kw (pick! st kw-lits)
        :char (pick! st char-lits)
        :nil nil
        :key (pick! st [(pick! st kw-lits) (pick! st idx-lits) (pick! st str-lits)])
        :small (pick! st small-lits)
        :idx (pick! st idx-lits)
        :posidx (pick! st pos-lits)
        :shift (pick! st shift-lits)
        :nzint (pick! st (remove zero? int-lits))
        :ratio (pick! st [1 2 3 7])
        :ikey (pick! st [-2 -1 0 1 2 3 5 100])
        :ikeyvec (vec (distinct (repeatedly (rint! st 4) #(leaf st :ikey env))))
        :sortedmap (list 'sorted-map)
        :sortedset (list 'sorted-set)
        :nznum (if (coin! st 2) (pick! st (remove zero? double-lits)) (pick! st (remove zero? int-lits)))
        :scalar (leaf st (pick! st scalar-types) env)
        :any (leaf st (pick! st scalar-types) env)
        :vec (vec (repeatedly (rint! st 4) #(leaf st :scalar env)))
        :seq (vec (repeatedly (rint! st 4) #(leaf st :scalar env)))
        :seqint (vec (repeatedly (rint! st 4) #(leaf st :int env)))
        :seqs (vec (repeatedly (rint! st 4) #(leaf st :scalar env)))
        :map (let [ks (vec (distinct (repeatedly (inc (rint! st 3)) #(leaf st :key env))))]
               (zipmap ks (repeatedly (count ks) #(leaf st :any env))))
        :set (set (distinct (repeatedly (rint! st 4) #(leaf st :scalar env))))
        :coll (leaf st (pick! st [:vec :seq :map :set]) env)
        :fn1 (pick! st ['inc 'dec 'identity 'count 'first 'reverse 'list 'vector 'name 'boolean])
        :pred1 (pick! st ['even? 'odd? 'pos? 'neg? 'zero? 'string? 'number? 'keyword? 'nil? 'some?
                          'identity 'coll?])
        :fn2 (pick! st ['+ '- '* 'max 'min 'conj 'vector 'list])))))

(def ^:private leaf-only #{:kw :char :nil :key :ikey :ikeyvec :small :idx :posidx :shift :nzint :nznum :scalar})

(defn- special [st op t env depth]
  (let [d (dec depth)]
    (case op
      :op/leaf [(leaf st t env) {}]
      :op/coll [nil {:delegate (gen st (pick! st [:vec :seq :map :set]) env d)}]
      :op/int [nil {:delegate (gen st :int env d)}]
      :op/num [nil {:delegate (gen st :num env d)}]
      :op/str [nil {:delegate (gen st :str env d)}]
      :op/bool [nil {:delegate (gen st :bool env d)}]
      :op/vec [nil {:delegate (gen st :vec env d)}]
      :op/seq [nil {:delegate (gen st :seq env d)}]
      :op/let (let [vt (pick! st [:int :num :str :bool :any :vec :seq :seqint :map :set])
                    s (fresh! st "v")]
                [(list 'let [s '__h1] '__h2)
                 {'__h1 (gen st vt env d)
                  '__h2 (gen st t (update env vt (fnil conj []) s) d)}])
      :op/if [(list 'if '__h1 '__h2 '__h3)
              {'__h1 (gen st :bool env d) '__h2 (gen st t env d) '__h3 (gen st t env d)}]
      :op/cond [(list 'cond '__h1 '__h2 :else '__h3)
                {'__h1 (gen st :bool env d) '__h2 (gen st t env d) '__h3 (gen st t env d)}]
      :op/try [(list 'try '__h1 (list 'catch 'Throwable (fresh! st "e") '__h2))
               {'__h1 (gen st t env d) '__h2 (gen st t env d)}]
      :op/loop (let [k (rint! st 6)
                     i (fresh! st "i")
                     a (fresh! st "a")
                     env' (-> env (update t (fnil conj []) a) (update :int (fnil conj []) i))]
                 [(list 'loop [i 0 a '__h1]
                        (list 'if (list '< i k) (list 'recur (list 'inc i) '__h2) a))
                  {'__h1 (gen st t env d) '__h2 (gen st t env' d)}])
      :op/thread [(list '->> '__h1 (list 'map '__h2) (list 'filter '__h3))
                  {'__h1 (gen st :seq env d) '__h2 (gen st :fn1 env d) '__h3 (gen st :pred1 env d)}]
      :op/fn1 (let [x (fresh! st "x")]
                [(list 'fn [x] '__h1) {'__h1 (gen st :any (update env :any (fnil conj []) x) d)}])
      :op/pred1 (let [x (fresh! st "x")]
                  [(list 'fn [x] '__h1) {'__h1 (gen st :bool (update env :any (fnil conj []) x) d)}])
      :op/fn2 (let [x (fresh! st "x") y (fresh! st "x")]
                [(list 'fn [x y] '__h1)
                 {'__h1 (gen st :any (update env :any (fnil into []) [x y]) d)}])
      :op/destructure (if (coin! st 2)
                        (let [a (fresh! st "d") b (fresh! st "d") r (fresh! st "r")]
                          [(list 'let [[a b '& r] '__h1] '__h2)
                           {'__h1 (gen st :seq env d)
                            '__h2 (gen st t (-> env
                                                (update :any (fnil into []) [a b])
                                                (update :seq (fnil conj []) r)) d)}])
                        (let [a (fresh! st "d") b (fresh! st "d")]
                          [(list 'let [{a (pick! st kw-lits) b (pick! st kw-lits)} '__h1] '__h2)
                           {'__h1 (gen st :map env d)
                            '__h2 (gen st t (update env :any (fnil into []) [a b]) d)}]))
      :op/apply-fn (let [f (pick! st ['vector 'list 'max 'min 'conj])]
                     [(list 'apply f '__h1) {'__h1 (gen st :seq env d)}])
      :op/kw-get (let [k (pick! st kw-lits)]
                   (if (coin! st 2)
                     [(list k '__h1) {'__h1 (gen st :map env d)}]
                     [(list k '__h1 '__h2) {'__h1 (gen st :map env d) '__h2 (gen st :any env d)}]))
      :op/vec-lit (let [hs (mapv (fn [i] (symbol (str "__h" (inc i)))) (range (rint! st 4)))]
                    [hs (into {} (map (fn [h] [h (gen st :any env d)]) hs))])
      :op/int-vec [(vec (repeatedly (inc (rint! st 5)) #(leaf st :int env))) {}]
      :op/scalar-vec [(vec (repeatedly (rint! st 5) #(leaf st :scalar env))) {}]
      :op/map-lit (let [ks (vec (distinct (repeatedly (inc (rint! st 3)) #(leaf st :key env))))]
                    [(zipmap ks (repeatedly (count ks) #(leaf st :any env))) {}])
      :op/set-lit [(set (distinct (repeatedly (inc (rint! st 4)) #(leaf st :scalar env)))) {}]
      :op/map-bridge (let [inner (pick! st ['keys 'vals 'seq])]
                       [(list 'sort (list 'map 'pr-str (list inner '__h1)))
                        {'__h1 (gen st :map env d)}])
      :op/set-bridge [(list 'sort (list 'map 'pr-str '__h1)) {'__h1 (gen st :set env d)}])))

(defn- gen-template [st _t env depth tmpl]
  (let [n (atom 0)
        holes (atom {})]
    (letfn [(ex [spec]
              (cond
                (and (vector? spec) (= :lit (first spec))) (second spec)
                (or (vector? spec) (seq? spec)) (apply list (map ex spec))
                (keyword? spec) (let [h (symbol (str "__h" (swap! n inc)))]
                                  (swap! holes assoc h (gen st spec env (dec depth)))
                                  h)
                :else spec))]
      (splice (apply list (map ex tmpl)) @holes))))

(def ^:private fn-arity {:fn1 1 :pred1 1 :fn2 2})

;; Every generated function is wrapped so that it cannot throw: whether it throws before the element's value is
;; asked for is the JVM's chunked-seq decision, not an outcome (fuzz/exclusions.edn, :no-chunk-eagerness).
(defn- wrap-fn [st t env form recs]
  (if-let [n (fn-arity t)]
    (let [ps (vec (repeatedly n #(fresh! st "p")))
          under [2 1 0]]
      [(list 'fn ps (list 'try (apply list form ps) (list 'catch 'Throwable (fresh! st "t") nil)))
       (conj (mapv (fn [r] (-> r (update :path #(into under %)) (update :scope into ps))) recs)
             {:path [] :type t :scope (scope-of env)})])
    [form recs]))

(defn- gen
  "Returns [form recs]: a rec is {:path :type :scope} for every generated subexpression."
  [st t env depth]
  (let [[form recs]
        (if (or (<= depth 0) (leaf-only t) (coin! st 6))
          [(leaf st t env) []]
          (let [op (pick! st (get op-table t))]
            (if (keyword? op)
              (let [[tmpl holes] (special st op t env depth)]
                (if-let [dl (:delegate holes)] dl (splice tmpl holes)))
              (gen-template st t env depth op))))]
    (wrap-fn st t env form (conj (vec recs) {:path [] :type t :scope (scope-of env)}))))

(defn gen-form [st depth order-raw?]
  (if (and order-raw? (coin! st 4))
    (gen-template st :any {} depth (pick! st raw-order-ops))
    (gen st (pick! st [:int :int :num :str :bool :vec :seq :map :set :any :any :seqint :ratio :sortedmap :sortedset]) {} depth)))

;; ---------------------------------------------------------------- case files

(def ^:private prelude-path "fuzz/prelude.clj")

(defn case-text [forms {:keys [seed sort-unordered? bare-integers? group]}]
  (let [groups (partition-all (max 1 (or group 1)) (map-indexed vector forms))]
    (str ";; A pippin fuzz case; docs/notes/fuzzing.md says how it was made and how to replay it.\n"
         ";; seed " seed "\n"
         "(def fz-sort-unordered " (boolean sort-unordered?) ")\n"
         "(def fz-bare-integers " (boolean bare-integers?) ")\n"
         (slurp prelude-path)
         (str/join "\n"
                   (for [g groups]
                     (let [calls (for [[i f] g] (str "(fz " i " " (pr-str f) ")"))]
                       (if (= 1 (count calls)) (first calls) (str "(do " (str/join " " calls) ")")))))
         "\n(flush)\n")))

;; ---------------------------------------------------------------- processes

(defn- run-argv [argv timeout-s]
  (let [pb (ProcessBuilder. ^java.util.List (vec argv))
        p (.start pb)
        out (future (slurp (.getInputStream p)))
        err (future (slurp (.getErrorStream p)))
        done (.waitFor p (long timeout-s) TimeUnit/SECONDS)]
    (when-not done (.destroyForcibly p) (.waitFor p))
    {:out @out :err @err :exit (if done (.exitValue p) :timeout)}))

(defn- oracle-argv [file]
  [(str (System/getProperty "java.home") "/bin/java")
   "-cp" (System/getProperty "java.class.path")
   "clojure.main" "fuzz/oracle.clj" file])

(defn- runner-argv [cmd file] ["sh" "-c" (str cmd " '" file "'")])

(defn- transcript [{:keys [out]}]
  (into {} (for [l (str/split-lines out)
                 :let [[i v] (str/split l #"\t" 2)]
                 :when (and v (re-matches #"\d+" i))]
             [(parse-long i) v])))

(defn- eval-batch [forms opts runners tag]
  (let [file (str (:work opts) "/" tag ".clj")]
    (io/make-parents file)
    (spit file (case-text forms opts))
    (let [oracle (run-argv (oracle-argv file) (:timeout opts))]
      {:file file
       :oracle oracle
       :oracle-t (transcript oracle)
       :runners (into {} (for [[name cmd] runners
                               :let [r (run-argv (runner-argv cmd file) (:timeout opts))]]
                           [name {:run r :t (transcript r)}]))})))

(defn- sources [res]
  (into {"oracle" (:oracle-t res)} (for [[n v] (:runners res)] [n (:t v)])))

(defn- pairs-to-check [names]
  (concat (for [n names] ["oracle" n])
          (when (> (count names) 1)
            (for [[a b] (partition 2 1 names)] [a b]))))

(defn- divergences [n a b]
  (for [i (range n)
        :let [x (get a i ::missing) y (get b i ::missing)]
        :when (not= x y)]
    {:index i :a x :b y}))

;; ---------------------------------------------------------------- shrinking

(defn- prefix? [p q] (and (<= (count p) (count q)) (= (vec p) (subvec (vec q) 0 (count p)))))
(defn- strict-prefix? [p q] (and (< (count p) (count q)) (prefix? p q)))

(defn- at [form path] (reduce (fn [f i] (nth (vec f) i)) form path))

(defn- put [form path v]
  (if (empty? path)
    v
    (let [kids (vec form)
          i (first path)
          kids (assoc kids i (put (nth kids i) (vec (next path)) v))]
      (if (vector? form) kids (apply list kids)))))

(def ^:private min-lit
  {:int 0 :num 0 :str "" :bool true :kw :a :char \a :nil nil :key :a :small 0 :idx 0 :posidx 1
   :shift 0 :nzint 1 :nznum 1 :ratio 1 :ikey 0 :ikeyvec [] :scalar nil
   :sortedmap (list 'sorted-map) :sortedset (list 'sorted-set) :any nil :vec [] :seq [] :seqint [] :seqs [] :map {} :set #{} :coll []
   :fn1 'identity :pred1 'identity :fn2 'vector})

(def ^:private fits
  {:any (set any-types) :scalar (set scalar-types) :num #{:int :num}
   :ratio #{:ratio :int} :ikey #{:ikey :int} :seq #{:seq :vec :seqint :seqs} :seqs #{:seqs :seqint} :coll #{:vec :seq :seqint :seqs :map :set}})

(defn- fits? [target source]
  (or (= target source) (contains? (get fits target #{}) source)))

(defn- candidates [form recs]
  (distinct
   (concat
    (for [r recs
          :when (seq (:path r))
          :let [m (get min-lit (:type r) nil)]
          :when (not= m (at form (:path r)))]
      {:form (put form (:path r) m) :drop (:path r)})
    (for [r recs d recs
          :when (and (strict-prefix? (:path r) (:path d))
                     (fits? (:type r) (:type d))
                     (set/subset? (:scope d) (:scope r)))]
      {:form (put form (:path r) (at form (:path d))) :lift [(:path r) (:path d)]})
    (for [r recs
          :let [v (at form (:path r))]
          :when (and (coll? v) (not (map? v)) (not (seq? v)) (seq v))
          k (range (count v))]
      {:form (put form (:path r)
                   (if (set? v)
                     (disj v (nth (vec v) k))
                     (vec (concat (take k v) (drop (inc k) v)))))
       :drop (:path r)}))))

(defn- rebase [recs step]
  (cond
    (:drop step) (vec (remove #(strict-prefix? (:drop step) (:path %)) recs))
    (:lift step) (let [[p d] (:lift step)
                       kept (remove #(prefix? p (:path %)) recs)
                       moved (for [r recs :when (prefix? d (:path r))]
                               (assoc r :path (into (vec p) (subvec (vec (:path r)) (count d)))))]
                   (vec (concat kept moved)))
    :else (vec recs)))

(defn- shrink [form recs an bn opts runners tag]
  (loop [form form recs recs round 0]
    (let [cands (vec (take (:shrink-width opts) (candidates form recs)))]
      (if (or (>= round (:shrink-rounds opts)) (empty? cands))
        form
        (let [res (eval-batch (map :form cands) opts runners (str tag "-s" round))
              srcs (sources res)
              a (get srcs an) b (get srcs bn)
              hit (first (for [i (range (count cands))
                               :let [x (get a i ::missing) y (get b i ::missing)]
                               :when (and (not= x y) (not= ::missing x) (not= ::missing y))]
                           i))]
          (if-not hit
            form
            (let [c (nth cands hit)]
              (recur (:form c) (rebase recs c) (inc round)))))))))

;; ---------------------------------------------------------------- the passes

;; A name the generator uses but a runner's core lacks aborts the whole case file (docs/api-parity.md).
(defn- preflight [opts runners]
  (let [syms (->> (concat (vals op-table) raw-order-ops
                          ['sort 'map 'pr-str 'keys 'vals 'seq 'assoc 'inc 'recur 'resolve 'boolean])
                  (tree-seq coll? seq)
                  (filter symbol?)
                  distinct
                  (filter resolve)
                  (remove '#{let if cond try loop fn do quote var recur def})
                  sort vec)
        res (eval-batch (map (fn [s] (list 'boolean (list 'resolve (list 'quote s)))) syms)
                        (assoc opts :seed "preflight") runners "preflight")
        srcs (sources res)]
    (vec (for [[n _] runners
               d (divergences (count syms) (get srcs "oracle") (get srcs n))]
           (str n " cannot resolve " (nth syms (:index d)))))))

(defn- report-runner-exit [seed n v nforms]
  (when (not= 0 (:exit (:run v)))
    (println (format "seed %s: runner %s exited %s, %d of %d forms reported"
                     (str seed) n (str (:exit (:run v))) (count (:t v)) nforms))
    (let [e (str/trim (:err (:run v)))]
      (when (seq e) (println "  stderr:" (last (str/split-lines e)))))
    true))

(defn run-pass [opts runners]
  (let [t0 (System/nanoTime)
        tally (atom {:forms 0 :bad 0})
        missing (preflight opts runners)]
    (doseq [m missing] (println "fuzz:" m))
    (when (seq missing) (swap! tally update :bad + (count missing)))
    (doseq [seed (:seeds opts)]
      (let [st (new-st seed)
            pairs (vec (repeatedly (:forms opts) #(gen-form st (:depth opts) (:order-raw? opts))))
            forms (mapv first pairs)
            res (eval-batch forms (assoc opts :seed seed) runners (str "seed" seed))
            srcs (sources res)]
        (swap! tally update :forms + (count forms))
        (doseq [[n v] (:runners res)]
          (when (report-runner-exit seed n v (count forms)) (swap! tally update :bad inc)))
        (when (not= 0 (:exit (:oracle res)))
          (swap! tally update :bad inc)
          (println (format "seed %s: THE ORACLE exited %s; the case file is %s"
                           (str seed) (str (:exit (:oracle res))) (:file res)))
          (println (str/trim (:err (:oracle res)))))
        (doseq [[an bn] (pairs-to-check (map first runners))
                d (take (:per-seed opts) (divergences (count forms) (get srcs an) (get srcs bn)))]
          (swap! tally update :bad inc)
          (let [i (:index d)]
            (println (format "\nseed %s form %d: %s vs %s" (str seed) i an bn))
            (println "  form   :" (pr-str (nth forms i)))
            (println (str "  " an " : " (pr-str (:a d))))
            (println (str "  " bn " : " (pr-str (:b d))))
            (when (:shrink? opts)
              (let [[f rs] (nth pairs i)
                    tag (str "seed" seed "-f" i)
                    small (shrink f rs an bn (assoc opts :seed seed) runners tag)
                    one (eval-batch [small] (assoc opts :seed seed) runners (str tag "-min"))
                    osrc (sources one)]
                (println "  minimal:" (pr-str small))
                (println (str "  " an " : " (pr-str (get (get osrc an) 0))))
                (println (str "  " bn " : " (pr-str (get (get osrc bn) 0))))
                (println "  case   :" (:file one))))))))
    (let [secs (/ (- (System/nanoTime) t0) 1.0E9)
          s @tally]
      (println (format "\n%d forms over %d seeds in %.1f s (%.0f forms/s); %d findings"
                       (:forms s) (count (:seeds opts)) secs (/ (:forms s) secs) (:bad s)))
      (:bad s))))

(defn run-replay [opts runners dir]
  (let [files (sort-by #(.getName %) (filter #(str/ends-with? (.getName %) ".clj")
                                             (seq (.listFiles (io/file dir)))))
        bad (atom 0)]
    (doseq [f files]
      (let [path (.getPath f)
            oracle (run-argv (oracle-argv path) (:timeout opts))
            res {:oracle oracle
                 :oracle-t (transcript oracle)
                 :runners (into {} (for [[n cmd] runners
                                         :let [r (run-argv (runner-argv cmd path) (:timeout opts))]]
                                     [n {:run r :t (transcript r)}]))}
            srcs (sources res)
            n (inc (apply max -1 (keys (:oracle-t res))))
            found (atom 0)]
        (doseq [[an bn] (pairs-to-check (map first runners))
                d (divergences n (get srcs an) (get srcs bn))]
          (swap! found inc)
          (println (format "%s form %d: %s %s vs %s %s"
                           (.getName f) (:index d) an (pr-str (:a d)) bn (pr-str (:b d)))))
        (swap! bad + @found)
        (println (format "%s: %d forms, %s" (.getName f) n (if (zero? @found) "agree" "DIVERGED")))))
    @bad))

;; ---------------------------------------------------------------- CLI

(def ^:private defaults
  {:seeds [1] :forms 300 :depth 4 :group 1 :timeout 300 :work ".build/fuzz"
   :shrink? true :shrink-rounds 25 :shrink-width 400 :per-seed 3 :exclusions "fuzz/exclusions.edn"})

(defn- parse-seeds [s]
  (vec (mapcat (fn [part]
                 (if-let [[_ a b] (re-matches #"(\d+)-(\d+)" part)]
                   (range (parse-long a) (inc (parse-long b)))
                   [(parse-long part)]))
               (str/split s #","))))

(defn- parse-args [args]
  (loop [opts defaults runners [] args args]
    (if (empty? args)
      [opts runners]
      (let [[a & more] args]
        (case a
          "--seeds" (recur (assoc opts :seeds (parse-seeds (first more))) runners (rest more))
          "--forms" (recur (assoc opts :forms (parse-long (first more))) runners (rest more))
          "--depth" (recur (assoc opts :depth (parse-long (first more))) runners (rest more))
          "--group" (recur (assoc opts :group (parse-long (first more))) runners (rest more))
          "--timeout" (recur (assoc opts :timeout (parse-long (first more))) runners (rest more))
          "--work" (recur (assoc opts :work (first more)) runners (rest more))
          "--per-seed" (recur (assoc opts :per-seed (parse-long (first more))) runners (rest more))
          "--shrink-rounds" (recur (assoc opts :shrink-rounds (parse-long (first more))) runners (rest more))
          "--no-shrink" (recur (assoc opts :shrink? false) runners more)
          "--exclusions" (recur (assoc opts :exclusions (first more)) runners (rest more))
          "--runner" (let [[n c] (str/split (first more) #"=" 2)]
                       (recur opts (conj runners [n c]) (rest more)))
          (do (println "unknown option" a) (System/exit 2)))))))

(defn -main [& argv]
  (let [[cmd & args] argv
        [opts runners] (parse-args args)
        ex (load-exclusions (:exclusions opts))
        problems (audit-exclusions ex)]
    (when (seq problems)
      (binding [*out* *err*]
        (println "fuzz: an exclusion without a resolving citation is refused:")
        (doseq [p problems] (println "  " p)))
      (System/exit 1))
    (when-not (= "1.12.6" (clojure-version))
      (binding [*out* *err*]
        (println "fuzz: the oracle must be the pinned JVM Clojure 1.12.6, not" (clojure-version)))
      (System/exit 1))
    (let [have (ids ex)
          opts (assoc opts
                      :sort-unordered? (contains? have :map-seq-order)
                      :bare-integers? (contains? have :no-bigint-marker)
                      :order-raw? (not (contains? have :map-seq-order)))]
      (when-not (contains? have :map-seq-order)
        (println "fuzz: exclusion :map-seq-order is OFF, so a map's and a set's order is being compared"))
      (when-not (contains? have :no-bigint-marker)
        (println "fuzz: exclusion :no-bigint-marker is OFF, so the JVM's N suffix is being compared"))
      (case cmd
        "audit" (println (format "fuzz: %d exclusions, every citation resolves; %d uncovered areas"
                                 (count (:exclusions ex)) (count (:uncovered ex))))
        "run" (System/exit (min 1 (run-pass opts runners)))
        "replay" (System/exit (min 1 (run-replay opts runners "fuzz/regressions")))
        (do (println "usage: differential.clj audit | run | replay [--seeds 1-8] [--forms N] [--depth N]"
                     "[--group N] [--runner name=cmd]... [--no-shrink] [--per-seed N] [--work DIR]")
            (System/exit 2))))))

(apply -main *command-line-args*)
