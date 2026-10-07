;; Tuples (design §4): small literals and map entries are inline; `trie` builds the other layout to compare.
(ns fixture.tuples)

(defn pair [a b] [a b])
(defn trie [& xs] (into [] xs))
(defn grow [v n] (reduce conj v (range n)))
(defn shrink [v] (loop [v v out []] (if (seq v) (recur (pop v) (conj out (count v))) out)))

(let [p (pair 1 2)
      [a b] p
      [x & more] [1 2 3]]
  (println p a b x more (nth p 1) (nth p 5 :nf) (get p 0) (get p 9 :nf) (p 1) (count p) (peek p) (pop p) (seq p) (rseq p) (next p)))
(println (conj [1 2 3 4 5] 6) (conj [1 2 3 4 5 6] 7) (grow [:a] 40) (count (grow [1 2 3 4 5 6] 100)) (conj [1 2] 3 4 5 6 7 8))
(println (assoc [1 2 3] 0 :x) (assoc [1 2 3] 3 4) (assoc [1 2 3 4 5 6] 6 7) (try (assoc [1 2] 5 0) (catch :default e (ex-message e))))
(println (shrink [1 2 3 4 5 6]) (pop [1]) (try (nth [1 2] 2) (catch :default e (ex-message e))))
(println (= [1 2] (pair 1 2) (trie 1 2) '(1 2)) (= (trie 1 2) [1 2]) (= [1 2] [1 3]) (= [1 2] [1 2 3])
         (= (hash [1 2]) (hash (trie 1 2)) (hash '(1 2))) (= [1 2 3 4 5 6 7] (conj [1 2 3 4 5 6] 7)) (= (hash [nil]) (hash (trie nil))))
(println (meta (with-meta [1 2] {:m 1})) (meta (conj (with-meta [1 2] {:m 1}) 3)) (meta (conj (with-meta [1 2 3 4 5 6] {:m 2}) 7)) (meta (assoc (with-meta [1] {:m 4}) 0 2)))
(println (map (fn [[k v]] (str k v)) (sorted-map :a 1 :b 2)) (reduce (fn [acc [_ v]] (+ acc v)) 0 {:a 1 :b 2 :c 3})
         (key (first {:a 1})) (val (first {:a 1})) (first {:a 1}) (map-entry? (first {:a 1})))
(println {[1 2] :pair} (get {[1 2] :pair} (trie 1 2)) (contains? #{(trie 1 2)} [1 2]))
(println (reduce-kv (fn [acc i x] (+ acc (* i x))) 0 [1 2 3]) (reduce + [1 2 3 4]) (into [] (map inc) [1 2 3]) (subvec [1 2 3 4 5] 1 3)
         (mapv inc [1 2]) (vector 1 2 3) (apply vector [1 2]) (vec '(1 2)) (zipmap [:a :b] [1 2]))
(println (let [v [1 2 3]] [(conj v 4) v (assoc v 1 :b) v (pop v) v]) (let [v (pair 1 2)] [(conj v 3) (conj v 4)]))
(println (persistent! (conj! (transient [1 2]) 3)) (seq (reverse [1 2 3])) (empty [1 2]) (str [1 "a" \b]) (pr-str [1 "a" \b]))
