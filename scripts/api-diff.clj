;; @ai-generated(guided)
;; Runs on JVM Clojure through `make api-diff`; the dumps are vectors of {:name :arglists :macro :dynamic :extension}.
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

;; The version comes from the jar the classpath actually resolved, so the report cannot name a stale one.
(defn- async-jar-version []
  (when-let [url (io/resource "clojure/core/async.clj")]
    (second (re-find #"core\.async-([^/!]+)\.jar" (str url)))))

(defn dump-async []
  (require 'clojure.core.async)
  {:version (or (async-jar-version) "unknown")
   :publics (->> (ns-publics 'clojure.core.async)
                 (map (fn [[sym v]] {:name sym :deprecated (boolean (:deprecated (meta v)))}))
                 (sort-by :name)
                 vec)})

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

(defn- internal?
  "The `name*` convention for a native helper; earmuffs are the dynamic-var convention, not that one."
  [sym]
  (let [n (name sym)]
    (and (str/ends-with? n "*") (not (str/starts-with? n "*")))))

(defn- names [syms] (str/join " " (map #(str "`" % "`") syms)))

(defn- unmarked [ours-by syms] (remove #(:extension (ours-by %)) syms))

(defn- async-section
  "The core.async part of the report; returns the ours-only publics that carry no mark."
  [line ours-async-file jvm-async-file]
  (let [ours-by (into {} (map (juxt :name identity) (edn/read-string (slurp ours-async-file))))
        jvm (edn/read-string (slurp jvm-async-file))
        jvm-names (set (map :name (:publics jvm)))
        deprecated (->> (:publics jvm) (filter :deprecated) (map :name) set)
        built (sort (filter jvm-names (keys ours-by)))
        missing (sort (remove ours-by jvm-names))
        ;; core.async's own protocol methods end in *, so the internal convention may only judge what is ours.
        ours-only (->> (keys ours-by) (remove jvm-names) sort)
        extra (remove internal? ours-only)
        internal (filter internal? ours-only)
        bare (unmarked ours-by extra)]
    (line)
    (line "## clojure.core.async")
    (line)
    (line "Against `(ns-publics 'clojure.core.async)` of core.async " (:version jvm) ", dumped from the library itself"
          " (docs/jvm-differences.md, \"core.async\": the blocking variants are aliases, any function may park).")
    (line)
    (line "| | count |")
    (line "|---|---|")
    (line "| core.async public vars | " (count jvm-names) " |")
    (line "| built | " (count built) " |")
    (line "| missing | " (count missing) " |")
    (line "| ours only, public | " (count extra) " |")
    (line "| ours only, internal (`name*`) | " (count internal) " |")
    (line "| ours only, public without `^:pippin/extension` | " (count bare) " |")
    (line)
    (line "Built: " (names built))
    (line)
    (line "Of those, deprecated upstream but still public in " (:version jvm) ", so ours by compatibility and not"
          " extensions: " (names (filter deprecated built)))
    (line)
    (line "Missing: " (if (seq missing) (names missing) "none"))
    (line)
    (line "Ours only: " (names extra))
    (line)
    (line "Internal helpers (`name*`): " (names internal))
    bare))

(defn diff [jvm-file ours-file corpus-dir out-file & [ours-async-file jvm-async-file]]
  (let [jvm (edn/read-string (slurp jvm-file))
        ours (edn/read-string (slurp ours-file))
        jvm-by (into {} (map (juxt :name identity) jvm))
        ours-by (into {} (map (juxt :name identity) ours))
        uses (symbol-uses corpus-dir)
        missing (->> (keys jvm-by) (remove ours-by) sort)
        ours-only (->> (keys ours-by) (remove jvm-by) sort)
        extra (remove internal? ours-only)
        internal (filter internal? ours-only)
        bare (unmarked ours-by extra)
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
    (line "A public var this core has and the JVM's clojure.core has not must carry `^:pippin/extension`; `make api-diff`"
          " fails on an unmarked one (docs/design.md).")
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
    (line "| ours only, public without `^:pippin/extension` | " (count bare) " |")
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
    (line (names missing))
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
      (line (names dynamic-mismatch))
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
    (line "C builtins and host primitives carry no :arglists (NOTES.md): " (names no-arglists))
    (line)
    (line "## Ours only")
    (line)
    (line "Public: " (names extra))
    (line)
    (line "Internal helpers (`name*`): " (names internal))
    (let [async-bare (when ours-async-file (async-section line ours-async-file jvm-async-file))
          offenders (concat (map #(str "clojure.core/" %) bare)
                            (map #(str "clojure.core.async/" %) async-bare))]
      (line)
      (line "## Unmarked extensions")
      (line)
      (line (if (seq offenders) (names offenders) "none"))
      (spit out-file (str sb))
      (println "wrote" out-file ":" (count missing) "missing," (count used-missing) "used by the corpus")
      (when (seq offenders)
        (binding [*out* *err*]
          (println "api-diff: ours-only public vars without ^:pippin/extension:" (str/join " " offenders)))
        (System/exit 1)))))

(let [[cmd & args] *command-line-args*]
  (case cmd
    "dump-jvm" (pp/pprint (dump-jvm))
    "dump-async" (pp/pprint (dump-async))
    "diff" (apply diff args)
    (do (println "usage: api-diff.clj dump-jvm | dump-async | diff jvm.edn ours.edn corpus-dir out.md [ours-async.edn jvm-async.edn]")
        (System/exit 2))))
