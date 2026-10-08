;; Level 2 (design §5): interpreted and compiled (dev, closed), each run prints stubs.out (SwiftStubTests).
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

;; Members are vars Type.member: an initializer is Type., the receiver goes first, a static member takes none.
(def s (fx/Point. :x 3 :y 4))
(show (describe s) (fx/Point.sum s) (fx/Point.first s) (fx/Point.description s))
(show (describe (fx/Point.scaled s :by 2)) (describe (fx/Point.origin)) (describe (fx/Point.unit)))
(show (try (fx/Point. :validating -1) (catch PippinFixture/FixtureError e (ex-message e))) (describe (fx/Point. :validating 2)))

;; mutating returns the new self and leaves the box alone; with a result, [self' result]; a setter is mutating.
(show (describe (fx/Point.move s :by 5)) (describe s))
(show (let [[s' x] (fx/Point.take s)] [(describe s') x]) (describe (fx/Point.set-first s 10)) (describe s))
(def a (atom s))
(swap! a fx/Point.move :by 1)
(show (describe @a))

;; inout parameters come back in order, then the result; one alone comes back bare.
(show (fx/swapped 1 2) (describe (fx/bump s)))

;; Module variables: a getter, a setter, and a constant.
(show (fx/greeting) (fx/set-greeting "hi") (fx/greeting) (fx/set-greeting "hello") (fx/answer))

;; A class instance is a box of the object: setters change it behind every box of it.
(def made (fx/Counter.made))
(def c (fx/Counter. :name "c"))
(show (fx/Counter.name c) (fx/Counter.increment c :by 2) (fx/Counter.set-count c 10) (fx/Counter.count c) (- (fx/Counter.made) made))

;; Without Equatable a class compares by the object: the same object twice is =, two objects are not.
(show (= (fx/Counter.shared) (fx/Counter.shared)) (count (hash-set (fx/Counter.shared) (fx/Counter.shared)))
      (= (fx/Counter. :name "x") (fx/Counter. :name "x")) (= c c))
;; Hashable: its own == and hash. Equatable: its own ==, and no key.
(show (= (fx/Version. :major 1) (fx/Version. :major 1)) (count (hash-set (fx/Version. :major 1) (fx/Version. :major 1))))
(show (= (fx/Label. :text "a") (fx/Label. :text "a")) (try (conj #{} (fx/Label. :text "a")) (catch :default e (ex-message e))))
;; A subclass crosses where its superclass is expected, the override answers, and one object is one box type.
(def sq (fx/Square. :side 3))
(show (fx/Shape.area sq) (fx/Shape.area (fx/Shape.)) (= (fx/as-shape sq) sq) (fx/Shape.area (fx/as-shape sq)))
(show (try (fx/Square.area (fx/Shape.)) (catch :default e (ex-message e))))
(show (try (.area sq) (catch :default e (ex-message e))))

;; The four forms of throws: the Swift error is a host error caught by its type.
(show (fx/risky 3) (try (fx/risky 30) (catch PippinFixture/FixtureError e [(ex-message e) (= (ex-type e) PippinFixture/FixtureError)])))
(show (try (fx/checked 30) (catch :default e (ex-message e))) (fx/safe 4))
(show (fx/apply-twice (fn [x] (* x 3)) :to 1))
(show (try (fx/apply-twice (fn [x] (fx/risky (* x 10))) :to 2) (catch PippinFixture/FixtureError e (ex-message e))) (fx/saw-fixture-error))
(show (try (fx/apply-twice (fn [x] (throw (ex-info "from clojure" {:x x}))) :to 1) (catch :default e [(ex-message e) (ex-data e)])))

;; async: the caller parks; async throws; @MainActor async; a method; from a pool coroutine.
(show (fx/later 21) (fx/fetch 5) (try (fx/fetch -1) (catch PippinFixture/FixtureError e (ex-message e))))
(show (fx/main-later 7) (fx/Counter.increment-later c) @(future (fx/later 2)))

;; Cancelling the parked caller cancels the Swift Task. A future holds its coroutine, so none outlives the form.
(show (let [cancels (fx/cancellations)
            waiting (future (fx/wait-for-cancel))]
        (Thread/sleep 20)
        [(future-cancel waiting)
         (loop [i 0] (if (or (< cancels (fx/cancellations)) (> i 2000)) (- (fx/cancellations) cancels) (do (Thread/sleep 1) (recur (inc i)))))]))

;; Optionals, collections, tuples and fixed-width numbers: a slot's form is built from its parts' forms.
(show (fx/maybe 3) (fx/maybe -1) (fx/or-zero nil) (fx/or-zero 4) (fx/lookup nil) (fx/lookup p))
(show (fx/Point. :non-negative -1) (describe (fx/Point. :non-negative 2)))
(show (fx/sum :of [1 2 3]) (fx/sum :of (range 4)) (fx/sum :of []) (mapv describe (fx/points 2)) (fx/xs :of [p q]))
(show (= {"a" 2 "b" 1} (fx/counts ["a" "b" "a"])) (= #{1 2 3} (fx/unique [1 2 2 3])) (fx/size :of #{"x" "y"}))
(show (fx/echo {"k" [1 nil 3]}) (fx/origin) (fx/swap-pair [1 "one"]))
(show (fx/widths 200 2.5 1000000007) (fx/max-u-int) (fx/widths 1 1.0 (fx/max-u-int)))
(show (try (fx/widths 300 1.0 1) (catch :default e (ex-message e))))
(show (try (fx/size :of ["x"]) (catch :default e (ex-message e))) (try (fx/swap-pair [1]) (catch :default e (ex-message e))))
