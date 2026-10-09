;; @ai-generated(solo)
;; No rand: the JVM and every backend here must see one input and print one result.
(ns workloads.util)

(defn lcg
  "The next state of a 31-bit linear congruential generator; the product stays inside a fixnum."
  [x]
  (bit-and (+ (* x 1103515245) 12345) 0x7fffffff))

(defn lcg-vec
  "n successive states starting after seed, as a vector."
  [seed n]
  (loop [i 0 x seed acc (transient [])]
    (if (< i n)
      (let [x (lcg x)]
        (recur (inc i) x (conj! acc x)))
      (persistent! acc))))
