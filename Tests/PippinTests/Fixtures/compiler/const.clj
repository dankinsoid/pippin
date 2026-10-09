;; CONST nodes: every literal kind the constant pool must carry through its printed form.
(ns fixture.const)

(defn show [& xs] (apply println (map pr-str xs)))

(show nil true false 0 -1 42 9223372036854775807 9223372036854775808 1/3 2.5 -0.0 1e300 ##Inf ##NaN 12345678901234567890N 1.5M)
(show \a \newline \space \é "plain" "esc\"aped\\ \n\t" "юникод ☃" "" :kw :ns/kw ::local 'sym 'ns/sym)
(show [1 "two" :three] {:a 1 "b" [2 3] 4 {5 6}} #{1} '(1 2 (3)) '() [] {} #{})
(show #"a+b" (re-find #"\d+" "ab12cd") #uuid "550e8400-e29b-41d4-a716-446655440000" #inst "2020-01-02T03:04:05.000-00:00")
(show (type #"x") (uuid? #uuid "550e8400-e29b-41d4-a716-446655440000") (inst? #inst "2020-01-02T03:04:05.000-00:00"))
(show (quote (a b c)) (quote [x y]) (quote {:k v}) (identical? :kw :kw) (= 'sym 'sym) (= "юникод ☃" (str "юникод" " " "☃")))
(show (let [big 12345678901234567890N] (+ big 1)) (+ 1/3 2/3) (* 2.5 2) (count "юникод ☃"))

;; Metadata beyond the reader's position is not in a constant's printed form: the pool rebuilds it (compiler.c).
(show (meta (quote ^:dynamic p)) (meta (second (second (quote (do (declare ^:dynamic p) p))))))
(show (meta (:k (quote {:k ^{:tag long} v}))) (meta (first (quote [^{:a 1 :b "two"} x]))))
(show (meta (first (quote (^:private f 1)))) (mapv meta (quote [^:a x ^:b y])))
;; A ?? before - or / is a C trigraph unless escaped (async-error's <??-test).
(defn <??-x [] "a??-b??/c")
(show (<??-x) (quote <??=y))
;; A constant whose text passes ISO C's 4095-byte string literal is emitted as an array.
(defmacro long-text [] (apply str (repeat 2100 "ab")))
(defmacro long-vector [] (vec (range 1200)))
(defn long-constants [] [(long-text) (long-vector)])
(show (count (first (long-constants))) (subs (first (long-constants)) 4198) (count (second (long-constants))) (reduce + (second (long-constants))))
