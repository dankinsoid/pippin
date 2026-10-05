;; A pippin fuzz regression (docs/notes/fuzzing.md): every runner must answer as the oracle does.
;; quot and rem of a double pair: the fused multiply-add, fmod on the mixed-rank path, and the long cast.
(def fz-sort-unordered true)
(def fz-bare-integers true)
;; @ai-generated(solo)
;; The differential fuzzer's harness (docs/notes/fuzzing.md), inlined at the head of every case file.

;; Portable Clojure only: the JVM oracle and every runner of ours load this same text.

;; fz-sort-unordered comes from the case file's header, above this text, so no backend folds it into fz-norm.

(require 'clojure.string)
(require 'clojure.set)

;; A case that prints megabytes drowns the divergence it was meant to show; the cut is the same on both sides.
(def fz-max-chars 20000)

(declare fz-norm)

(defn fz-pairs [m]
  (map (fn [e] [(fz-norm (key e)) (fz-norm (val e))]) (seq m)))

;; The comparison runs over this text, never over pr-str (docs/notes/fuzzing.md).
(defn fz-norm [x]
  (cond
    (map? x) (let [ps (fz-pairs x)
                   ps (if fz-sort-unordered (sort-by first ps) ps)]
               (str "{" (clojure.string/join ", " (map (fn [p] (str (first p) " " (second p))) ps)) "}"))
    (set? x) (let [es (map fz-norm x)
                   es (if fz-sort-unordered (sort es) es)]
               (str "#{" (clojure.string/join " " es) "}"))
    (vector? x) (str "[" (clojure.string/join " " (map fz-norm x)) "]")
    (sequential? x) (str "(" (clojure.string/join " " (map fz-norm x)) ")")
    :else (pr-str x)))

(defn fz-emit [i s]
  (println (str i "\t" (if (> (count s) fz-max-chars) (str (subs s 0 fz-max-chars) " #fz/cut") s))))

;; Which error was thrown is out of the comparison: a JVM class and an ex-type are not one alphabet.
(defmacro fz [i expr]
  (list 'fz-emit i (list 'try (list 'fz-norm expr) (list 'catch 'Throwable 'fz-t "#fz/throw"))))
(fz 0 (rem 1.0 0.1))
(fz 1 (mod 1.0 0.1))
(fz 2 (rem 5 (/ -1 1.5)))
(fz 3 (rem 1.0 0.1))
(fz 4 (rem 1N ##Inf))
(fz 5 (rem 1 ##Inf))
(fz 6 (rem 1/2 ##Inf))
(fz 7 (try (rem ##Inf 1N) (catch Throwable t :threw)))
(fz 8 (try (quot ##Inf 1N) (catch Throwable t :threw)))
(fz 9 (quot 1N ##Inf))
(fz 10 (quot 1 ##Inf))
(fz 11 (try (rem ##NaN 1N) (catch Throwable t :threw)))
(fz 12 (try (rem 1N ##NaN) (catch Throwable t :threw)))
(fz 13 (try (rem 1 ##NaN) (catch Throwable t :threw)))
(fz 14 (rem 1N 0.1))
(fz 15 (rem 10N 3.3))
(fz 16 (mod 1N 0.1))
(fz 17 (rem 12345678901234567890N 0.1))
(fz 18 (quot 1.5 -4611686018427387904))
(fz 19 (quot -0.5 1))
(fz 20 (quot (dec 0.1) (inc 0)))
(fz 21 (rem -0.0 1.0))
(fz 22 (mod -0.0 1.0))
(flush)
