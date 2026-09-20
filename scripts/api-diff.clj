;; @ai-generated(guided)
;; Runs on JVM Clojure through `make api-diff`; the dumps are vectors of {:name :arglists :macro :dynamic}.
(ns api-diff
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.pprint :as pp]
            [clojure.string :as str]
            [clojure.walk :as walk]))

(defn dump-jvm []
  (->> (ns-publics 'clojure.core)
       (map (fn [[sym v]]
              (let [m (meta v)]
                {:name sym
                 :arglists (:arglists m)
                 :macro (boolean (:macro m))
                 :dynamic (boolean (:dynamic m))})))
       (sort-by :name)
       vec))

(defn- read-all-forms [file]
  (with-open [r (java.io.PushbackReader. (io/reader file))]
    (binding [*read-eval* false]
      (loop [forms []]
        (let [form (try (read {:eof ::eof :read-cond :allow :features #{:clj}} r)
                        (catch Exception e ::error))]
          (cond
            (= form ::eof) forms
            (= form ::error) forms
            :else (recur (conj forms form))))))))

(defn- symbol-uses
  "Occurrences of unqualified or clojure.core-qualified symbols in the corpus sources, by name."
  [corpus-dir]
  (let [files (->> (file-seq (io/file corpus-dir))
                   (filter #(re-find #"\.clj[cs]?$" (.getName %))))
        counts (atom {})]
    (doseq [f files form (read-all-forms f)]
      (walk/postwalk (fn [x]
                       (when (symbol? x)
                         (let [ns (namespace x) nm (symbol (name x))]
                           (when (or (nil? ns) (= ns "clojure.core"))
                             (swap! counts update nm (fnil inc 0)))))
                       x)
                     form))
    @counts))

(defn- arity-signature [arglists]
  (->> arglists
       (map (fn [args]
              (let [n (count (take-while #(not= '& %) args))]
                (if (some #{'&} args) (str n "+") (str n)))))
       set))

;; clojure.core.async 1.6.681's public names: the JVM dump would need the library on the classpath.
(def async-jvm-publics
  '#{<! <!! >! >!! admix alt! alt!! alts! alts!! buffer chan close! do-alt dropping-buffer go go-loop into map
     merge mix mult offer! onto-chan onto-chan! onto-chan!! pipe pipeline pipeline-async pipeline-blocking poll!
     promise-chan pub put! reduce sliding-buffer solo-mode split sub take take! tap thread thread-call timeout
     to-chan to-chan! to-chan!! toggle transduce unblocking-buffer? unique unmix unmix-all unsub unsub-all untap
     untap-all})

(defn- async-section [line ours-async-file]
  (let [ours (set (map :name (edn/read-string (slurp ours-async-file))))
        built (sort (filter async-jvm-publics ours))
        missing (sort (remove ours async-jvm-publics))
        extra (sort (remove async-jvm-publics ours))]
    (line)
    (line "## clojure.core.async")
    (line)
    (line "Against core.async 1.6.681's public names (docs/jvm-differences.md, \"core.async\": the blocking variants are aliases, any function may park).")
    (line)
    (line "| | count |")
    (line "|---|---|")
    (line "| built | " (count built) " |")
    (line "| missing | " (count missing) " |")
    (line "| ours only | " (count extra) " |")
    (line)
    (line "Built: " (str/join " " (map #(str "`" % "`") built)))
    (line)
    (line "Missing: " (if (seq missing) (str/join " " (map #(str "`" % "`") missing)) "none"))
    (line)
    (line "Ours only: " (str/join " " (map #(str "`" % "`") extra)))))

(defn diff [jvm-file ours-file corpus-dir out-file & [ours-async-file]]
  (let [jvm (edn/read-string (slurp jvm-file))
        ours (edn/read-string (slurp ours-file))
        jvm-by (into {} (map (juxt :name identity) jvm))
        ours-by (into {} (map (juxt :name identity) ours))
        uses (symbol-uses corpus-dir)
        missing (->> (keys jvm-by) (remove ours-by) sort)
        extra (->> (keys ours-by) (remove jvm-by) (remove #(str/ends-with? (name %) "*")) sort)
        internal (->> (keys ours-by) (remove jvm-by) (filter #(str/ends-with? (name %) "*")) sort)
        common (->> (keys jvm-by) (filter ours-by) sort)
        kind-mismatch (filter (fn [n] (not= (:macro (jvm-by n)) (:macro (ours-by n)))) common)
        dynamic-mismatch (filter (fn [n] (not= (:dynamic (jvm-by n)) (:dynamic (ours-by n)))) common)
        arity-mismatch (->> common
                            (filter (fn [n] (and (:arglists (jvm-by n)) (:arglists (ours-by n))
                                                 (not= (arity-signature (:arglists (jvm-by n)))
                                                       (arity-signature (:arglists (ours-by n)))))))
                            (map (fn [n] [n (:arglists (jvm-by n)) (:arglists (ours-by n))])))
        no-arglists (filter (fn [n] (and (:arglists (jvm-by n)) (nil? (:arglists (ours-by n))) (not (:macro (jvm-by n))))) common)
        weighted (->> missing
                      (map (fn [n] [n (get uses n 0)]))
                      (sort-by (fn [[n c]] [(- c) (str n)])))
        used-missing (filter (fn [[_ c]] (pos? c)) weighted)
        sb (StringBuilder.)
        line (fn [& xs] (.append sb (apply str xs)) (.append sb "\n"))]
    (line "# clojure.core API parity")
    (line)
    (line "Generated by `make api-diff` (scripts/api-diff.clj) from `(ns-publics 'clojure.core)` on JVM Clojure "
          (clojure-version) " against this runtime's clojure.core, with the corpus under `corpus/` as the weight.")
    (line)
    (line "| | count |")
    (line "|---|---|")
    (line "| JVM public vars | " (count jvm) " |")
    (line "| ours | " (count ours-by) " |")
    (line "| in both | " (count common) " |")
    (line "| missing here | " (count missing) " |")
    (line "| missing and used by the corpus | " (count used-missing) " |")
    (line "| ours only, public | " (count extra) " |")
    (line "| ours only, internal (`name*`) | " (count internal) " |")
    (line "| macro/fn mismatches | " (count kind-mismatch) " |")
    (line "| arity mismatches | " (count arity-mismatch) " |")
    (line "| fns without :arglists here | " (count no-arglists) " |")
    (line)
    (line "## Missing, weighted by corpus uses")
    (line)
    (line "Occurrences of the name in `corpus/**/*.clj*` (unqualified or `clojure.core/`-qualified; every position, definitions"
          " and shadowed locals included, so the count is an upper bound).")
    (line)
    (line "| uses | name | JVM arglists |")
    (line "|---|---|---|")
    (doseq [[n c] (take 60 weighted)]
      (line "| " c " | `" n "` | `" (pr-str (:arglists (jvm-by n))) "` |"))
    (line)
    (line "## Every missing name")
    (line)
    (line (str/join " " (map #(str "`" % "`") missing)))
    (line)
    (line "## Macro/fn mismatches")
    (line)
    (if (seq kind-mismatch)
      (do (line "| name | JVM | ours |") (line "|---|---|---|")
          (doseq [n kind-mismatch]
            (line "| `" n "` | " (if (:macro (jvm-by n)) "macro" "fn") " | " (if (:macro (ours-by n)) "macro" "fn") " |")))
      (line "none"))
    (line)
    (line "## Dynamic mismatches")
    (line)
    (if (seq dynamic-mismatch)
      (line (str/join " " (map #(str "`" % "`") dynamic-mismatch)))
      (line "none"))
    (line)
    (line "## Arity mismatches")
    (line)
    (line "Compared as the set of fixed arities plus `n+` for a variadic one.")
    (line)
    (line "| name | JVM | ours |")
    (line "|---|---|---|")
    (doseq [[n j o] arity-mismatch]
      (line "| `" n "` | `" (pr-str j) "` | `" (pr-str o) "` |"))
    (line)
    (line "## Fns without :arglists here")
    (line)
    (line "C builtins and host primitives carry no :arglists (NOTES.md): " (str/join " " (map #(str "`" % "`") no-arglists)))
    (line)
    (line "## Ours only")
    (line)
    (line "Public: " (str/join " " (map #(str "`" % "`") extra)))
    (line)
    (line "Internal helpers (`name*`): " (str/join " " (map #(str "`" % "`") internal)))
    (when ours-async-file (async-section line ours-async-file))
    (spit out-file (str sb))
    (println "wrote" out-file ":" (count missing) "missing," (count used-missing) "used by the corpus")))

(let [[cmd & args] *command-line-args*]
  (case cmd
    "dump-jvm" (pp/pprint (dump-jvm))
    "diff" (apply diff args)
    (do (println "usage: api-diff.clj dump-jvm | diff jvm.edn ours.edn corpus-dir out.md")
        (System/exit 2))))
