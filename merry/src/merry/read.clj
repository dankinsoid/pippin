;; @ai-generated(guided)
(ns merry.read
  (:require [clojure.java.io :as io])
  (:import [clojure.lang LineNumberingPushbackReader]))

(defn read-forms
  "Top-level forms of src (a path or a URL), each with the line it ends on."
  [src]
  (with-open [r (LineNumberingPushbackReader. (io/reader src))]
    (binding [*read-eval* false
              *ns* (create-ns 'merry.user)]
      (loop [acc []]
        (let [form (read {:eof ::eof} r)]
          (if (= ::eof form)
            acc
            (recur (conj acc {:form form :end (.getLineNumber r)}))))))))
