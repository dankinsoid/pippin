;; A pippin fuzz regression (docs/notes/fuzzing.md): every runner must answer as the oracle does.
;; apply past 21 spread arguments, where a multi-arity variadic fn takes its rest at its rest arity's position.
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

;; Our jvm-hash against the oracle's own hash of the same value; the JVM's core has no jvm-hash to resolve.
(def fz-hash (if-let [v (resolve 'clojure.core/jvm-hash)] (deref v) hash))

(defn fz-out [x] (str (fz-norm x) " #fz/hash " (fz-hash x)))

;; What was thrown, raw: the JVM's class or our ex-type, and the message. The driver maps both to one error family
;; (fuzz/errors.edn); `type` is the class on the JVM, which has no ex-type to resolve.
(def fz-ex-type (when-let [v (resolve 'clojure.core/ex-type)] (deref v)))

(defn fz-throw [t]
  (str "#fz/throw " (if fz-ex-type (pr-str (fz-ex-type t)) (str (type t))) " " (pr-str (ex-message t))))

(defmacro fz [i expr]
  (list 'fz-emit i (list 'try (list 'fz-out expr) (list 'catch 'Throwable 'fz-t (list 'fz-throw 'fz-t)))))
(fz 0 (apply max (range 22)))
(fz 1 (apply max 3 (range 21)))
(fz 2 (apply min 5 4 (range 3 100)))
(fz 3 (apply max-key count (repeat 21 [1 2])))
(fz 4 (apply max-key :a (map (fn [i] {:a (mod (* i 7) 101) :i i}) (range 30))))
(fz 5 (apply min-key - (range 1 23)))
(fz 6 (apply distinct? (range 30)))
(fz 7 (apply distinct? 1 2 (range 30)))
(fz 8 (apply str (range 25)))
(fz 9 (apply + 1 2 3 4 (range 100)))
(fz 10 (apply merge (map (fn [i] {(mod i 7) i}) (range 25))))
(fz 11 (apply concat (map vector (range 22))))
(fz 12 (apply (fn ([] :a0) ([a] [:a1 a]) ([a b] [:a2 a b]) ([a b c & r] [:v a b c r])) (range 22)))
(fz 13 (apply (fn ([a] [:g1 a]) ([a b c d & r] [:gv a b c d r])) 1 2 (range 3 40)))
(fz 14 (apply (fn [a & r] [a r]) (range 21)))
(fz 15 (apply (partial max 500) (range 30)))
(fz 16 (let [k 7] (apply (fn ([] k) ([a] [k a]) ([a b & r] [k a b (count r)])) 0 1 2 3 (range 50))))
(fz 17 (apply vector 1 2 3 4 (range 100)))
(flush)
