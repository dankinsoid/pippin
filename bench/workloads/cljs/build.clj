;; @ai-generated(solo)
;; build.clj <out> <entry-file>...: each entry (scripts/corpus-bench.py writes them) to <out>/<entry name>.js,
;; :advanced, one bundle per workload as the closed build is one binary per workload.
(require '[cljs.build.api :as api]
         '[clojure.java.io :as io])

(let [[out & entries] *command-line-args*]
  (doseq [entry entries]
    (let [f (io/file entry)
          js (io/file out (str (second (re-find #"(.+)\.cljs$" (.getName f))) ".js"))
          t0 (System/nanoTime)]
      (api/build f {:main (symbol (str "bench." (.replace (second (re-find #"(.+)\.cljs$" (.getName f))) "_" "-")))
                    :optimizations :advanced
                    :output-to (.getPath js)
                    ;; The bundles share the compiled core: only Closure's pass runs per workload.
                    :output-dir (.getPath (io/file out "obj"))
                    ;; Closure's goog.global is `this`, which Node's module scope sets to module.exports.
                    :output-wrapper "(function(){\n%s\n}).call(globalThis);"
                    :language-out :es5
                    :warnings {:infer-warning false}})
      (println "built" (.getPath js) (format "%.1f s" (/ (- (System/nanoTime) t0) 1e9))))))
(shutdown-agents)
