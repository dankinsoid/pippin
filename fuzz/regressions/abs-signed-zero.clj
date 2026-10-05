;; A pippin fuzz regression (docs/notes/fuzzing.md): every runner must answer as the oracle does.
;; abs clears the sign bit, where (neg? -0.0) is false; the NaN rules of min and max beside it.
(def fz-sort-unordered true)
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
(fz 0 (abs -0.0))
(fz 1 (abs 0.0))
(fz 2 (abs -1.5))
(fz 3 (abs -3))
(fz 4 (abs ##NaN))
(fz 5 (abs ##-Inf))
(fz 6 (min ##NaN 1.0))
(fz 7 (min 1.0 ##NaN))
(fz 8 (max ##NaN 1.0))
(fz 9 (max 1.0 ##NaN))
(fz 10 (min 1 2.0))
(fz 11 (max 1 2.0))
(fz 12 (min 2.0 1))
(flush)
