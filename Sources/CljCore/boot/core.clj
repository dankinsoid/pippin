;; @ai-generated(guided)
;; Order matters: a macro must be defined before the first form that uses it.
;; Docstrings are dropped: no metadata slot to keep them in yet (NOTES.md).
;; Up to the `fn` macro only let*/loop*/fn* and the macros above a form are available;
;; defmacro emits fn* until `fn` is a macro, so those macro params cannot destructure.

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

(defmacro lazy-seq [& body]
  `(lazy-seq* (fn* [] ~@body)))

(defmacro when [test & body]
  `(if ~test (do ~@body)))

(defmacro when-not [test & body]
  `(if ~test nil (do ~@body)))

(defmacro if-not
  ([test then] `(if-not ~test ~then nil))
  ([test then else] `(if ~test ~else ~then)))

(defmacro cond [& clauses]
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

(def check-bindings
  (fn* [what bindings]
    (if (vector? bindings)
      nil
      (throw (ex-info (str what " requires a vector for its binding") nil)))
    (if (odd? (count bindings))
      (throw (ex-info (str what " requires an even number of forms in binding vector") nil))
      nil)))

(defmacro let [bindings & body]
  (check-bindings "let" bindings)
  `(let* ~(destructure bindings) ~@body))

;; Destructured bindings are re-bound to gensyms so recur still targets the loop.
(defmacro loop [bindings & body]
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

(defmacro fn [& sigs]
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
  "(defn name docstring? [params] body...) or (defn name docstring? ([params] body...)...)"
  [name & fdecl]
  (let [fdecl (if (string? (first fdecl)) (next fdecl) fdecl)]
    `(def ~name (fn ~@fdecl))))

(defmacro and
  ([] true)
  ([x] x)
  ([x & next]
   `(let [and# ~x]
      (if and# (and ~@next) and#))))

(defmacro or
  ([] nil)
  ([x] x)
  ([x & next]
   `(let [or# ~x]
      (if or# or# (or ~@next)))))

(defmacro -> [x & forms]
  (loop [x x forms forms]
    (if forms
      (let [form (first forms)
            threaded (if (seq? form)
                       `(~(first form) ~x ~@(next form))
                       (list form x))]
        (recur threaded (next forms)))
      x)))

(defmacro ->> [x & forms]
  (loop [x x forms forms]
    (if forms
      (let [form (first forms)
            threaded (if (seq? form)
                       `(~(first form) ~@(next form) ~x)
                       (list form x))]
        (recur threaded (next forms)))
      x)))

(defmacro comment [& body])

(defmacro dotimes [bindings & body]
  (let [i (first bindings)
        n (second bindings)]
    `(let [n# ~n]
       (loop [~i 0]
         (when (< ~i n#)
           ~@body
           (recur (inc ~i)))))))

(defmacro if-let
  ([bindings then] `(if-let ~bindings ~then nil))
  ([bindings then else]
   (let [form (first bindings)
         tst (second bindings)]
     `(let [temp# ~tst]
        (if temp#
          (let [~form temp#] ~then)
          ~else)))))

(defmacro when-let [bindings & body]
  (let [form (first bindings)
        tst (second bindings)]
    `(let [temp# ~tst]
       (when temp#
         (let [~form temp#] ~@body)))))

(defmacro assert
  ([x]
   `(when-not ~x
      (throw (ex-info (str "Assert failed: " (pr-str '~x)) {}))))
  ([x message]
   `(when-not ~x
      (throw (ex-info (str "Assert failed: " ~message "\n" (pr-str '~x)) {})))))

(defmacro declare [& names]
  `(do ~@(loop [names (seq (reverse names)) defs nil]
           (if names
             (recur (next names) (cons `(def ~(first names)) defs))
             defs))))

;; ---- seqs. Lazy where Clojure is lazy; eager walks use loop/recur so long seqs cost no stack.

(defn complement [f]
  (fn [& args] (not (apply f args))))

(defn nthrest [coll n]
  (loop [n n xs coll]
    (if (and (pos? n) (seq xs))
      (recur (dec n) (rest xs))
      xs)))

(defn some [pred coll]
  (loop [s (seq coll)]
    (when s
      (or (pred (first s)) (recur (next s))))))

(defn every? [pred coll]
  (loop [s (seq coll)]
    (cond
      (nil? s) true
      (pred (first s)) (recur (next s))
      :else false)))

(defn not-any? [pred coll] (not (some pred coll)))

(defn not-every? [pred coll] (not (every? pred coll)))

(defn reduce
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

(defn filter [pred coll]
  (lazy-seq
    (when-let [s (seq coll)]
      (let [f (first s) r (rest s)]
        (if (pred f)
          (cons f (filter pred r))
          (filter pred r))))))

(defn remove [pred coll]
  (filter (complement pred) coll))

(defn keep [f coll]
  (lazy-seq
    (when-let [s (seq coll)]
      (let [x (f (first s))]
        (if (nil? x)
          (keep f (rest s))
          (cons x (keep f (rest s))))))))

(defn take [n coll]
  (lazy-seq
    (when (pos? n)
      (when-let [s (seq coll)]
        (cons (first s) (take (dec n) (rest s)))))))

(defn drop [n coll]
  (let [step (fn [n coll]
               (let [s (seq coll)]
                 (if (and (pos? n) s)
                   (recur (dec n) (rest s))
                   s)))]
    (lazy-seq (step n coll))))

(defn take-while [pred coll]
  (lazy-seq
    (when-let [s (seq coll)]
      (when (pred (first s))
        (cons (first s) (take-while pred (rest s)))))))

(defn drop-while [pred coll]
  (let [step (fn [pred coll]
               (let [s (seq coll)]
                 (if (and s (pred (first s)))
                   (recur pred (rest s))
                   s)))]
    (lazy-seq (step pred coll))))

(defn iterate [f x]
  (cons x (lazy-seq (iterate f (f x)))))

(defn repeat
  ([x] (lazy-seq (cons x (repeat x))))
  ([n x] (take n (repeat x))))

;; Fixnum ranges are the O(1) range type; step 0 repeats as Clojure's does; doubles walk a lazy seq.
(defn range
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

(defn interpose [sep coll]
  (drop 1 (interleave (repeat sep) coll)))

;; Not (apply concat ...): apply spreads its whole seq here (NOTES.md), which would realize an infinite input.
(defn mapcat [f & colls]
  (let [step (fn step [ss]
               (lazy-seq
                 (when-let [s (seq ss)]
                   (concat (first s) (step (rest s))))))]
    (step (apply map f colls))))

(defn dorun
  ([coll]
   (loop [s (seq coll)]
     (when s (recur (next s)))))
  ([n coll]
   (loop [n n s (seq coll)]
     (when (and s (pos? n))
       (recur (dec n) (next s))))))

(defn doall
  ([coll] (dorun coll) coll)
  ([n coll] (dorun n coll) coll))

(defn vec [coll] (into [] coll))

(defn partition
  ([n coll] (partition n n coll))
  ([n step coll]
   (lazy-seq
     (when-let [s (seq coll)]
       (let [p (doall (take n s))]
         (when (= n (count p))
           (cons p (partition n step (nthrest s step)))))))))

(defn zipmap [keys vals]
  (loop [m {} ks (seq keys) vs (seq vals)]
    (if (and ks vs)
      (recur (assoc m (first ks) (first vs)) (next ks) (next vs))
      m)))
