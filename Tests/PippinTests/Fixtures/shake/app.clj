;; The entry of the whole-program closed build (scripts/shake.sh); wide on purpose.
(ns fixture.shake.app
  (:require [clojure.string :as str]
            [clojure.set :as set]))

(defrecord Token [kind text])

(defprotocol Render
  (render [this]))

(extend-protocol Render
  Token
  (render [this] (str (name (:kind this)) ":" (:text this))))

(defmulti classify (fn [s] (cond (re-matches #"\d+" s) :number
                                 (str/starts-with? s "#") :tag
                                 :else :word)))
(defmethod classify :number [s] (->Token :number s))
(defmethod classify :tag [s] (->Token :tag (subs s 1)))
(defmethod classify :default [s] (->Token :word (str/lower-case s)))

(def ^:private counter (atom 0))

(defn- tick [] (swap! counter inc))

(defn tokenize [line]
  (->> (str/split line #"\s+")
       (remove str/blank?)
       (map (fn [w] (tick) (classify w)))
       vec))

(defn summarize [tokens]
  (reduce (fn [acc t] (update acc (:kind t) (fnil inc 0))) (sorted-map) tokens))

(defn- safe-div [a b]
  (try (/ a b) (catch :default e (str "no: " (ex-message e)))))

;; A closed build inlines leaf inner-boom, so only a __TEXT,__cljsite marker names it (NOTES "Compiler").
(defn- inner-boom [x] (throw (ex-info "boom" {:x x})))
(defn- outer-boom [x] (inner-boom x))
(defn- boom-names [] (try (outer-boom 1) (catch :default e (mapv :fn (ex-trace e)))))

(def lines ["Hello 42 #clojure world" "  7 #c #d  Eight " "nine"])

(let [tokens (mapcat tokenize lines)
      {:keys [number tag word]} (summarize tokens)]
  (println (mapv render tokens))
  (println (summarize tokens) number tag word @counter)
  ;; sort and compare are host primitives (Primitives.swift) and a C-only clj-load has neither.
  (println (sort-by identity (map str (set/union #{:a :b} #{:b :c}))))
  (println (into [] (comp (map inc) (filter odd?) (take 3)) (range 20)))
  (println (take 5 (iterate (partial * 2) 1)) (apply str (interpose "," (map str [1 2 3]))))
  (println (safe-div 6 3) (safe-div 1 0))
  (println (str/join "|" (for [t tokens :when (= :word (:kind t))] (:text t))))
  (println (let [[a b & more] (vec (range 6))] [a b more]) (frequencies "abracadabra"))
  (println (boom-names)))
