// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

private func kw(_ s: String) -> Value { Value(keyword: s) }

private func clojureError(_ rt: Runtime, _ source: String) -> ClojureError? {
	do {
		_ = try rt.eval(source)
		return nil
	} catch let e as ClojureError {
		return e
	} catch {
		return nil
	}
}

private func message(_ rt: Runtime, _ source: String) -> String? { clojureError(rt, source)?.message }

extension CoreTests {
	@Suite struct RuntimeTests {
		let rt = Runtime()

		// Keywords are interned immortals; take them out of the baselines.
		init() {
			for k in ["t", "f", "k", "a", "b", "c", "v", "zero", "one", "two", "more", "new", "default", "even", "odd", "fibs", "total", "parity", "fib-88"] {
				_ = kw(k)
			}
		}

		// Vars are immortal: declare the ones a test defines before its baseline, and unbind closures after.
		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func letAddsEndToEnd() throws {
			let before = clj_debug_live_objects()
			#expect(try rt.eval("(let [x 1] (+ x 2))") == 3)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func arithmeticAndTruthiness() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(+ 1 2)") == 3)
				#expect(try rt.eval("(+ 1 2.5)") == 3.5)
				#expect(try rt.eval("(/ 7 2)") == 3.5)
				#expect(try rt.eval("(/ 8 2)") == 4)
				#expect(try rt.eval("(* 1.5 2)") == 3.0)
				#expect(message(rt, "(* 4611686018427387903 2)") == "integer overflow")
				#expect(message(rt, "(+ 1 nil)") == "nil cannot be cast to a number")
				#expect(try rt.eval("(if nil :t :f)") == kw("f"))
				#expect(try rt.eval("(if false :t :f)") == kw("f"))
				#expect(try rt.eval("[(if 0 1 2) (if \"\" 1 2) (if [] 1 2) (if () 1 2) (if :k 1 2) (if true 1 2)]") == [1, 1, 1, 1, 1, 1])
				#expect(try rt.eval("(if false 1)") == nil)
				#expect(try rt.eval("(if (< 1 2) (if (> 1 2) :a :b) :c)") == kw("b"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func letDoAndOutput() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [x 1 y (+ x 1) x (* y 10)] [x y])") == [20, 2])
				#expect(try rt.eval("(let [x 1] (let [x 2] x))") == 2)
				#expect(try rt.eval("(let [x 1] (let [x 2] x) x)") == 1)
				#expect(try rt.eval("(let [] 5)") == 5)
				#expect(try rt.eval("(let [x 1])") == nil)
				var result: Value = nil
				let out = try capturingOutput {
					result = try rt.eval("(do (println \"one\") (println \"two\" 2) (let [x 3] (println x) x))")
				}
				#expect(result == 3)
				#expect(out == "one\ntwo 2\n3\n")
				#expect(try rt.eval("(do)") == nil)
				#expect(try rt.eval("1 2 3") == 3)
				#expect(try rt.eval("") == nil)
				#expect(try rt.eval("; just a comment") == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func functions() throws {
			try declare("rt-adder", "rt-compose", "rt-fact", "rt-count-down", "rt-many")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("((fn [x] (* x x)) 7)") == 49)
				#expect(try rt.eval("(let [a 10 add-a (fn [x] (+ a x))] (add-a 5))") == 15)
				#expect(try rt.eval("(let [inc2 (fn [x] (+ x 2)) twice (fn [f x] (f (f x)))] (twice inc2 1))") == 5)
				#expect(try rt.eval("(def rt-adder (fn [n] (fn [x] (+ x n)))) ((rt-adder 3) 4)") == 7)
				#expect(try rt.eval("(def rt-compose (fn [f g] (fn [x] (f (g x))))) ((rt-compose inc (rt-adder 10)) 1)") == 12)
				#expect(try rt.eval("(let [x 1 f (fn [] (let [x 2] (fn [] x)))] ((f)))") == 2)
				#expect(try rt.eval("(let [x 1 f (fn [y] (fn [z] [x y z]))] ((f 2) 3))") == [1, 2, 3])
				let arities = "(def rt-many (fn ([] :zero) ([a] [:one a]) ([a b] [:two a b]) ([a b & more] [:more a b more])))"
				#expect(try rt.eval("\(arities) [(rt-many) (rt-many 1) (rt-many 1 2) (rt-many 1 2 3 4)]") ==
					[kw("zero"), [kw("one"), 1], [kw("two"), 1, 2], [kw("more"), 1, 2, Value(list: [3, 4])]])
				#expect(try rt.eval("((fn [a & r] r) 1)") == nil)
				#expect(try rt.eval("((fn [& r] (count r)) 1 2 3)") == 3)
				#expect(message(rt, "((fn [a b] a) 1)") == "Wrong number of args (1) passed to: fn")
				#expect(message(rt, "((fn named [a b] a) 1 2 3)") == "Wrong number of args (3) passed to: named")
				#expect(message(rt, "(rt-adder)") == "Wrong number of args (0) passed to: user/rt-adder")
				#expect(message(rt, "(inc 1 2)") == "Wrong number of args (2) passed to: clojure.core/inc")
				#expect(try rt.eval("(def rt-fact (fn [n] (if (<= n 1) 1 (* n (rt-fact (dec n)))))) (rt-fact 20)") == 2432902008176640000)
				#expect(message(rt, "(rt-fact 21)") == "integer overflow")
				#expect(try rt.eval("(def rt-count-down (fn [n acc] (if (zero? n) acc (recur (dec n) (conj acc n))))) (rt-count-down 3 [])") == [3, 2, 1])
				#expect(try rt.eval("((fn [n & acc] (if (zero? n) acc (recur (dec n) (cons n acc)))) 3)") == Value(list: [1, 2, 3]))
				#expect(try rt.eval("(fn? rt-fact)") == true)
				#expect(try rt.eval("(str rt-fact)") == "#object[fn user/rt-fact]")
				try unbind("rt-adder", "rt-compose", "rt-fact", "rt-count-down", "rt-many")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func loopRecurRunsWithoutStackGrowth() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(loop [i 0 sum 0] (if (< i 1000000) (recur (inc i) (+ sum i)) sum))") == 499999500000)
				#expect(try rt.eval("(loop [i 0 acc []] (if (= i 5) acc (recur (inc i) (conj acc (* i i)))))") == [0, 1, 4, 9, 16])
				#expect(try rt.eval("(loop [a 1 b 2] (if (= a 1) (recur b a) [a b]))") == [2, 1])
				#expect(try rt.eval("(loop [i 0] (let [j (inc i)] (if (< j 10) (recur j) j)))") == 10)
				#expect(try rt.eval("(loop [i 0] (do (if (< i 3) (recur (inc i)) i)))") == 3)
				#expect(try rt.eval("(loop [] 7)") == 7)
				#expect(try rt.eval("((fn [n] (loop [i n acc 0] (if (zero? i) acc (recur (dec i) (+ acc i))))) 100)") == 5050)
				#expect(message(rt, "(loop [i 0] (inc (recur 1)))") == "Can only recur from tail position")
				#expect(message(rt, "(fn [] (recur 1))") == "Mismatched argument count to recur, expected: 0 args, got: 1")
				#expect(message(rt, "(loop [i 0] (if true (recur) 1))") == "Mismatched argument count to recur, expected: 1 args, got: 0")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func defAndRedefinition() throws {
			try declare("rt-x", "rt-f")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(def rt-x 1)").description == "#'user/rt-x")
				#expect(try rt.eval("rt-x") == 1)
				#expect(try rt.eval("(+ rt-x 1)") == 2)
				#expect(try rt.eval("(def rt-x \"two\") rt-x") == "two")
				#expect(try rt.eval("(def rt-f (fn [] rt-x)) (rt-f)") == "two")
				#expect(try rt.eval("(def rt-x 3) (rt-f)") == 3)
				#expect(try rt.eval("(def rt-f (fn [] :new)) (rt-f)") == kw("new"))
				#expect(try rt.eval("user/rt-x") == 3)
				#expect(try rt.eval("clojure.core/inc") == rt.eval("inc"))
				#expect(message(rt, "(def other/y 1)") == "Can't create defs outside of current ns")
				#expect(message(rt, "rt-unbound-never-defined") == "Unable to resolve symbol: rt-unbound-never-defined in this context")
				#expect(message(rt, "(def rt-x-unbound) rt-x-unbound") == "Unbound var: #'user/rt-x-unbound")
				try unbind("rt-x", "rt-f")
			}
			#expect(clj_debug_live_objects() == before + 3) // rt-x-unbound: the var, its name symbol and the name string
		}

		@Test func collectionsAsFunctionsAndApply() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(:a {:a 1 :b 2})") == 1)
				#expect(try rt.eval("(:c {:a 1} :default)") == kw("default"))
				#expect(try rt.eval("(:a nil)") == nil)
				#expect(try rt.eval("(:a [1 2])") == nil)
				#expect(try rt.eval("({:a 1} :a)") == 1)
				#expect(try rt.eval("({:a 1} :b 0)") == 0)
				#expect(try rt.eval("([10 20 30] 2)") == 30)
				#expect(try rt.eval("(let [f {:k :v}] (f :k))") == kw("v"))
				#expect(try rt.eval("(apply + [1 2 3])") == 6)
				#expect(try rt.eval("(apply vector 1 2 '(3 4))") == [1, 2, 3, 4])
				#expect(try rt.eval("(apply (fn [& xs] xs) [])") == nil)
				#expect(try rt.eval("(apply :a [{:a 5}])") == 5)
				#expect(message(rt, "([1 2] 2)") == "Index 2 out of bounds for length 2")
				#expect(message(rt, "([1 2] 0 1)") == "Wrong number of args (2) passed to: [1 2]")
				#expect(message(rt, "(:a)") == "Wrong number of args (0) passed to: :a")
				#expect(message(rt, "(\"s\" 1)") == "\"s\" cannot be invoked")
				#expect(message(rt, "(nil 1)") == "nil cannot be invoked")
				#expect(message(rt, "(nth [1 2 3] 5)") == "Index 5 out of bounds for length 3")
				#expect(message(rt, "(nth '(1) 1)") == "Index 1 out of bounds for length 1")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func errorsCarryPositionsAndPropagate() throws {
			try declare("rt-deep")
			let before = clj_debug_live_objects()
			do {
				let e = try #require(clojureError(rt, "1\n  (let [x 1]\n    (nope x))"))
				#expect(e.message == "Unable to resolve symbol: nope in this context")
				#expect(try e.data == Value(reading: "{:line 2 :column 3}"))
				#expect(e.cause == nil)
				#expect(e.causeError == nil)
				#expect(e.description == e.message)
				let runtime = try #require(clojureError(rt, "(+ 1 2) (+ 1 \"x\")"))
				#expect(runtime.message == "string cannot be cast to a number")
				#expect(runtime.data == nil)
				#expect(throws: ReaderError.self) { try rt.eval("(+ 1") }
				#expect(throws: ReaderError.self) { try rt.eval("1 )") }
				#expect(try clojureError(rt, "(let [x 1] (if x (recur)))")?.data == Value(reading: "{:line 1 :column 1}"))
				#expect(try rt.eval("(def rt-deep (fn [n] (+ 1 (rt-deep (inc n)))))").description == "#'user/rt-deep")
				#expect(message(rt, "(rt-deep 0)") == "Stack overflow")
				#expect(message(rt, "(rt-deep 0)") == "Stack overflow")
				#expect(try rt.eval("(+ 1 2)") == 3)
				try unbind("rt-deep")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func valuesAreCallableFromSwift() throws {
			try declare("rt-sum")
			let before = clj_debug_live_objects()
			do {
				let square = try rt.eval("(fn [x] (* x x))")
				#expect(try square(9) == 81)
				#expect(square.isFn)
				let plus = try rt.eval("+")
				#expect(try plus() == 0)
				#expect(try plus(1, 2, 3.5) == 6.5)
				let m = try rt.eval("{:a 1}")
				#expect(try m(kw("a")) == 1)
				#expect(try kw("a")(m) == 1)
				#expect(try Value([5, 6])(1) == 6)
				#expect(throws: ClojureError.self) { try square() }
				#expect(throws: ClojureError.self) { try Value(1)(2) }
				#expect(throws: ClojureError.self) { try Value([1])(3) }
				do {
					_ = try square("s")
					Issue.record("expected a ClojureError")
				} catch let e as ClojureError {
					#expect(e.message == "string cannot be cast to a number")
				}
				_ = try rt.eval("(def rt-sum (fn [& xs] (apply + xs)))")
				let sum = try rt.eval("rt-sum")
				#expect(try sum(1, 2, 3) == 6)
				try unbind("rt-sum")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func smallProgram() throws {
			try declare("fib", "rt-reduce", "frequencies", "rt-range")
			let before = clj_debug_live_objects()
			do {
				let program = """
				(def fib
				  (fn [n]
				    (loop [i 0 a 0 b 1]
				      (if (= i n) a (recur (inc i) b (+ a b))))))

				(def rt-reduce
				  (fn [f init coll]
				    (loop [acc init coll coll]
				      (if (nil? (first coll))
				        acc
				        (recur (f acc (first coll)) (rest coll))))))

				(def rt-range
				  (fn [n]
				    (loop [i (dec n) acc nil]
				      (if (neg? i) acc (recur (dec i) (cons i acc))))))

				(def frequencies
				  (fn [coll]
				    (rt-reduce (fn [m k] (assoc m k (inc (get m k 0)))) {} coll)))

				(let [fibs (rt-reduce (fn [v i] (conj v (fib i))) [] (rt-range 10))
				      total (rt-reduce + 0 fibs)
				      parity (frequencies (rt-reduce (fn [v x] (conj v (if (even? x) :even :odd))) [] fibs))]
				  (println "fibs:" fibs "total:" total)
				  {:fibs fibs :total total :parity parity :fib-88 (fib 88)})
				"""
				var result: Value = nil
				let out = try capturingOutput { result = try rt.eval(program) }
				#expect(out == "fibs: [0 1 1 2 3 5 8 13 21 34] total: 88\n")
				#expect(try result == Value(reading: "{:fibs [0 1 1 2 3 5 8 13 21 34] :total 88 :parity {:even 4 :odd 6} :fib-88 1100087778366101931}"))
				try unbind("fib", "rt-reduce", "frequencies", "rt-range")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
