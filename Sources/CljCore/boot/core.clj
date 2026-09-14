;; @ai-generated(guided)
;; Order matters: a macro must be defined before the first form that uses it.
;; Docstrings are dropped: no metadata slot to keep them in yet (NOTES.md). def takes none at all,
;; so the helpers it defines carry a comment instead.
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
(def check-bindings
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
(def maybe-destructured
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

(defmacro defn
  "(defn name docstring? [params] body...) or (defn name docstring? ([params] body...)...).
  Defines name as the fn. The docstring is read and discarded: there is no metadata yet."
  [name & fdecl]
  (let [fdecl (if (string? (first fdecl)) (next fdecl) fdecl)]
    `(def ~name (fn ~@fdecl))))

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

;; ---- seqs. Lazy where Clojure is lazy; eager walks use loop/recur so long seqs cost no stack.

(defn complement
  "Returns a fn taking the same args as f and returning the opposite truth value."
  [f]
  (fn [& args] (not (apply f args))))

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

(defn reduce
  "Feeds the accumulator and each item of coll to f in turn and returns the last
  accumulator. Without val the first item seeds it and (f) answers an empty coll.
  There is no reduced, so the walk always runs to the end."
  ([f coll]
   (let [s (seq coll)]
     (if s
       (reduce f (first s) (next s))
       (f))))
  ([f val coll]
   (loop [acc val s (seq coll)]
     (if s
       (recur (f acc (first s)) (next s))
       acc))))

(defn map
  "Returns a lazy seq of f applied to the items of the colls in parallel, ending
  with the shortest."
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
  "Returns a lazy seq of the items of coll for which (pred item) is logical true."
  [pred coll]
  (lazy-seq
    (when-let [s (seq coll)]
      (let [f (first s) r (rest s)]
        (if (pred f)
          (cons f (filter pred r))
          (filter pred r))))))

(defn remove
  "Returns a lazy seq of the items of coll for which (pred item) is logical false."
  [pred coll]
  (filter (complement pred) coll))

(defn keep
  "Returns a lazy seq of the non-nil results of (f item); false is kept."
  [f coll]
  (lazy-seq
    (when-let [s (seq coll)]
      (let [x (f (first s))]
        (if (nil? x)
          (keep f (rest s))
          (cons x (keep f (rest s))))))))

(defn take
  "Returns a lazy seq of the first n items of coll, or all of them when there are fewer."
  [n coll]
  (lazy-seq
    (when (pos? n)
      (when-let [s (seq coll)]
        (cons (first s) (take (dec n) (rest s)))))))

(defn drop
  "Returns a lazy seq of the items of coll past the first n."
  [n coll]
  (let [step (fn [n coll]
               (let [s (seq coll)]
                 (if (and (pos? n) s)
                   (recur (dec n) (rest s))
                   s)))]
    (lazy-seq (step n coll))))

(defn take-while
  "Returns a lazy seq of the leading items of coll while (pred item) is logical true."
  [pred coll]
  (lazy-seq
    (when-let [s (seq coll)]
      (when (pred (first s))
        (cons (first s) (take-while pred (rest s)))))))

(defn drop-while
  "Returns a lazy seq of the items of coll from the first one for which (pred item)
  is logical false."
  [pred coll]
  (let [step (fn [pred coll]
               (let [s (seq coll)]
                 (if (and s (pred (first s)))
                   (recur pred (rest s))
                   s)))]
    (lazy-seq (step pred coll))))

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
  "Returns a lazy seq of the items of coll separated by sep."
  [sep coll]
  (drop 1 (interleave (repeat sep) coll)))

;; Not (apply concat ...): apply spreads its whole seq here (NOTES.md), which would realize an infinite input.
(defn mapcat
  "Returns a lazy seq of the concatenated results of applying f to the items of the
  colls in parallel."
  [f & colls]
  (let [step (fn step [ss]
               (lazy-seq
                 (when-let [s (seq ss)]
                   (concat (first s) (step (rest s))))))]
    (step (apply map f colls))))

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

(defn zipmap
  "Returns a map of the keys to the corresponding vals, ending with the shorter."
  [keys vals]
  (loop [m {} ks (seq keys) vs (seq vals)]
    (if (and ks vs)
      (recur (assoc m (first ks) (first vs)) (next ks) (next vs))
      m)))

;; ---- protocols and types. Dispatch lives in C (proto.c); these macros only shape the forms.

;; (P (m [this] ...) (m [this a] ...) Q (n [x] ...)) → [[P [[m [([this] ...) ([this a] ...)]]]] [Q [[n [([x] ...)]]]],
;; a protocol named twice merging into one group.
(defn group-impls
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

(defn form-uses?
  "True when sym occurs anywhere in form; shadowing is ignored."
  [form sym]
  (cond
    (= form sym) true
    (seq? form) (if (some (fn [x] (form-uses? x sym)) form) true false)
    (vector? form) (if (some (fn [x] (form-uses? x sym)) form) true false)
    (map? form) (if (some (fn [e] (form-uses? e sym)) (seq form)) true false)
    :else false))

;; A method's arities as one fn form; wrap turns (params body) into the body forms to emit.
(defn method-fn
  "The fn form implementing one method from its sigs ((params body...) ...)."
  [sigs wrap]
  `(fn ~@(map (fn [sig] (list* (first sig) (wrap (first sig) (next sig)))) sigs)))

(defn method-map
  "The {:method (fn ...)} form of one protocol's grouped methods."
  [ms wrap]
  (loop [ms (seq ms) m {}]
    (if ms
      (let [[nm sigs] (first ms)]
        (recur (next ms) (assoc m (keyword (name nm)) (method-fn sigs wrap))))
      m)))

(defn body-as-is
  "The wrap that emits a method body unchanged."
  [params body]
  body)

(defmacro defprotocol
  "(defprotocol P docstring? (m [this] [this a] docstring?) ...): P holds the protocol, each method a dispatching fn."
  [nm & specs]
  (let [specs (if (string? (first specs)) (next specs) specs)
        sigs (vec (map (fn [s] [(first s) (vec (filter vector? (next s)))]) specs))]
    `(do
       (def ~nm (protocol* '~nm '~sigs))
       ~@(map (fn [i] `(def ~(first (nth sigs i)) (protocol-method* ~nm ~i))) (range (count sigs)))
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

;; The type is made once, at expansion: its slots and protocol tables hold trampolines into the
;; instance's fields, one per method, which the expansion fills with closures over the site's locals.
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
        proto-value (fn [p]
                      (if (symbol? p)
                        (let [v (resolve p)]
                          (if v (deref v) (throw (ex-info (str "Unable to resolve protocol: " p) nil))))
                        p))
        trampolines (fn [g]
                      (loop [es (seq (filter (fn [e] (= (nth e 0) (first g))) entries)) m {}]
                        (if es
                          (recur (next es) (assoc m (keyword (name (nth (first es) 1))) (trampoline* (nth (first es) 3))))
                          m)))
        t (apply deftype* (gensym "reify__") (vec (map (fn [e] (nth e 1)) entries))
                 (mapcat (fn [g] [(proto-value (first g)) (trampolines g)]) groups))]
    `(new* ~t ~@(map (fn [e] (method-fn (nth e 2) body-as-is)) entries))))
