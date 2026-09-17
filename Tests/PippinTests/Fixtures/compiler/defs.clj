;; DEF and VAR: def, defn, defmacro used by later forms, dynamic vars and binding, redefinition, var meta, unbound.
(ns fixture.defs)

(def plain 1)
(def ^:dynamic *dyn* :root)
(def ^{:doc "documented" :private true} hidden 2)
(defmacro twice [x] `(+ ~x ~x))
(defn uses-macro [y] (twice y))
(def later)
(def never-bound)

(println plain *dyn* hidden (uses-macro 21) (twice 3) (:doc (meta #'hidden)) (:private (meta #'hidden)) (:name (meta #'plain)) (:ns (meta #'plain)))
(println (binding [*dyn* :bound] [*dyn* ((fn [] *dyn*))]) *dyn* (try (binding [plain 2] plain) (catch :default e (ex-message e))))
(println (try never-bound (catch :default e (ex-message e))) (bound? #'never-bound) (do (def later 3) later) (var? #'plain) (deref #'plain))
(def plain 10)
(defn f [] :first)
(println plain (f) (do (defn f [] :second) (f)) (with-redefs [f (fn [] :redef)] (f)) (f))
(println (macroexpand-1 '(twice 1)) (:macro (meta #'twice)) (let [v (def inside-let 5)] [v inside-let]))
(println ((fn [] (def from-fn 7))) from-fn (binding [*dyn* 1] (set! *dyn* 2) *dyn*) *dyn*)
