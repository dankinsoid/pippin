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
