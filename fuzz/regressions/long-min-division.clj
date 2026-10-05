;; A pippin fuzz regression (docs/notes/fuzzing.md): every runner must answer as the oracle does.
;; Long/MIN_VALUE: an exact quotient no long holds is a bigint, and the checked operators still refuse it.
(def fz-sort-unordered true)
(def fz-bare-integers true)
;; @ai-generated(solo)
;; The differential fuzzer's harness (docs/notes/fuzzing.md), inlined at the head of every case file.

;; Portable Clojure only: the JVM oracle and every runner of ours load this same text.

;; fz-sort-unordered and fz-bare-integers come from the case file's header, above this text, so that no
;; backend folds either into fz-norm.

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
    (and fz-bare-integers (integer? x)) (str x)
    (vector? x) (str "[" (clojure.string/join " " (map fz-norm x)) "]")
    (sequential? x) (str "(" (clojure.string/join " " (map fz-norm x)) ")")
    :else (pr-str x)))

(defn fz-emit [i s]
  (println (str i "\t" (if (> (count s) fz-max-chars) (str (subs s 0 fz-max-chars) " #fz/cut") s))))

;; Which error was thrown is out of the comparison: a JVM class and an ex-type are not one alphabet.
(defmacro fz [i expr]
  (list 'fz-emit i (list 'try (list 'fz-norm expr) (list 'catch 'Throwable 'fz-t "#fz/throw"))))
(fz 0 (/ -9223372036854775808 -1))
(fz 1 (/ -9223372036854775808 1))
(fz 2 (/ -9223372036854775808 2))
(fz 3 (/ -9223372036854775808 3))
(fz 4 (/ -9223372036854775808 -2))
(fz 5 (/ -1 -9223372036854775808))
(fz 6 (rem -9223372036854775808 -1))
(fz 7 (mod -9223372036854775808 -1))
(fz 8 (- -998 -9223372036854775808))
(fz 9 (try (- 0 -9223372036854775808) (catch Throwable t :threw)))
(fz 10 (try (+ -9223372036854775808 -9223372036854775808) (catch Throwable t :threw)))
(fz 11 (try (- -9223372036854775808) (catch Throwable t :threw)))
(flush)
