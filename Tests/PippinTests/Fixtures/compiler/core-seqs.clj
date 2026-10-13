;; rseq of a vector is a view walking down; the seq library over it sees an ordinary counted, reducible seq.
(ns fixture.core-seqs)

(defn throws [f] (try (f) :no-error (catch :default e :thrown)))

(def big (vec (range 100)))

(println 'rseq (rseq [1 2 3]) (rseq []) (rseq [:a]) (first (rseq [1 2 3])) (next (rseq [1 2])) (next (rseq [1]))
         (rest (rseq [1])) (count (rseq [1 2 3])) (count (next (rseq [1 2 3]))) (seq? (rseq [1])) (counted? (rseq [1])))
(println 'rseq-walks (reduce conj [] (rseq [1 2 3])) (reduce + (rseq [5])) (reduce + (rseq [1 2 3]))
         (reduce (fn [a x] (if (= x 2) (reduced a) (+ a x))) 0 (rseq [1 2 3])) (into [] (rseq [4 5])) (map inc (rseq [1 2]))
         (apply str (rseq [\a \b])) (last (rseq [1 2 3])) (nth (rseq [1 2 3]) 1) (conj (rseq [1 2]) 0)
         (= (vec (rseq big)) (vec (reverse big))) (reduce + (rseq big)) (take 3 (rseq big)) (take-last 2 (rseq big)))
(println 'rseq-values (= (rseq [1 2 3]) '(3 2 1)) (= (hash (rseq [1 2])) (hash '(2 1))) (meta (with-meta (rseq [1 2]) {:a 1}))
         (with-meta (rseq [1 2]) {:a 1}) (rseq (subvec [1 2 3 4] 1 3)) (rseq (first {:a 1})) (rseq (sorted-set 1 2 3))
         (throws (fn [] (rseq '(1 2)))) (let [v [1 2 3] s (rseq v)] [(conj v 4) s]))
