;; @ai-generated(solo)
;; ClojureScript's workloads.util (bench/workloads/src/workloads/util.clj): the same generator and the same states.
(ns workloads.util)

(defn lcg
  "The next state of a 31-bit linear congruential generator."
  [x]
  ;; The double product passes 2^53 and loses the low bits; Math.imul keeps the 32 low ones exact.
  (bit-and (+ (js/Math.imul x 1103515245) 12345) 0x7fffffff))

(defn lcg-vec
  "n successive states starting after seed, as a vector."
  [seed n]
  (loop [i 0 x seed acc (transient [])]
    (if (< i n)
      (let [x (lcg x)]
        (recur (inc i) x (conj! acc x)))
      (persistent! acc))))
