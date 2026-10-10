;; A pippin fuzz regression (docs/notes/fuzzing.md): every runner must answer as the oracle does.
;; Non-ASCII strings where both index units agree (design §4 «Строки»), literal-pattern split and replace.
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
(fz 0 (count "日本語"))
(fz 1 (count "é"))
(fz 2 (subs "日本語テキスト" 2 4))
(fz 3 (int (nth "héllo" 1)))
(fz 4 (int (get "日本語" 2)))
(fz 5 (clojure.string/index-of "日本語日本語" "語" 3))
(fz 6 (clojure.string/last-index-of "日本語日本語" "本"))
(fz 7 (clojure.string/last-index-of "日本語日本語" "本" 3))
(fz 8 (clojure.string/reverse "日本語"))
(fz 9 (clojure.string/split "日,本,語" #","))
(fz 10 (re-find #"本(.)" "日本語"))
(fz 11 (format "%5s|%-4s|" "日本" "é"))
(fz 12 (format "%.2s" "日本語"))
(fz 13 (mapv int (seq "héllo")))
(fz 14 (contains? "日本語" 2))
(fz 15 (contains? "日本語" 3))
(fz 16 (clojure.string/index-of "héllo" "" 10))
(fz 17 (clojure.string/capitalize "a日本"))
(fz 18 (clojure.string/trim-newline "日本\n"))
(fz 19 (clojure.string/starts-with? "日本語" "日本"))
(fz 20 (clojure.string/ends-with? "日本語" "本語"))
(fz 21 "😀")
(fz 22 "👨‍👩‍👧")
(fz 23 (str "🇺🇸" "a"))
(fz 24 (clojure.string/reverse "a😀b"))
(fz 25 (clojure.string/replace "a😀b😀" "😀" "x"))
(fz 26 (clojure.string/split "a😀b😀c" #"😀"))
(fz 27 (re-find #"a(.)b" "a😀b"))
(fz 28 (clojure.string/includes? "a😀b" "😀b"))
(fz 29 (clojure.string/capitalize "😀ab"))
(fz 30 (let [s (apply str (repeat 100 "é日a"))] [(count s) (subs s 150 153) (clojure.string/index-of s "a" 200) (clojure.string/last-index-of s "é") (int (nth s 192)) (count (subs s 63 129))]))
(fz 31 (let [s (apply str (repeat 128 "é"))] [(count s) (subs s 127) (subs s 128) (nth s 128 :none) (count (rest s)) (clojure.string/index-of (str s "x") "x")]))
(fz 32 (let [s (str (apply str (repeat 70 "a")) "é" "b")] [(count s) (clojure.string/index-of s "b") (subs s 69 71) (clojure.string/last-index-of s "a" 72)]))
(fz 33 (let [s (apply str (repeat 64 "日"))] [(count s) (subs s 63) (clojure.string/index-of s "日" 63) (clojure.string/index-of s "日" 64)]))
(fz 34 (clojure.string/split "a.b.c" #"\."))
(fz 35 (clojure.string/split "...a..b..." #"\."))
(fz 36 (clojure.string/split "..." #"\."))
(fz 37 (clojure.string/split "" #"\."))
(fz 38 (clojure.string/split "abc" #"\."))
(fz 39 (clojure.string/split "a.b.c" #"\." 2))
(fz 40 (clojure.string/split "a.b.." #"\." -1))
(fz 41 (clojure.string/split ".a.b." #"\." 1))
(fz 42 (clojure.string/split "a::b::::c::" #"::"))
(fz 43 (clojure.string/split "a b  c" #" "))
(fz 44 (clojure.string/split "a|b|" #"\|" 3))
(fz 45 (clojure.string/split "a,b,c" #"," 0))
(fz 46 (clojure.string/split "x" #"x"))
(fz 47 (clojure.string/split "xax" #"x" -1))
(fz 48 (clojure.string/replace "a.b.c" #"\." "-"))
(fz 49 (clojure.string/replace "a.b" #"\." "$0$0"))
(fz 50 (clojure.string/replace "a.b" #"\." "\\$"))
(fz 51 (clojure.string/replace "a.b" #"\." "$1"))
(fz 52 (clojure.string/replace "a.b" #"\." "${x}"))
(fz 53 (clojure.string/replace-first "a.b.c" #"\." "-"))
(fz 54 (clojure.string/replace "a.b" #"\." (fn [m] (str "<" m ">"))))
(fz 55 (clojure.string/replace "aaa" #"aa" "b"))
(fz 56 (clojure.string/replace "" #"a" "b"))
(fz 57 (clojure.string/replace "日本語" #"本" "x"))
(fz 58 (clojure.string/replace "a-b" #"-" "$"))
(fz 59 (clojure.string/replace "a b" #" " (fn [m] 1)))
(fz 60 (str 0 -1 42 -100 1000000 4611686018427387903 -4611686018427387904))
(fz 61 (str Long/MAX_VALUE " " Long/MIN_VALUE " " (inc 4611686018427387903)))
(fz 62 (pr-str [Long/MAX_VALUE Long/MIN_VALUE 7 -7]))
(fz 63 (format "%d|%,d|%05d|%+d|%d" -42 -1234567 -42 7 Long/MIN_VALUE))
(fz 64 (str 9 10 99 100 999 1000 -9 -10 -99 -100))
(fz 65 (clojure.string/join "," (range -3 3)))
(flush)
