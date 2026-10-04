;; @ai-generated(solo)
;; Ours: a stand-in for org.clojure/test.generative (SOURCE).
(ns clojure.test.generative
  (:refer-clojure :exclude [is])
  (:require [clojure.test] [clojure.data.generators]))

(def ^:dynamic *trials* 20)

;; test.generative's own `is`, which the vendored files exclude in favour of clojure.test's.
(defn is [& _] true)

;; A syntax-quoted tag reads as (quote ns/name), so the call form is the symbol inside it.
(defn- generator-call [tag]
  (list (if (and (seq? tag) (= 'quote (first tag))) (second tag) tag)))

;; The real defspec needs test.generative's runner, so test-ns runs no spec; a deftest runs them here.
(defmacro defspec
  "name, the fn whose result the body sees as %, an arg vector whose :tag meta names each arg's generator,
   and assertions over the args and %."
  [name fn-under-test argvec & body]
  (let [gens (mapv #(generator-call (:tag (meta %))) argvec)
        args (mapv #(vary-meta % dissoc :tag) argvec)]
    `(clojure.test/deftest ~name
       (clojure.data.generators/set-seed! 42)
       (dotimes [_# *trials*]
         (let [~@(mapcat vector args gens)
               ~'% (~fn-under-test ~@args)]
           ~@body)))))
