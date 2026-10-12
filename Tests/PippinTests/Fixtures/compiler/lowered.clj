;; reduce, reduce-kv, swap! and swap-vals! over a literal fn, run without its closure (design §10 step 7a′).
(ns fixture.lowered)

(def kept (atom nil))
(deftype Keeper [n]
  IReduceInit
  (reduce [_ f init] (reset! kept f) (f init n)))

(defn sum-plus [xs k] (reduce (fn [a x] (+ a x k)) 0 xs))
(defn sum2 [xs] (reduce (fn [a x] (+ a x)) xs))
(defn sum2-zero [xs] (reduce (fn ([] :empty) ([a x] (+ a x))) xs))
(defn stop-at [xs t] (reduce (fn [a x] (if (> x t) (reduced a) (conj a x))) [] xs))
(defn doubled [s] (reduce (fn [a c] (str a c c)) "" s))
(defn kv [m] (reduce-kv (fn [acc k v] (assoc acc v k)) {} m))
(defn kv-vec [v] (reduce-kv (fn [acc i x] (+ acc (* i x))) 0 v))
(defn nested [xss] (reduce (fn [a xs] (+ a (reduce (fn [b x] (* b x)) 1 xs))) 0 xss))
(defn named [xs] (reduce (fn step [a x] (+ a x)) 0 xs))

(println (sum-plus [1 2 3] 10) (sum-plus '(1 2 3) 10) (sum-plus (range 4) 1) (sum-plus #{1 2} 0) (sum-plus nil 5)
         (sum-plus (map inc [1 2]) 0) (sum-plus (vec (range 40)) 0) (sum-plus (sorted-set 3 4) 0) (sum-plus (seq [1 2]) 0))
(println (sum2 [1 2 3]) (sum2 [5]) (try (sum2 []) (catch :default _ :no-zero-arity)) (sum2-zero []) (sum2-zero nil) (sum2-zero '(4 5)))
(println (stop-at [1 2 3 4] 2) (stop-at (range) 3) (stop-at [] 0) (doubled "ab") (doubled nil) (named [1 2 3]))
(println (sort (kv {:a 1 :b 2})) (kv nil) (kv-vec [3 4 5]) (nested [[1 2] [3 4]]) (nested []))
(println (sum-plus (->Keeper 5) 1) (@kept 10 20))
(println (try (kv-vec '(1 2)) (catch :default e (ex-message e))) (try (sum-plus 5 0) (catch :default _ :not-seqable)))

(defn bump! [a k] (swap! a (fn [m] (update m k (fnil inc 0)))))
(defn add! [a x y] (swap! a (fn [v p q] (+ v p q)) x y))
(defn push! [a x] (swap-vals! a (fn [v] (conj v x))))
(defn watched []
  (let [log (atom [])
        a   (atom 0 :validator (fn [v] (swap! log conj [:valid v]) (number? v)))]
    (add-watch a :w (fn [_ _ old new] (swap! log conj [:watch old new])))
    [(swap! a (fn [v] (swap! log conj [:f v]) (inc v))) @a
     (try (swap! a (fn [v] (swap! log conj [:f v]) :no)) (catch :default e (ex-message e))) @a @log]))
(defn nested-swap [a] (try (swap! a (fn [v] (swap! a (fn [w] (inc w))) v)) (catch :default e (ex-message e))))
(defn thrown-swap [a] [(try (swap! a (fn [v] (throw (ex-info "in f" {:v v})))) (catch :default e (ex-data e))) @a])

(let [a (atom {})] (bump! a :x) (bump! a :x) (println (bump! a :y) @a))
(let [a (atom 1)] (println (add! a 2 3) @a (push! (atom []) :p) (watched) (nested-swap a) (thrown-swap a)))

(defn names [f] (try (f) (catch :default e (mapv :fn (take 2 (ex-trace e))))))
(defn reduce-boom [] (reduce (fn lit [a x] (throw (ex-info "lit" {:x x}))) 0 [1]))
(defn swap-boom [] (swap! (atom 0) (fn lit2 [v] (throw (ex-info "lit2" {:v v})))))
(println (names reduce-boom) (names swap-boom))
