;; Lazy seqs, for, doseq, destructuring, transducers, atoms and traces: the macro-heavy shapes of ordinary code.
(ns fixture.seqs)

(defn lazy-nums [n] (lazy-seq (when (pos? n) (cons n (lazy-nums (dec n))))))
(defn comprehension [] (for [x (range 3) y [:a :b] :when (odd? x)] [x y]))
(defn side [] (let [a (atom [])] (doseq [x [1 2] y [10 20]] (swap! a conj (+ x y))) @a))
(defn destr [{:keys [a b] :or {b 5} :as m} [x & xs]] [a b m x xs])
(defn trace-names [] (try ((fn outer [] ((fn inner [] (throw (ex-info "t" {})))))) (catch :default e (mapv :fn (ex-trace e)))))
(defn realize [] (let [calls (atom 0) s (map (fn [x] (swap! calls inc) x) (range 10))] [(first s) @calls (count s) @calls]))
(defn strings [] [(str "a" 1 nil :k [1]) (pr-str "q") (subs "hello" 1 3) (keyword "k") (name :a/b) (namespace :a/b) (symbol "s")])

(println (lazy-nums 3) (comprehension) (side) (destr {:a 1} [1 2 3]) (destr {:a 1 :b 2} []) (trace-names) (realize) (strings))
(println (take 5 (iterate inc 0)) (partition 2 [1 2 3 4 5]) (group-by odd? (range 6)) (frequencies "abca") (sort-by - [3 1 2]) (some even? [1 3 4]) (every? odd? [1 3]))
(println (let [a (atom {:n 0})] (swap! a update :n inc) (reset! a (assoc @a :m 1)) @a) (deref (delay :d)) (force (delay 1)) ((memoize identity) 3))
(println (loop [s (seq [1 2 3]) acc []] (if s (recur (next s) (conj acc (first s))) acc)) (doall (map inc [1 2])) (dorun (map inc [1 2])) (interleave [1 2] [:a :b]))
