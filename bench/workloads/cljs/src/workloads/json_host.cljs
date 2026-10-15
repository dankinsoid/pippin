;; @ai-generated(solo)
;; ClojureScript's workloads.json-host (bench/workloads/src/workloads/json_host.clj): clojure.data.json is JVM-only,
;; and what a ClojureScript app writes instead is the engine's JSON with js->clj and clj->js around it.
(ns workloads.json-host)

(defn parse [text] (js->clj (js/JSON.parse text) :keywordize-keys true))

(defn emit [x] (js/JSON.stringify (clj->js x)))
