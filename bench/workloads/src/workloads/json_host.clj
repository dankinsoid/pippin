;; @ai-generated(solo)
;; workloads.json's parser and writer: clojure.data.json, the JVM's jar, bb's built-in, this runtime's
;; corpus/data-json port. ClojureScript and ClojureDart have their own file, over the platform's JSON.
(ns workloads.json-host
  (:require [clojure.data.json :as json]))

(defn parse [text] (json/read-str text :key-fn keyword))

;; UTF-8 out, as JSON.stringify, JSONEncoder and jsonEncode write it: the written lengths agree.
(defn emit [x] (json/write-str x :escape-unicode false :escape-slash false))
