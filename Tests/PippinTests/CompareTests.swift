// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

// clj_compare through the C entry point, with both words borrowed from live values.
private func cCompare(_ a: Value, _ b: Value) throws -> Int {
	try withExtendedLifetime((a, b)) {
		var out: Int32 = 0
		if clj_compare(a.raw, b.raw, &out) == CLJ_THROWN {
			let ex = Value(owning: clj_take_pending())
			throw CljEvalFailure(message: ex.description)
		}
		return Int(out)
	}
}

extension CoreTests {
	@Suite struct CompareTests {
		init() {
			clj_init()
			for k in ["a", "b", "c", "k", "x", "y", "a/b"] { _ = kw(k) }
		}

		@Test func orderingByTable() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try cCompare(1, 2) == -1)
				#expect(try cCompare(2, 1) == 1)
				#expect(try cCompare(1, 1) == 0)
				#expect(try cCompare(1, 1.0) == 0)
				#expect(try cCompare(1.5, 1) == 1)
				#expect(try cCompare(nil, 1) == -1)
				#expect(try cCompare(1, nil) == 1)
				#expect(try cCompare(nil, nil) == 0)
				#expect(try cCompare(false, true) == -1)
				#expect(try cCompare(true, true) == 0)
				#expect(try cCompare(Value("a" as Unicode.Scalar), Value("b" as Unicode.Scalar)) == -1)
				#expect(try cCompare("ab", "a") == 1)
				#expect(try cCompare("é", "z") == 1)
				#expect(try cCompare(kw("a"), kw("a/b")) == -1)
				#expect(try cCompare(Value(symbol: "a/x"), Value(symbol: "a/y")) == -1)
				#expect(try cCompare([1, 2], [1, 3]) == -1)
				#expect(try cCompare([1], [1, 2]) == -1)
				#expect(try cCompare([[1], 2], [[1], 3]) == -1)
				// A bigint against a fixnum goes through the numeric tower, not the fixnum fast path.
				#expect(try eval("[(compare 1N 2) (compare 1/2 0.6) (compare 1M 1)]") == [-1, -1, 0])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func incomparablePairsThrow() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(message("(compare 1 \"a\")") == "string cannot be cast to a number")
				#expect(message("(compare \"a\" 1)") == "long cannot be cast to a string")
				#expect(message("(compare :a \"a\")") == "string cannot be cast to a keyword")
				#expect(message("(compare [1] :a)") == "keyword cannot be cast to a vector")
				#expect(message("(compare {:a 1} {:b 2})") == "map cannot be cast to Comparable")
				#expect(message("(compare [1] [:a])") == "keyword cannot be cast to a number")
				// The same object of an unordered type is still equal to itself.
				#expect(try eval("(let [m {:a 1}] (compare m m))") == 0)
				#expect(throws: CljEvalFailure.self) { try cCompare(1, "a") }
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The default sorted collections carry a nil comparator and never reach a Clojure fn.
		@Test func sortedCollectionsUseTheCComparator() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(pr-str (sorted-map :b 2 :a 1 :c 3))") == "{:a 1, :b 2, :c 3}")
				#expect(try eval("(pr-str (sorted-set 3 1 2))") == "#{1 2 3}")
				#expect(try eval("(pr-str (seq (sorted-map \"b\" 1 \"a\" 2)))") == "([\"a\" 2] [\"b\" 1])")
				#expect(try eval("(get (sorted-map :a 1) :a)") == 1)
				#expect(try eval("(pr-str (subseq (sorted-set 1 2 3 4) > 2))") == "(3 4)")
				#expect(try eval("(pr-str (rseq (sorted-set 1 2 3)))") == "(3 2 1)")
				#expect(message("(assoc (sorted-map :a 1) \"b\" 2)") == "keyword cannot be cast to a string")
				#expect(message("(get (sorted-map :a 1) 1)") == "keyword cannot be cast to a number")
				// A comparator given by hand still goes through clj_invoke.
				#expect(try eval("(pr-str (sorted-map-by > 1 :a 3 :c 2 :b))") == "{3 :c, 2 :b, 1 :a}")
				#expect(try eval("(pr-str (sorted-set-by (fn [a b] (< (count a) (count b))) \"ccc\" \"a\" \"bb\"))") == "#{\"a\" \"bb\" \"ccc\"}")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func sortAndSortBy() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(pr-str (sort [3 1 2]))") == "(1 2 3)")
				#expect(try eval("[(pr-str (sort [])) (pr-str (sort nil)) (list? (sort [2 1]))]") == ["()", "()", true])
				#expect(try eval("(pr-str (sort > [1 3 2]))") == "(3 2 1)")
				#expect(try eval("(pr-str (sort-by :k [{:k 2} {:k 1}]))") == "({:k 1} {:k 2})")
				#expect(try eval("(pr-str (sort-by count [\"ccc\" \"a\" \"bb\"]))") == "(\"a\" \"bb\" \"ccc\")")
				#expect(try eval("(pr-str (sort-by :k > [{:k 1} {:k 2}]))") == "({:k 2} {:k 1})")
				// Stable: equal keys keep their input order.
				#expect(try eval("(pr-str (sort-by :k [[:a 1] [:b 1] [:c 1]]))") == "([:a 1] [:b 1] [:c 1])")
				#expect(try eval("(pr-str (sort-by first [[1 :a] [0 :x] [1 :b] [0 :y] [1 :c]]))") == "([0 :x] [0 :y] [1 :a] [1 :b] [1 :c])")
				#expect(try eval("(pr-str (sort (seq \"cba\")))") == "(\\a \\b \\c)")
				#expect(try eval("(pr-str (sort (map inc [3 1 2])))") == "(2 3 4)")
				#expect(message("(sort [1 :a])") == "keyword cannot be cast to a number")
				#expect(message("(sort-by :k nil [{:k 1}])") == "comparator must be a function, got: nil")
				#expect(message("(sort nil [1 2])") == "nil cannot be invoked")
				#expect(message("(sort-by (fn [_] (throw (ex-info \"boom\" {}))) [1 2])") == "boom")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func hostPrimitivesDelegate() throws {
			let rt = Runtime()
			let before = clj_debug_live_objects()
			do {
				#expect(try Runtime.compare("b", "a") == 1)
				#expect(try Runtime.compare(1, 1.0) == 0)
				#expect(throws: ClojureError.self) { try Runtime.compare(1, "a") }
				#expect(try Runtime.sort(Value([3, 1, 2])).description == "(1 2 3)")
				#expect(try Runtime.sort(Value([1, 3, 2]), by: rt.eval(">")).description == "(3 2 1)")
				#expect(throws: ClojureError.self) { try Runtime.sort(Value([1, 3, 2]), by: nil) }
				#expect(try rt.eval("(pr-str (sort [3 1 2]))") == "(1 2 3)")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
