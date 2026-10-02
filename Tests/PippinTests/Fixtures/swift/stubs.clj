;; The first level-2 slice: interpreted and compiled (dev, closed), each run prints stubs.out (SwiftStubTests).
(ns fixture.swift-stubs
  (:require-swift [PippinFixture :as fx :refer [make-point moved describe is-last-made tag blob scaled]]))

(defn show [& xs] (apply println (map pr-str xs)))

(def p (make-point :x 1 :y 2))
(show p (describe p))

;; @MainActor: from this thread the call hops to the main actor and parks; from a pool coroutine too.
(def q (moved p :by 3))
(show (describe q) (describe @(future (moved q :by 10))))

;; The box handed back is the value Swift made, not an equal one rebuilt: only the last one made is "last".
(def r (moved p :by 1))
(show (is-last-made r) (is-last-made p) (is-last-made (moved p :by 1)))

;; Hashable: equal points are =, hash alike, and are one key.
(show (= p (make-point :x 1 :y 2)) (= p q) (= (hash p) (hash (make-point :x 1 :y 2))))
(show (count (hash-set p (make-point :x 1 :y 2) q)) (get {p :found} (make-point :x 1 :y 2)) (contains? #{q} p))
(show (try #{p (make-point :x 1 :y 2)} (catch :default e (ex-message e))))

;; Equatable only: = works, and being a key refuses out loud.
(show (= (tag "a") (tag "a")) (= (tag "a") (tag "b")))
(show (try (conj #{} (tag "a")) (catch :default e (ex-message e))))
(show (try (hash (tag "a")) (catch :default e (ex-message e))))

;; Neither: no key, and no = between two distinct boxes; a lookup answers "absent", which is true.
(show (try {(blob 1) 1} (catch :default e (ex-message e))))
(show (try (assoc {} [(blob 1)] 1) (catch :default e (ex-message e))))
(show (try (= (blob 1) (blob 1)) (catch :default e (ex-message e))))
(show (let [b (blob 1)] (= b b)) (get {:a 1} (blob 2)) (contains? #{1} (blob 3)) (= (blob 1) 1))

;; Scalars, an alias, and the errors a wrong call gets.
(show (scaled 1.5 :by 2.0) (fx/describe p))
(show (try (moved p :to 3) (catch :default e (ex-message e))))
(show (try (moved 1 :by 3) (catch :default e (ex-message e))))
