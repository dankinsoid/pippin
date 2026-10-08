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

;; Our jvm-hash against the oracle's own hash of the same value; the JVM's core has no jvm-hash to resolve.
(def fz-hash (if-let [v (resolve 'clojure.core/jvm-hash)] (deref v) hash))

(defn fz-out [x] (str (fz-norm x) " #fz/hash " (fz-hash x)))

;; Which error was thrown is out of the comparison: a JVM class and an ex-type are not one alphabet.
(defmacro fz [i expr]
  (list 'fz-emit i (list 'try (list 'fz-out expr) (list 'catch 'Throwable 'fz-t "#fz/throw"))))
