;; Fused sites under a rebind: helpers a step or a body stands for (cat, assoc!), stage vars (dedupe, partition-all). Dev only.
(ns fixture.fused-seq-rebind)

(defn ct [v] (count (sequence (comp (mapcat (fn [x] [x x])) (dedupe)) v)))
(defn dd [v] (vec (dedupe (map (fn [x] (quot x 2)) v))))
(defn pa [v] (into [] (comp (partition-all 2) (map count)) v))
(defn gb [v] (into (sorted-map) (group-by identity (map (fn [x] x) v))))

(println (ct [1 1 2]) (with-redefs [cat (fn [rf] (fn ([] (rf)) ([r] (rf r)) ([r x] (rf (rf r :a) :b))))] (ct [1 1 2])) (ct [1 1 2]))
(println (dd [1 2 3]) (with-redefs [dedupe (fn [& args] [:redef])] (dd [1 2 3])) (dd [1 2 3]))
(println (pa [1 2 3]) (with-redefs [partition-all (fn [n] (map (fn [x] [:p x])))] (pa [1 2 3])) (pa [1 2 3]))
(println (gb [:mark :x]) (let [a assoc!] (with-redefs [assoc! (fn [m k v & kvs] (apply a m k (if (= k :mark) (conj v :tag) v) kvs))] (gb [:mark :x]))) (gb [:mark :x]))
