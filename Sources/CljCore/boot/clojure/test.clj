;; @ai-generated(guided)
;; Failure positions come from the `is` form's reader position: there is no stack trace to read them from.
(ns clojure.test
  (:require [clojure.template :as temp]))

(def ^:dynamic *load-tests*
  "True by default. If set to false, no test functions will be created by deftest."
  true)

(def ^:dynamic *stack-trace-depth* nil)

(def ^:dynamic *report-counters* nil)

(def ^:dynamic *initial-report-counters* {:test 0 :pass 0 :fail 0 :error 0})

(def ^:dynamic *testing-vars* (list))

(def ^:dynamic *testing-contexts* (list))

(def ^:dynamic *assertion-pos*
  "{:file :line :column} of the `is` form being evaluated; nil outside one."
  nil)

(defmacro with-test-out
  "Runs body; output goes wherever println goes here."
  [& body]
  `(do ~@body))

(defn testing-vars-str
  "Returns a string representation of the current test, with the file and line of the assertion."
  [m]
  (let [{:keys [file line]} m]
    (str (reverse (map #(:name (meta %)) *testing-vars*))
         " (" (or file "NO_SOURCE_PATH") ":" line ")")))

(defn testing-contexts-str
  "Returns a string representation of the current test context."
  []
  (apply str (interpose " " (reverse *testing-contexts*))))

(defn inc-report-counter
  "Increments the named counter in *report-counters*, when bound."
  [name]
  (when *report-counters*
    (swap! *report-counters* update name (fnil inc 0))))

(defmulti ^:dynamic report
  "Generic reporting function; rebind to plug in a custom reporter. Dispatches on :type."
  :type)

(defn- print-trace [e]
  (when-let [tr (ex-trace e)]
    (doseq [frame (if *stack-trace-depth* (take *stack-trace-depth* tr) tr)]
      (println "    at" (or (:fn frame) "fn") (str "(" (:line frame) ":" (:column frame) ")")))))

(defmethod report :default [m]
  (prn m))

(defmethod report :pass [m]
  (inc-report-counter :pass))

(defmethod report :fail [m]
  (inc-report-counter :fail)
  (println "\nFAIL in" (testing-vars-str m))
  (when (seq *testing-contexts*) (println (testing-contexts-str)))
  (when-let [message (:message m)] (println message))
  (println "expected:" (pr-str (:expected m)))
  (println "  actual:" (pr-str (:actual m))))

(defmethod report :error [m]
  (inc-report-counter :error)
  (println "\nERROR in" (testing-vars-str m))
  (when (seq *testing-contexts*) (println (testing-contexts-str)))
  (when-let [message (:message m)] (println message))
  (println "expected:" (pr-str (:expected m)))
  (println "  actual:" (pr-str (:actual m)))
  (print-trace (:actual m)))

(defmethod report :summary [m]
  (println "\nRan" (:test m) "tests containing"
           (+ (:pass m) (:fail m) (:error m)) "assertions.")
  (println (:fail m) "failures," (:error m) "errors."))

(defmethod report :begin-test-ns [m]
  (println "\nTesting" (ns-name (:ns m))))

(defmethod report :end-test-ns [m])
(defmethod report :begin-test-var [m])
(defmethod report :end-test-var [m])

(defn do-report
  "Adds the position of the `is` form, or of the current test var, to a :fail or :error that carries none."
  [m]
  (report
   (if (and (or (= :fail (:type m)) (= :error (:type m))) (nil? (:line m)))
     (let [v (first *testing-vars*)]
       (merge (or *assertion-pos* {:file (:file (meta v)) :line (:line (meta v))}) m))
     m)))

;; ---- assertions

(defn function?
  "Returns true if argument is a function or a symbol that resolves to a function (not a macro)."
  [x]
  (if (symbol? x)
    (when-let [v (resolve x)]
      (and (bound? v) (not (:macro (meta v))) (fn? @v)))
    (fn? x)))

(defn assert-predicate
  "Returns generic assertion code for any functional predicate: the expected and actual arguments are
  evaluated once and reported with the predicate."
  [msg form]
  (let [args (rest form)
        pred (first form)]
    `(let [values# (list ~@args)
           result# (apply ~pred values#)]
       (if result#
         (do-report {:type :pass, :message ~msg,
                     :expected '~form, :actual (cons '~pred values#)})
         (do-report {:type :fail, :message ~msg,
                     :expected '~form, :actual (list '~'not (cons '~pred values#))}))
       result#)))

(defn assert-any
  "Returns generic assertion code for any test, including macros, Java method calls, or isolated symbols."
  [msg form]
  `(let [value# ~form]
     (if value#
       (do-report {:type :pass, :message ~msg,
                   :expected '~form, :actual value#})
       (do-report {:type :fail, :message ~msg,
                   :expected '~form, :actual value#}))
     value#))

(defmulti assert-expr
  "Extension point for `is`: dispatches on the head of the asserted form."
  (fn [msg form]
    (cond
      (nil? form) :always-fail
      (seq? form) (first form)
      :else :default)))

(defmethod assert-expr :always-fail [msg form]
  `(do-report {:type :fail, :message ~msg}))

(defmethod assert-expr :default [msg form]
  (if (and (sequential? form) (function? (first form)))
    (assert-predicate msg form)
    (assert-any msg form)))

(defmethod assert-expr 'instance? [msg form]
  `(let [klass# ~(nth form 1)
         object# ~(nth form 2)]
     (let [result# (instance? klass# object#)]
       (if result#
         (do-report {:type :pass, :message ~msg,
                     :expected '~form, :actual (type object#)})
         (do-report {:type :fail, :message ~msg,
                     :expected '~form, :actual (type object#)}))
       result#)))

(defmethod assert-expr 'thrown? [msg form]
  (let [klass (second form)
        body (nthnext form 2)]
    `(try ~@body
          (do-report {:type :fail, :message ~msg,
                      :expected '~form, :actual nil})
          (catch ~klass e#
            (do-report {:type :pass, :message ~msg,
                        :expected '~form, :actual e#})
            e#))))

(defmethod assert-expr 'thrown-with-msg? [msg form]
  (let [klass (nth form 1)
        re (nth form 2)
        body (nthnext form 3)]
    `(try ~@body
          (do-report {:type :fail, :message ~msg, :expected '~form, :actual nil})
          (catch ~klass e#
            (let [m# (ex-message e#)]
              (if (and (string? m#) (some? (re-find ~re m#)))
                (do-report {:type :pass, :message ~msg,
                            :expected '~form, :actual e#})
                (do-report {:type :fail, :message ~msg,
                            :expected '~form, :actual e#})))
            e#))))

(defmacro try-expr
  "Used by the `is` macro to catch unexpected exceptions."
  [msg form]
  `(try ~(assert-expr msg form)
        (catch :default t#
          (do-report {:type :error, :message ~msg,
                      :expected '~form, :actual t#}))))

(defmacro is
  "Generic assertion macro. 'form' is any predicate test. 'msg' is an optional message to attach.
  Special forms: (is (thrown? c body)), (is (thrown-with-msg? c re body))."
  ([form] (with-meta `(is ~form nil) (meta &form)))
  ([form msg]
   (let [{:keys [line column]} (meta &form)]
     `(binding [*assertion-pos* {:file ~*file* :line ~line :column ~column}]
        (try-expr ~msg ~form)))))

(defmacro are
  "Checks multiple assertions with a template expression: (are [x y] (= x y) 2 (+ 1 1) 4 (* 2 2))."
  [argv expr & args]
  (if (or
       (and (empty? argv) (empty? args))
       (and (pos? (count argv))
            (pos? (count args))
            (zero? (mod (count args) (count argv)))))
    `(temp/do-template ~argv (is ~expr) ~@args)
    (throw (ex-info "The number of args doesn't match are's argv or testing doesn't have any args" {}))))

(defmacro testing
  "Adds a new string to the list of testing contexts for the extent of body."
  [string & body]
  `(binding [*testing-contexts* (conj *testing-contexts* ~string)]
     ~@body))

;; ---- defining tests

(defmacro with-test
  "Adds a test fn to the var defined by definition."
  [definition & body]
  (if *load-tests*
    `(doto ~definition (alter-meta! assoc :test (fn [] ~@body)))
    definition))

(defmacro deftest
  "Defines a test function with no arguments; the body becomes the var's :test."
  [name & body]
  (when *load-tests*
    `(def ~(vary-meta name assoc :test `(fn [] ~@body))
       (fn [] (test-var (var ~name))))))

(defmacro deftest-
  "Like deftest but creates a private var."
  [name & body]
  (when *load-tests*
    `(def ~(vary-meta name assoc :test `(fn [] ~@body) :private true)
       (fn [] (test-var (var ~name))))))

(defmacro set-test
  "Sets the :test of an existing var."
  [name & body]
  (when *load-tests*
    `(alter-meta! (var ~name) assoc :test (fn [] ~@body))))

;; ---- fixtures: namespaces carry no metadata, so they live in an atom keyed by namespace name

(def ^:private fixtures (atom {}))

(defmulti use-fixtures
  "Wrap test runs in a fixture function to perform setup and teardown: (use-fixtures :each f) around every
  test var, (use-fixtures :once f) around the whole namespace run."
  (fn [fixture-type & args] fixture-type))

(defmethod use-fixtures :each [fixture-type & args]
  (swap! fixtures assoc-in [(ns-name *ns*) :each] args))

(defmethod use-fixtures :once [fixture-type & args]
  (swap! fixtures assoc-in [(ns-name *ns*) :once] args))

(defn- default-fixture
  "The default, empty, fixture function. Just calls its argument."
  [f]
  (f))

(defn compose-fixtures
  "Composes two fixture functions, creating a new fixture function that combines their behavior."
  [f1 f2]
  (fn [g] (f1 (fn [] (f2 g)))))

(defn join-fixtures
  "Composes a collection of fixtures, in order. Always returns a valid fixture function."
  [fixtures]
  (reduce compose-fixtures default-fixture fixtures))

(defn- ns-key [ns] (if (symbol? ns) ns (ns-name ns)))

;; ---- running

(defn test-var
  "If v has a function in its :test metadata, calls that function, with *testing-vars* bound to (conj *testing-vars* v)."
  [v]
  (when-let [t (:test (meta v))]
    (binding [*testing-vars* (conj *testing-vars* v)]
      (do-report {:type :begin-test-var, :var v})
      (inc-report-counter :test)
      (try (t)
           (catch :default e
             (do-report {:type :error, :message "Uncaught exception, not in assertion."
                         :expected nil, :actual e})))
      (do-report {:type :end-test-var, :var v}))))

(defn test-vars
  "Groups vars by their namespace and runs test-var on them with appropriate fixtures assuming they are
  present in the current namespace."
  [vars]
  (doseq [[ns vars] (group-by (comp :ns meta) vars)]
    (let [fx (get @fixtures ns)
          once-fixture-fn (join-fixtures (:once fx))
          each-fixture-fn (join-fixtures (:each fx))]
      (once-fixture-fn
       (fn []
         (doseq [v vars]
           (when (:test (meta v))
             (each-fixture-fn (fn [] (test-var v))))))))))

(defn test-all-vars
  "Calls test-vars on every var interned in the namespace, in file order."
  [ns]
  (test-vars (sort-by (fn [v] (or (:line (meta v)) 0)) (vals (ns-interns ns)))))

(defn test-ns
  "Runs the tests of one namespace (a symbol or namespace) under fresh report counters, which it returns.
  A test-ns-hook var replaces test-all-vars."
  [ns]
  (binding [*report-counters* (atom *initial-report-counters*)]
    (let [ns-obj (the-ns ns)]
      (do-report {:type :begin-test-ns, :ns ns-obj})
      (if-let [v (get (ns-interns ns-obj) 'test-ns-hook)]
        ((var-get v))
        (test-all-vars ns-obj))
      (do-report {:type :end-test-ns, :ns ns-obj}))
    @*report-counters*))

(defn run-tests
  "Runs all tests in the given namespaces; prints results. Defaults to current namespace if none given.
  Returns a map summarizing test results."
  ([] (run-tests *ns*))
  ([& namespaces]
   (let [summary (assoc (apply merge-with + (map test-ns namespaces))
                        :type :summary)]
     (do-report summary)
     summary)))

(defn run-all-tests
  "Runs all tests in all namespaces; prints results. Optional argument is a regex or a predicate
  matched against each namespace name."
  ([] (apply run-tests (all-ns)))
  ([re-or-pred]
   (let [match? (if (regex? re-or-pred) (fn [name] (re-find re-or-pred name)) re-or-pred)]
     (apply run-tests (filter (fn [ns] (match? (str (ns-name ns)))) (all-ns))))))

(defn run-test-var
  "Runs the tests for a single var, with fixtures executed around the test, and summarizes the results."
  [v]
  (binding [*report-counters* (atom *initial-report-counters*)]
    (test-vars [v])
    (let [summary (assoc @*report-counters* :type :summary)]
      (do-report summary)
      summary)))

(defmacro run-test
  "Runs a single test. Because the intent is to run a single test, there is no check for the namespace test-ns-hook."
  [test-symbol]
  (let [test-var (resolve test-symbol)]
    (cond
      (nil? test-var) (throw (ex-info (str "Unable to resolve " test-symbol " to a test function.") {}))
      (not (:test (meta test-var))) (throw (ex-info (str test-symbol " is not a test.") {}))
      :else `(run-test-var ~test-var))))

(defn successful?
  "Returns true if the given test summary indicates all tests were successful, false otherwise."
  [summary]
  (and (zero? (:fail summary 0))
       (zero? (:error summary 0))))
