// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

private func eval(_ source: String) throws -> Value { try cljEval(source) }
private func evalError(_ source: String) -> String? { cljEvalError(source) }
private func message(_ source: String) -> String? {
	guard let text = evalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

extension CoreTests {
	@Suite struct BuiltinsTests {
		@Test func arithmetic() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(+)") == 0)
				#expect(try eval("(*)") == 1)
				#expect(try eval("(+ 1 2 3)") == 6)
				#expect(try eval("(- 10 1 2)") == 7)
				#expect(try eval("(- 3)") == -3)
				#expect(try eval("(* 2 3 4)") == 24)
				#expect(try eval("(/ 12 4)") == 3)
				#expect(try eval("(/ 12 -4 3)") == -1)
				#expect(try eval("(/ 1 2)") == 0.5)
				#expect(try eval("(/ 2)") == 0.5)
				#expect(try eval("(/ 4.0 2)") == 2.0)
				#expect(try eval("(+ 1 2.5)") == 3.5)
				#expect(try eval("(- 1.5 0.5)") == 1.0)
				#expect(try eval("(* 2 1.5)") == 3.0)
				#expect(try eval("(inc 1)") == 2)
				#expect(try eval("(dec 1.5)") == 0.5)
				#expect(try eval("(+ 4611686018427387903 0)") == Value(Value.fixnumRange.upperBound))
				#expect(try eval("(/ 1.0 0)").double == .infinity)
				#expect(message("(+ 4611686018427387903 1)") == "integer overflow")
				#expect(message("(- -4611686018427387904 1)") == "integer overflow")
				#expect(message("(* 4611686018427387903 2)") == "integer overflow")
				#expect(message("(/ -4611686018427387904 -1)") == "integer overflow")
				#expect(message("(/ 1 0)") == "Divide by zero")
				#expect(message("(+ 1 \"a\")") == "string cannot be cast to a number")
				#expect(message("(inc nil)") == "nil cannot be cast to a number")
				#expect(message("(-)") == "Wrong number of args (0) passed to: clojure.core/-")
				#expect(message("(inc)") == "Wrong number of args (0) passed to: clojure.core/inc")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func comparisonsAndPredicates() throws {
			clj_init()
			for k in ["a", "s"] { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(< 1 2 3)") == true)
				#expect(try eval("(< 1 3 2)") == false)
				#expect(try eval("(<= 1 1 2)") == true)
				#expect(try eval("(> 3 2 1)") == true)
				#expect(try eval("(>= 3 3 4)") == false)
				#expect(try eval("(< 1 1.5)") == true)
				#expect(try eval("(< 1)") == true)
				#expect(try eval("(= 1 1 1)") == true)
				#expect(try eval("(= 1 1.0)") == false)
				#expect(try eval("(= [1 2] '(1 2))") == true)
				#expect(try eval("(= {:a 1} {:a 1})") == true)
				#expect(try eval("(not= 1 2)") == true)
				#expect(try eval("(not= 1 1)") == false)
				#expect(try eval("(not nil)") == true)
				#expect(try eval("(not 0)") == false)
				#expect(try eval("[(nil? nil) (nil? false) (zero? 0) (zero? 0.0) (zero? 1) (pos? 1) (pos? -1.5) (neg? -1) (neg? 0)]") == [true, false, true, true, false, true, false, true, false])
				#expect(try eval("[(even? 2) (even? 3) (odd? 3) (odd? -2)]") == [true, false, true, false])
				#expect(try eval("[(number? 1) (number? 1.5) (number? \"1\") (string? \"s\") (string? :s) (keyword? :a) (keyword? 'a) (symbol? 'a) (symbol? :a)]") == [true, true, false, true, false, true, false, true, false])
				#expect(try eval("[(vector? []) (vector? '()) (map? {}) (map? []) (list? '()) (list? '(1)) (list? []) (fn? +) (fn? (fn [] 1)) (fn? :a)]") == [true, false, true, false, true, true, false, true, true, false])
				#expect(message("(even? 1.5)") == "Argument must be an integer: 1.5")
				#expect(message("(< 1 :a)") == "keyword cannot be cast to a number")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func collections() throws {
			clj_init()
			for k in ["a", "b", "c", "d", "none"] { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(get {:a 1} :a)") == 1)
				#expect(try eval("(get {:a 1} :b)") == nil)
				#expect(try eval("(get {:a 1} :b 7)") == 7)
				#expect(try eval("(get [5 6] 1)") == 6)
				#expect(try eval("(get [5 6] 2)") == nil)
				#expect(try eval("(get [5 6] -1 :none)") == Value(keyword: "none"))
				#expect(try eval("(get nil :a)") == nil)
				#expect(try eval("(get \"abc\" 1)") == Value("b" as Unicode.Scalar))
				#expect(try eval("(get 5 :a :d)") == Value(keyword: "d"))
				#expect(try eval("(assoc {:a 1} :b 2 :c 3)") == Value(reading: "{:a 1 :b 2 :c 3}"))
				#expect(try eval("(assoc nil :a 1)") == Value(reading: "{:a 1}"))
				#expect(try eval("(assoc [1 2] 0 9)") == [9, 2])
				#expect(try eval("(assoc [1 2] 2 9)") == [1, 2, 9])
				#expect(try eval("(dissoc {:a 1 :b 2} :a)") == Value(reading: "{:b 2}"))
				#expect(try eval("(dissoc {:a 1 :b 2} :a :b :c)") == Value(reading: "{}"))
				#expect(try eval("(dissoc nil :a)") == nil)
				#expect(try eval("[(contains? {:a 1} :a) (contains? {:a 1} :b) (contains? [1 2] 1) (contains? [1 2] 2) (contains? nil 1)]") == [true, false, true, false, false])
				#expect(try eval("[(count nil) (count []) (count [1 2]) (count '(1 2 3)) (count {:a 1}) (count \"λ→\")]") == [0, 0, 2, 3, 1, 2])
				#expect(try eval("(conj)") == [])
				#expect(try eval("(conj [1] 2 3)") == [1, 2, 3])
				#expect(try eval("(conj '(1) 2 3)") == Value(list: [3, 2, 1]))
				#expect(try eval("(conj nil 1 2)") == Value(list: [2, 1]))
				#expect(try eval("(conj {:a 1} [:b 2] {:c 3 :d 4} nil)") == Value(reading: "{:a 1 :b 2 :c 3 :d 4}"))
				#expect(try eval("(nth [1 2 3] 1)") == 2)
				#expect(try eval("(nth '(1 2 3) 2)") == 3)
				#expect(try eval("(nth \"aλb\" 1)") == Value("λ" as Unicode.Scalar))
				#expect(try eval("(nth [1 2 3] 5 :d)") == Value(keyword: "d"))
				#expect(try eval("(nth nil 5)") == nil)
				#expect(try eval("[(first [1 2]) (first '(1 2)) (first []) (first nil)]") == [1, 1, nil, nil])
				#expect(try eval("(rest [1 2 3])") == Value(list: [2, 3]))
				#expect(try eval("(rest '(1 2 3))") == Value(list: [2, 3]))
				#expect(try eval("(rest [1])") == Value(list: []))
				#expect(try eval("(rest nil)") == Value(list: []))
				#expect(try eval("(list? (rest [1 2]))") == true)
				#expect(try eval("[(next [1 2]) (next [1]) (next nil) (next '())]") == [Value(list: [2]), nil, nil, nil])
				#expect(try eval("(cons 1 [2 3])") == Value(list: [1, 2, 3]))
				#expect(try eval("(cons 1 nil)") == Value(list: [1]))
				#expect(try eval("(first (rest (cons 1 [2 3])))") == 2)
				#expect(try eval("(list 1 2 3)") == Value(list: [1, 2, 3]))
				#expect(try eval("(list)") == Value(list: []))
				#expect(try eval("(vector 1 2)") == [1, 2])
				#expect(try eval("(hash-map :a 1 :b 2 :a 3)") == Value(reading: "{:a 3 :b 2}"))
				#expect(try eval("(hash-map)") == Value(reading: "{}"))
				#expect(try eval("(identity :a)") == Value(keyword: "a"))
				#expect(try eval("(apply + 1 2 [3 4])") == 10)
				#expect(try eval("(apply + '(1 2))") == 3)
				#expect(try eval("(apply list 1 nil)") == Value(list: [1]))
				#expect(try eval("(apply (fn [& xs] (count xs)) 1 2 [3])") == 3)
				#expect(message("(nth [1 2 3] 5)") == "Index 5 out of bounds for length 3")
				#expect(message("(nth [1 2 3] -1)") == "Index -1 out of bounds for length 3")
				#expect(message("(nth '(1 2) 2)") == "Index 2 out of bounds for length 2")
				#expect(message("(nth [1] :a)") == "Key must be integer")
				#expect(message("(nth {:a 1} 0)") == "nth not supported on this type: map")
				#expect(message("(assoc [1] 5 2)") == "Index 5 out of bounds for length 1")
				#expect(message("(assoc {} :a 1 :b)") == "assoc expects even number of arguments after map/vector, found odd number")
				#expect(message("(assoc 1 :a 2)") == "assoc not supported on this type: fixnum")
				#expect(message("(conj {} 1)") == "Vector arg to map conj must be a pair")
				#expect(message("(conj 1 2)") == "conj not supported on this type: fixnum")
				#expect(message("(count 1)") == "count not supported on this type: fixnum")
				#expect(message("(first 1)") == "Don't know how to create ISeq from: fixnum")
				#expect(message("(rest {})") == "Don't know how to create ISeq from: map")
				#expect(message("(cons 1 2)") == "Don't know how to create ISeq from: fixnum")
				#expect(message("(hash-map :a)") == "No value supplied for key: :a")
				#expect(message("(apply + 1)") == "Don't know how to create ISeq from: fixnum")
				#expect(message("(contains? 1 2)") == "contains? not supported on type: fixnum")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func stringsAndOutput() throws {
			clj_init()
			_ = Value(keyword: "k")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(str)") == "")
				#expect(try eval("(str nil)") == "")
				#expect(try eval("(str \"a\" 1 :k 2.5 \\λ nil [1 \"x\"] 'sym)") == "a1:k2.5λ[1 \"x\"]sym")
				#expect(try eval("(pr-str)") == "")
				#expect(try eval("(pr-str \"a\" 1 :k [1 \"x\"] \\a)") == "\"a\" 1 :k [1 \"x\"] \\a")
				#expect(try eval("(str +)") == "#object[fn clojure.core/+]")
				var results: [Value] = []
				let out = try capturingOutput {
					for src in ["(println \"hi\" 1 :k [\"x\"])", "(prn \"hi\" 1 :k [\"x\"])", "(print \"a\" \"b\")", "(pr \"a\")", "(println)", "(do (println 1) (println 2) 3)"] {
						results.append(try eval(src))
					}
				}
				#expect(results == [nil, nil, nil, nil, nil, 3])
				#expect(out == "hi 1 :k [x]\n\"hi\" 1 :k [\"x\"]\na b\"a\"\n1\n2\n")
				results = []
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
