;; vswap! with conj: a vector the volatile alone holds grows in place; a held one, another value or f is copied.
(ns fixture.core-vswap)

(defn grown []
  (let [v (volatile! []) _ (vswap! v conj 1) _ (vswap! v conj 2) held @v _ (vswap! v conj 3)]
    [@v held (vswap! v conj :k) @v]))

(defn snapshots []
  (let [w (volatile! [1])]
    [(vswap! w conj nil) (vswap! w conj :s) (count (vswap! w conj "s")) (vswap! w conj 'sym) @w]))

(defn others []
  [(let [s (volatile! #{})] (vswap! s conj 1) (vswap! s conj 1) @s)
   (let [l (volatile! nil)] (vswap! l conj 1) (vswap! l conj 2) @l)
   (let [n (volatile! 0)] (vswap! n + 5) (vswap! n inc) @n)
   (let [conj (fn [c x] (str c x)) w (volatile! [])] (vswap! w conj 1))
   (let [w (volatile! {})] [(try (vswap! w conj 1) (catch :default e :caught)) @w])
   (try (vswap! (atom []) conj 1) (catch :default e :not-a-volatile))
   (let [x 7 w (volatile! [])] (vswap! w conj x) (vswap! w conj x) @w)])

(defn buffers []
  [(into [] (partition-all 3) (range 10)) (transduce (partition-by odd?) conj [] [1 3 2 4 5])
   (sequence (partition-all 2) (range 5)) (into [] (distinct) [1 2 1 3])])

(println 'grown (grown))
(println 'snapshots (snapshots))
(println 'others (others))
(println 'buffers (buffers))
