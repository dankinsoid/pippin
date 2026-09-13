// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

// Fixnum addition, registered as clojure.core/plus so the evaluator can be exercised before the builtins exist.
private let plus: clj_native_fn = { args, n in
	var sum = 0
	for i in 0..<n { sum += clj_fixnum_val(args![i]) }
	return clj_fixnum(sum)
}

private let lt: clj_native_fn = { args, n in
	for i in 1..<n where clj_fixnum_val(args![i - 1]) >= clj_fixnum_val(args![i]) { return CLJ_FALSE }
	return CLJ_TRUE
}

private func installNatives() {
	for (text, native) in [("plus", plus), ("lt", lt)] {
		let name = Value(symbol: text)
		let fn = Value(owning: withExtendedLifetime(name) { clj_fn_native(name.raw, native, 0, CLJ_ARITY_ANY) })
		withExtendedLifetime((name, fn)) {
			let v = clj_ns_intern(clj_ns_core(), name.raw)
			clj_var_bind_root(v, fn.raw)
		}
	}
}

private func eval(_ source: String) throws -> Value {
	var last: Value = nil
	for form in try Value.readAll(source) {
		let raw = withExtendedLifetime(form) { clj_eval(form.raw, nil) }
		if raw == CLJ_THROWN {
			let ex = Value(owning: clj_take_pending())
			throw EvalFailure(message: ex.description)
		}
		last = Value(owning: raw)
	}
	return last
}

private struct EvalFailure: Error { let message: String }

private func evalError(_ source: String) -> String? {
	do {
		_ = try eval(source)
		return nil
	} catch let e as EvalFailure {
		return e.message
	} catch {
		return nil
	}
}

extension CoreTests {
	@Suite struct EvalTests {
		@Test func specialForms() throws {
			installNatives()
			for k in ["yes", "no", "a", "b", "k", "message", "data", "cause"] { _ = Value(keyword: k) }
			_ = try eval("(def ev-x) (def ev-f) (def ev-g) (def ev-h)")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [x 1] (plus x 2))") == 3)
				#expect(try eval("(let [x 1 x (plus x 1)] x)") == 2)
				#expect(try eval("(if nil 1 2)") == 2)
				#expect(try eval("(if false 1)") == nil)
				#expect(try eval("(if 0 :yes :no)") == Value(keyword: "yes"))
				#expect(try eval("(do 1 2 3)") == 3)
				#expect(try eval("(do)") == nil)
				#expect(try eval("'(1 x)") == Value(list: [1, Value(symbol: "x")]))
				#expect(try eval("(quote x)") == Value(symbol: "x"))
				#expect(try eval("[1 (plus 1 1) 3]") == [1, 2, 3])
				#expect(try eval("((fn [x y] (plus x y)) 1 2)") == 3)
				#expect(try eval("((fn [& xs] xs) 1 2)") == Value(list: [1, 2]))
				#expect(try eval("((fn [& xs] xs))") == nil)
				#expect(try eval("((fn ([] 0) ([x] x) ([x & r] r)) 5)") == 5)
				#expect(try eval("((fn ([] 0) ([x] x) ([x & r] r)))") == 0)
				#expect(try eval("((fn ([] 0) ([x] x) ([x & r] r)) 5 6 7)") == Value(list: [6, 7]))
				#expect(try eval("(let [a 10 f (fn [x] (plus a x))] (f 5))") == 15)
				#expect(try eval("(let [a 1] (let [g (fn [] (fn [] a))] ((g))))") == 1)
				#expect(try eval("(loop [i 0 acc 0] (if (lt i 1000000) (recur (plus i 1) (plus acc i)) acc))") == 499999500000)
				#expect(try eval("((fn [n acc] (if (lt n 1) acc (recur (plus n -1) (plus acc n)))) 10 0)") == 55)
				#expect(try eval("(def ev-x 41) (plus ev-x 1)") == 42)
				#expect(try eval("(def ev-f (fn [n] (if (lt n 1) 0 (plus n (ev-f (plus n -1)))))) (ev-f 50)") == 1275)
				#expect(try eval("(def ev-g (fn ev-self [n] (if (lt n 1) 0 (plus 1 (ev-self (plus n -1)))))) (ev-g 3)") == 3)
				#expect(try eval("(let [m {:a 1}] (:a m))") == 1)
				#expect(try eval("({:a 1} :b 7)") == 7)
				#expect(try eval("([10 20 30] 1)") == 20)
				#expect(try eval("(let [x 5] {:k x (plus x 1) [x]})") == Value(reading: "{:k 5, 6 [5]}"))
				#expect(evalError("(nope 1)") == "#error {:message \"Unable to resolve symbol: nope in this context\", :data nil}")
				#expect(evalError("((fn [x] x))") == "#error {:message \"Wrong number of args (0) passed to: fn\", :data nil}")
				#expect(evalError("(ev-f 1 2)") == "#error {:message \"Wrong number of args (2) passed to: user/ev-f\", :data nil}")
				#expect(evalError("(1 2)") == "#error {:message \"1 cannot be invoked\", :data nil}")
				#expect(evalError("(loop [x 1] (plus 1 (recur 2)))") == "#error {:message \"Can only recur from tail position\", :data nil}")
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
				#expect(evalError("(def ev-h (fn ev-deep [n] (plus 1 (ev-deep n)))) (ev-h 0)")?.hasPrefix("#error {:message \"Stack overflow\"") == true)
				print("PROBE after ev-h", clj_debug_live_objects() - before)
				_ = try eval("(def ev-x nil) (def ev-f nil) (def ev-g nil) (def ev-h nil)")
				print("PROBE after reset", clj_debug_live_objects() - before)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
