// @ai-generated(solo)
import CljCore
import Testing
@testable import Clojure

private func read(_ s: String) throws -> Value { try Value(reading: s) }

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

private func sym(_ s: String) -> Value { Value(symbol: s) }
private func kw(_ s: String) -> Value { Value(keyword: s) }
private func list(_ xs: Value...) -> Value { Value(list: xs) }

private let errorCases: [(text: String, line: Int, column: Int, message: String)] = [
	(")", 1, 1, "Unmatched delimiter: )"),
	("(1 2]", 1, 5, "Unmatched delimiter: ]"),
	("[1\n 2\n}", 3, 1, "Unmatched delimiter: }"),
	("(1 2", 1, 1, "EOF while reading"),
	("  [1\n (2", 2, 2, "EOF while reading"),
	("'", 1, 1, "EOF while reading"),
	("#_ 1", 1, 1, "EOF while reading"),
	("\"abc", 1, 1, "EOF while reading string"),
	("\"a\\", 1, 1, "EOF while reading string"),
	("\\", 1, 1, "EOF while reading character"),
	("#", 1, 1, "EOF while reading dispatch character"),
	("\"a\\q\"", 1, 3, "Unsupported escape character: \\q"),
	("\"\\u12\"", 1, 2, "Invalid unicode escape: expected 4 hex digits"),
	("\"\\ud800\"", 1, 2, "Invalid unicode escape: lone surrogate \\ud800"),
	("\"\\777\"", 1, 2, "Octal escape sequence must be in range [0, 377]"),
	("\\newlin", 1, 1, "Unsupported character: \\newlin"),
	("\\u12", 1, 1, "Unsupported character: \\u12"),
	("\\uzzzz", 1, 1, "Unsupported character: \\uzzzz"),
	("\\ud800", 1, 1, "Invalid character constant: \\ud800"),
	("\\o777", 1, 1, "Octal escape sequence must be in range [0, 377]"),
	("{:a 1 :b}", 1, 1, "Map literal must contain an even number of forms"),
	("\n {:a 1, :a 2}", 2, 2, "Duplicate key: :a"),
	("{[1 2] 1 (1 2) 2}", 1, 1, "Duplicate key: (1 2)"),
	("::a", 1, 1, "Auto-resolved keywords (::) need a current namespace, not supported yet: ::a"),
	(":", 1, 1, "Invalid token: :"),
	("a:", 1, 1, "Invalid token: a:"),
	("a::b", 1, 1, "Invalid token: a::b"),
	("foo/", 1, 1, "Invalid token: foo/"),
	("/foo", 1, 1, "Invalid token: /foo"),
	("foo/1", 1, 1, "Invalid token: foo/1"),
	("a:/b", 1, 1, "Invalid token: a:/b"),
	("9223372036854775807", 1, 1, "Integer out of fixnum range, bigint is not supported yet: 9223372036854775807"),
	("4611686018427387904", 1, 1, "Integer out of fixnum range, bigint is not supported yet: 4611686018427387904"),
	("-4611686018427387905", 1, 1, "Integer out of fixnum range, bigint is not supported yet: -4611686018427387905"),
	("99999999999999999999999", 1, 1, "Integer out of fixnum range, bigint is not supported yet: 99999999999999999999999"),
	("0x1F", 1, 1, "Hex literals are not supported yet: 0x1F"),
	("2r101", 1, 1, "Radix literals are not supported yet: 2r101"),
	("36rZZ", 1, 1, "Radix literals are not supported yet: 36rZZ"),
	("1/2", 1, 1, "Ratios are not supported yet: 1/2"),
	("42N", 1, 1, "BigInt literals (N suffix) are not supported yet: 42N"),
	("1.5M", 1, 1, "BigDecimal literals (M suffix) are not supported yet: 1.5M"),
	("017", 1, 1, "Octal literals are not supported yet: 017"),
	("1e", 1, 1, "Invalid number: 1e"),
	("1.2.3", 1, 1, "Invalid number: 1.2.3"),
	("1a", 1, 1, "Invalid number: 1a"),
	("1/a", 1, 1, "Invalid number: 1/a"),
	("(1\n  #{2})", 2, 3, "Set literals are not supported yet"),
	("#(+ 1 %)", 1, 1, "Anonymous function literals are not supported yet"),
	("#\"re\"", 1, 1, "Regex literals are not supported yet"),
	("#'x", 1, 1, "Var quote is not supported yet"),
	("#:a{:b 1}", 1, 1, "Namespaced map literals are not supported yet"),
	("#?(:clj 1)", 1, 1, "Reader conditionals are not supported yet"),
	("#=(+ 1 2)", 1, 1, "Read-eval is not supported yet"),
	("#^{} x", 1, 1, "Metadata is not supported yet"),
	("#<x>", 1, 1, "Unreadable form"),
	("#inst \"2020\"", 1, 1, "Tagged literals are not supported yet"),
	("^:a x", 1, 1, "Metadata is not supported yet"),
	("`x", 1, 1, "Syntax-quote is not supported yet"),
	("~x", 1, 1, "Unquote is not supported yet"),
	("(a ~@b)", 1, 4, "Unquote is not supported yet"),
	("##Foo", 1, 1, "Unknown symbolic value: ##Foo"),
	("1 2", 1, 3, "Unexpected trailing input"),
	("1 )", 1, 3, "Unmatched delimiter: )"),
	("λ )", 1, 3, "Unmatched delimiter: )"),
	("\"λ🙂\" )", 1, 6, "Unmatched delimiter: )"),
	("", 1, 1, "EOF while reading"),
	("  ; only a comment\n", 1, 1, "EOF while reading"),
	("#_ 1 #_ 2", 1, 1, "EOF while reading"),
]

extension CoreTests {
	@Suite struct ReaderTests {
		@Test func scalars() throws {
			for k in ["a", "ns/a", "a.b/c-d?", "/"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try read("nil") == nil)
				#expect(try read("true") == true)
				#expect(try read("false") == false)
				#expect(try read("0") == 0)
				#expect(try read("42") == 42)
				#expect(try read("+5") == 5)
				#expect(try read("-5") == -5)
				#expect(try read("-0") == 0)
				#expect(try read("4611686018427387903").int == Value.fixnumRange.upperBound)
				#expect(try read("-4611686018427387904").int == Value.fixnumRange.lowerBound)
				#expect(try read("1.0") == 1.0)
				#expect(try read("1.") == 1.0)
				#expect(try read("-2.5") == -2.5)
				#expect(try read("+2.5") == 2.5)
				#expect(try read("1e3") == 1000.0)
				#expect(try read("1E3") == 1000.0)
				#expect(try read("1.5E-3") == 0.0015)
				#expect(try read("1.5e+3") == 1500.0)
				#expect(try read("##Inf").double == .infinity)
				#expect(try read("##-Inf").double == -.infinity)
				#expect(try read("##NaN").double?.isNaN == true)
				#expect(try read("\"\"") == "")
				#expect(try read("\"hello\"") == "hello")
				#expect(try read("\"a\\\"b\\\\c\\nd\\te\\rf\\bg\\fh\"") == "a\"b\\c\nd\te\rf\u{8}g\u{C}h")
				#expect(try read("\"\\u03bb\\u00e9\"") == "λé")
				#expect(try read("\"\\ud83d\\ude42\"") == "🙂")
				#expect(try read("\"\\101\\60\"") == "A0")
				#expect(try read("\"λ→🙂\"") == "λ→🙂")
				#expect(try read("\"multi\nline\"") == "multi\nline")
				#expect(try read("\\a").scalar == "a")
				#expect(try read("\\λ").scalar == "λ")
				#expect(try read("\\🙂").scalar == "🙂")
				#expect(try read("\\newline").scalar == "\n")
				#expect(try read("\\space").scalar == " ")
				#expect(try read("\\tab").scalar == "\t")
				#expect(try read("\\backspace").scalar == "\u{8}")
				#expect(try read("\\formfeed").scalar == "\u{C}")
				#expect(try read("\\return").scalar == "\r")
				#expect(try read("\\u03BB").scalar == "λ")
				#expect(try read("\\o101").scalar == "A")
				#expect(try read("\\(").scalar == "(")
				#expect(try read("\\\\").scalar == "\\")
				#expect(try read("\\\"").scalar == "\"")
				#expect(try read("\\;").scalar == ";")
				#expect(try read(":a") == kw("a"))
				#expect(try read(":ns/a") == kw("ns/a"))
				#expect(try read(":a.b/c-d?") == kw("a.b/c-d?"))
				#expect(try read(":/") == kw("/"))
				#expect(try read("a") == sym("a"))
				#expect(try read("ns/name") == sym("ns/name"))
				#expect(try read("/") == sym("/"))
				#expect(try read("clojure.core//") == sym("clojure.core//"))
				#expect(try read("+") == sym("+"))
				#expect(try read("-") == sym("-"))
				#expect(try read(".") == sym("."))
				#expect(try read("..") == sym(".."))
				#expect(try read(".5") == sym(".5"))
				#expect(try read("a#") == sym("a#"))
				#expect(try read("a'b") == sym("a'b"))
				#expect(try read("%") == sym("%"))
				#expect(try read("a/b/c") == sym("a/b/c"))
				#expect(try read("λ→") == sym("λ→"))
				#expect(try read("nil?") == sym("nil?"))
				#expect(try read("true-ish") == sym("true-ish"))
				#expect(try read("nil").isNil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func collectionsAndWrappers() throws {
			for k in ["a", "b", "k"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try read("()") == Value(list: []))
				#expect(try read("()").raw == clj_list_empty())
				#expect(try read("(1 2 3)") == list(1, 2, 3))
				#expect(try read("(1 2 3)").typeName == "cons")
				#expect(try read("[]") == Value([]))
				#expect(try read("[1 [2 [3]]]") == [1, [2, [3]]])
				#expect(try read("[1 [2 [3]]]").typeName == "vector")
				#expect(try read("{}").raw == clj_map_empty())
				let m = try read("{:a 1, :b [2]}")
				withExtendedLifetime(m) {
					#expect(m.typeName == "map")
					#expect(clj_map_count(m.raw) == 2)
					#expect(Value(borrowing: clj_map_get(m.raw, kw("a").raw, CLJ_NIL)) == 1)
					#expect(Value(borrowing: clj_map_get(m.raw, kw("b").raw, CLJ_NIL)) == [2])
				}
				#expect(try read("{nil 1}") == read("{nil 1}"))
				#expect(try read("(1,2,,3)") == list(1, 2, 3))
				#expect(try read(" \t\n\r\u{C}(1\n2) ; trailing\n") == list(1, 2))
				#expect(try read("(1 ; comment )\n 2)") == list(1, 2))
				#expect(try read("#!/usr/bin/env clj\n7") == 7)
				#expect(try read("'x") == list(sym("quote"), sym("x")))
				#expect(try read("'(1)") == list(sym("quote"), list(1)))
				#expect(try read("''x") == list(sym("quote"), list(sym("quote"), sym("x"))))
				#expect(try read("@x") == list(sym("clojure.core/deref"), sym("x")))
				#expect(try read("'@x") == list(sym("quote"), list(sym("clojure.core/deref"), sym("x"))))
				#expect(try read("[1 #_2 3]") == [1, 3])
				#expect(try read("[1 #_(2 [3]) 4]") == [1, 4])
				#expect(try read("[#_ #_ 1 2 3]") == [3])
				#expect(try read("#_1 2") == 2)
				#expect(try read("#_ (1 2) 3") == 3)
				#expect(try read("'#_1 x") == list(sym("quote"), sym("x")))
				#expect(try read("(a(b)c[d]e{f g}h\"s\"i\\j :k)") ==
					list(sym("a"), list(sym("b")), sym("c"), [sym("d")], sym("e"), read("{f g}"), sym("h"), "s", sym("i"), Value("j" as Unicode.Scalar), kw("k")))
				#expect(try read("(1 2 3)") == [1, 2, 3])
				#expect(try read("(1 2 3)").hashValue == Value([1, 2, 3]).hashValue)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test(arguments: errorCases) func errors(text: String, line: Int, column: Int, message: String) {
			let before = clj_debug_live_objects()
			#expect(readError(text) == ReaderError(message: message, line: line, column: column), "\(text.debugDescription)")
			#expect(clj_debug_live_objects() == before)
		}

		@Test func truncatedInputFailsInReadAll() {
			let before = clj_debug_live_objects()
			for text in ["(", "\"", "#", "{", "{:a", "[1 2", "'(1 2", "@", "#_", "#_ #_ 1", "1 2 (", "\\", "##"] {
				#expect(throws: ReaderError.self, Comment(rawValue: text.debugDescription)) { try Value.readAll(text) }
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func readAllAndPositions() throws {
			_ = kw("k")
			let before = clj_debug_live_objects()
			do {
				#expect(try Value.readAll("") == [])
				#expect(try Value.readAll("  ; nothing\n") == [])
				#expect(try Value.readAll("1 :k (a) \"s\" [x]\n#_ skipped 'q") ==
					[1, kw("k"), list(sym("a")), "s", [sym("x")], list(sym("quote"), sym("q"))])
				let text = "  1\n(2\n 3) ; c\n\n  #_ 4 [5]\n\"λ\" :k"
				var bytes = Array(text.utf8)
				bytes.withUnsafeMutableBufferPointer { buf in
					buf.withMemoryRebound(to: CChar.self) { chars in
						var r = clj_reader()
						clj_reader_init(&r, chars.baseAddress, chars.count)
						var positions: [(UInt32, UInt32)] = []
						var values: [Value] = []
						var raw: clj_value = CLJ_NIL
						while clj_read(&r, &raw) == CLJ_READ_OK {
							positions.append((r.form_line, r.form_col))
							values.append(Value(owning: raw))
						}
						#expect(values == [1, list(2, 3), [5], "λ", kw("k")])
						#expect(positions.map(\.0) == [1, 2, 5, 6, 6])
						#expect(positions.map(\.1) == [3, 1, 8, 1, 5])
						#expect(clj_read(&r, &raw) == CLJ_READ_EOF)
					}
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func deepNestingUsesNoCStack() throws {
			let before = clj_debug_live_objects()
			do {
				let depth = 200_000
				let text = String(repeating: "[", count: depth) + String(repeating: "]", count: depth)
				let v = try read(text)
				var cur = v
				var d = 0
				while let inner = cur.array, !inner.isEmpty {
					cur = inner[0]
					d += 1
				}
				#expect(d == depth - 1)
				#expect(v.description == text)
				let lists = String(repeating: "(", count: depth) + String(repeating: ")", count: depth)
				#expect(try read(lists).description == lists)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func hundredThousandElementList() throws {
			let before = clj_debug_live_objects()
			do {
				let n = 100_000
				let text = "(" + (0..<n).map(String.init).joined(separator: " ") + ")"
				let v = try read(text)
				#expect(v.list?.count == n)
				#expect(v.list?.last == Value(n - 1))
				#expect(v.description == text)
				#expect(try read(v.description) == v)
				#expect(v == Value((0..<n).map { Value($0) }))
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
