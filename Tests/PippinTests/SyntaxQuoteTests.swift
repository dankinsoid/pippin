// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

// Printed as Clojure's (pr-str '`form) prints it.
private func expand(_ s: String) throws -> String { try Value(reading: s).description }

private func readError(_ s: String) -> ReaderError? {
	do {
		_ = try Value(reading: s)
		return nil
	} catch let e as ReaderError {
		return e
	} catch {
		return nil
	}
}

extension CoreTests {
	@Suite struct SyntaxQuoteTests {
		@Test func qualifiesSymbolsAndRebuildsCollections() throws {
			clj_init()
			_ = try cljEval("(def sq-defined)")
			let before = clj_debug_live_objects()
			do {
				#expect(try expand("`a") == "(quote user/a)")
				#expect(try expand("`(a b)") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote user/a)) (clojure.core/list (quote user/b))))")
				#expect(try expand("`(inc sq-defined)") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote clojure.core/inc)) (clojure.core/list (quote user/sq-defined))))")
				#expect(try expand("`(if a (do b))") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote if)) (clojure.core/list (quote user/a)) (clojure.core/list (clojure.core/seq (clojure.core/concat (clojure.core/list (quote do)) (clojure.core/list (quote user/b)))))))")
				#expect(try expand("`(fn* [a & r] (let* [x 1] (recur x)))").contains("(quote fn*)) (clojure.core/list (clojure.core/apply clojure.core/vector (clojure.core/seq (clojure.core/concat (clojure.core/list (quote user/a)) (clojure.core/list (quote &)) (clojure.core/list (quote user/r)))))) (clojure.core/list (clojure.core/seq (clojure.core/concat (clojure.core/list (quote let*))"))
				// let/loop/fn are core.clj macros, so they qualify like any other var.
				#expect(try expand("`(fn let loop)") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote clojure.core/fn)) (clojure.core/list (quote clojure.core/let)) (clojure.core/list (quote clojure.core/loop))))")
				#expect(try expand("`other/x") == "(quote other/x)")
				#expect(try expand("`[a]") == "(clojure.core/apply clojure.core/vector (clojure.core/seq (clojure.core/concat (clojure.core/list (quote user/a)))))")
				#expect(try expand("`{a b}") == "(clojure.core/apply clojure.core/hash-map (clojure.core/seq (clojure.core/concat (clojure.core/list (quote user/a)) (clojure.core/list (quote user/b)))))")
				#expect(try expand("`()") == "(clojure.core/list)")
				#expect(try expand("`[]") == "(clojure.core/apply clojure.core/vector (clojure.core/seq (clojure.core/concat)))")
				#expect(try expand("`(1 \"s\" :k \\c 2.5 nil true)") == "(clojure.core/seq (clojure.core/concat (clojure.core/list 1) (clojure.core/list \"s\") (clojure.core/list :k) (clojure.core/list \\c) (clojure.core/list 2.5) (clojure.core/list (quote nil)) (clojure.core/list (quote true))))")
				#expect(try expand("`'x") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote quote)) (clojure.core/list (quote user/x))))")
				#expect(try expand("'`x") == "(quote (quote user/x))")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func unquoteAndSplice() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try expand("`~x") == "x")
				#expect(try expand("`(a ~b ~@c)") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote user/a)) (clojure.core/list b) c))")
				#expect(try expand("`[~a ~@b]") == "(clojure.core/apply clojure.core/vector (clojure.core/seq (clojure.core/concat (clojure.core/list a) b)))")
				#expect(try expand("`{~k ~v}") == "(clojure.core/apply clojure.core/hash-map (clojure.core/seq (clojure.core/concat (clojure.core/list k) (clojure.core/list v))))")
				#expect(try expand("`(a ~(+ 1 2))") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote user/a)) (clojure.core/list (+ 1 2))))")
				#expect(try expand("`(~@a)") == "(clojure.core/seq (clojure.core/concat a))")
				#expect(try expand("`(a #_b c)") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote user/a)) (clojure.core/list (quote user/c))))")
				#expect(try expand("`~'x") == "(quote x)")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func autoGensym() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				let one = try Value(reading: "`(x# x# y#)").description
				let parts = one.split(separator: " ").filter { $0.contains("__auto__") }
				#expect(parts.count == 3)
				let names = parts.map { $0.trimmingCharacters(in: ["(", ")"]) }
				#expect(names[0] == names[1])
				#expect(names[0] != names[2])
				#expect(names[0].hasPrefix("x__") && names[0].hasSuffix("__auto__"))
				#expect(names[2].hasPrefix("y__") && names[2].hasSuffix("__auto__"))
				// A fresh table per syntax-quote, as in Clojure: nested ones and separate reads never share.
				let two = try Value(reading: "`(x#)").description
				#expect(two != "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote \(names[0])))))")
				let nested = try Value(reading: "`(x# `(x#))").description
				let inner = nested.split(separator: " ").filter { $0.contains("x__") }
				#expect(inner.count == 2)
				#expect(inner[0] != inner[1])
				#expect(inner[1].contains("user/x__"))
				#expect(try expand("`a#b") == "(quote user/a#b)")
				#expect(try expand("`ns/a#") == "(quote ns/a#)")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nestedSyntaxQuote() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try expand("``x") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote quote)) (clojure.core/list (quote user/x))))")
				#expect(try expand("`(a `(b ~c))") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote user/a)) (clojure.core/list (clojure.core/seq (clojure.core/concat (clojure.core/list (quote clojure.core/seq)) (clojure.core/list (clojure.core/seq (clojure.core/concat (clojure.core/list (quote clojure.core/concat)) (clojure.core/list (clojure.core/seq (clojure.core/concat (clojure.core/list (quote clojure.core/list)) (clojure.core/list (clojure.core/seq (clojure.core/concat (clojure.core/list (quote quote)) (clojure.core/list (quote user/b)))))))) (clojure.core/list (clojure.core/seq (clojure.core/concat (clojure.core/list (quote clojure.core/list)) (clojure.core/list (quote user/c)))))))))))))")
				#expect(try expand("`(a ~`b)") == "(clojure.core/seq (clojure.core/concat (clojure.core/list (quote user/a)) (clojure.core/list (quote user/b))))")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func errorsCarryPositions() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(readError("~x") == ReaderError(message: "Unquote outside syntax-quote", line: 1, column: 1))
				#expect(readError("(a\n  ~@b)") == ReaderError(message: "Unquote-splicing outside syntax-quote", line: 2, column: 3))
				#expect(readError("`~@x") == ReaderError(message: "splice not in list", line: 1, column: 1))
				#expect(readError("(1 `[~@x] 3)") == nil)
				#expect(readError("`(1 `[`~@x])") == ReaderError(message: "splice not in list", line: 1, column: 7))
				#expect(readError("`") == ReaderError(message: "EOF while reading", line: 1, column: 1))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func unqualifiedWithoutResolver() throws {
			let before = clj_debug_live_objects()
			do {
				var bytes = Array("`(a b#)".utf8)
				let text = try bytes.withUnsafeMutableBufferPointer { buf in
					try buf.withMemoryRebound(to: CChar.self) { chars in
						var reader = clj_reader()
						clj_reader_init(&reader, chars.baseAddress, chars.count)
						var raw: clj_value = CLJ_NIL
						try #require(clj_read(&reader, &raw) == CLJ_READ_OK)
						return Value(owning: raw).description
					}
				}
				#expect(text.hasPrefix("(clojure.core/seq (clojure.core/concat (clojure.core/list (quote a)) (clojure.core/list (quote b__"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func deepNestingUsesNoCStack() throws {
			let before = clj_debug_live_objects()
			do {
				let depth = 100_000
				let text = "`" + String(repeating: "[", count: depth) + String(repeating: "]", count: depth)
				let expanded = try Value(reading: text).description
				#expect(expanded.hasPrefix("(clojure.core/apply clojure.core/vector (clojure.core/seq (clojure.core/concat (clojure.core/list (clojure.core/apply clojure.core/vector"))
				#expect(expanded.hasSuffix("(clojure.core/seq (clojure.core/concat)))" + String(repeating: ")", count: 4 * (depth - 1))))
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
