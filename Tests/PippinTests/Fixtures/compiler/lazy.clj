;; Lazy def: pure inits at the first deref, effects at the def; ^:lazy, ^:eager, a throw, a recursion, redefinitions.
(ns fixture.lazy)

(def log (atom []))
(defn note! [x] (swap! log conj x) x)

(def eager-effect (note! :eager-effect))
(defmulti kind identity)
(defmethod kind :a [_] (note! :method) :a-kind)
(println "after registrations" @log)

(def ^:lazy on-demand (note! :on-demand))
(println "before deref" @log)
(println on-demand on-demand @log)

(defn square [x] (* x x))
(def ^:eager squared (square 7))

(defn checked-div [a b] (quot a b))
(def broken (checked-div 1 0))
(println "loaded past broken")
(println (try broken (catch :default e (ex-message e))))
(println (let [a (try broken (catch :default e e)) b (try broken (catch :default e e))] (identical? a b)))

(declare self)
(defn read-self [] (count self))
(def self (assoc {} :n (read-self)))
(println (try self (catch :default e (ex-message e))))

(def base 1)
(defn get-base [] base)
(def derived (+ 10 (get-base)))
(def base 2)
(println "derived" derived "base" base)

(def twice (square 3))
(def twice (+ twice (square 4)))
(println "twice" twice)
(def forced (square 5))
(println "forced" forced)
(def forced (+ forced (square 6)))
(println "forced again" forced)

(def altered (square 2))
(alter-var-root #'altered inc)
(println "altered" altered)

(def table (into {} (map (fn [k] [k (square k)]) (range 4))))
(println (bound? #'table) table)
(println squared (kind :a) @log)
