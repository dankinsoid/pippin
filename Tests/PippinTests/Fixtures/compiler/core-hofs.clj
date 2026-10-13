;; juxt, fnil and partial: Clojure's fixed arities, the variadic one past them, and fnil's arity errors.
(ns fixture.core-hofs)

(defn arity-error [f & args] (try (apply f args) :no-error (catch :default e :arity-error)))

(println 'juxt ((juxt (constantly :a))) ((juxt inc) 1) ((juxt +) 1 2) ((juxt +) 1 2 3) ((juxt +) 1 2 3 4 5)
         ((juxt + *)) ((juxt + -) 5) ((juxt + - *) 2 3) ((juxt + - *) 2 3 4 5) ((juxt + * vector list))
         ((juxt + - * vector) 1) ((juxt + - * vector) 1 2) ((juxt + - * vector) 1 2 3) ((juxt + - * vector) 1 2 3 4))
(println 'fnil ((fnil + 10) nil) ((fnil + 10) nil 1) ((fnil + 10) nil 1 2) ((fnil + 10) nil 1 2 3) ((fnil + 10) 5)
         ((fnil + 1 2) nil nil) ((fnil + 1 2) nil nil 3) ((fnil + 1 2) nil nil 3 4) ((fnil + 1 2) 7 nil)
         ((fnil + 1 2 3) nil nil) ((fnil + 1 2 3) nil nil nil) ((fnil + 1 2 3) 1 nil nil 4) ((fnil conj #{}) nil :x)
         (arity-error (fnil + 1)) (arity-error (fnil + 1 2) 1) (arity-error (fnil + 1 2 3) 1))
(println 'partial ((partial + 1)) ((partial + 1) 2) ((partial list 1) 2 3) ((partial list 1) 2 3 4) ((partial list 1) 2 3 4 5)
         ((partial list 1 2)) ((partial list 1 2) 3 4 5 6) ((partial list 1 2 3)) ((partial list 1 2 3) 4 5 6 7)
         ((partial list 1 2 3 4)) ((partial list 1 2 3 4) 5) (apply (partial list 1) 2 3 4 [5 6]))
(println 'group (map :id (sort-by (juxt (comp - :salary) :id) [{:id 2 :salary 5} {:id 1 :salary 5} {:id 3 :salary 9}]))
         (update {} :k (fnil conj []) 1) (sort ((reduce (fn [m x] (update m (odd? x) (fnil conj #{}) x)) {} (range 5)) false)))
