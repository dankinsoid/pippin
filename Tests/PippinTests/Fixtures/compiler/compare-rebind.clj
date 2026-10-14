;; compare, ==, long and Integer/compare rebound: a site on the boot fn falls back to the new root, and back again.
(ns fixture.compare-rebind)

(defn cmp [a b] (compare a b))
(defn chain [a b] (let [c (long (compare a b))] (if (== 0 c) :same c)))
(defn icmp [a b] (Integer/compare a b))

(println (cmp 1 2) (chain 1 1) (with-redefs [compare (fn [a b] 42)] [(cmp 1 2) (chain 1 2) (sort [3 1 2]) (sort-by - [1 3 2])])
         (cmp 1 2) (chain 2 1))
(println (with-redefs [== (fn [& _] false) long (fn [x] 7)] [(chain 1 1) (chain 1 2)]) (chain 1 1) (chain 1 2))
(println (with-redefs [Integer/compare (fn [a b] :redef)] (icmp 1 2)) (icmp 1 2))
