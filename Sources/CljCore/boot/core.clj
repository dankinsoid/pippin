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
   (let [form (first bindings)
         tst (second bindings)]
     `(let [temp# ~tst]
        (if temp#
          (let [~form temp#] ~then)
          ~else)))))

(defmacro when-let
  "Like if-let with body in an implicit do and no else branch."
  [bindings & body]
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
    (if (and (pos? n) (seq xs))
      (recur (dec n) (rest xs))
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
  ([n x] (take n (repeat x))))

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
  [coll] (into [] coll))

(defn partition
  "Returns a lazy seq of n-item seqs, starting step apart (default n), dropping a
  short trailing partition."
  ([n coll] (partition n n coll))
  ([n step coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [p (doall (take n s))]
         (when (= n (count p))
           (cons p (partition n step (nthrest s step)))))))))

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

;; clojure.set's basics live here until namespaces beyond user and clojure.core exist (NOTES.md).
(defn union
  "Returns a set of the elements of every set."
  ([] #{})
  ([s1] s1)
  ([s1 s2] (if (< (count s1) (count s2)) (reduce conj s2 s1) (reduce conj s1 s2)))
  ([s1 s2 & sets] (reduce union (union s1 s2) sets)))

(defn intersection
  "Returns a set of the elements every set holds."
  ([s1] s1)
  ([s1 s2]
   (if (< (count s2) (count s1))
     (recur s2 s1)
     (reduce (fn [result item] (if (contains? s2 item) result (disj result item))) s1 s1)))
  ([s1 s2 & sets] (reduce intersection (intersection s1 s2) sets)))

(defn difference
  "Returns a set of the elements of s1 that no other set holds."
  ([s1] s1)
  ([s1 s2]
   (if (< (count s1) (count s2))
     (reduce (fn [result item] (if (contains? s2 item) (disj result item) result)) s1 s1)
     (reduce disj s1 s2)))
  ([s1 s2 & sets] (reduce difference (difference s1 s2) sets)))

(defn subset?
  "Is every element of set1 in set2?"
  [set1 set2]
  (and (<= (count set1) (count set2)) (every? (fn [item] (contains? set2 item)) set1)))

(defn superset?
  "Is every element of set2 in set1?"
  [set1 set2]
  (and (>= (count set1) (count set2)) (every? (fn [item] (contains? set1 item)) set2)))

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

;; Fields are read through field* at the top of each method body, only those the body names and no
;; param shadows: a positional slot per field, no (.-field x) access (NOTES.md).
;; The type is made with every impl in one deftype* call: core interfaces fill its slots at creation.
;; Name and ->Name are declared first so a method body can construct or test for its own type.
(defmacro deftype
  "(deftype Name [field ...] proto (m [this a] ...) ...): a type, its ->Name constructor and the impls."
  [nm fields & impls]
  (let [groups (group-impls impls)
        ctor (symbol (str "->" (name nm)))
        wrap (fn [params body]
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
                   (if (seq bindings) (list `(let ~bindings ~@body)) body))))]
    `(do
       (declare ~nm)
       (def ~ctor (fn [~@fields] (new* ~nm ~@fields)))
       (def ~nm (deftype* '~nm '~fields ~@(mapcat (fn [g] [(first g) (method-map (second g) wrap)]) groups)))
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
