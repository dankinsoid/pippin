;; @ai-generated(solo)
(ns clojure.core.async.impl.buffers
  "The JVM's buffer constructors, for code that requires this namespace: the buffers are the spec objects chan
  reads (NOTES.md, \"Channels\"), not containers with add!/remove! of their own.")

(defn fixed-buffer [n] (buffer* n))
(defn dropping-buffer [n] (dropping-buffer* n))
(defn sliding-buffer [n] (sliding-buffer* n))
(defn promise-buffer [] (promise-buffer*))
