// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

private func eval(_ source: String) throws -> Value { try cljEval(source) }
private func evalError(_ source: String) -> String? { cljEvalError(source) }

extension CoreTests {
	@Suite struct EvalTests {
		@Test func specialForms() throws {
			for k in ["yes", "no", "a", "b", "k", "message", "data", "cause"] { _ = Value(keyword: k) }
			_ = try eval("(def ev-x) (def ev-f) (def ev-g) (def ev-h)")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [x 1] (+ x 2))") == 3)
				#expect(try eval("(let [x 1 x (+ x 1)] x)") == 2)
				#expect(try eval("(if nil 1 2)") == 2)
				#expect(try eval("(if false 1)") == nil)
				#expect(try eval("(if 0 :yes :no)") == Value(keyword: "yes"))
				#expect(try eval("(do 1 2 3)") == 3)
				#expect(try eval("(do)") == nil)
				#expect(try eval("'(1 x)") == Value(list: [1, Value(symbol: "x")]))
				#expect(try eval("(quote x)") == Value(symbol: "x"))
				#expect(try eval("[1 (+ 1 1) 3]") == [1, 2, 3])
				#expect(try eval("((fn [x y] (+ x y)) 1 2)") == 3)
				#expect(try eval("((fn [& xs] xs) 1 2)") == Value(list: [1, 2]))
				#expect(try eval("((fn [& xs] xs))") == nil)
				#expect(try eval("((fn ([] 0) ([x] x) ([x & r] r)) 5)") == 5)
				#expect(try eval("((fn ([] 0) ([x] x) ([x & r] r)))") == 0)
				#expect(try eval("((fn ([] 0) ([x] x) ([x & r] r)) 5 6 7)") == Value(list: [6, 7]))
				#expect(try eval("(let [a 10 f (fn [x] (+ a x))] (f 5))") == 15)
				#expect(try eval("(let [a 1] (let [g (fn [] (fn [] a))] ((g))))") == 1)
				#expect(try eval("(loop [i 0 acc 0] (if (< i 1000000) (recur (+ i 1) (+ acc i)) acc))") == 499999500000)
				#expect(try eval("((fn [n acc] (if (< n 1) acc (recur (+ n -1) (+ acc n)))) 10 0)") == 55)
				#expect(try eval("(def ev-x 41) (+ ev-x 1)") == 42)
				#expect(try eval("(def ev-f (fn [n] (if (< n 1) 0 (+ n (ev-f (+ n -1)))))) (ev-f 50)") == 1275)
				#expect(try eval("(def ev-g (fn ev-self [n] (if (< n 1) 0 (+ 1 (ev-self (+ n -1)))))) (ev-g 3)") == 3)
				#expect(try eval("(let [m {:a 1}] (:a m))") == 1)
				#expect(try eval("({:a 1} :b 7)") == 7)
				#expect(try eval("([10 20 30] 1)") == 20)
				#expect(try eval("(let [x 5] {:k x (+ x 1) [x]})") == Value(reading: "{:k 5, 6 [5]}"))
				#expect(evalError("(nope 1)") == "#error {:message \"Unable to resolve symbol: nope in this context\", :data nil}")
				#expect(evalError("((fn [x] x))") == "#error {:message \"Wrong number of args (0) passed to: fn\", :data nil}")
				#expect(evalError("(ev-f 1 2)") == "#error {:message \"Wrong number of args (2) passed to: user/ev-f\", :data nil}")
				#expect(evalError("(1 2)") == "#error {:message \"1 cannot be invoked\", :data nil}")
				#expect(evalError("(loop [x 1] (+ 1 (recur 2)))") == "#error {:message \"Can only recur from tail position\", :data nil}")
				#expect(evalError("(recur 1)") == "#error {:message \"Can only recur from tail position\", :data nil}")
				#expect(evalError("(loop [x 1] (recur 1 2))") == "#error {:message \"Mismatched argument count to recur, expected: 1 args, got: 2\", :data nil}")
				#expect(evalError("(let [x] x)") == "#error {:message \"let requires an even number of forms in binding vector\", :data nil}")
				#expect(evalError("(fn [x] (fn [x] 1) (fn [x x] 2) (fn [x] 3))") == nil)
				#expect(evalError("(fn ([x] 1) ([y] 2))") == "#error {:message \"Can't have 2 overloads with same arity\", :data nil}")
				#expect(evalError("(fn ([x & r] 1) ([a b] 2))") == "#error {:message \"Can't have fixed arity function with more params than variadic function\", :data nil}")
				#expect(evalError("([1 2] 5)") == "#error {:message \"Index 5 out of bounds for length 2\", :data nil}")
				#expect(evalError("(def ev-x 1 2 3)") == "#error {:message \"Too many arguments to def\", :data nil}")
				#expect(evalError("(let [x 1 y (nope)] x)") == "#error {:message \"Unable to resolve symbol: nope in this context\", :data nil}")
				#expect(evalError("(if 1 (nope) 2)") == "#error {:message \"Unable to resolve symbol: nope in this context\", :data nil}")
				#expect(evalError("(def ev-f (fn [x] (nope)))") == "#error {:message \"Unable to resolve symbol: nope in this context\", :data nil}")
				print("PROBE before ev-h", clj_debug_live_objects() - before)
				#expect(evalError("(def ev-h (fn ev-deep [n] (+ 1 (ev-deep n)))) (ev-h 0)")?.hasPrefix("#error {:message \"Stack overflow\"") == true)
				print("PROBE after ev-h", clj_debug_live_objects() - before)
				_ = try eval("(def ev-x nil) (def ev-f nil) (def ev-g nil) (def ev-h nil)")
				print("PROBE after reset", clj_debug_live_objects() - before)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
