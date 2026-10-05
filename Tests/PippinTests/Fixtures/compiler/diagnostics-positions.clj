;; design §3 «Диагностика»: the position of a mistake is the innermost positioned form, as a range, and it
;; is the same position in both backends. The file is the absolute path of this fixture, so only its
;; presence is printed. The closed pass is skipped for the whole fixture because it evals (CompilerFixtureTests).
(defn dg-span [text]
  (try (eval (read-string text)) :no-throw
       (catch :default e
         (let [d (ex-data e)]
           [(ex-message e) (:line d) (:column d) (:end-line d) (:end-column d) (boolean (:file d)) (:suggestion d)]))))

(prn (dg-span "(defn dgp-a [x] (let [y (inc x)] (dgp-undefined-name y)))"))
(prn (dg-span "(defn dgp-b [x] (inx x))"))
(prn (dg-span "(defn dgp-c [] (let [a] a))"))
(prn (dg-span "(do\n  (if 1 (dgp-nope) 2))"))
(prn (dg-span "(loop [x 1] (+ 1 (recur 2)))"))
