;; Take-out get (NOTES "Owned parameters"): every other holder of the parent or the child keeps what it saw.
(ns fixture.takeout)

(defn sorted [m] (into (sorted-map) m))
(defn fresh [] {:a (hash-map :b 1 :c (vector 1 2)) :n 0 :v (mapv vector (range 40))})
(defn big [] (into {} (map (fn [i] [i (vector i)]) (range 40))))
(defn small [] (into {} (map (fn [i] [i (vector i)]) (range 5))))

;; known paths over a handed-over parameter, each level a take-out
(defn inc-in [m] (update-in m [:a :b] inc))
(defn put-in [m x] (assoc-in m [:a :c 1] x))
(defn conj-at [m i x] (update-in m [:v i] conj x))
(defn steps [n] (loop [i 0 m (fresh)] (if (< i n) (recur (inc i) (inc-in m)) m)))
;; a vector's item in the trie, in the tail, in a tuple
(defn vconj [v i x] (update v i conj x))
;; a trie parent and a flat one whose child goes back as nil: the entry was nil already, the hash must not be stale
(defn drop-vec [m k] (update m k (fn [x] (if (vector? x) nil x))))
(defn hashed-drop [m k] (let [h (hash m)] [h (drop-vec m k)]))
;; not taken out: another key, a second read of the parent, the get in a loop
(defn cross [m] (assoc m :n (count (get m :a))))
(defn twice [m] (assoc m :a (assoc (get m :a) :d (count (get m :a)))))
(defn looped [m] (assoc m :a (loop [i 0 acc nil] (if (< i 2) (recur (inc i) (assoc (get m :a) :i i)) acc))))
;; the value put back is nil, the same child, an absent key's
(defn nil-out [m] (update m :a (fn [_] nil)))
(defn same [m] (update m :a identity))
(defn absent [m] (update m :zz conj 1))
;; a throw inside f, the parent then dead; a handler reading the parent keeps it whole
(defn boom [m] (try (update-in m [:a :c] (fn [c] (throw (ex-info "boom" {:c c})))) (catch Exception e (ex-data e))))
(defn boom-keep [m] (try (update m :a (fn [_] (throw (ex-info "keep" {})))) (catch Exception e (get-in m [:a :b]))))
;; a closure over the parent made before
(defn captured [m] (let [f (fn [] (get-in m [:a :b]))] [(inc-in m) (f)]))
;; the accumulator of a lowered reduce, its child set handed to an opaque fn
(defn buckets [ps] (reduce (fn [m p] (update m (:d p) (fnil conj #{}) (:c p))) {} ps))

(println (get-in (inc-in (fresh)) [:a :b]) (get-in (put-in (fresh) 9) [:a :c]) (get-in (steps 5) [:a :b]))
(let [m (fresh) c (:a m) m2 (inc-in m)] (println (get-in m2 [:a :b]) (sorted c)))
(let [m (fresh) m2 (inc-in m)] (println (get-in m [:a :b]) (get-in m2 [:a :b])))
(let [c (hash-map :b 1) m (hash-map :a c :z c) m2 (inc-in m)] (println (get-in m2 [:a :b]) (get-in m2 [:z :b]) c))
(let [m (conj-at (fresh) 3 :x)] (println (get-in m [:v 3]) (get-in m [:v 39]) (count (:v m))))
(let [v (mapv vector (range 40)) w (vconj v 39 :t) u (vconj (mapv vector (range 40)) 5 :u)] (println (nth w 39) (nth v 39) (nth u 5) (nth u 39)))
(println (vconj (vector (vector 1) (vector 2)) 0 3))
(let [[h m] (hashed-drop (big) 7)] (println (= m (assoc (big) 7 nil)) (= (hash m) (hash (assoc (big) 7 nil))) (= h (hash (big)))))
(let [[h m] (hashed-drop (small) 2)] (println (= m (assoc (small) 2 nil)) (= (hash m) (hash (assoc (small) 2 nil))) (count m)))
(let [m (big) m2 (update-in m [7 0] inc)] (println (get m 7) (get m2 7) (= (hash m2) (hash (assoc (big) 7 [8])))))
(println (:n (cross (fresh))) (sorted (:a (twice (fresh)))) (sorted (:a (looped (fresh)))))
(println (:a (nil-out (fresh))) (contains? (nil-out (fresh)) :a) (sorted (:a (same (fresh)))) (:zz (absent (fresh))))
(println (boom (fresh)) (boom-keep (fresh)))
(let [m (fresh)] (println (boom m) (get-in m [:a :c])))
(let [[a b] (captured (fresh))] (println (get-in a [:a :b]) b))
(println (sorted (update-vals (buckets [{:d 1 :c 2} {:d 1 :c 3} {:d 2 :c 2} {:d 1 :c 2}]) sort)))
