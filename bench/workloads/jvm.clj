;; @ai-generated(solo)
;; jvm.clj <ns> [size|once]: clj-corpus-bench's report lines for JVM Clojure; once is the caller's cold-process run.
(defn- now-ms [] (/ (double (System/nanoTime)) 1e6))

(defn- median [xs]
  (let [s (vec (sort xs))]
    (nth s (quot (count s) 2))))

(let [[ns-name mode] *command-line-args*
      t0 (now-ms)
      _ (require (symbol ns-name))
      load-ms (- (now-ms) t0)
      run (resolve (symbol ns-name "run"))]
  (println "load_ms" load-ms)
  (cond
    (= mode "once")
    (let [t1 (now-ms)
          r (run)]
      (println "first_ms" (- (now-ms) t1))
      (println "result" (pr-str r)))

    mode
    (println "result" (pr-str ((resolve (symbol ns-name "run*")) (Long/parseLong mode))))

    :else
    (let [t1 (now-ms)
          first-result (run)
          first-ms (- (now-ms) t1)
          ;; C2 settles within seconds; the warm-up runs at least 10 iterations and 10 s.
          warm-start (now-ms)
          warm (loop [k 0]
                 (if (or (< k 10) (< (- (now-ms) warm-start) 10000.0))
                   (do (run) (recur (inc k)))
                   k))
          times (loop [acc []]
                  (if (or (< (count acc) 10) (and (< (reduce + acc) 5000.0) (< (count acc) 50)))
                    (let [t (now-ms)]
                      (run)
                      (recur (conj acc (- (now-ms) t))))
                    acc))]
      (println "first_ms" first-ms)
      (println "warmup_iterations" warm)
      (println "iterations" (count times))
      (println "median_ms" (median times))
      (println "min_ms" (apply min times))
      (println "result" (pr-str first-result)))))
(flush)
(System/exit 0)
