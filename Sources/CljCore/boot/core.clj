;; @ai-generated(guided)
;; Order matters: a macro must be defined before the first form that uses it.
;; Docstrings land in the var's :doc; a helper defined with def (above defn) carries a comment instead.
;; Up to the `fn` macro only let*/loop*/fn* and the macros above a form are available;
;; defmacro emits fn* until `fn` is a macro, so those macro params cannot destructure.

;; A lazy seq of the elements of every coll, left to right.
;; Syntax-quote expands ~@ to (seq (concat ...)), so concat precedes every macro and is written
;; without any. Lazy as in Clojure: each step realizes one element of the first live argument.
(def concat
  (fn* concat
    ([] (lazy-seq* (fn* [] nil)))
    ([x] (lazy-seq* (fn* [] x)))
    ([x y]
     (lazy-seq* (fn* []
       (let* [s (seq x)]
         (if s
           (cons (first s) (concat (rest s) y))
           y)))))
    ([x y & zs]
     (let* [cat (fn* cat [xys zs]
                  (lazy-seq* (fn* []
                    (let* [xys (seq xys)]
                      (if xys
                        (cons (first xys) (cat (rest xys) zs))
                        (if zs
                          (cat (first zs) (next zs))
                          nil))))))]
       (cat (concat x y) zs)))))

(defmacro lazy-seq
  "Yields a seq that evaluates body on its first realization and caches the result."
  [& body]
  `(lazy-seq* (fn* [] ~@body)))

(defmacro when
  "Evaluates body in an implicit do when test is logical true, else nil."
  [test & body]
  `(if ~test (do ~@body)))

(defmacro when-not
  "Evaluates body in an implicit do when test is logical false, else nil."
  [test & body]
  `(if ~test nil (do ~@body)))

(defmacro if-not
  "Like if with the branches swapped: then is evaluated when test is logical false."
  ([test then] `(if-not ~test ~then nil))
  ([test then else] `(if ~test ~else ~then)))

(defmacro cond
  "Takes test/expr pairs and yields the expr of the first logical-true test, or nil
  when none passes. :else is the conventional last test."
  [& clauses]
  (when clauses
    `(if ~(first clauses)
       ~(if (next clauses)
          (second clauses)
          (throw (ex-info "cond requires an even number of forms" {})))
       (cond ~@(next (next clauses))))))

;; Clojure's destructure: bindings for let* with nested forms expanded to nth/get, or the
;; input itself when every binding form is already a symbol. Not supported: a keyword as a
;; binding form and a map key that is a keyword other than :as/:or/:keys/:strs/:syms (both are
;; spec errors in Clojure) — reported as unsupported.
(def destructure
  (fn* [bindings]
    (let* [pvec
           (fn* [pb bvec b v]
             (let* [gvec (gensym "vec__")
                    gseq (gensym "seq__")
                    gfirst (gensym "first__")
                    has-rest (loop* [bs (seq b)]
                               (if bs
                                 (if (= (first bs) '&) true (recur (next bs)))
                                 false))]
               (loop* [ret (let* [ret (conj bvec gvec v)]
                             (if has-rest (conj ret gseq (list `seq gvec)) ret))
                       n 0
                       bs (seq b)
                       seen-rest? false]
                 (if bs
                   (let* [firstb (first bs)]
                     (cond
                       (= firstb '&) (recur (pb ret (second bs) gseq) n (next (next bs)) true)
                       (= firstb :as) (pb ret (second bs) gvec)
                       :else (if seen-rest?
                               (throw (ex-info "Unsupported binding form, only :as can follow & parameter" nil))
                               (recur (pb (if has-rest
                                            (conj ret gfirst `(first ~gseq) gseq `(next ~gseq))
                                            ret)
                                          firstb
                                          (if has-rest gfirst (list `nth gvec n nil)))
                                      (inc n)
                                      (next bs)
                                      seen-rest?))))
                   ret))))
           ;; [[binding key] ...] for the idents under :keys/:strs/:syms, key made by f.
           key-entries
           (fn* [idents f]
             (loop* [s (seq idents) acc []]
               (if s
                 (recur (next s) (conj acc [(first s) (f (first s))]))
                 acc)))
           pmap
           (fn* [pb bvec b v]
             (let* [gmap (gensym "map__")
                    defaults (get b :or)
                    ret (conj bvec gmap v
                              gmap `(if (seq? ~gmap)
                                      (if (next ~gmap) (apply hash-map ~gmap) (if (seq ~gmap) (first ~gmap) {}))
                                      ~gmap))
                    ret (if (get b :as) (conj ret (get b :as) gmap) ret)
                    bes (loop* [es (seq b) acc []]
                          (if es
                            (let* [k (nth (first es) 0)
                                   x (nth (first es) 1)]
                              (recur (next es)
                                     (if (keyword? k)
                                       (let* [kn (name k)
                                              kns (namespace k)]
                                         (cond
                                           (= k :as) acc
                                           (= k :or) acc
                                           (= kn "keys") (into acc (key-entries x (fn* [i] (keyword (if kns kns (namespace i)) (name i)))))
                                           (= kn "syms") (into acc (key-entries x (fn* [i] (list 'quote (symbol (if kns kns (namespace i)) (name i))))))
                                           (= kn "strs") (into acc (key-entries x str))
                                           :else (throw (ex-info (str "Unsupported binding key: " k) nil))))
                                       (conj acc [k x]))))
                            acc))]
               (loop* [bes (seq bes) ret ret]
                 (if bes
                   (let* [bb (nth (first bes) 0)
                          bk (nth (first bes) 1)
                          local (if (if (symbol? bb) true (keyword? bb)) (symbol nil (name bb)) bb)
                          bv (if (contains? defaults local)
                               (list `get gmap bk (get defaults local))
                               (list `get gmap bk))]
                     (recur (next bes)
                            (if (symbol? local)
                              (conj ret local bv)
                              (pb ret bb bv))))
                   ret))))
           pb
           (fn* pb [bvec b v]
             (cond
               (symbol? b) (conj bvec b v)
               (vector? b) (pvec pb bvec b v)
               (map? b) (pmap pb bvec b v)
               :else (throw (ex-info (str "Unsupported binding form: " b) nil))))]
      (if (loop* [i 0]
            (if (< i (count bindings))
              (if (symbol? (nth bindings i)) (recur (+ i 2)) false)
              true))
        bindings
        (loop* [i 0 ret []]
          (if (< i (count bindings))
            (recur (+ i 2) (pb ret (nth bindings i) (nth bindings (inc i))))
            ret))))))

;; Throws unless bindings is a vector of an even number of forms; what names the form in the message.
(def ^:private check-bindings
  (fn* [what bindings]
    (if (vector? bindings)
      nil
      (throw (ex-info (str what " requires a vector for its binding") nil)))
    (if (odd? (count bindings))
      (throw (ex-info (str what " requires an even number of forms in binding vector") nil))
      nil)))

(defmacro let
  "binding => binding-form init-expr. Evaluates body with every binding-form
  destructured against its init-expr, each visible to the ones after it."
  [bindings & body]
  (check-bindings "let" bindings)
  `(let* ~(destructure bindings) ~@body))

;; Destructured bindings are re-bound to gensyms so recur still targets the loop.
(defmacro loop
  "Like let, and a recursion point that recur rebinds with as many args as there are bindings."
  [bindings & body]
  (check-bindings "loop" bindings)
  (let* [db (destructure bindings)]
    (if (= db bindings)
      `(loop* ~bindings ~@body)
      (loop* [i 0 bfs [] gs [] bs []]
        (if (< i (count bindings))
          (let* [b (nth bindings i)
                 v (nth bindings (inc i))
                 g (if (symbol? b) b (gensym))]
            (recur (+ i 2)
                   (if (symbol? b) (conj bfs g v) (conj bfs g v b g))
                   (conj gs g g)
                   (conj bs b g)))
          `(let ~bfs (loop* ~gs (let ~bs ~@body))))))))

;; Params with every non-symbol replaced by a gensym, destructured by a let wrapped around body.
(def ^:private maybe-destructured
  (fn* [params body]
    (loop* [i 0 new-params [] lets []]
      (if (< i (count params))
        (let* [p (nth params i)]
          (if (symbol? p)
            (recur (inc i) (conj new-params p) lets)
            (let* [g (gensym "p__")]
              (recur (inc i) (conj new-params g) (conj lets p g)))))
        (if (empty? lets)
          (list* new-params body)
          (list new-params `(let ~lets ~@body)))))))

(defmacro fn
  "(fn name? [params*] body) or (fn name? ([params*] body)+). fn* plus destructuring
  in the parameter vectors; name, when given, is in scope in the body."
  [& sigs]
  (let* [name (if (symbol? (first sigs)) (first sigs) nil)
         sigs (if name (next sigs) sigs)
         sigs (if (vector? (first sigs))
                (list sigs)
                (if (seq? (first sigs))
                  sigs
                  (throw (ex-info (if (seq sigs)
                                    (str "Parameter declaration " (first sigs) " should be a vector")
                                    "Parameter declaration missing")
                                  nil))))
         psig (fn* [sig]
                (if (seq? sig)
                  nil
                  (throw (ex-info (str "Invalid signature " sig " should be a list") nil)))
                (let* [params (first sig)]
                  (if (vector? params)
                    nil
                    (throw (ex-info (str "Parameter declaration " params " should be a vector") nil)))
                  (maybe-destructured params (next sig))))
         new-sigs (loop* [s (seq sigs) acc []]
                    (if s
                      (recur (next s) (conj acc (psig (first s))))
                      acc))]
    (if name
      (list* 'fn* name new-sigs)
      (list* 'fn* new-sigs))))

;; The param vectors of an fdecl, one per arity, as the :arglists value.
(def ^:private sigs
  (fn* [fdecl]
    (if (seq? (first fdecl))
      (loop* [ret [] fdecls (seq fdecl)]
        (if fdecls
          (recur (conj ret (first (first fdecls))) (next fdecls))
          (seq ret)))
      (list (first fdecl)))))

(defmacro defn
  "(defn name docstring? attr-map? [params] body... attr-map?) or with ([params] body...)+ arities.
  Same as (def name (fn ...)) with the docstring, the attr-maps and :arglists added to the var's metadata."
  [name & fdecl]
  (when-not (symbol? name)
    (throw (ex-info "First argument to defn must be a symbol" nil)))
  (let [m (if (string? (first fdecl)) {:doc (first fdecl)} {})
        fdecl (if (string? (first fdecl)) (next fdecl) fdecl)
        m (if (map? (first fdecl)) (conj m (first fdecl)) m)
        fdecl (if (map? (first fdecl)) (next fdecl) fdecl)
        fdecl (if (vector? (first fdecl)) (list fdecl) fdecl)
        m (if (map? (last fdecl)) (conj m (last fdecl)) m)
        fdecl (if (map? (last fdecl)) (butlast fdecl) fdecl)
        m (conj {:arglists (list 'quote (sigs fdecl))} m)
        m (conj (if (meta name) (meta name) {}) m)]
    (list 'def (with-meta name m) (cons `fn fdecl))))

(defmacro and
  "Evaluates its args left to right and returns the first logical-false one, or the
  last. The rest go unevaluated; (and) is true."
  ([] true)
  ([x] x)
  ([x & next]
   `(let [and# ~x]
      (if and# (and ~@next) and#))))

(defmacro or
  "Evaluates its args left to right and returns the first logical-true one, or the
  last. The rest go unevaluated; (or) is nil."
  ([] nil)
  ([x] x)
  ([x & next]
   `(let [or# ~x]
      (if or# or# (or ~@next)))))

(defmacro defn-
  "Same as defn, yielding a non-public def."
  [name & decls]
  (list* `defn (with-meta name (assoc (or (meta name) {}) :private true)) decls))

(defn vary-meta
  "Returns an object of the same type and value as obj, with (apply f (meta obj) args) as its metadata."
  [obj f & args]
  (with-meta obj (apply f (meta obj) args)))

(defmacro ->
  "Threads x into each form as its first argument: (-> x (f a) g) is (g (f x a))."
  [x & forms]
  (loop [x x forms forms]
    (if forms
      (let [form (first forms)
            threaded (if (seq? form)
                       `(~(first form) ~x ~@(next form))
                       (list form x))]
        (recur threaded (next forms)))
      x)))

(defmacro ->>
  "Threads x into each form as its last argument: (->> x (f a) g) is (g (f a x))."
  [x & forms]
  (loop [x x forms forms]
    (if forms
      (let [form (first forms)
            threaded (if (seq? form)
                       `(~(first form) ~@(next form) ~x)
                       (list form x))]
        (recur threaded (next forms)))
      x)))

(defmacro comment
  "Ignores body and yields nil. The forms are still read, so they must be readable."
  [& body])

;; Not a top-level do: that would analyze (and macroexpand) the rest with the profiler already on.
(defmacro profile
  "Runs body with the fn profiler on: {:result v :profile {:fns [...]}} (see profile-stop!)."
  [& body]
  `(let [v# (do (profile-start!)
                (try (do ~@body) (catch :default e# (profile-stop!) (throw e#))))]
     {:result v# :profile (profile-stop!)}))

(defmacro dotimes
  "binding => name n. Evaluates body once for each integer from 0 below n, with
  name bound to it. Returns nil."
  [bindings & body]
  (let [i (first bindings)
        n (second bindings)]
    `(let [n# ~n]
       (loop [~i 0]
         (when (< ~i n#)
           ~@body
           (recur (inc ~i)))))))

(defmacro if-let
  "binding => binding-form test. Evaluates then with binding-form bound to the value
  of test when that value is logical true, else evaluates else without the binding."
  ([bindings then] `(if-let ~bindings ~then nil))
  ([bindings then else]
   (when-not (and (vector? bindings) (= 2 (count bindings)))
     (throw (ex-info "if-let requires a vector of exactly 2 forms in binding" {})))
   (let [form (first bindings)
         tst (second bindings)]
     `(let [temp# ~tst]
        (if temp#
          (let [~form temp#] ~then)
          ~else)))))

(defmacro when-let
  "Like if-let with body in an implicit do and no else branch."
  [bindings & body]
  (when-not (and (vector? bindings) (= 2 (count bindings)))
    (throw (ex-info "when-let requires a vector of exactly 2 forms in binding" {})))
  (let [form (first bindings)
        tst (second bindings)]
    `(let [temp# ~tst]
       (when temp#
         (let [~form temp#] ~@body)))))

(defmacro assert
  "Throws when x is logical false, reporting the form and the message when given.
  Always evaluated: there is no flag to elide it."
  ([x]
   `(when-not ~x
      (throw (ex-info (str "Assert failed: " (pr-str '~x)) {}))))
  ([x message]
   `(when-not ~x
      (throw (ex-info (str "Assert failed: " ~message "\n" (pr-str '~x)) {})))))

(defmacro declare
  "Interns each name unbound, so forms written above its definition can refer to it."
  [& names]
  `(do ~@(loop [names (seq (reverse names)) defs nil]
           (if names
             (recur (next names) (cons `(def ~(first names)) defs))
             defs))))

;; Clojure's print-doc layout: a rule, ns/name, the arglists, Macro when it is one, the docstring indented.
(defn- print-doc [m]
  (println "-------------------------")
  (println (str (when-let [ns (:ns m)] (str ns "/")) (:name m)))
  (when (:arglists m) (prn (:arglists m)))
  (when (:macro m) (println "Macro"))
  (when (:doc m) (println " " (:doc m))))

;; #'print-doc: the expansion runs in the caller's namespace, where a private var does not resolve.
(defmacro doc
  "Prints the documentation of the var name resolves to."
  [name]
  `(#'print-doc (meta (var ~name))))

;; ---- seqs. Lazy where Clojure is lazy; eager walks use loop/recur so long seqs cost no stack.

(defn complement
  "Returns a fn taking the same args as f and returning the opposite truth value."
  [f]
  (fn [& args] (not (apply f args))))

(defn comp
  "Composes fns right to left: ((comp f g) x) is (f (g x)); (comp) is identity."
  ([] identity)
  ([f] f)
  ([f g]
   (fn
     ([] (f (g)))
     ([x] (f (g x)))
     ([x y] (f (g x y)))
     ([x y z] (f (g x y z)))
     ([x y z & args] (f (apply g x y z args)))))
  ([f g & fs]
   (reduce comp (list* f g fs))))

(defn partial
  "Returns a fn that calls f with args followed by the args of the call."
  ([f] f)
  ([f arg1] (fn [& args] (apply f arg1 args)))
  ([f arg1 arg2] (fn [& args] (apply f arg1 arg2 args)))
  ([f arg1 arg2 arg3] (fn [& args] (apply f arg1 arg2 arg3 args)))
  ([f arg1 arg2 arg3 & more] (fn [& args] (apply f arg1 arg2 arg3 (concat more args)))))

(defn constantly
  "Returns a fn that takes any number of args and returns x."
  [x]
  (fn [& args] x))

;; ---- transducers. A transducer is (fn [rf] rf'); reduce, transduce, into and sequence drive them.

(defn completing
  "Wraps f as a reducing fn whose completion arity is cf, identity by default."
  ([f] (completing f identity))
  ([f cf]
   (fn
     ([] (f))
     ([x] (cf x))
     ([x y] (f x y)))))

(defn transduce
  "Reduces coll with (xform f), seeding with (f) when init is not given, and passes
  the result through the completion arity of the transformed f."
  ([xform f coll] (transduce xform f (f) coll))
  ([xform f init coll]
   (let [f (xform f)]
     (f (reduce f init coll)))))

;; A reduced result is boxed once more so the inner reduce of cat stops without unwrapping it.
(defn- preserving-reduced [rf]
  (fn [a b]
    (let [ret (rf a b)]
      (if (reduced? ret) (reduced ret) ret))))

(defn cat
  "A transducer that concatenates the contents of each input, which must be reducible."
  [rf]
  (let [rrf (preserving-reduced rf)]
    (fn
      ([] (rf))
      ([result] (rf result))
      ([result input] (reduce rrf result input)))))

(defmacro vswap!
  "Sets the value of the volatile to (apply f current-value args) and returns it."
  [vol f & args]
  `(vreset! ~vol (~f (deref ~vol) ~@args)))

(defn nthrest
  "Returns coll without its first n items: coll itself when n is not positive,
  otherwise a seq, empty rather than nil once coll runs out."
  [coll n]
  (loop [n n xs coll]
    (if (pos? n)
      (if (seq xs) (recur (dec n) (rest xs)) (rest xs))
      xs)))

(defn some
  "Returns the first logical-true (pred x) over coll, or nil when there is none."
  [pred coll]
  (loop [s (seq coll)]
    (when s
      (or (pred (first s)) (recur (next s))))))

(defn every?
  "Returns true when (pred x) is logical true for every x in coll, and for an empty coll."
  [pred coll]
  (loop [s (seq coll)]
    (cond
      (nil? s) true
      (pred (first s)) (recur (next s))
      :else false)))

(defn not-any?
  "Returns true when no (pred x) over coll is logical true."
  [pred coll] (not (some pred coll)))

(defn not-every?
  "Returns true when some (pred x) over coll is logical false."
  [pred coll] (not (every? pred coll)))

(defn map
  "Returns a lazy seq of f applied to the items of the colls in parallel, ending
  with the shortest; with f alone, the transducer of the same."
  ([f]
   (fn [rf]
     (fn
       ([] (rf))
       ([result] (rf result))
       ([result input] (rf result (f input)))
       ([result input & inputs] (rf result (apply f input inputs))))))
  ([f coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (cons (f (first s)) (map f (rest s))))))
  ([f c1 c2]
   (lazy-seq
     (let [s1 (seq c1) s2 (seq c2)]
       (when (and s1 s2)
         (cons (f (first s1) (first s2)) (map f (rest s1) (rest s2)))))))
  ([f c1 c2 c3]
   (lazy-seq
     (let [s1 (seq c1) s2 (seq c2) s3 (seq c3)]
       (when (and s1 s2 s3)
         (cons (f (first s1) (first s2) (first s3)) (map f (rest s1) (rest s2) (rest s3)))))))
  ([f c1 c2 c3 & colls]
   (let [step (fn step [cs]
                (lazy-seq
                  (let [ss (map seq cs)]
                    (when (every? identity ss)
                      (cons (map first ss) (step (map rest ss)))))))]
     (map (fn [xs] (apply f xs)) (step (conj colls c3 c2 c1))))))

(defn filter
  "Returns a lazy seq of the items of coll for which (pred item) is logical true,
  or the transducer of the same."
  ([pred]
   (fn [rf]
     (fn
       ([] (rf))
       ([result] (rf result))
       ([result input] (if (pred input) (rf result input) result)))))
  ([pred coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [f (first s) r (rest s)]
         (if (pred f)
           (cons f (filter pred r))
           (filter pred r)))))))

(defn remove
  "Returns a lazy seq of the items of coll for which (pred item) is logical false,
  or the transducer of the same."
  ([pred] (filter (complement pred)))
  ([pred coll] (filter (complement pred) coll)))

(defn keep
  "Returns a lazy seq of the non-nil results of (f item), false kept, or the
  transducer of the same."
  ([f]
   (fn [rf]
     (fn
       ([] (rf))
       ([result] (rf result))
       ([result input]
        (let [v (f input)]
          (if (nil? v) result (rf result v)))))))
  ([f coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [x (f (first s))]
         (if (nil? x)
           (keep f (rest s))
           (cons x (keep f (rest s)))))))))

(defn take
  "Returns a lazy seq of the first n items of coll, or all of them when there are
  fewer; with n alone, the transducer of the same."
  ([n]
   (fn [rf]
     (let [nv (volatile! n)]
       (fn
         ([] (rf))
         ([result] (rf result))
         ([result input]
          (let [n @nv
                nn (vswap! nv dec)
                result (if (pos? n) (rf result input) result)]
            (if (not (pos? nn)) (ensure-reduced result) result)))))))
  ([n coll]
   (lazy-seq
     (when (pos? n)
       (when-let [s (seq coll)]
         (cons (first s) (take (dec n) (rest s))))))))

(defn drop
  "Returns a lazy seq of the items of coll past the first n, or the transducer of the same."
  ([n]
   (fn [rf]
     (let [nv (volatile! n)]
       (fn
         ([] (rf))
         ([result] (rf result))
         ([result input]
          (let [n @nv]
            (vswap! nv dec)
            (if (pos? n) result (rf result input))))))))
  ([n coll]
   (let [step (fn [n coll]
                (let [s (seq coll)]
                  (if (and (pos? n) s)
                    (recur (dec n) (rest s))
                    s)))]
     (lazy-seq (step n coll)))))

(defn take-while
  "Returns a lazy seq of the leading items of coll while (pred item) is logical
  true, or the transducer of the same."
  ([pred]
   (fn [rf]
     (fn
       ([] (rf))
       ([result] (rf result))
       ([result input] (if (pred input) (rf result input) (reduced result))))))
  ([pred coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (when (pred (first s))
         (cons (first s) (take-while pred (rest s))))))))

(defn drop-while
  "Returns a lazy seq of the items of coll from the first one for which (pred item)
  is logical false, or the transducer of the same."
  ([pred]
   (fn [rf]
     (let [dv (volatile! true)]
       (fn
         ([] (rf))
         ([result] (rf result))
         ([result input]
          (let [drop? @dv]
            (if (and drop? (pred input))
              result
              (do (vreset! dv nil) (rf result input)))))))))
  ([pred coll]
   (let [step (fn [pred coll]
                (let [s (seq coll)]
                  (if (and s (pred (first s)))
                    (recur pred (rest s))
                    s)))]
     (lazy-seq (step pred coll)))))

(defn iterate
  "Returns an infinite seq of x, (f x), (f (f x)) ... f must be free of side effects."
  [f x]
  (cons x (lazy-seq (iterate f (f x)))))

(defn repeat
  "Returns a lazy seq of x, endlessly or n times."
  ([x] (lazy-seq (cons x (repeat x))))
  ;; quot truncates a fractional count, as Repeat.create's long cast does; take alone would round up.
  ([n x] (take (quot n 1) (repeat x))))

;; Fixnum ranges are the O(1) range type; step 0 repeats as Clojure's does; doubles walk a lazy seq.
(defn range
  "Returns a seq of numbers from start (default 0) below end by step (default 1),
  counting down when step is negative; with no args an infinite seq from 0."
  ([] (iterate inc 0))
  ([end] (range 0 end 1))
  ([start end] (range start end 1))
  ([start end step]
   (cond
     (zero? step) (if (< start end) (repeat start) ())
     (and (integer? start) (integer? end) (integer? step)) (range* start end step)
     :else (let [cmp (if (pos? step) < >)]
             (take-while (fn [x] (cmp x end)) (iterate (fn [x] (+ x step)) start))))))

(defn interleave
  "Returns a lazy seq of the first item of each coll, then the second, ending with
  the shortest."
  ([] ())
  ([c1] (lazy-seq c1))
  ([c1 c2]
   (lazy-seq
     (let [s1 (seq c1) s2 (seq c2)]
       (when (and s1 s2)
         (cons (first s1) (cons (first s2) (interleave (rest s1) (rest s2))))))))
  ([c1 c2 & colls]
   (lazy-seq
     (let [ss (map seq (conj colls c2 c1))]
       (when (every? identity ss)
         (concat (map first ss) (apply interleave (map rest ss))))))))

(defn interpose
  "Returns a lazy seq of the items of coll separated by sep, or the transducer of the same."
  ([sep]
   (fn [rf]
     (let [started (volatile! false)]
       (fn
         ([] (rf))
         ([result] (rf result))
         ([result input]
          (if @started
            (let [sepr (rf result sep)]
              (if (reduced? sepr) sepr (rf sepr input)))
            (do (vreset! started true) (rf result input))))))))
  ([sep coll] (drop 1 (interleave (repeat sep) coll))))

;; Not (apply concat ...): apply spreads its whole seq here (NOTES.md), which would realize an infinite input.
(defn mapcat
  "Returns a lazy seq of the concatenated results of applying f to the items of the
  colls in parallel; with f alone, the transducer of the same."
  ([f] (comp (map f) cat))
  ([f & colls]
   (let [step (fn step [ss]
                (lazy-seq
                  (when-let [s (seq ss)]
                    (concat (first s) (step (rest s))))))]
     (step (apply map f colls)))))

(defn dorun
  "Walks coll for its side effects and returns nil; the 2-arity stops after n items."
  ([coll]
   (loop [s (seq coll)]
     (when s (recur (next s)))))
  ([n coll]
   (loop [n n s (seq coll)]
     (when (and s (pos? n))
       (recur (dec n) (next s))))))

(defn doall
  "Realizes coll and returns it; the 2-arity realizes only its first n items."
  ([coll] (dorun coll) coll)
  ([n coll] (dorun n coll) coll))

(defn vec
  "Returns a vector of the items of coll."
  {:=> [:=> [:cat :any] :vector]}
  [coll] (into [] coll))

(defn partition
  "Returns a lazy seq of n-item seqs, starting step apart (default n), dropping a
  short trailing partition, or filling it from pad (at most n items) when given."
  ([n coll] (partition n n coll))
  ([n step coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [p (doall (take n s))]
         (when (= n (count p))
           (cons p (partition n step (nthrest s step))))))))
  ([n step pad coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [p (doall (take n s))]
         (if (= n (count p))
           (cons p (partition n step pad (nthrest s step)))
           (list (take n (concat p pad)))))))))

(defn partitionv
  "partition with vector partitions."
  ([n coll] (partitionv n n coll))
  ([n step coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [p (into [] (take n) s)]
         (when (= n (count p))
           (cons p (partitionv n step (nthrest s step))))))))
  ([n step pad coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [p (into [] (take n) s)]
         (if (= n (count p))
           (cons p (partitionv n step pad (nthrest s step)))
           (list (into [] (take n) (concat p pad)))))))))

(defn splitv-at
  "Returns [(into [] (take n) coll) (drop n coll)]"
  [n coll]
  [(into [] (take n) coll) (drop n coll)])

(defn reductions
  "Returns a lazy seq of the intermediate values of the reduction (as per reduce) of coll by f,
  starting with init."
  ([f coll]
   (lazy-seq
     (if-let [s (seq coll)]
       (reductions f (first s) (rest s))
       (list (f)))))
  ([f init coll]
   (if (reduced? init)
     (list @init)
     (cons init
           (lazy-seq
             (when-let [s (seq coll)]
               (reductions f (f init (first s)) (rest s))))))))

(defn partition-all
  "Returns a lazy seq of n-item seqs like partition, keeping a short trailing one;
  the transducer flushes it on completion."
  ([n]
   (fn [rf]
     (let [a (volatile! [])]
       (fn
         ([] (rf))
         ([result]
          (let [result (if (empty? @a)
                         result
                         (let [v @a]
                           (vreset! a [])
                           (unreduced (rf result v))))]
            (rf result)))
         ([result input]
          (vswap! a conj input)
          (if (= n (count @a))
            (let [v @a]
              (vreset! a [])
              (rf result v))
            result))))))
  ([n coll] (partition-all n n coll))
  ([n step coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [seg (doall (take n s))]
         (cons seg (partition-all n step (nthrest s step))))))))

(defn partitionv-all
  "partition-all with vector partitions; the transducer is partition-all's."
  ([n] (partition-all n))
  ([n coll] (partitionv-all n n coll))
  ([n step coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [seg (into [] (take n) s)]
         (cons seg (partitionv-all n step (drop step s))))))))

(defn map-indexed
  "Returns a lazy seq of (f index item) over coll, or the transducer of the same."
  ([f]
   (fn [rf]
     (let [i (volatile! -1)]
       (fn
         ([] (rf))
         ([result] (rf result))
         ([result input] (rf result (f (vswap! i inc) input)))))))
  ([f coll]
   (let [mapi (fn mapi [idx coll]
                (lazy-seq
                  (when-let [s (seq coll)]
                    (cons (f idx (first s)) (mapi (inc idx) (rest s))))))]
     (mapi 0 coll))))

(defn keep-indexed
  "Returns a lazy seq of the non-nil results of (f index item), or the transducer of the same."
  ([f]
   (fn [rf]
     (let [iv (volatile! -1)]
       (fn
         ([] (rf))
         ([result] (rf result))
         ([result input]
          (let [i (vswap! iv inc)
                v (f i input)]
            (if (nil? v) result (rf result v))))))))
  ([f coll]
   (let [keepi (fn keepi [idx coll]
                 (lazy-seq
                   (when-let [s (seq coll)]
                     (let [x (f idx (first s))]
                       (if (nil? x)
                         (keepi (inc idx) (rest s))
                         (cons x (keepi (inc idx) (rest s))))))))]
     (keepi 0 coll))))

;; Push-based transducers over a pull-based seq: each realization feeds one input to xf, whose rf parks the
;; outputs in buf, and the outputs come out as the next items, so an infinite source stays lazy.
(defn sequence
  "Coerces coll to a seq, () when empty; with xform, a lazy seq of the transformed items."
  ([coll] (if (seq? coll) coll (or (seq coll) ())))
  ([xform coll]
   (let [buf (volatile! [])
         xf (xform (fn ([] nil) ([acc] acc) ([acc x] (vswap! buf conj x) nil)))
         drain (fn [] (let [out @buf] (vreset! buf []) out))
         step (fn step [s]
                (lazy-seq
                  (loop [s (seq s)]
                    (if s
                      (let [r (xf nil (first s))
                            out (drain)]
                        (cond
                          (reduced? r) (do (xf nil) (concat out (drain)))
                          (seq out) (concat out (step (rest s)))
                          :else (recur (next s))))
                      (do (xf nil) (seq (drain)))))))]
     (step coll))))

(defn halt-when
  "Returns a transducer that ends transduction when pred is true for an input. When retf is
  supplied it must be a fn of 2 arguments: the (completed) result so far and the input that
  triggered the predicate; its return value becomes the result. Without retf the input is."
  ([pred] (halt-when pred nil))
  ([pred retf]
   (fn [rf]
     (fn
       ([] (rf))
       ([result]
        (if (and (map? result) (contains? result ::halt))
          (::halt result)
          (rf result)))
       ([result input]
        (if (pred input)
          (reduced {::halt (if retf (retf (rf result) input) input)})
          (rf result input)))))))

(defn dedupe
  "Removes consecutive duplicates: a lazy seq over coll, or the transducer of the same."
  ([]
   (fn [rf]
     (let [pv (volatile! :clojure.core/none)]
       (fn
         ([] (rf))
         ([result] (rf result))
         ([result input]
          (let [prior @pv]
            (vreset! pv input)
            (if (= prior input) result (rf result input))))))))
  ([coll] (sequence (dedupe) coll)))

(defn distinct
  "Removes duplicates: a lazy seq over coll, or the transducer of the same. Sets carry the seen elements."
  ([]
   (fn [rf]
     (let [seen (volatile! #{})]
       (fn
         ([] (rf))
         ([result] (rf result))
         ([result input]
          (if (contains? @seen input)
            result
            (do (vswap! seen conj input)
                (rf result input))))))))
  ([coll]
   (let [step (fn step [xs seen]
                (lazy-seq
                  (loop [s (seq xs) seen seen]
                    (when s
                      (let [f (first s)]
                        (if (contains? seen f)
                          (recur (next s) seen)
                          (cons f (step (rest s) (conj seen f)))))))))]
     (step coll #{}))))

(defn group-by
  "Returns a map from each (f x) to the vector of the xs with that key, in order."
  [f coll]
  (reduce (fn [ret x]
            (let [k (f x)]
              (assoc ret k (conj (get ret k []) x))))
          {} coll))

(defn frequencies
  "Returns a map from each distinct item of coll to the number of times it appears."
  [coll]
  (reduce (fn [counts x] (assoc counts x (inc (get counts x 0)))) {} coll))

(defn zipmap
  "Returns a map of the keys to the corresponding vals, ending with the shorter."
  [keys vals]
  (loop [m {} ks (seq keys) vs (seq vals)]
    (if (and ks vs)
      (recur (assoc m (first ks) (first vs)) (next ks) (next vs))
      m)))

(defn get-in
  "Returns the value at the path of keys, or not-found (default nil) where a step is missing."
  ([m ks] (reduce get m ks))
  ([m ks not-found]
   (loop [m m ks (seq ks)]
     (if ks
       (let [v (get m (first ks) :clojure.core/not-found)]
         (if (identical? v :clojure.core/not-found) not-found (recur v (next ks))))
       m))))

(defn assoc-in
  "Associates v at the path of keys, creating nested maps where a step is missing."
  [m [k & ks] v]
  (if ks (assoc m k (assoc-in (get m k) ks v)) (assoc m k v)))

(defn update
  "Replaces the value at k with (f current args...)."
  ([m k f] (assoc m k (f (get m k))))
  ([m k f x] (assoc m k (f (get m k) x)))
  ([m k f x y] (assoc m k (f (get m k) x y)))
  ([m k f x y & more] (assoc m k (apply f (get m k) x y more))))

(defn update-in
  "Replaces the value at the path of keys with (f current args...), creating nested maps where a step is missing."
  [m ks f & args]
  (let [up (fn up [m ks f args]
             (let [[k & ks] ks]
               (if ks
                 (assoc m k (up (get m k) ks f args))
                 (assoc m k (apply f (get m k) args)))))]
    (up m ks f args)))


;; ---- more of the core library: predicates, numbers, seqs, maps, control macros.

(defn boolean
  "Coerces to boolean: nil and false are false, everything else true."
  [x] (if x true false))

(defn true? "Returns true when x is the value true." [x] (identical? x true))
(defn false? "Returns true when x is the value false." [x] (identical? x false))
(defn some? "Returns true when x is not nil." [x] (not (nil? x)))
(defn boolean? "Returns true when x is a boolean." [x] (or (true? x) (false? x)))
(defn parse-boolean
  "Parses \"true\" or \"false\" to the boolean; nil for any other string, a throw for a non-string."
  [s]
  (if (string? s)
    (cond (= s "true") true (= s "false") false :else nil)
    (throw (ex-info (str (type s) " cannot be cast to a string") {}))))
(defn any? "Returns true given any argument." [x] true)
(defn ident? "Returns true when x is a symbol or keyword." [x] (or (keyword? x) (symbol? x)))
(defn simple-ident? "Returns true when x is an unqualified symbol or keyword." [x] (and (ident? x) (nil? (namespace x))))
(defn qualified-ident? "Returns true when x is a qualified symbol or keyword." [x] (boolean (and (ident? x) (namespace x) true)))
(defn simple-symbol? "Returns true when x is an unqualified symbol." [x] (and (symbol? x) (nil? (namespace x))))
(defn qualified-symbol? "Returns true when x is a qualified symbol." [x] (boolean (and (symbol? x) (namespace x) true)))
(defn simple-keyword? "Returns true when x is an unqualified keyword." [x] (and (keyword? x) (nil? (namespace x))))
(defn qualified-keyword? "Returns true when x is a qualified keyword." [x] (boolean (and (keyword? x) (namespace x) true)))
(defn nat-int? "Returns true when x is a non-negative fixed-precision integer." [x] (and (int? x) (not (neg? x))))
(defn pos-int? "Returns true when x is a positive fixed-precision integer." [x] (and (int? x) (pos? x)))
(defn neg-int? "Returns true when x is a negative fixed-precision integer." [x] (and (int? x) (neg? x)))
(defn float? "Returns true when x is a floating point number." [x] (double? x))
(defn distinct?
  "Returns true when no two of the arguments are equal."
  ([x] true)
  ([x y] (not (= x y)))
  ([x y & more]
   (if (not= x y)
     (loop [s #{x y} xs more]
       (if xs
         (if (contains? s (first xs)) false (recur (conj s (first xs)) (next xs)))
         true))
     false)))

(defn max
  "Returns the greatest of the nums; a NaN argument wins, as clojure.lang.Numbers/max does."
  ([x] x)
  ([x y] (cond (NaN? x) x (NaN? y) y (> x y) x :else y))
  ([x y & more] (reduce max (max x y) more)))

(defn min
  "Returns the least of the nums; a NaN argument wins, as clojure.lang.Numbers/min does."
  ([x] x)
  ([x y] (cond (NaN? x) x (NaN? y) y (< x y) x :else y))
  ([x y & more] (reduce min (min x y) more)))

(defn abs "Returns the absolute value of a." [a] (if (neg? a) (- a) a))

(defn mod
  "Modulus of num and div, with the sign of div."
  [num div]
  (let [m (rem num div)]
    (if (or (zero? m) (= (pos? num) (pos? div))) m (+ m div))))

(defn max-key
  "Returns the x for which (k x), a number, is greatest; the last one on ties."
  ([k x] x)
  ([k x y] (if (> (k x) (k y)) x y))
  ([k x y & more]
   (let [kx (k x) ky (k y)
         [v kv] (if (> kx ky) [x kx] [y ky])]
     (loop [v v kv kv more more]
       (if more
         (let [w (first more) kw (k w)]
           (if (>= kw kv) (recur w kw (next more)) (recur v kv (next more))))
         v)))))

(defn min-key
  "Returns the x for which (k x), a number, is least; the last one on ties."
  ([k x] x)
  ([k x y] (if (< (k x) (k y)) x y))
  ([k x y & more]
   (let [kx (k x) ky (k y)
         [v kv] (if (< kx ky) [x kx] [y ky])]
     (loop [v v kv kv more more]
       (if more
         (let [w (first more) kw (k w)]
           (if (<= kw kv) (recur w kw (next more)) (recur v kv (next more))))
         v)))))

(defn rand
  "Returns a random double in [0, n), n defaulting to 1."
  ([] (rand*))
  ([n] (* n (rand*))))

(defn rand-int "Returns a random integer in [0, n)." [n] (int (rand n)))

(defn random-sample
  "Returns items from coll with random probability of prob (0.0 - 1.0), or the transducer of the same."
  ([prob] (filter (fn [_] (< (rand) prob))))
  ([prob coll] (filter (fn [_] (< (rand) prob)) coll)))

(defn ffirst "Same as (first (first x))" [x] (first (first x)))
(defn nfirst "Same as (next (first x))" [x] (next (first x)))
(defn fnext "Same as (first (next x))" [x] (first (next x)))
(defn nnext "Same as (next (next x))" [x] (next (next x)))

(defn nthnext
  "Returns the nth next of coll, (seq coll) when n is 0."
  [coll n]
  (loop [n n xs (seq coll)]
    (if (and xs (pos? n)) (recur (dec n) (next xs)) xs)))

(defn not-empty "Returns coll when it has items, else nil." [coll] (when (seq coll) coll))

(defn peek
  "For a list or a queue, the first item; for a vector, the last. nil for an empty collection."
  [coll]
  (cond (nil? coll) nil
        (vector? coll) (when (pos? (count coll)) (nth coll (dec (count coll))))
        (instance? PersistentQueue coll) (first coll)
        (list? coll) (first coll)
        :else (throw (ex-info (str "peek not supported on this type: " (type coll)) {}))))

(defn pop
  "For a list or a queue, without its first item; for a vector, without its last. Throws on an empty
  list or vector; an empty queue pops to itself."
  [coll]
  (cond (nil? coll) nil
        (vector? coll) (if (pos? (count coll))
                         (into [] (take (dec (count coll)) coll))
                         (throw (ex-info "Can't pop empty vector" {})))
        (instance? PersistentQueue coll) (queue-pop* coll)
        ;; PersistentList.pop hands the empty list the popped list's meta.
        (list? coll) (cond (next coll) (rest coll)
                           (seq coll) (with-meta () (meta coll))
                           :else (throw (ex-info "Can't pop empty list" {})))
        :else (throw (ex-info (str "pop not supported on this type: " (type coll)) {}))))

(defn subvec
  "Returns a vector of the items of v from start (inclusive) to end (exclusive, default count)."
  ([v start] (subvec v start (count v)))
  ([v start end]
   (if (or (neg? start) (> end (count v)) (> start end))
     (throw (ex-info (str "Index out of bounds: subvec " start " " end) {}))
     (loop [i start acc []]
       (if (< i end) (recur (inc i) (conj acc (nth v i))) acc)))))

(defn rseq
  "Returns a seq of the items of a vector or sorted collection in reverse order, nil when empty."
  [v]
  (cond
    (sorted? v) (sorted-seq* v false)
    (vector? v) (seq (reverse v))
    :else (throw (ex-info (str "rseq not supported on this type: " (type v)) {}))))

(defn keys "Returns a seq of the map's keys." {:=> [:=> [:cat [:maybe :map]] [:maybe :seq]]} [m] (seq (map (fn [e] (nth e 0)) m)))
(defn vals "Returns a seq of the map's values." {:=> [:=> [:cat [:maybe :map]] [:maybe :seq]]} [m] (seq (map (fn [e] (nth e 1)) m)))
(defn map-entry? "Returns true when x is a map entry (a two-element vector here)." [x] (and (vector? x) (= 2 (count x))))
(defn key
  "Returns the key of the map entry."
  [e]
  (if (map-entry? e) (nth e 0) (throw (ex-info (str (type e) " cannot be cast to a map entry") {}))))
(defn val
  "Returns the value of the map entry."
  [e]
  (if (map-entry? e) (nth e 1) (throw (ex-info (str (type e) " cannot be cast to a map entry") {}))))

(defn find
  "Returns the map entry for key, or nil when absent."
  [m k]
  (when (and (or (map? m) (vector? m)) (contains? m k)) [k (get m k)]))

(defn select-keys
  "Returns a map of only the entries of m whose key is in keyseq."
  [m keyseq]
  (loop [ret {} ks (seq keyseq)]
    (if ks
      (let [k (first ks) e (find m k)]
        (recur (if e (conj ret e) ret) (next ks)))
      (with-meta ret (meta m)))))

(defn merge
  "Returns a map of the maps conj'd left to right; a later key wins. nil when every map is nil."
  [& maps]
  (when (some identity maps)
    (reduce (fn [m1 m2] (conj (or m1 {}) m2)) maps)))

(defn merge-with
  "Like merge, but (f val-in-result val-in-latter) resolves a key present in both."
  [f & maps]
  (when (some identity maps)
    (let [merge-entry (fn [m e]
                        (let [k (key e) v (val e)]
                          (if (contains? m k) (assoc m k (f (get m k) v)) (assoc m k v))))
          merge2 (fn [m1 m2] (reduce merge-entry (or m1 {}) (seq m2)))]
      (reduce merge2 maps))))

(defn juxt
  "Returns a fn that returns a vector of the results of applying each f to its args."
  ([f] (fn [& args] [(apply f args)]))
  ([f g] (fn [& args] [(apply f args) (apply g args)]))
  ([f g h] (fn [& args] [(apply f args) (apply g args) (apply h args)]))
  ([f g h & fs]
   (let [fs (list* f g h fs)]
     (fn [& args] (reduce (fn [acc f] (conj acc (apply f args))) [] fs)))))

(defn some-fn
  "Returns a fn that returns the first logical-true value of any p applied to its args, else the last
  falsey one: ((some-fn even?) 1) is false, not nil, as Clojure's is."
  [p & more]
  (fn [& args]
    (loop [ps (cons p more) falsey nil]
      (if-not ps
        falsey
        (let [v (loop [as (seq args) falsey nil]
                  (if-not as
                    falsey
                    (let [r ((first ps) (first as))]
                      (if r r (recur (next as) r)))))]
          (if v v (recur (next ps) v)))))))

(defn every-pred
  "Returns a fn that returns true when every p is logical true of every arg."
  [p & more]
  (let [ps (cons p more)]
    (fn [& args] (every? (fn [p] (every? p args)) ps))))

(defn fnil
  "Returns a fn calling f with nil leading arguments replaced by the defaults."
  ([f x] (fn [a & args] (apply f (if (nil? a) x a) args)))
  ([f x y] (fn [a b & args] (apply f (if (nil? a) x a) (if (nil? b) y b) args)))
  ([f x y z] (fn [a b c & args] (apply f (if (nil? a) x a) (if (nil? b) y b) (if (nil? c) z c) args))))

(defn cycle
  "Returns a lazy infinite seq of repetitions of the items in coll."
  [coll]
  (let [step (fn step [s] (lazy-seq (if s (cons (first s) (step (next s))) (step (seq coll)))))]
    (lazy-seq (when (seq coll) (step (seq coll))))))

(defn repeatedly
  "Returns a lazy seq of calls to f, endlessly or n times."
  ([f] (lazy-seq (cons (f) (repeatedly f))))
  ([n f] (take n (repeatedly f))))

(defn take-last
  "Returns a seq of the last n items of coll."
  [n coll]
  (loop [s (seq coll) lead (seq (drop n coll))]
    (if lead (recur (next s) (next lead)) s)))

(defn take-nth
  "Returns a lazy seq of every nth item of coll, or the transducer of the same."
  ([n]
   (fn [rf]
     (let [iv (volatile! -1)]
       (fn
         ([] (rf))
         ([result] (rf result))
         ([result input]
          (let [i (vswap! iv inc)]
            (if (zero? (rem i n)) (rf result input) result)))))))
  ([n coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (cons (first s) (take-nth n (drop n s)))))))

(defn drop-last
  "Returns a lazy seq of all but the last n (default 1) items of coll."
  ([coll] (drop-last 1 coll))
  ([n coll] (map (fn [x _] x) coll (drop n coll))))

(defn split-at "Returns [(take n coll) (drop n coll)]" [n coll] [(take n coll) (drop n coll)])
(defn split-with "Returns [(take-while pred coll) (drop-while pred coll)]" [pred coll] [(take-while pred coll) (drop-while pred coll)])
(defn replicate "DEPRECATED: Use 'repeat' instead. Returns a lazy seq of n xs." [n x] (take n (repeat x)))
(defmacro lazy-cat
  "Expands to (concat (lazy-seq e1) (lazy-seq e2) ...): each argument is evaluated only as it is reached."
  [& colls]
  `(concat ~@(map (fn [c] `(lazy-seq ~c)) colls)))
(defn reversible? "Returns true when rseq is supported: vectors and the sorted collections." [coll] (or (vector? coll) (sorted? coll)))

(defn bounded-count
  "The count of a counted coll, else at most n items of its seq, walked."
  [n coll]
  (if (counted? coll)
    (count coll)
    (loop [i 0 s (seq coll)]
      (if (and s (< i n))
        (recur (inc i) (next s))
        i))))


(defn flatten
  "Returns a lazy seq of the leaves of a nested sequential collection; () for anything else."
  [x]
  (let [walk (fn walk [x]
               (lazy-seq
                 (when-let [s (seq x)]
                   (let [f (first s)]
                     (if (sequential? f)
                       (concat (walk f) (walk (rest s)))
                       (cons f (walk (rest s))))))))]
    (if (sequential? x) (walk x) ())))

(defn mapv
  "Returns a vector of f applied to the items of the colls."
  ([f coll] (reduce (fn [v x] (conj v (f x))) [] coll))
  ([f c1 c2] (into [] (map f c1 c2)))
  ([f c1 c2 c3] (into [] (map f c1 c2 c3)))
  ([f c1 c2 c3 & colls] (into [] (apply map f c1 c2 c3 colls))))

(defn filterv
  "Returns a vector of the items of coll for which (pred item) is logical true."
  [pred coll]
  (reduce (fn [v x] (if (pred x) (conj v x) v)) [] coll))

(defn run!
  "Runs (proc x) over every item of coll for its side effects; returns nil."
  [proc coll]
  (reduce (fn [_ x] (proc x)) nil coll)
  nil)

;; compare and sort are host primitives bound after boot (Primitives.swift); both delegate to the C
;; comparator (compare.h), which sort-by* and the sorted collections reach without the host at all.
(declare compare sort)

(defn sort-by
  "Returns a sorted sequence of the items in coll, by (compare (keyfn a) (keyfn b)) or comp on the keys."
  ([keyfn coll] (sort-by* keyfn coll))
  ([keyfn comp coll] (sort-by* keyfn comp coll)))

(defn sorted-map
  "Returns a sorted map of the key/value pairs, ordered by compare."
  [& keyvals]
  (apply sorted-map* keyvals))

(defn sorted-set
  "Returns a sorted set of the keys, ordered by compare."
  [& ks]
  (apply sorted-set* ks))

;; Arrays are mutable, so amap clones before writing and areduce only reads (NOTES.md, "Arrays").
(defmacro amap
  "Maps expr over array a, binding idx to each index and ret to a clone of a; returns ret."
  [a idx ret expr]
  `(let [arr# ~a
         ~ret (aclone arr#)]
     (loop [~idx 0]
       (if (< ~idx (alength arr#))
         (do (aset ~ret ~idx ~expr)
             (recur (inc ~idx)))
         ~ret))))

(defmacro areduce
  "Reduces expr over array a, binding idx to each index and ret to the accumulator, seeded with init."
  [a idx ret init expr]
  `(let [arr# ~a]
     (loop [~idx 0 ~ret ~init]
       (if (< ~idx (alength arr#))
         (recur (inc ~idx) ~expr)
         ~ret))))

;; The collection's own comparator orders the bound, so a custom one bounds subseq as it orders the tree.
(defn- mk-bound-fn [sc test k]
  (let [entry-key (if (map? sc) (fn [e] (nth e 0)) identity)]
    (fn [e] (test (sorted-compare* sc (entry-key e) k) 0))))

(defn subseq
  "Ascending seq of the entries of a sorted collection whose keys pass the test(s): <, <=, > or >=."
  ([sc test k]
   (let [include (mk-bound-fn sc test k)]
     (if (or (identical? test >) (identical? test >=))
       (when-let [s (sorted-seq-from* sc k true)]
         (if (include (first s)) s (next s)))
       (take-while include (sorted-seq* sc true)))))
  ([sc start-test start end-test end]
   (when-let [s (sorted-seq-from* sc start true)]
     (take-while (mk-bound-fn sc end-test end)
                 (if ((mk-bound-fn sc start-test start) (first s)) s (next s))))))

(defn rsubseq
  "Descending seq of the entries of a sorted collection whose keys pass the test(s): <, <=, > or >=."
  ([sc test k]
   (let [include (mk-bound-fn sc test k)]
     (if (or (identical? test <) (identical? test <=))
       (when-let [s (sorted-seq-from* sc k false)]
         (if (include (first s)) s (next s)))
       (take-while include (sorted-seq* sc false)))))
  ([sc start-test start end-test end]
   (when-let [s (sorted-seq-from* sc end false)]
     (take-while (mk-bound-fn sc start-test start)
                 (if ((mk-bound-fn sc end-test end) (first s)) s (next s))))))

(defn partition-by
  "Returns a lazy seq of partitions, splitting each time (f item) changes; or the transducer of the same."
  ([f]
   (fn [rf]
     (let [a (volatile! []) pv (volatile! :clojure.core/none)]
       (fn
         ([] (rf))
         ([result]
          (let [result (if (empty? @a)
                         result
                         (let [v @a] (vreset! a []) (unreduced (rf result v))))]
            (rf result)))
         ([result input]
          (let [pval @pv val (f input)]
            (vreset! pv val)
            (if (or (identical? pval :clojure.core/none) (= val pval))
              (do (vswap! a conj input) result)
              (let [v @a]
                (vreset! a [])
                (let [ret (rf result v)]
                  (when-not (reduced? ret) (vswap! a conj input))
                  ret)))))))))
  ([f coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [fst (first s)
             fv (f fst)
             run (cons fst (take-while (fn [x] (= fv (f x))) (next s)))]
         (cons run (partition-by f (lazy-seq (drop (count run) s)))))))))

(defn tree-seq
  "Returns a lazy seq of the nodes of a tree, depth first: branch? tells whether a node has children, children returns them."
  [branch? children root]
  (let [walk (fn walk [node]
               (lazy-seq
                 (cons node (when (branch? node) (mapcat walk (children node))))))]
    (walk root)))

(defn shuffle
  "Returns a vector of the items of coll in random order."
  [coll]
  (when-not (or (vector? coll) (list? coll) (seq? coll) (set? coll))
    (throw (ex-info (str "shuffle not supported on this type: " (type coll)) {})))
  (loop [v (vec coll) i (dec (count v))]
    (if (pos? i)
      (let [j (rand-int (inc i)) x (nth v i)]
        (recur (assoc (assoc v i (nth v j)) j x) (dec i)))
      v)))

(defn rand-nth "Returns a random item of coll." [coll] (nth coll (rand-int (count coll))))

(defn array-map "Returns a map of the key/value pairs; the same map type as hash-map here." [& kvs] (apply hash-map kvs))

;; One matcher drives the whole seq, so two consumers of the same re-seq share its position, as in Clojure.
(defn re-seq
  "Returns a lazy seq of successive matches of re in s, each as re-find returns it."
  [re s]
  (let [m (re-matcher re s)
        step (fn step []
               (lazy-seq
                 (when-let [found (re-find m)]
                   (cons found (step)))))]
    (step)))

;; Transients are the persistent operations themselves: no separate mutable phase (NOTES.md).
(defn transient "Returns coll itself: persistent operations stand in for transients here." [coll] coll)
(defn persistent! "Returns coll itself (see transient)." [coll] coll)
(defn conj! "conj on a transient (see transient)." ([] (transient [])) ([coll] coll) ([coll x] (conj coll x)))
(defn assoc! "assoc on a transient (see transient)." ([coll k v] (assoc coll k v)) ([coll k v & kvs] (apply assoc coll k v kvs)))
(defn dissoc! "dissoc on a transient (see transient)." ([m k] (dissoc m k)) ([m k & ks] (apply dissoc m k ks)))
(defn disj! "disj on a transient (see transient)." ([s k] (disj s k)) ([s k & ks] (apply disj s k ks)))
(defn pop! "pop on a transient (see transient)." [coll] (pop coll))

(defn update-keys
  "m with f applied to each key, keeping m's meta; f must produce distinct keys."
  [m f]
  (let [ret (persistent! (reduce-kv (fn [acc k v] (assoc! acc (f k) v)) (transient {}) m))]
    (with-meta ret (meta m))))

(defn update-vals
  "m with f applied to each value, keeping m's meta and, for an editable map, its type."
  [m f]
  (with-meta
    (persistent!
      (reduce-kv (fn [acc k v] (assoc! acc k (f v)))
                 (if (instance? IEditableCollection m) (transient m) (transient {}))
                 m))
    (meta m)))

;; ---- control macros

(defmacro when-first
  "bindings => x xs. Evaluates body with x bound to the first item of xs when xs has one, else nil."
  [bindings & body]
  (let [x (first bindings) xs (second bindings)]
    `(when-let [xs# (seq ~xs)]
       (let [~x (first xs#)] ~@body))))

(defmacro if-some
  "bindings => binding-form test. Like if-let, but binds when the test value is not nil."
  ([bindings then] `(if-some ~bindings ~then nil))
  ([bindings then else]
   (let [form (first bindings) tst (second bindings)]
     `(let [temp# ~tst]
        (if (nil? temp#) ~else (let [~form temp#] ~then))))))

(defmacro when-some
  "Like when-let, but binds when the test value is not nil."
  [bindings & body]
  (let [form (first bindings) tst (second bindings)]
    `(let [temp# ~tst]
       (if (nil? temp#) nil (let [~form temp#] ~@body)))))

(defmacro while
  "Evaluates body while test is logical true."
  [test & body]
  `(loop [] (when ~test ~@body (recur))))

(defmacro doto
  "Evaluates x, then each form with x as its first argument; returns x."
  [x & forms]
  (let [gx (gensym)]
    `(let [~gx ~x]
       ~@(map (fn [f] (if (seq? f) `(~(first f) ~gx ~@(next f)) `(~f ~gx))) forms)
       ~gx)))

(defmacro cond->
  "Threads expr through the forms whose test is logical true, as ->."
  [expr & clauses]
  (assert (even? (count clauses)))
  (let [g (gensym)
        steps (map (fn [[test step]] `(if ~test (-> ~g ~step) ~g)) (partition 2 clauses))]
    `(let [~g ~expr ~@(interleave (repeat g) (butlast steps))]
       ~(if (empty? steps) g (last steps)))))

(defmacro cond->>
  "Threads expr through the forms whose test is logical true, as ->>."
  [expr & clauses]
  (assert (even? (count clauses)))
  (let [g (gensym)
        steps (map (fn [[test step]] `(if ~test (->> ~g ~step) ~g)) (partition 2 clauses))]
    `(let [~g ~expr ~@(interleave (repeat g) (butlast steps))]
       ~(if (empty? steps) g (last steps)))))

(defmacro as->
  "Binds name to expr, then to each successive form's value; returns the last."
  [expr name & forms]
  `(let [~name ~expr ~@(interleave (repeat name) (butlast forms))]
     ~(if (empty? forms) name (last forms))))

(defmacro some->
  "Threads expr through the forms as ->, stopping at the first nil."
  [expr & forms]
  (let [g (gensym)
        steps (map (fn [step] `(if (nil? ~g) nil (-> ~g ~step))) forms)]
    `(let [~g ~expr ~@(interleave (repeat g) (butlast steps))]
       ~(if (empty? steps) g (last steps)))))

(defmacro some->>
  "Threads expr through the forms as ->>, stopping at the first nil."
  [expr & forms]
  (let [g (gensym)
        steps (map (fn [step] `(if (nil? ~g) nil (->> ~g ~step))) forms)]
    `(let [~g ~expr ~@(interleave (repeat g) (butlast steps))]
       ~(if (empty? steps) g (last steps)))))

(defmacro case
  "Takes an expression and clauses of constant/result pairs; a list groups constants. Constants are
  compared with =, so a clause is O(n) in the number of clauses, not a jump table."
  [e & clauses]
  (let [ge (gensym "case__")
        default? (odd? (count clauses))
        default (if default? (last clauses) `(throw (ex-info (str "No matching clause: " ~ge) {})))
        pairs (partition 2 (if default? (butlast clauses) clauses))
        test (fn [c]
               (if (seq? c)
                 `(or ~@(map (fn [x] `(= ~ge '~x)) c))
                 `(= ~ge '~c)))]
    `(let [~ge ~e]
       (cond ~@(mapcat (fn [[c r]] [(test c) r]) pairs)
             :else ~default))))

(defmacro condp
  "Takes a binary predicate, an expression, and clauses of test-expr/result pairs; (pred test-expr expr)
  selects. A :>> after a test passes the predicate's result to the result fn. A trailing single
  expression is the default, else a match failure throws."
  [pred expr & clauses]
  (let [gpred (gensym "pred__")
        gexpr (gensym "expr__")
        emit (fn emit [pred expr args]
               (let [[[a b c :as clause] more] (split-at (if (= :>> (second args)) 3 2) args)
                     n (count clause)]
                 (cond
                   (= 0 n) `(throw (ex-info (str "No matching clause: " ~expr) {}))
                   (= 1 n) a
                   (= 2 n) `(if (~pred ~a ~expr) ~b ~(emit pred expr more))
                   :else `(if-let [p# (~pred ~a ~expr)] (~c p#) ~(emit pred expr more)))))]
    `(let [~gpred ~pred ~gexpr ~expr]
       ~(emit gpred gexpr clauses))))

;; Each fn's body rebinds every letfn name from a volatile at entry: closures copy their captures when
;; made, so a forward reference is read at call time instead (NOTES.md).
(defmacro letfn
  "fnspecs => (fname [params*] body) or (fname ([params*] body)+). Binds the fns, which may refer
  to each other, then evaluates body."
  [fnspecs & body]
  (let [names (map first fnspecs)
        cells (map (fn [n] (gensym (str (name n) "__cell"))) names)
        rebind (vec (interleave names (map (fn [c] `(deref ~c)) cells)))
        wrap (fn [[n & sigs]]
               (let [sigs (if (vector? (first sigs)) (list sigs) sigs)]
                 `(fn ~n ~@(map (fn [[params & b]] `(~params (let ~rebind ~@b))) sigs))))]
    `(let [~@(interleave cells (repeat `(volatile! nil)))]
       ~@(map (fn [c spec] `(vreset! ~c ~(wrap spec))) cells fnspecs)
       (let ~rebind ~@body))))

(defmacro doseq
  "Like for, for side effects: nested seq bindings with :let, :when and :while modifiers; returns nil."
  [seq-exprs & body]
  (check-bindings "doseq" seq-exprs)
  (let [step (fn step [recform exprs]
               (if-not exprs
                 [true `(do ~@body)]
                 (let [k (first exprs)
                       v (second exprs)
                       seqsym (when-not (keyword? k) (gensym))
                       recform (if (keyword? k) recform `(recur (next ~seqsym)))
                       steppair (step recform (nnext exprs))
                       needrec (steppair 0)
                       subform (steppair 1)]
                   (cond
                     (= k :let) [needrec `(let ~v ~subform)]
                     (= k :while) [false `(when ~v ~subform ~@(when needrec [recform]))]
                     (= k :when) [false `(if ~v (do ~subform ~@(when needrec [recform])) ~recform)]
                     :else [true `(loop [~seqsym (seq ~v)]
                                    (when ~seqsym
                                      (let [~k (first ~seqsym)]
                                        ~subform
                                        ~@(when needrec [recform]))))]))))]
    (nth (step nil (seq seq-exprs)) 1)))

(defmacro for
  "List comprehension: nested seq bindings with :let, :when and :while modifiers, yielding a lazy seq
  of body-expr evaluations."
  [seq-exprs body-expr]
  (check-bindings "for" seq-exprs)
  (let [to-groups (fn [seq-exprs]
                    (reduce (fn [groups [k v]]
                              (if (keyword? k)
                                (conj (pop groups) (conj (peek groups) [k v]))
                                (conj groups [k v])))
                            [] (partition 2 seq-exprs)))
        emit (fn emit [[[bind expr & mod-pairs] & [[_ next-expr] :as next-groups]]]
               (let [giter (gensym "iter__")
                     gxs (gensym "s__")
                     do-mod (fn do-mod [[[k v :as pair] & etc]]
                              (cond
                                (= k :let) `(let ~v ~(do-mod etc))
                                (= k :while) `(when ~v ~(do-mod etc))
                                (= k :when) `(if ~v ~(do-mod etc) (recur (rest ~gxs)))
                                (keyword? k) (throw (ex-info (str "Invalid 'for' keyword " k) {}))
                                next-groups `(let [iterys# ~(emit next-groups)
                                                   fs# (seq (iterys# ~next-expr))]
                                               (if fs#
                                                 (concat fs# (~giter (rest ~gxs)))
                                                 (recur (rest ~gxs))))
                                :else `(cons ~body-expr (~giter (rest ~gxs)))))]
                 `(fn ~giter [~gxs]
                    (lazy-seq
                      (loop [~gxs ~gxs]
                        (when-first [~bind ~gxs]
                          ~(do-mod mod-pairs)))))))]
    `(let [iter# ~(emit (to-groups seq-exprs))]
       (iter# ~(second seq-exprs)))))

(defmacro defonce
  "Defines name with the value of expr unless the var already has a root."
  [name expr]
  `(let [v# (def ~name)]
     (when-not (bound? v#) (def ~name ~expr))))

(defmacro locking
  "Evaluates body; there is no monitor to hold, the runtime evaluates on one thread at a time (NOTES.md)."
  [x & body]
  `(do ~x ~@body))

(defn memoize
  "Returns a memoized version of f, caching its results by argument list."
  [f]
  (let [mem (atom {})]
    (fn [& args]
      (if-let [e (find @mem args)]
        (val e)
        (let [ret (apply f args)]
          (swap! mem assoc args ret)
          ret)))))

(defn trampoline
  "Calls f with args; while the result is a fn, calls it with no args. Returns the first non-fn result."
  ([f]
   (let [ret (f)]
     (if (fn? ret) (recur ret) ret)))
  ([f & args] (trampoline (fn [] (apply f args)))))

(defmacro with-out-str
  "Evaluates body with println and friends writing into a string, which is returned."
  [& body]
  `(do (out-capture-push*)
       (let [r# (try (do ~@body)
                     (catch :default e# (out-capture-pop*) (throw e#)))]
         (out-capture-pop*))))

(defn print-str "print to a string, returning it." [& xs] (with-out-str (apply print xs)))
(defn println-str "println to a string, returning it." [& xs] (with-out-str (apply println xs)))
(defn prn-str "prn to a string, returning it." [& xs] (with-out-str (apply prn xs)))
(defn printf "Prints formatted output, as per format." [fmt & args] (print (apply format fmt args)))
(defn newline "Writes a newline." [] (print "\n") nil)
(defn flush "Nothing to flush: output goes straight to the host hook." [] nil)

;; The pr and print families read these (printer.c); str and error messages do not.
(def ^:dynamic *print-length* "Items of a collection pr and print show before `...`; nil for all of them." nil)
(def ^:dynamic *print-level* "Nesting depth pr and print show; a collection deeper prints as `#`. nil for no limit." nil)

;; ---- protocols and types. Dispatch lives in C (proto.c); these macros only shape the forms.

;; (P (m [this] ...) (m [this a] ...) Q (n [x] ...)) → [[P [[m [([this] ...) ([this a] ...)]]]] [Q [[n [([x] ...)]]]],
;; a protocol named twice merging into one group.
(defn- group-impls
  "Groups the method impls of a deftype, reify or extend-type body by protocol, then by method name."
  [impls]
  (loop [impls (seq impls) cur -1 acc []]
    (if impls
      (let [x (first impls)]
        (if (seq? x)
          (if (neg? cur)
            (throw (ex-info (str "Method " (first x) " given before any protocol") nil))
            (let [[p ms] (nth acc cur)
                  nm (first x)
                  ;; (m [x] ...) is one arity, (m ([x] ...) ([x y] ...)) several.
                  sigs (if (vector? (second x)) [(next x)] (vec (next x)))]
              (when-not (every? (fn [sig] (and (seq? sig) (vector? (first sig)))) sigs)
                (throw (ex-info (str "Method " nm " needs a parameter vector") nil)))
              (recur (next impls) cur
                     (assoc acc cur
                            [p (if (some (fn [m] (= (first m) nm)) ms)
                                 (vec (map (fn [m] (if (= (first m) nm) [nm (into (second m) sigs)] m)) ms))
                                 (conj ms [nm sigs]))]))))
          (let [at (loop [i 0] (cond (= i (count acc)) nil (= (first (nth acc i)) x) i :else (recur (inc i))))]
            (if at
              (recur (next impls) at acc)
              (recur (next impls) (count acc) (conj acc [x []]))))))
      acc)))

(defn- form-uses?
  "True when sym occurs anywhere in form; shadowing is ignored."
  [form sym]
  (cond
    (= form sym) true
    (seq? form) (if (some (fn [x] (form-uses? x sym)) form) true false)
    (vector? form) (if (some (fn [x] (form-uses? x sym)) form) true false)
    (map? form) (if (some (fn [e] (form-uses? e sym)) (seq form)) true false)
    :else false))

;; A method's arities as one fn form; wrap turns (params body) into the body forms to emit.
(defn- method-fn
  "The fn form implementing one method from its sigs ((params body...) ...)."
  [sigs wrap]
  `(fn ~@(map (fn [sig] (list* (first sig) (wrap (first sig) (next sig)))) sigs)))

(defn- method-map
  "The {:method (fn ...)} form of one protocol's grouped methods."
  [ms wrap]
  (loop [ms (seq ms) m {}]
    (if ms
      (let [[nm sigs] (first ms)]
        (recur (next ms) (assoc m (keyword (name nm)) (method-fn sigs wrap))))
      m)))

(defn- body-as-is
  "The wrap that emits a method body unchanged."
  [params body]
  body)

(defmacro defprotocol
  "(defprotocol P docstring? (m [this] [this a] docstring?) ...): P holds the protocol, each method a
  dispatching fn; the docstrings land in :doc of the vars, the param vectors in :arglists of the methods."
  [nm & specs]
  (let [pdoc (when (string? (first specs)) (first specs))
        specs (if pdoc (next specs) specs)
        sigs (vec (map (fn [s] [(first s) (vec (filter vector? (next s)))]) specs))
        docs (vec (map (fn [s] (some (fn [x] (when (string? x) x)) (next s))) specs))
        method-def (fn [i]
                     (let [[mname arglists] (nth sigs i)
                           m {:arglists (list 'quote (seq arglists))}
                           m (if (nth docs i) (assoc m :doc (nth docs i)) m)]
                       `(def ~(with-meta mname m) (protocol-method* ~nm ~i))))]
    `(do
       (def ~(if pdoc (with-meta nm {:doc pdoc}) nm) (protocol* '~nm '~sigs))
       ~@(map method-def (range (count sigs)))
       '~nm)))

(defn extend
  "(extend type proto {:m (fn ...)} proto2 {...}): implements protocols for a type from method maps."
  [t & proto+mmaps]
  (loop [s (seq proto+mmaps)]
    (when s
      (if (next s)
        (do
          (extend* t (first s) (second s))
          (recur (next (next s))))
        (throw (ex-info "extend expects protocol and method-map pairs" nil)))))
  nil)

(defmacro extend-type
  "(extend-type type proto (m [this] ...) ... proto2 ...)"
  [t & impls]
  `(extend ~t ~@(mapcat (fn [g] [(first g) (method-map (second g) body-as-is)]) (group-impls impls))))

(defmacro extend-protocol
  "(extend-protocol proto type (m [this] ...) ... type2 ...)"
  [p & specs]
  (let [groups (loop [specs (seq specs) acc []]
                 (if specs
                   (let [x (first specs)]
                     (if (seq? x)
                       (if (empty? acc)
                         (throw (ex-info (str "Method " (first x) " given before any type") nil))
                         (let [i (dec (count acc))]
                           (recur (next specs) (assoc acc i (conj (nth acc i) x)))))
                       (recur (next specs) (conj acc [x]))))
                   acc))]
    `(do ~@(map (fn [g] `(extend-type ~(first g) ~p ~@(next g))) groups))))

;; A positional slot per field, no (.-field x) access; a record keeps its basis in the same slots (NOTES.md).
(defn- field-wrap
  "The method wrap of deftype and defrecord: the fields a body names become locals over field*."
  [fields]
  (fn [params body]
    (let [this (first params)]
      (when-not (symbol? this)
        (throw (ex-info (str "deftype method params must start with this, got: " params) nil)))
      (let [bindings (loop [i 0 acc []]
                       (if (< i (count fields))
                         (let [f (nth fields i)]
                           (recur (inc i)
                                  (if (and (form-uses? body f) (not-any? (fn [p] (= p f)) params))
                                    (conj acc f `(field* ~this ~i))
                                    acc)))
                         acc))]
        (if (seq bindings) (list `(let ~bindings ~@body)) body)))))

;; Name and ->Name come first so a method body can construct or test for its own type.
(defmacro deftype
  "(deftype Name [field ...] proto (m [this a] ...) ...): a type, its ->Name constructor and the impls."
  [nm fields & impls]
  (let [groups (group-impls impls)
        ctor (symbol (str "->" (name nm)))
        wrap (field-wrap fields)]
    `(do
       (declare ~nm)
       (def ~ctor (fn [~@fields] (new* ~nm ~@fields)))
       (def ~nm (deftype* '~nm '~fields ~@(mapcat (fn [g] [(first g) (method-map (second g) wrap)]) groups)))
       ~nm)))

;; The map bits and slots are the record's own, so the body may add protocols only (record.c).
(defmacro defrecord
  "(defrecord Name [field ...] proto (m [this a] ...) ...): a record type, its ->Name and map->Name
  constructors and the impls."
  [nm fields & impls]
  (let [groups (group-impls impls)
        ctor (symbol (str "->" (name nm)))
        from-map (symbol (str "map->" (name nm)))
        wrap (field-wrap fields)]
    `(do
       (declare ~nm)
       (def ~ctor (fn [~@fields] (new* ~nm ~@fields)))
       (def ~from-map (fn [m#] (record-map* ~nm m#)))
       (def ~nm (record* '~nm '~fields ~@(mapcat (fn [g] [(first g) (method-map (second g) wrap)]) groups)))
       ~nm)))

;; The expansion is data and var references only, so the tree serializes: the type is made on the
;; first evaluation of the site (reify-type* caches it under the gensym'd name), its slots and
;; protocol tables hold trampolines into the instance's fields, one per method, which each
;; evaluation fills with closures over the site's locals.
(defmacro reify
  "(reify proto (m [this a] ...) ...): an instance of an anonymous type closing over the locals in scope."
  [& impls]
  (let [groups (group-impls impls)
        entries (loop [gs (seq groups) acc []]
                  (if gs
                    (let [[p ms] (first gs)]
                      (recur (next gs)
                             (loop [ms (seq ms) acc acc]
                               (if ms
                                 (recur (next ms) (conj acc [p (first (first ms)) (second (first ms)) (count acc)]))
                                 acc))))
                    acc))
        check-proto (fn [p]
                      (when (and (symbol? p) (not (resolve p)))
                        (throw (ex-info (str "Unable to resolve protocol: " p) nil)))
                      p)
        slots (fn [g]
                (loop [es (seq (filter (fn [e] (= (nth e 0) (first g))) entries)) m {}]
                  (if es
                    (recur (next es) (assoc m (keyword (name (nth (first es) 1))) (nth (first es) 3)))
                    m)))]
    `(new* (reify-type* '~(gensym "reify__") '~(vec (map (fn [e] (nth e 1)) entries))
                        ~@(mapcat (fn [g] [(check-proto (first g)) (slots g)]) groups))
           ~@(map (fn [e] (method-fn (nth e 2) body-as-is)) entries))))

;; A deftype, not a C type: the IReduceInit slot trampoline (proto.c) makes this four lines, and reduce on
;; it reaches the source through the source's own slot with no seq in between.
(deftype Eduction [xform coll]
  Seqable
  (seq [_] (seq (sequence xform coll)))
  IReduceInit
  (reduce [_ f init] (transduce xform (completing f) init coll)))

(defn eduction
  "Returns a reducible and seqable application of the transducers to coll; the
  transformation runs anew on every reduce or seq."
  [& xforms]
  (->Eduction (apply comp (butlast xforms)) (last xforms)))

(defn iteration
  "Creates a seqable/reducible given step, a function of some (opaque continuation data) k.
  step returns a (possibly nil) return value, ret; (somef ret) tells whether ret is a value,
  (vf ret) is the value to yield, (kf ret) the next k or nil when done. initk is the first k."
  [step & {:keys [somef vf kf initk] :or {vf identity kf identity somef some? initk nil}}]
  (reify
    Seqable
    (seq [_]
      ((fn next [ret]
         (when (somef ret)
           (cons (vf ret)
                 (when-some [k (kf ret)]
                   (lazy-seq (next (step k)))))))
       (step initk)))
    IReduceInit
    (reduce [_ rf init]
      (loop [acc init ret (step initk)]
        (if (somef ret)
          (let [acc (rf acc (vf ret))]
            (if (reduced? acc)
              @acc
              (if-some [k (kf ret)]
                (recur acc (step k))
                acc)))
          acc)))))


;; ---- dynamic vars: binding frames live in C (var.c); these macros shape the push/pop pairs.

(defmacro binding
  "binding => var-symbol init-expr. Binds the dynamic vars to the values on this thread for the
  extent of body, then restores the previous bindings."
  [bindings & body]
  (check-bindings "binding" bindings)
  (let [var-ize (fn [var-vals]
                  (loop [ret [] vvs (seq var-vals)]
                    (if vvs
                      (recur (conj (conj ret `(var ~(first vvs))) (second vvs)) (next (next vvs)))
                      (seq ret))))]
    `(do
       (push-thread-bindings (hash-map ~@(var-ize bindings)))
       (try
         ~@body
         (finally
           (pop-thread-bindings))))))

(defn with-bindings*
  "Calls f with the supplied arguments under the thread bindings of binding-map (var → value)."
  [binding-map f & args]
  (push-thread-bindings binding-map)
  (try
    (apply f args)
    (finally
      (pop-thread-bindings))))

(defmacro with-bindings
  "Evaluates body under the thread bindings of binding-map (var → value)."
  [binding-map & body]
  `(with-bindings* ~binding-map (fn [] ~@body)))

(defn bound-fn*
  "Returns a fn that calls f with the thread bindings in effect when bound-fn* was called."
  [f]
  (let [bindings (get-thread-bindings)]
    (fn [& args]
      (apply with-bindings* bindings f args))))

(defmacro bound-fn
  "Returns a fn (fntail as for fn) that runs with the thread bindings in effect where it was made."
  [& fntail]
  `(bound-fn* (fn ~@fntail)))

(defn with-redefs-fn
  "Temporarily rebinds the roots of the vars in binding-map (var → value) while calling func, then
  restores them. The roots are process-wide: every thread sees the change."
  [binding-map func]
  (let [root-bind (fn [m] (doseq [[a-var a-val] m] (alter-var-root a-var (fn [_] a-val))))
        old-vals (zipmap (keys binding-map) (map deref (keys binding-map)))]
    (try
      (root-bind binding-map)
      (func)
      (finally
        (root-bind old-vals)))))

(defmacro with-redefs
  "binding => var-symbol temp-value-expr. Rebinds the vars' roots for the extent of body, dynamic or
  not, and restores them afterwards. For tests and REPL work, not for production code."
  [bindings & body]
  (check-bindings "with-redefs" bindings)
  `(with-redefs-fn ~(zipmap (map (fn [v] `(var ~v)) (take-nth 2 bindings))
                            (take-nth 2 (drop 1 bindings)))
     (fn [] ~@body)))

;; ---- hierarchies: keyword and symbol tags; no host-type superclass lookup (NOTES.md).

(defn make-hierarchy "Creates a new, empty hierarchy." [] {:parents {} :descendants {} :ancestors {}})

(def ^{:private true} global-hierarchy (make-hierarchy))

(defn isa?
  "Returns true when child is parent or derives from it; vectors of tags are compared elementwise."
  ([child parent] (isa? global-hierarchy child parent))
  ([h child parent]
   (boolean
    (or (= child parent)
        (contains? (get (:ancestors h) child) parent)
        (and (vector? parent) (vector? child) (= (count parent) (count child))
             (loop [ret true i 0]
               (if (or (not ret) (= i (count parent)))
                 ret
                 (recur (isa? h (nth child i) (nth parent i)) (inc i)))))))))

(defn parents
  "The immediate parents of tag, or nil."
  ([tag] (parents global-hierarchy tag))
  ([h tag] (not-empty (get (:parents h) tag))))

(defn ancestors
  "The transitive parents of tag, or nil."
  ([tag] (ancestors global-hierarchy tag))
  ([h tag] (not-empty (get (:ancestors h) tag))))

(defn descendants
  "The transitive children of tag, or nil."
  ([tag] (descendants global-hierarchy tag))
  ([h tag] (not-empty (get (:descendants h) tag))))

(defn- tag?
  "A dispatch tag is a keyword, a symbol or a type; isa? treats a type as a plain key, with no supertypes."
  [x]
  (or (ident? x) (identical? Type (type x))))

(defn derive
  "Makes parent a parent of tag. Without a hierarchy, alters the global one and returns nil."
  ([tag parent]
   (assert (namespace parent))
   (assert (or (and (ident? tag) (namespace tag)) (identical? Type (type tag))))
   (alter-var-root #'global-hierarchy derive tag parent)
   nil)
  ([h tag parent]
   (assert (not= tag parent))
   (assert (tag? tag))
   (assert (ident? parent))
   ;; The maps are called, not `get`-ed: an h that lacks one of the three keys must fail, as on the JVM.
   (let [tp (:parents h) td (:descendants h) ta (:ancestors h)
         tf (fn [m source sources target targets]
              (reduce (fn [ret k]
                        (assoc ret k (reduce conj (targets k #{}) (cons target (targets target)))))
                      m (cons source (sources source))))]
     (or
      (when-not (contains? (tp tag) parent)
        (when (contains? (ta tag) parent)
          (throw (ex-info (str tag " already has " parent " as ancestor") {})))
        (when (contains? (ta parent) tag)
          (throw (ex-info (str "Cyclic derivation: " parent " has " tag " as ancestor") {})))
        {:parents (assoc tp tag (conj (tp tag #{}) parent))
         :ancestors (tf ta tag td parent ta)
         :descendants (tf td parent ta tag td)})
      h))))

(defn underive
  "Removes parent as a parent of tag. Without a hierarchy, alters the global one and returns nil."
  ([tag parent]
   (alter-var-root #'global-hierarchy underive tag parent)
   nil)
  ([h tag parent]
   (let [parent-map (:parents h)
         childs-parents (if (parent-map tag) (disj (parent-map tag) parent) #{})
         new-parents (if (not-empty childs-parents) (assoc parent-map tag childs-parents) (dissoc parent-map tag))
         ;; child p1 child p2 ... for every remaining edge: the hierarchy is rebuilt from them.
         deriv-seq (flatten (map (fn [e] (cons (key e) (interpose (key e) (val e)))) (seq new-parents)))]
     (if (contains? (parent-map tag) parent)
       (reduce (fn [acc pair] (apply derive acc pair)) (make-hierarchy) (partition 2 deriv-seq))
       h))))

;; ---- delays and multimethods: deftypes over protocols, since C knows neither.

(defprotocol IDeref
  "deref of a value that is not a var, atom, volatile or reduced box: the C builtin falls back to this method."
  (-deref [this]))

(defprotocol IPending
  (-realized? [this]))

(deftype Delay [state]
  IDeref
  (-deref [_]
    (let [s @state]
      (if (:realized s)
        (:val s)
        (let [v ((:f s))]
          (reset! state {:realized true :val v})
          v))))
  IPending
  (-realized? [_] (boolean (:realized @state))))

(defn realized?
  "Returns true when a pending value (a lazy seq, a delay) has been forced."
  [x]
  (if (satisfies? IPending x) (-realized? x) (lazy-seq-realized?* x)))

(defmacro delay
  "Yields a Delay: body runs on the first deref or force, and its value is cached."
  [& body]
  `(->Delay (atom {:realized false :f (fn [] ~@body)})))

(defn delay? "Returns true when x is a Delay." [x] (instance? Delay x))
(defn force "Derefs a Delay, or returns x itself." [x] (if (delay? x) (deref x) x))

(defprotocol IMultiFn
  (-add-method [mf dispatch-val f])
  (-remove-method [mf dispatch-val])
  (-remove-all-methods [mf])
  (-methods [mf])
  (-get-method [mf dispatch-val])
  (-prefer-method [mf x y])
  (-prefers [mf]))

(defn- mf-prefers?
  [h prefers x y]
  (or (contains? (get prefers x) y)
      (boolean (some (fn [p] (mf-prefers? h prefers x p)) (parents h y)))
      (boolean (some (fn [p] (mf-prefers? h prefers p y)) (parents h x)))))

(defn- mf-dominates? [h prefers x y] (or (mf-prefers? h prefers x y) (isa? h x y)))

;; ::none, not nil: nil is a legal dispatch value, so it cannot mark "no match yet".
(defn- mf-best-method
  [mname h table prefers dv default]
  (let [best (reduce-kv
              (fn [best k _]
                (if (isa? h dv k)
                  (cond
                    (= best ::none) k
                    (mf-dominates? h prefers k best) k
                    (mf-dominates? h prefers best k) best
                    :else (throw (ex-info (str "Multiple methods in multimethod '" mname "' match dispatch value: "
                                               (pr-str dv) " -> " (pr-str k) " and " (pr-str best)
                                               ", and neither is preferred")
                                          {:multifn mname :dispatch-val dv})))
                  best))
              ::none table)]
    (get table (if (= best ::none) default best))))

;; The cache is [hierarchy-value {dispatch-val method}], as Clojure keys its method cache by the hierarchy
;; it was built from: a derive that replaces the value invalidates every entry at once.
(defn- mf-method [mf mname hierarchy cache dv]
  (or (let [c @cache] (when (identical? (nth c 0) @hierarchy) (get (nth c 1) dv)))
      (-get-method mf dv)
      (throw (ex-info (str "No method in multimethod '" mname "' for dispatch value: " (pr-str dv))
                      {:multifn mname :dispatch-val dv}))))

(deftype MultiFn [mname dispatch-fn default hierarchy table prefers cache]
  IMultiFn
  (-add-method [_ dispatch-val f]
    (swap! table assoc dispatch-val f)
    (reset! cache [::none {}])
    nil)
  (-remove-method [_ dispatch-val]
    (swap! table dissoc dispatch-val)
    (reset! cache [::none {}])
    nil)
  (-remove-all-methods [_]
    (reset! table {})
    (reset! cache [::none {}])
    nil)
  (-methods [_] @table)
  (-prefers [_] @prefers)
  (-prefer-method [this x y]
    (when (mf-prefers? @hierarchy @prefers y x)
      (throw (ex-info (str "Preference conflict in multimethod '" mname "': " (pr-str y)
                           " is already preferred to " (pr-str x))
                      {:multifn mname})))
    (swap! prefers (fn [p] (assoc p x (conj (get p x #{}) y))))
    (reset! cache [::none {}])
    this)
  (-get-method [_ dispatch-val]
    (let [hv @hierarchy
          c @cache
          entries (if (identical? (nth c 0) hv) (nth c 1) {})]
      (or (get entries dispatch-val)
          (let [f (mf-best-method mname hv @table @prefers dispatch-val default)]
            (when f (reset! cache [hv (assoc entries dispatch-val f)]))
            f))))
  IFn
  ;; Fixed arities up to three, as Clojure's MultiFn has them: a rest seq and two applies cost more than
  ;; the dispatch itself.
  (invoke
    ([this] ((mf-method this mname hierarchy cache (dispatch-fn))))
    ([this a] ((mf-method this mname hierarchy cache (dispatch-fn a)) a))
    ([this a b] ((mf-method this mname hierarchy cache (dispatch-fn a b)) a b))
    ([this a b c] ((mf-method this mname hierarchy cache (dispatch-fn a b c)) a b c))
    ([this a b c & more]
     (let [args (list* a b c more)]
       (apply (mf-method this mname hierarchy cache (apply dispatch-fn args)) args)))))

(defmacro defmulti
  "(defmulti name docstring? attr-map? dispatch-fn & options): a multimethod var. :default names the
  fallback dispatch value, :hierarchy the var holding the hierarchy isa? dispatch reads."
  [mm-name & options]
  (let [docstring (when (string? (first options)) (first options))
        options (if docstring (next options) options)
        m (if (map? (first options)) (first options) {})
        options (if (map? (first options)) (next options) options)
        dispatch-fn (first options)
        opts (apply hash-map (next options))
        default (get opts :default :default)
        hierarchy (get opts :hierarchy `#'global-hierarchy)
        m (if docstring (assoc m :doc docstring) m)]
    `(defonce ~(with-meta mm-name (merge (meta mm-name) m))
       (->MultiFn '~mm-name ~dispatch-fn ~default ~hierarchy (atom {}) (atom {}) (atom [::none {}])))))

(defmacro defmethod
  "Adds a method for dispatch-val to the multimethod."
  [multifn dispatch-val & fn-tail]
  `(do (-add-method ~multifn ~dispatch-val (fn ~@fn-tail)) ~multifn))

(defn methods "Returns a map of dispatch values to methods." [multifn] (-methods multifn))
(defn get-method "Returns the method isa? dispatch picks for dispatch-val, or the default one." [multifn dispatch-val] (-get-method multifn dispatch-val))
(defn remove-method "Removes the method for dispatch-val." [multifn dispatch-val] (-remove-method multifn dispatch-val) multifn)
(defn remove-all-methods "Removes every method." [multifn] (-remove-all-methods multifn) multifn)
(defn prefer-method "Makes dispatch-val-x win over dispatch-val-y when both match." [multifn dispatch-val-x dispatch-val-y] (-prefer-method multifn dispatch-val-x dispatch-val-y))
(defn prefers "Returns the multimethod's preference table." [multifn] (-prefers multifn))

;; ---- data readers: what `#tag form` resolves through, in this order (runtime.c reads the vars while reading).

(def ^:dynamic *data-readers* "Map of tag symbol to reader fn, consulted before default-data-readers." {})
(def ^:dynamic *default-data-reader-fn* "When set, (f tag value) reads a tag no table names; nil makes it a reader error." nil)
(def default-data-readers "The built-in tags, #inst and #uuid." {'inst read-inst* 'uuid read-uuid*})

;; ---- namespaces: ns, require, refer, use over the C namespace API (in-ns, alias, ns-publics, load-file, ...).

(def ^:dynamic *loaded-libs* (atom #{'clojure.core}))

(defn loaded-libs "Returns the set of libs loaded so far." [] @*loaded-libs*)

(defn- load-one [lib]
  (let [path (lib-path* lib)
        file (load-resource* path)]
    (when-not file
      (throw (ex-info (str "Could not locate " path ".cljc or " path ".clj on load path.") {:lib lib})))
    (load-file file)
    (when-not (find-ns lib)
      (throw (ex-info (str "namespace '" lib "' not found after loading '" file "'") {:lib lib})))
    (swap! *loaded-libs* conj lib)
    nil))

(defn refer
  "Refers the public vars of ns-sym into the current namespace. Filters: :only [syms], :exclude [syms],
  :rename {sym sym}, :refer [syms] or :all. clojure.core is visible unqualified by default, so for it
  only the exclusions and renames take effect."
  [ns-sym & filters]
  (let [ns (or (find-ns ns-sym) (throw (ex-info (str "No namespace: " ns-sym) {})))
        fs (apply hash-map filters)
        publics (ns-publics ns)
        rename (or (:rename fs) {})
        exclude (set (:exclude fs))
        only (if (= :all (:refer fs)) nil (or (:refer fs) (:only fs)))
        to-do (or only (keys publics))
        core? (= ns-sym 'clojure.core)]
    (doseq [sym to-do]
      (when-not (contains? exclude sym)
        (let [v (get publics sym)]
          (when-not v
            (throw (ex-info (if (get (ns-interns ns) sym) (str sym " is not public") (str sym " does not exist")) {:sym sym})))
          (when (or (not core?) (contains? rename sym))
            (ns-refer* *ns* (get rename sym sym) v)))))
    (when core?
      (ns-exclude* *ns* (set (concat exclude (keys rename) (when only (remove (set only) (keys publics)))))))
    nil))

(defn refer-clojure
  "Same as (refer 'clojure.core filters...)."
  [& filters]
  (apply refer 'clojure.core filters))

(defn- libspec? [x]
  (or (symbol? x) (and (vector? x) (or (nil? (second x)) (keyword? (second x))))))

(defn- load-lib [prefix lib & options]
  (let [lib (if prefix (symbol (str prefix "." lib)) lib)
        opts (apply hash-map options)
        as (:as opts)
        as-alias (:as-alias opts)
        refer-opt (:refer opts)
        use? (:use opts)
        reload (or (:reload opts) (:reload-all opts))]
    (when-not (symbol? lib) (throw (ex-info (str "lib names must be symbols: " lib) {})))
    (when (and (not as-alias) (or reload (not (contains? @*loaded-libs* lib))))
      (load-one lib))
    (when as-alias (create-ns lib))
    (when (or as as-alias) (alias (or as as-alias) lib))
    (when (or use? refer-opt)
      (apply refer lib (mapcat (fn [k] (when-let [v (get opts k)] [k v])) [:refer :only :exclude :rename])))
    nil))

(defn- load-libs [& args]
  (let [flags (filter keyword? args)
        opts (interleave flags (repeat true))
        args (filter (complement keyword?) args)]
    (doseq [arg args]
      (if (libspec? arg)
        (apply load-lib nil (concat (if (symbol? arg) [arg] arg) opts))
        (let [[prefix & libspecs] arg]
          (when (nil? prefix) (throw (ex-info "prefix cannot be nil" {})))
          (doseq [ls libspecs]
            (apply load-lib prefix (concat (if (symbol? ls) [ls] ls) opts))))))))

(defn require
  "Loads libs, skipping any already loaded. Libspecs: a symbol, or [lib :as alias :refer [syms] or :all
  :as-alias alias], or a prefix list (prefix libspec+). Flags: :reload, :reload-all, :verbose."
  [& args]
  (apply load-libs :require args))

(defn use
  "Like require, then refers the libs' public vars (:only, :exclude, :rename apply)."
  [& args]
  (apply load-libs :require :use args))

(defmacro ns
  "(ns name docstring? attr-map? references*): sets the current namespace, creating it when needed, and
  processes (:refer-clojure ...), (:require ...) and (:use ...). (:import ...) and (:gen-class) name JVM
  classes and are ignored; a class named later fails to resolve where it is used (NOTES.md)."
  [name & references]
  (let [docstring (when (string? (first references)) (first references))
        references (if docstring (next references) references)
        attr-map (when (map? (first references)) (first references))
        references (if attr-map (next references) references)
        quote-all (fn [args] (map (fn [a] (list 'quote a)) args))
        process (fn [[kname & args]]
                  (cond
                    (= kname :refer-clojure) `(refer-clojure ~@(quote-all args))
                    (= kname :require) `(require ~@(quote-all args))
                    (= kname :use) `(use ~@(quote-all args))
                    (= kname :import) nil
                    (= kname :gen-class) nil
                    :else (throw (ex-info (str "Unsupported ns reference: " kname) {}))))
        refers-clojure? (some (fn [r] (= :refer-clojure (first r))) references)]
    `(do
       (in-ns '~name)
       ~@(when attr-map [`(alter-meta! (the-ns '~name) merge ~attr-map)])
       ~@(when-not refers-clojure? [`(refer-clojure)])
       ~@(map process references)
       nil)))

;; The :=> declarations of C builtins (design §3, anchor 1): what a defn carries in its attr-map, set here because a builtin has none.
;; :any where the true argument is "seqable": arrays have no tag in the vocabulary yet (NOTES.md, "Facts").
(run! (fn [e] (alter-meta! (resolve (key e)) assoc :=> (val e)))
      {'count     [:=> [:cat :any] :int]
       'nth       [:=> [:cat :any :int] :any]
       'next      [:=> [:cat :any] [:maybe :seq]]
       'rest      [:=> [:cat :any] :seq]
       'seq       [:=> [:cat :any] [:maybe :seq]]
       'inc       [:=> [:cat :number] :number]
       'dec       [:=> [:cat :number] :number]
       'name      [:=> [:cat [:or :keyword :symbol :string]] :string]
       'namespace [:=> [:cat [:or :keyword :symbol]] [:maybe :string]]
       'conj      [:=> [:cat [:maybe [:or :seq :vector :map :set]] [:* :any]] [:or :seq :vector :map :set]]
       'assoc     [:=> [:cat [:maybe [:or :map :vector]] :any :any [:* :any]] [:or :map :vector]]
       'zero?     [:=> [:cat :number] :boolean]
       'pos?      [:=> [:cat :number] :boolean]
       'neg?      [:=> [:cat :number] :boolean]})
