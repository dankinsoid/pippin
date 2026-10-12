;; Dead slots dropped where they die (NOTES "Compiler", dead slots dropped): every reader of a slot still sees its value.
(ns fixture.drop)

(defn fresh [] (hash-map :u (hash-map :a (vector 1 2)) :n 0))

;; the last read in one branch, the other branch dropping the slot on entry
(defn branch [m k] (let [u (get m :u)] (if (contains? u k) (count (get u k)) (assoc m :n (count m)))))
;; a handler or finally reading the slot keeps it to the end
(defn handled [m] (let [u (get m :u)] (try (count u) (throw (ex-info "h" {})) (catch Exception e (get u :a)))))
(defn finally-reads [m] (let [u (get m :u) a (atom nil)] (try (count u) (finally (reset! a (get u :a)))) @a))
;; bound outside a loop: live across the recur, dead on the exit path only
(defn looped [m n] (let [u (get m :u)] (loop [i 0 acc []] (if (< i n) (recur (inc i) (conj acc (count u))) acc))))
;; an earlier operand borrows the slot the branch would drop
(defn held [m p] (let [v (get m :u)] (vector v (if p v 1))))
;; dropped before the recur rebinds
(defn summed [xs] (loop [s (seq xs) acc 0] (if s (let [x (first s)] (recur (next s) (+ acc (count x)))) acc)))
;; a closure keeps its own reference
(defn captured [m] (let [u (get m :u) f (fn [] (get u :a))] [(f) (f)]))
;; past a do's item, and a binding nothing reads
(defn done [m] (let [u (get m :u)] (count u) (assoc m :n 7)))
(defn unread [m] (let [x (assoc m :z 1)] (count m)))
;; the child dropped before the update that would copy it; another holder of the parent sees it unchanged
(defn bump [st] (let [c (let [u (get st :u)] (if (nil? u) 0 (count (get u :a))))] [c (update-in st [:u :a] conj 9)]))
(defn steps [n]
  (loop [i 0 st (fresh) acc 0]
    (if (< i n)
      (let [c (let [u (get st :u)] (if (nil? u) 0 (count (get u :a))))]
        (recur (inc i) (assoc st :u (let [m (get st :u)] (assoc m :a (conj (get m :a) i)))) (+ acc c)))
      [(get-in st [:u :a]) acc])))
;; a throw past the drop
(defn thrown [m] (let [u (get m :u)] (if (pos? (count u)) (throw (ex-info "t" {:a (get u :a)})) u)))

(println (branch (fresh) :a) (get (branch (fresh) :b) :n))
(println (handled (fresh)) (finally-reads (fresh)))
(println (looped (fresh) 3))
(println (held (fresh) true) (held (fresh) false))
(println (summed [[1] [1 2] [] [3 4 5]]))
(println (captured (fresh)))
(println (:n (done (fresh))) (unread (fresh)))
(let [m (fresh) [c m2] (bump m)] (println c (get-in m2 [:u :a]) (get-in m [:u :a])))
(println (steps 4))
(println (try (thrown (fresh)) (catch Exception e (ex-data e))))
