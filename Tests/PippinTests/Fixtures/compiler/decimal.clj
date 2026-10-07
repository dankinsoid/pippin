;; Decimal arithmetic under with-precision: *math-context* is read per operation, so no constant operand is folded ahead of the binding.
(ns fixture.decimal)

(defn third [] (/ 1M 3))
(defn scaled [p x] (with-precision p (* x 1M)))
(defn run [] [(with-precision 10 (third))
              (with-precision 5 :rounding FLOOR (- 2M 1/3))
              (mapv #(scaled 2 %) [1.25M 1.35M -1.25M])
              (with-precision 1 :rounding UP (* 1.1M 1M))
              (with-precision 2 (- 100M 99.99M))
              (try (third) (catch :default e (ex-message e)))
              (try (with-precision 1 :rounding UNNECESSARY (* 1.5M 1M)) (catch :default e (ex-message e)))])
(prn (run))
