;; Fused sites under a rebind: a helper a mapcat step stands for (cat), a stage var (dedupe). Dev only, as with-redefs is.
(ns fixture.fused-seq-rebind)

(defn ct [v] (count (sequence (comp (mapcat (fn [x] [x x])) (dedupe)) v)))
(defn dd [v] (vec (dedupe (map (fn [x] (quot x 2)) v))))

(println (ct [1 1 2]) (with-redefs [cat (fn [rf] (fn ([] (rf)) ([r] (rf r)) ([r x] (rf (rf r :a) :b))))] (ct [1 1 2])) (ct [1 1 2]))
(println (dd [1 2 3]) (with-redefs [dedupe (fn [& args] [:redef])] (dd [1 2 3])) (dd [1 2 3]))
