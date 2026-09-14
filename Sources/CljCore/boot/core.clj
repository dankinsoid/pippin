;; @ai-generated(guided)
;; Order matters: a macro must be defined before the first form that uses it.
;; Docstrings are dropped: no metadata slot to keep them in yet (NOTES.md).
;; Up to the `fn` macro only let*/loop*/fn* and the macros above a form are available;
;; defmacro emits fn* until `fn` is a macro, so those macro params cannot destructure.

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

(def destructure
  (fn* [bindings] bindings))

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
