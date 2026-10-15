;; The number statics clojure.data.json reads its numbers and escapes with: the JVM's answers and its refusals.
(ns fixture.statics)

(defn- refused [f] (try (f) :accepted (catch NumberFormatException _ :refused)))

(defn long-value-of []
  [(Long/valueOf 42) (Long/valueOf "-9223372036854775808") (Long/valueOf "+17") (Long/valueOf "007")
   (refused #(Long/valueOf "9223372036854775808")) (refused #(Long/valueOf "")) (refused #(Long/valueOf "-"))
   (refused #(Long/valueOf "1.5")) (refused #(Long/valueOf " 1"))])

(defn parse-int []
  [(Integer/parseInt "2147483647") (Integer/parseInt "-2147483648") (Integer/parseInt "00e9" 16)
   (Integer/parseInt "FFFF" 16) (Integer/parseInt "-zz" 36) (refused #(Integer/parseInt "2147483648"))
   (refused #(Integer/parseInt "g" 16)) (refused #(Integer/parseInt "+"))])

(defn to-hex-string []
  [(Integer/toHexString 0) (Integer/toHexString 31) (Integer/toHexString 8232) (Integer/toHexString -1)
   (Integer/toHexString Integer/MIN_VALUE)])

(defn double-value-of []
  [(Double/valueOf "4.37") (Double/valueOf "-0.0") (Double/valueOf "1e3") (Double/valueOf " 2.5\n")
   (Double/valueOf ".5") (Double/valueOf "1.") (Double/valueOf 1.25) (Double/isNaN (Double/valueOf "NaN"))
   (Double/valueOf "-Infinity") (refused #(Double/valueOf "1e")) (refused #(Double/valueOf ""))])

(defn math-min []
  [(Math/min 3 -4) (Math/min 0.0 -0.0) (Math/min -0.0 0.0) (Double/isNaN (Math/min 1.0 ##NaN))
   (Math/min Long/MIN_VALUE Long/MAX_VALUE)])

(println (long-value-of))
(println (parse-int))
(println (to-hex-string))
(println (double-value-of))
(println (math-min))
