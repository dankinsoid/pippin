;; design §3 «Диагностика»: an arity error must name the arities that exist, and both backends must word
;; it alike — the interpreter refuses at the call site, a compiled fn in its own dispatcher, and a closed
;; unit through a direct call. A quoted constant's position is not here: it is nil compiled and set
;; interpreted by decision (docs/notes/compiler.md), so diagnostics-positions.clj carries that half.
(defn dg-at [f]
  (try (f) :no-throw
       (catch :default e
         (let [d (ex-data e)]
           [(ex-message e) (:arities d) (:variadic d) (:given d) (:fn d)]))))

(defn dg-two [a b] (+ a b))
(defn dg-many ([a] a) ([a b] b) ([a b & r] r))
(defn dg-rest [a & r] r)

(prn (dg-at (fn [] (dg-two 1))))
(prn (dg-at (fn [] (dg-two 1 2 3))))
(prn (dg-at (fn [] (dg-many))))
(prn (dg-at (fn [] (dg-rest))))
(prn (dg-at (fn [] (inc))))
(prn (dg-at (fn [] ((fn [x] x)))))
(prn (dg-at (fn [] (nth 5 0))))
(prn (dg-at (fn [] (apply dg-two [1 2 3]))))
