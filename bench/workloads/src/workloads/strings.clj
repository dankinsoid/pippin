;; @ai-generated(solo)
;; Building a CSV text, parsing it back and reshaping strings with clojure.string.
;; Input: n = 20000 rows "i,name-(i mod 97),3i,yes|no,C(i mod 13)". Work: join, split-lines, split on a regex,
;; parse-long, frequencies, upper-case, starts-with?, interpose + apply str, replace (string and regex), trim,
;; a growing and halving string, split/capitalize/join, reverse, index-of.
;; Output: a vector of 12 counts and the first 64 characters of the capitalized text, `expected` below.
(ns workloads.strings
  (:require [clojure.string :as str]))

(defn- row [i]
  (str/join "," [i (str "name-" (mod i 97)) (* i 3) (if (even? i) "yes" "no") (str/upper-case (str "c" (mod i 13)))]))

(defn run* [n]
  (let [csv (str/join "\n" (map row (range n)))
        lines (str/split-lines csv)
        cells (mapv #(str/split % #",") lines)
        total (reduce + (map #(parse-long (nth % 2)) cells))
        names (frequencies (map second cells))
        upper (count (filter #(str/starts-with? % "NAME-1") (map (comp str/upper-case second) cells)))
        joined (apply str (interpose "|" (map str (range (quot n 4)))))
        replaced (str/replace csv "name" "N")
        regexed (str/replace (subs csv 0 (min (count csv) 200000)) #"[0-9]+" "#")
        trimmed (reduce + (map (comp count str/trim #(str "  " % "  ")) (take 5000 lines)))
        built (reduce (fn [s i] (if (< (count s) 4000) (str s i) (subs s 2000))) "" (range (quot n 2)))
        words (str/split (str/join " " (repeat 2000 "alpha beta gamma delta")) #" ")
        caps (str/join " " (map str/capitalize words))
        rev (count (filter #(= % (str/reverse %)) (map str (range 20000))))
        idx (reduce + (map #(or (str/index-of % "-5") -1) (take 10000 lines)))]
    [(count csv) total (count names) upper (count joined) (count replaced) (count regexed)
     trimmed (count built) (count caps) rev idx (subs caps 0 64)]))

(defn run [] (run* 20000))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '[517727 599970000 97 2275 23889 457727 139865 118316 2818 45999 299 1217 "Alpha Beta Gamma Delta Alpha Beta Gamma Delta Alpha Beta Gamma D"])
