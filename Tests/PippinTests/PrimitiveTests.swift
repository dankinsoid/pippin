// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

private func kw(_ s: String) -> Value { Value(keyword: s) }
private func sym(_ s: String) -> Value { Value(symbol: s) }

extension CoreTests {
	@Suite struct PrimitiveTests {
		let rt = Runtime()

		init() {
			// A host error interns its ex-type keyword on the first error of that Swift type (design §4).
			for k in ["a", "b", "a/b", "b/a", "a/x", "a/y", "k", "Pippin/ReaderError"] { _ = kw(k) }
		}

		@Test func compareTable() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("""
				[(compare 1 2) (compare 2 1) (compare 1 1) (compare -1 -2) (compare 1 1.0) (compare 1.5 1) (compare 1 1.5) (compare 2.5 2.5)
				 (compare nil nil) (compare nil 1) (compare 1 nil) (compare nil "a")
				 (compare false true) (compare true false) (compare true true)
				 (compare \\a \\b) (compare \\b \\a) (compare \\a \\a)
				 (compare "a" "b") (compare "b" "a") (compare "ab" "a") (compare "a" "ab") (compare "a" "a") (compare "" "a") (compare "é" "z")
				 (compare :a :b) (compare :b :a) (compare :a :a) (compare :a :a/b) (compare :b/a :a) (compare :a/x :a/y) (compare :b/a :a/y)
				 (compare 'a 'b) (compare 'x 'x) (compare 'a 'a/b) (compare 'a/x 'a/y)
				 (compare [1 2] [1 3]) (compare [1 3] [1 2]) (compare [1 2] [1 2]) (compare [1] [1 2]) (compare [1 2] [1]) (compare [] [])
				 (compare [[1] 2] [[1] 3]) (compare [nil] [1])]
				""") == [-1, 1, 0, 1, 0, 1, -1, 0,
				         0, -1, 1, -1,
				         -1, 1, 0,
				         -1, 1, 0,
				         -1, 1, 1, -1, 0, -1, 1,
				         -1, 1, 0, -1, 1, -1, 1,
				         -1, 0, -1, -1,
				         -1, 1, 0, -1, 1, 0,
				         -1, -1])
				// NaN orders as equal to any number, as Numbers.compare does.
				#expect(try rt.eval("(let [nan (/ 0.0 0.0)] [(compare nan 1) (compare 1 nan) (compare nan nan)])") == [0, 0, 0])
				#expect(try Runtime.compare("b", "a") == 1)
				#expect(try Runtime.compare(kw("a"), kw("a/b")) == -1)
				// Two objects of an unordered type compare only as the same object.
				#expect(try rt.eval("(let [f (fn [])] (compare f f))") == 0)
				let messages = try rt.eval("""
				(map (fn [[a b]] (try (compare a b) (catch ExceptionInfo e (ex-message e))))
				     [[1 "a"] ["a" 1] [:a "a"] [true 1] ['a :a] [\\a "a"] [[1] :a] [{:a 1} {:b 2}] [(fn []) (fn [])]])
				""")
				#expect(messages == Value(list: ["string cannot be cast to a number", "long cannot be cast to a string", "string cannot be cast to a keyword",
				                                  "long cannot be cast to a boolean", "keyword cannot be cast to a symbol", "string cannot be cast to a char",
				                                  "keyword cannot be cast to a vector", "map cannot be cast to Comparable", "fn cannot be cast to Comparable"]))
				#expect(throws: ClojureError.self) { try Runtime.compare(1, "a") }
				#expect(try rt.eval("(try (compare 1 \"a\") (catch ExceptionInfo e (instance? HostError e)))") == false)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func sortAgainstItsSpecification() throws {
			let spec = try rt.eval(Runtime.sortSpecification)
			let sort = try rt.eval("sort")
			let before = clj_debug_live_objects()
			do {
				let samples: [[Value]] = [
					[[]], [nil], [[1]], [[7, 7]],
					[[3, 1, 2]], [[2, 1, 2, 1, 3, 3]], [[-1, -5, 3, 0, -2]], [[5, 4, 3, 2, 1, 0]],
					[[1.5, 1, 2, -0.5, 1.0]], [[1, 1.0, 0, 0.0, 1, 1.0]],
					[["b", "a", "ab", "", "aa"]], [[Value("é"), Value("z"), Value("a")]],
					[[kw("b"), kw("a"), kw("a/b"), kw("b/a"), kw("a/x")]], [[sym("b"), sym("a"), sym("a/b"), sym("b/a")]],
					[[nil, 2, nil, 1]], [[true, false, true]], [[Value("c" as Unicode.Scalar), Value("a" as Unicode.Scalar), Value("b" as Unicode.Scalar)]],
					[try Value(reading: "(3 2 1)")], [try rt.eval("(map inc [3 1 2])")], [try rt.eval("(range 20 0 -1)")], ["cba"],
					[[[1]]], [[1, "a"]], [[kw("a"), "a", 1]], [[[1], [2]]], [[1, 2, kw("a")]],
					[try rt.eval("{:b 1 :a 2}")], [try rt.eval("(map (fn [x] (if (= x 2) (throw (ex-info \"boom\" {})) x)) [3 2 1])")],
				]
				// Messages stay out: the two sorts meet a mixed-type pair in different argument orders, and the
				// message names the later item's type.
				let divergences = Runtime.differential(primitive: sort, spec: spec, samples: samples)
				#expect(divergences.isEmpty, "\(divergences)")
				#expect(try rt.eval("(sort [3 1 2])") == Value(list: [1, 2, 3]))
				#expect(try rt.eval("(sort [3 1 2])").description == "(1 2 3)")
				#expect(try rt.eval("[(sort []) (sort nil) (list? (sort [2 1]))]") == [Value(list: []), Value(list: []), true])
				#expect(try rt.eval("(sort [1 1.0 0 0.0])").description == "(0 0.0 1 1.0)")
				#expect(try rt.eval("(try (sort [1 \"a\"]) (catch ExceptionInfo e (ex-message e)))") == "string cannot be cast to a number")
				#expect(try rt.eval("(try (sort 1) (catch ExceptionInfo e (ex-message e)))") == "Don't know how to create ISeq from: long")
				#expect(try Runtime.sort([2, 1]) == Value(list: [1, 2]))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func differentialReportsDivergences() throws {
			let before = clj_debug_live_objects()
			do {
				let inc = Value(function: "inc-spec") { args in Value(args[0].int! + 1) }
				let wrong = Value(function: "inc-wrong") { args in args[0].int! == 2 ? 0 : Value(args[0].int! + 1) }
				#expect(Runtime.differential(primitive: inc, spec: inc, samples: [[1], [2]]).isEmpty)
				let d = Runtime.differential(primitive: wrong, spec: inc, samples: [[1], [2], [3]])
				#expect(d.count == 1)
				#expect(d.first?.args == [2])
				#expect(d.first?.description == "(2): primitive returned 0, spec returned 3")
				let throwsA = Value(function: "throws-a") { _ in throw ClojureError(thrown: Value(exInfo: "a")) }
				let throwsB = Value(function: "throws-b") { _ in throw ClojureError(thrown: Value(exInfo: "b")) }
				#expect(Runtime.differential(primitive: throwsA, spec: throwsB, samples: [[1]]).isEmpty)
				#expect(Runtime.differential(primitive: throwsA, spec: throwsA, samples: [[1]], messages: true).isEmpty)
				let byMessage = Runtime.differential(primitive: throwsA, spec: throwsB, samples: [[1]], messages: true)
				#expect(byMessage.map(\.description) == ["(1): primitive threw \"a\", spec threw \"b\""])
				#expect(Runtime.differential(primitive: throwsA, spec: inc, samples: [[1]]).map(\.description) == ["(1): primitive threw \"a\", spec returned 2"])
				// A Swift error counts as a throw, with its description as the message.
				let host = Value(function: "throws-host") { _ in throw ReaderError(message: "m", line: 1, column: 2) }
				#expect(Runtime.differential(primitive: host, spec: throwsA, samples: [[1]]).isEmpty)
				#expect(Runtime.differential(primitive: host, spec: throwsA, samples: [[1]], messages: true).map(\.description) == ["(1): primitive threw \"1:2: m\", spec threw \"a\""])
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
