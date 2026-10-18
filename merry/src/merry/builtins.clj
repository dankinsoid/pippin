;; @ai-generated(guided)
;; Int traps on overflow, as in docs/merry.md «Два целых типа»; map, filter and reduce are eager.
(ns merry.builtins)

(defn- add [a b] (Math/addExact (long a) (long b)))
(defn- sub [a b] (Math/subtractExact (long a) (long b)))
(defn- mul [a b] (Math/multiplyExact (long a) (long b)))

(def table
  {'+ {:type '(Fn [& Int] Int) :impl (fn [& xs] (reduce add 0 xs))}
   '- {:type '(Fn [Int & Int] Int)
       :impl (fn ([x] (Math/negateExact (long x))) ([x & ys] (reduce sub x ys)))}
   '* {:type '(Fn [& Int] Int) :impl (fn [& xs] (reduce mul 1 xs))}
   'quot {:type '(Fn [Int Int] Int) :impl quot}
   'rem {:type '(Fn [Int Int] Int) :impl rem}
   'mod {:type '(Fn [Int Int] Int) :impl mod}
   'inc {:type '(Fn [Int] Int) :impl #(Math/incrementExact (long %))}
   'dec {:type '(Fn [Int] Int) :impl #(Math/decrementExact (long %))}
   'max {:type '(Fn [Int & Int] Int) :impl max}
   'min {:type '(Fn [Int & Int] Int) :impl min}
   '= {:type '(Fn [a & a] Bool) :impl =}
   'not= {:type '(Fn [a & a] Bool) :impl not=}
   '< {:type '(Fn [Int & Int] Bool) :impl <}
   '> {:type '(Fn [Int & Int] Bool) :impl >}
   '<= {:type '(Fn [Int & Int] Bool) :impl <=}
   '>= {:type '(Fn [Int & Int] Bool) :impl >=}
   'zero? {:type '(Fn [Int] Bool) :impl zero?}
   'pos? {:type '(Fn [Int] Bool) :impl pos?}
   'neg? {:type '(Fn [Int] Bool) :impl neg?}
   'even? {:type '(Fn [Int] Bool) :impl even?}
   'odd? {:type '(Fn [Int] Bool) :impl odd?}
   'not {:type '(Fn [Bool] Bool) :impl not}
   'nil? {:type '(Fn [a] Bool) :impl nil?}
   'str {:type '(Fn [& Any] String) :impl str}
   'println {:type '(Fn [& Any] nil {:eff #{Console}}) :impl println}
   'print {:type '(Fn [& Any] nil {:eff #{Console}}) :impl print}
   'count {:type '(Fn [[a]] Int) :impl count}
   'conj {:type '(Fn [[a] a] [a]) :impl conj}
   'nth {:type '(Fn [[a] Int] a) :impl nth}
   'first {:type '(Fn [[a]] (or nil a)) :impl first}
   'rest {:type '(Fn [[a]] [a]) :impl #(vec (rest %))}
   'map {:type '(Fn [(Fn [a] b {:eff e}) [a]] [b] {:eff e}) :impl mapv}
   'filter {:type '(Fn [(Fn [a] Bool {:eff e}) [a]] [a] {:eff e}) :impl filterv}
   'reduce {:type '(Fn [(Fn [b a] b {:eff e}) b [a]] b {:eff e}) :impl reduce}
   'range {:type '(Fn [Int] [Int]) :impl #(vec (range %))}
   'slurp {:type '(Fn [String] String {:eff #{Fs}}) :impl slurp}
   'rand-int {:type '(Fn [Int] Int {:eff #{Random}}) :impl rand-int}})

(defn impl [sym] (get-in table [sym :impl]))
