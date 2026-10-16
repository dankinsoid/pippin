;; @ai-generated(guided)
;; :int traps on overflow, as in docs/merry.md «Целые»; map, filter and reduce are eager over vectors.
(ns merry.builtins)

(defn- add [a b] (Math/addExact (long a) (long b)))
(defn- sub [a b] (Math/subtractExact (long a) (long b)))
(defn- mul [a b] (Math/multiplyExact (long a) (long b)))

(def table
  {'+ {:type '[:=> [& :int] :int] :impl (fn [& xs] (reduce add 0 xs))}
   '- {:type '[:=> [:int & :int] :int]
       :impl (fn ([x] (Math/negateExact (long x))) ([x & ys] (reduce sub x ys)))}
   '* {:type '[:=> [& :int] :int] :impl (fn [& xs] (reduce mul 1 xs))}
   'quot {:type '[:=> [:int :int] :int] :impl quot}
   'rem {:type '[:=> [:int :int] :int] :impl rem}
   'mod {:type '[:=> [:int :int] :int] :impl mod}
   'inc {:type '[:=> [:int] :int] :impl #(Math/incrementExact (long %))}
   'dec {:type '[:=> [:int] :int] :impl #(Math/decrementExact (long %))}
   'max {:type '[:=> [:int & :int] :int] :impl max}
   'min {:type '[:=> [:int & :int] :int] :impl min}
   '= {:type '[:=> [a & a] :bool] :impl =}
   'not= {:type '[:=> [a & a] :bool] :impl not=}
   '< {:type '[:=> [:int & :int] :bool] :impl <}
   '> {:type '[:=> [:int & :int] :bool] :impl >}
   '<= {:type '[:=> [:int & :int] :bool] :impl <=}
   '>= {:type '[:=> [:int & :int] :bool] :impl >=}
   'zero? {:type '[:=> [:int] :bool] :impl zero?}
   'pos? {:type '[:=> [:int] :bool] :impl pos?}
   'neg? {:type '[:=> [:int] :bool] :impl neg?}
   'even? {:type '[:=> [:int] :bool] :impl even?}
   'odd? {:type '[:=> [:int] :bool] :impl odd?}
   'not {:type '[:=> [:bool] :bool] :impl not}
   'nil? {:type '[:=> [a] :bool] :impl nil?}
   'str {:type '[:=> [& :any] :string] :impl str}
   'println {:type '[:=> [& :any] :nil {:eff #{Console}}] :impl println}
   'print {:type '[:=> [& :any] :nil {:eff #{Console}}] :impl print}
   'count {:type '[:=> [[:vector a]] :int] :impl count}
   'conj {:type '[:=> [[:vector a] a] [:vector a]] :impl conj}
   'nth {:type '[:=> [[:vector a] :int] a] :impl nth}
   'first {:type '[:=> [[:vector a]] [:maybe a]] :impl first}
   'rest {:type '[:=> [[:vector a]] [:vector a]] :impl #(vec (rest %))}
   'map {:type '[:=> [[:=> [a] b {:eff e}] [:vector a]] [:vector b] {:eff e}] :impl mapv}
   'filter {:type '[:=> [[:=> [a] :bool {:eff e}] [:vector a]] [:vector a] {:eff e}] :impl filterv}
   'reduce {:type '[:=> [[:=> [b a] b {:eff e}] b [:vector a]] b {:eff e}] :impl reduce}
   'range {:type '[:=> [:int] [:vector :int]] :impl #(vec (range %))}
   'slurp {:type '[:=> [:string] :string {:eff #{Fs}}] :impl slurp}
   'rand-int {:type '[:=> [:int] :int {:eff #{Random}}] :impl rand-int}})

(defn impl [sym] (get-in table [sym :impl]))
