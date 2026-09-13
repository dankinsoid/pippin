// @ai-generated(solo)
import Testing
@testable import Clojure

extension CoreTests {
	@Suite struct SmokeTests {
		@Test func verticalSlice() throws {
			let rt = Runtime()
			#expect(try rt.eval("(let [x 1] (+ x 2))").int == 3)

			let program = """
			(def fib
			  (fn [n]
			    (loop [i 0 a 0 b 1]
			      (if (= i n) a (recur (inc i) b (+ a b))))))

			(def make-counter
			  (fn [start]
			    (fn [step] (+ start step))))

			(def from-ten (make-counter 10))

			(def person {:name "Ada" :langs ["clj" "swift"]})

			[(fib 50)
			 (from-ten 5)
			 (:name person)
			 (nth (:langs person) 1)
			 (str "fib(10)=" (fib 10) " " :k " " nil)
			 (apply + 1 2 [3 4])]
			"""
			let v = try rt.eval(program)
			#expect(v.description == #"[12586269025 15 "Ada" "swift" "fib(10)=55 :k " 10]"#)
		}

		@Test func errorsArriveAsClojureError() throws {
			let rt = Runtime()
			#expect(throws: ClojureError.self) { try rt.eval("(nth [1 2 3] 5)") }
			do {
				_ = try rt.eval("(undefined-thing 1)")
			} catch let e as ClojureError {
				#expect(e.message.contains("Unable to resolve symbol: undefined-thing"))
			}
		}
	}
}
