;; @ai-generated(guided)
(ns merry.main
  (:require [clojure.java.io :as io]
            [clojure.pprint :as pp]
            [clojure.string :as str]
            [clojure.walk :as walk]
            [merry.check :as ck]
            [merry.eval :as ev]
            [merry.expand :as ex]
            [merry.read :as rd]
            [merry.types :as t]))

(defn- process [src]
  (mapv (fn [{:keys [form end]}]
          (let [r (try
                    (let [x (ex/expand-top form)]
                      (assoc (ck/check-top x) :expanded x))
                    (catch clojure.lang.ExceptionInfo e
                      (if (::t/error (ex-data e))
                        {:kind :error
                         :message (ex-message e)
                         :line (or (:line (ex-data e)) (:line (meta form)))}
                        (throw e))))]
            (assoc r :end end)))
        (rd/read-forms src)))

(defn- session [f]
  (binding [t/*state* (t/new-state)
            ck/*globals* (atom {})
            ck/*builtins* (atom {})
            ev/*globals* (atom {})]
    (f)))

(defn- errors [rs] (filter #(= :error (:kind %)) rs))

(defn- load-prelude []
  (let [rs (process (io/resource "merry/prelude.merry"))]
    (when-let [e (first (errors rs))]
      (throw (ex-info (str "prelude, line " (:line e) ": " (:message e)) {})))
    rs))

(defn- display
  "The elaborated form as a reader would write it: (clojure.core/deref x) back to @x."
  [form]
  (walk/postwalk #(if (and (seq? %) (= 'clojure.core/deref (first %)) (symbol? (second %)))
                    (symbol (str "@" (second %)))
                    %)
                 form))

(defn- commented [prefix s]
  (map #(str prefix %) (str/split-lines s)))

(defn- annotation [r explain?]
  (let [elab (when explain?
               (cons ";; elaborated:"
                     (commented ";;   " (with-out-str (pp/with-pprint-dispatch pp/code-dispatch
                                                         (pp/pprint (display (:elab r))))))))
        load-eff (when (seq (:load-eff r))
                   [(str ";; runs at load: #{" (str/join " " (sort (map str (:load-eff r)))) "}")])]
    (case (:kind r)
      (:defn :def) (concat [(str ";; => " (t/show-contract (:type r)))] load-eff elab)
      :expr (concat [(str ";; : " (t/show-contract (:type r)))] load-eff elab)
      :error (commented ";; " (str "error (line " (:line r) "): " (:message r)))
      :skip nil)))

(defn- check-cmd [path explain?]
  (session
    (fn []
      (load-prelude)
      (let [rs (process path)
            by-end (group-by :end rs)
            n (count (errors rs))]
        (doseq [[i line] (map-indexed vector (str/split-lines (slurp path)))]
          (println line)
          (doseq [r (get by-end (inc i)) a (annotation r explain?)]
            (println a)))
        (println (str ";; " n (if (= 1 n) " error" " errors")))
        (if (zero? n) 0 1)))))

(defn- expand-cmd [path]
  (binding [t/*state* (t/new-state)]
    (doseq [{:keys [form]} (rd/read-forms path)]
      (pp/with-pprint-dispatch pp/code-dispatch
        (pp/pprint (display (ex/expand-top form)))))
    0))

(defn- run-cmd [path]
  (session
    (fn []
      (let [pre (load-prelude)
            rs (process path)]
        (if-let [es (seq (errors rs))]
          (do (doseq [e es] (println (str path ":" (:line e) ": " (:message e))))
              1)
          (try
            (doseq [r (concat pre rs) :when (:elab r)]
              (ev/eval-top (:elab r)))
            (when-let [m (ev/global '-main)] (m))
            0
            (catch ArithmeticException e
              (println (str "panic: integer overflow (" (ex-message e) ")"))
              2)))))))

(def ^:private usage
  "usage: clojure -M:run <command> <file.merry>
  check [--explain]  print the file with inferred contracts; --explain adds elaborated forms
  expand             print the forms after macro expansion
  run                check, then call -main")

(defn -main [& [cmd path & flags]]
  (let [code (cond
               (nil? path) (do (println usage) 64)
               (= "check" cmd) (check-cmd path (some #{"--explain"} flags))
               (= "expand" cmd) (expand-cmd path)
               (= "run" cmd) (run-cmd path)
               :else (do (println usage) 64))]
    (flush)
    (System/exit code)))
