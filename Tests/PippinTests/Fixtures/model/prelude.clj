;; @ai-generated(solo)
;; The checks of ModelTests.swift; the expected values come from its Swift model.
(ns pippin.model)

(def mt-a0 (atom 0 :meta {:mt 0}))
(def mt-a1 (atom 1 :meta {:mt 1}))
(def mt-a2 (atom 2 :meta {:mt 2}))
(def mt-box (atom nil))

(defn mt-q [& xs] (into clojure.lang.PersistentQueue/EMPTY xs))
(defn mt-yes [] (some? mt-box))
(defn mt-no [] (throw (ex-info "model: the branch not taken ran" {})))
(defn mt-wrap [x] [x])

;; Published by the atom, then unique again once the caller drops the argument.
(defn mt-pub [x] (reset! mt-box x) (reset! mt-box nil) x)

(defn mt-join [sep xs] (apply str (interpose sep xs)))

;; The canonical text is also the code that rebuilds the value. sort-by: a C-only host binds no `sort`.
(defn mt-pr [x]
  (cond
    (atom? x) (str "mt-a" (:mt (meta x)))
    (map? x) (if (sorted? x)
               (str "(sorted-map" (apply str (map (fn [e] (str " " (mt-pr (key e)) " " (mt-pr (val e)))) x)) ")")
               (str "{" (mt-join ", " (sort-by identity (map (fn [e] (str (mt-pr (key e)) " " (mt-pr (val e)))) x))) "}"))
    (set? x) (if (sorted? x)
               (str "(sorted-set" (apply str (map (fn [e] (str " " (mt-pr e))) x)) ")")
               (str "#{" (mt-join " " (sort-by identity (map mt-pr x))) "}"))
    (vector? x) (str "[" (mt-join " " (map mt-pr x)) "]")
    (instance? clojure.lang.PersistentQueue x) (str "(mt-q" (apply str (map (fn [e] (str " " (mt-pr e))) x)) ")")
    (or (seq? x) (list? x)) (str "(list" (apply str (map (fn [e] (str " " (mt-pr e))) x)) ")")
    :else (pr-str x)))

(defn mt-fail [tag what] (println "MT-FAIL" tag (pr-str what)))

(defn mt-lookups [v exp]
  (cond
    (map? exp) (every? (fn [e] (= (get v (key e) ::absent) (val e))) exp)
    (set? exp) (every? (fn [x] (contains? v x)) exp)
    (vector? exp) (every? (fn [i] (= (nth v i) (nth exp i))) (range (count exp)))
    :else true))

;; exp is nil for a value whose text is too long to spell twice; it is rebuilt from the text then.
(defn mt-chk [tag v n txt exp mtxt]
  (let [exp (if (nil? exp) (eval (read-string txt)) exp)
        p (mt-pr v)]
    (cond
      (not= (count v) n) (mt-fail tag [:count (count v) n])
      (not= p txt) (mt-fail tag [:value p txt])
      (not (= v exp)) (mt-fail tag [:= p])
      (not (= exp v)) (mt-fail tag [:=reverse p])
      (not= (hash v) (hash exp)) (mt-fail tag [:hash (hash v) (hash exp)])
      (not= (mt-pr (meta v)) mtxt) (mt-fail tag [:meta (mt-pr (meta v)) mtxt])
      (not= (reduce (fn [a _] (inc a)) 0 v) n) (mt-fail tag [:reduce (reduce (fn [a _] (inc a)) 0 v)])
      (not (mt-lookups v exp)) (mt-fail tag [:lookup p]))
    v))

(defn mt-view [tag s unordered txt]
  (let [p (if unordered (mt-pr (set s)) (mt-pr (apply list s)))]
    (when (not= p txt) (mt-fail tag [:view p txt]))))

(defn mt-run [tag f]
  (try (f) (catch Exception e (mt-fail tag [:threw (ex-message e)]))))
