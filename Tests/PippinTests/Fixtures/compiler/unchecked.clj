;; The unchecked ops wrap as the JVM's: at 64 bits over longs, at 32 over ints, and an -int op's operand must fit an int.
(ns fixture.unchecked)

(defn err [f] (try (f) (catch :default e (ex-message e))))
(defn sum-to [n] (loop [i 0 acc 0] (if (< i n) (recur (unchecked-inc i) (unchecked-add acc i)) acc)))
(defn count-down [n] (loop [i (int n) k 0] (if (pos? i) (recur (unchecked-dec-int i) (unchecked-inc-int k)) k)))
(defn wrap [x] (unchecked-inc x))
(defn wrap-int [x y] (unchecked-add-int x y))
(defn back [x y] [(unchecked-dec x) (unchecked-subtract x y)])
(defn back-int [x y] [(unchecked-subtract-int x y) (unchecked-dec-int y)])

(println (sum-to 100) (count-down 7)
         (unchecked-inc 9223372036854775807) (unchecked-dec -9223372036854775808)
         (unchecked-add 9223372036854775807 1) (unchecked-subtract -9223372036854775808 1))
(println (unchecked-inc-int 2147483647) (unchecked-dec-int -2147483648) (unchecked-add-int 2147483647 2)
         (unchecked-subtract-int -2147483648 1) (unchecked-inc 1.5) (unchecked-add 1 0.5) (unchecked-add 1/2 1/2))
(println (wrap 9223372036854775807) (wrap 1.5) (wrap-int 2147483647 1) (err #(wrap-int 2147483648 1)) (unchecked-inc-int 1.5)
         (back -9223372036854775808 1) (back-int 5 -2147483648) (err #(back-int 1 2147483648)))
