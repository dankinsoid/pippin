;; @ai-generated(solo)
;; jvm.clj's report lines for a ClojureScript workload; run is continuation-style so a channel is timed to delivery.
(ns bench.harness
  (:require [goog.object :as gobj]))

;; The Hermes CLI has no performance object: its clock is Date.now, whole milliseconds.
(def ^:private hires (exists? js/performance))

(defn- now-ms []
  (if hires (.now js/performance) (.now js/Date)))

;; Hermes has print and no console; Node the other way round.
(defn- out [s]
  (if (exists? js/print) (js/print s) (.log js/console s)))

;; The Hermes CLI passes no arguments to the script: the caller prepends `var BENCH_ARGS = [...]` to the bundle.
(defn- args []
  (if-some [a (gobj/get js/globalThis "BENCH_ARGS")]
    (vec a)
    ;; Through globalThis: the browser target defines its own `process` for process.env.NODE_ENV.
    (vec (.slice (gobj/getValueByKeys js/globalThis "process" "argv") 2))))

(defn- timed [run-k k]
  (let [t (now-ms)]
    (run-k (fn [r] (k r (- (now-ms) t))))))

(defn- loop-times
  "Times iterations while (more? times elapsed-ms) holds, then calls k with the times."
  [run-k more? k]
  (let [start (now-ms)]
    ((fn step [acc]
       (if (more? acc (- (now-ms) start))
         (timed run-k (fn [_ ms] (step (conj acc ms))))
         (k acc)))
     [])))

(defn- median [xs]
  (let [s (vec (sort xs))]
    (nth s (quot (count s) 2))))

(defn- verdict [r expected]
  (if (= r expected) "same" "DIFFERENT"))

(defn main
  "run-k calls the workload's run and passes its value to the continuation it is given."
  [run-k expected]
  (out (str "clock_hires " hires))
  (let [mode (first (args))
        ;; jvm.clj's policy, for V8's tiers as for C2; nojit is clj-corpus-bench's, for Hermes, which has no JIT and
        ;; whose slowest workload would take minutes under the other.
        [warm? measure?] (if (= mode "nojit")
                           [(fn [_ _] false)
                            (fn [acc elapsed] (or (< (count acc) 3) (< elapsed 2000.0)))]
                           [(fn [acc elapsed] (or (< (count acc) 5) (< elapsed 5000.0)))
                            (fn [acc _] (or (< (count acc) 5) (and (< (reduce + acc) 3000.0) (< (count acc) 50))))])]
    (if (= mode "once")
      (timed run-k
             (fn [r ms]
               (out (str "first_ms " ms))
               (out (str "expected " (verdict r expected)))
               (out (str "result " (pr-str r)))))
      (timed run-k
             (fn [first-result first-ms]
               (loop-times run-k warm?
                           (fn [warm]
                             (loop-times run-k measure?
                                         (fn [times]
                                           (out (str "first_ms " first-ms))
                                           (out (str "warmup_iterations " (count warm)))
                                           (out (str "iterations " (count times)))
                                           (out (str "median_ms " (median times)))
                                           (out (str "min_ms " (apply min times)))
                                           (out (str "expected " (verdict first-result expected)))
                                           (out (str "result " (pr-str first-result))))))))))))
