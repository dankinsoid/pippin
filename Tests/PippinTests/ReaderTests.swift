// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

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
	(":", 1, 1, "Invalid token: :"),
	("a:", 1, 1, "Invalid token: a:"),
	("a::b", 1, 1, "Invalid token: a::b"),
	("foo/", 1, 1, "Invalid token: foo/"),
	("/foo", 1, 1, "Invalid token: /foo"),
	("foo/1", 1, 1, "Invalid token: foo/1"),
	("a:/b", 1, 1, "Invalid token: a:/b"),
	("1/0", 1, 1, "Invalid number: 1/0"),
	("1.5N", 1, 1, "Invalid number: 1.5N"),
	("1/2N", 1, 1, "Invalid number: 1/2N"),
	("0x1M", 1, 1, "Invalid number: 0x1M"),
	("1e", 1, 1, "Invalid number: 1e"),
	("1.2.3", 1, 1, "Invalid number: 1.2.3"),
	("1a", 1, 1, "Invalid number: 1a"),
	("1/a", 1, 1, "Invalid number: 1/a"),
	("#\"a(\"", 1, 1, "Unclosed group near index 2\na("),
	("#\"a", 1, 1, "EOF while reading regex"),
	("::no-such/a", 1, 1, "Invalid token: ::no-such/a"),
	("#(#(%))", 1, 3, "Nested #()s are not allowed"),
	("#(%0)", 1, 1, "arg literal must be %, %& or %integer"),
	("#(%x)", 1, 1, "arg literal must be %, %& or %integer"),
	("#?[:clj 1]", 1, 1, "read-cond body must be a list"),
	("#?(:clj)", 1, 1, "read-cond requires an even number of forms"),
	("#?(clj 1)", 1, 1, "Feature should be a keyword"),
	("#?@(:default [1])", 1, 1, "Reader conditional splicing not allowed at the top level."),
	("[#?@(:default 1)]", 1, 1, "Spliced form list in read-cond-splicing must implement ISequential"),
	("0x", 1, 1, "Invalid number: 0x"),
	("0x1G", 1, 1, "Invalid number: 0x1G"),
	("08", 1, 1, "Invalid number: 08"),
	("1r1", 1, 1, "Radix out of range: 1r1"),
	("#:a/b{:b 1}", 1, 1, "Namespaced map must specify a valid namespace: a/b"),
	("#cpp x", 1, 1, "No reader function for tag cpp"),
	("#?(:default #cpp x :jank 1)", 1, 13, "No reader function for tag cpp"),
	("#=(+ 1 2)", 1, 1, "Read-eval is not supported yet"),
	("^1 x", 1, 1, "Metadata must be Symbol,Keyword,String or Map"),
	("(a ^[] x)", 1, 4, "Metadata must be Symbol,Keyword,String or Map"),
	("^:a 1", 1, 1, "Metadata can only be applied to IMetas"),
	("^:a \"s\"", 1, 1, "Metadata can only be applied to IMetas"),
	("^:a :k", 1, 1, "Metadata can only be applied to IMetas"),
	("^:a nil", 1, 1, "Metadata can only be applied to IMetas"),
	("(^:a)", 1, 5, "Unmatched delimiter: )"),
	("^:a", 1, 1, "EOF while reading"),
	("^:a ^:b", 1, 5, "EOF while reading"),
	("#<x>", 1, 1, "Unreadable form"),
	("#inst \"20\"", 1, 1, "Unrecognized date/time syntax: 20"),
	("`~@x", 1, 1, "splice not in list"),
	("`(a `~@b)", 1, 5, "splice not in list"),
	("~x", 1, 1, "Unquote outside syntax-quote"),
	("(`a ~b)", 1, 5, "Unquote outside syntax-quote"),
	("(a ~@b)", 1, 4, "Unquote-splicing outside syntax-quote"),
	("`", 1, 1, "EOF while reading"),
	("`(a ~)", 1, 6, "Unmatched delimiter: )"),
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
		// @ai-generated(guided)
		@Test func fnLiteralsConditionalsAndRadixNumbers() throws {
			clj_init()
			for k in ["default", "nested", "user/a", "clojure.core/x", "clj", "cljs", "jank", "ok", "a", "ns"] { _ = kw(k) }
			_ = try cljEval("(alias 'rt-alias 'clojure.core)")
			let before = clj_debug_live_objects()
			do {
				#expect(try read("#(+ 1 %)").description.hasPrefix("(fn* [p1__"))
				#expect(try read("#(+ 1 %)").description.contains("] (+ 1 p1__"))
				#expect(try read("#(%2 %1)").list![1].array!.count == 2)
				#expect(try read("#(apply + % %&)").list![1].array!.map(\.description).joined(separator: " ").contains("& rest__"))
				#expect(try read("#()").description.hasSuffix(" ())"))
				#expect(try read("#([%] {%2 %} #{%})").list![1].array!.count == 2)
				#expect(try cljEval("(#(+ % 10) 5)") == 15)
				#expect(try cljEval("(#(vector %1 %2) 1 2)") == [1, 2])
				#expect(try cljEval("(#(apply + %&) 1 2 3)") == 6)
				#expect(try cljEval("(map #(* % %) [1 2 3])") == Value(list: [1, 4, 9]))
				#expect(try read("#?(:clj 1 :default 2)") == 2)
				#expect(try read("#?(:cljs 1) 7") == 7)
				#expect(readError("#?(:cljs 1)")?.message == "EOF while reading")
				#expect(try read("[1 #?(:cljs 2) 3]") == [1, 3])
				#expect(try read("[1 #?@(:default [2 3]) 4]") == [1, 2, 3, 4])
				#expect(try read("(a #?@(:cljs [x]) b)") == list(sym("a"), sym("b")))
				#expect(try read("[#?@(:default ()) 1]") == [1])
				#expect(try read("#?(:default #?(:default :nested))") == kw("nested"))
				// An unselected branch is data whatever it contains: a dispatch macro this reader has no support
				// for reads as nil there, as Clojure's suppressed read does.
				#expect(try read("#?(:jank #cpp (a b) :default 7)") == 7)
				#expect(try read("#?(:jank [#cpp x #js {:a 1}] :default [1 2])") == [1, 2])
				#expect(try read("#?(:jank #\"[a-z]+\" :default :ok)") == kw("ok"))
				#expect(try read("#?(:jank #:ns{:a 1} :default :ok)") == kw("ok"))
				#expect(try read("#?(:jank #=(+ 1 2) :default :ok)") == kw("ok"))
				#expect(try read("#?(:jank #?(:default #cpp x) :default :ok)") == kw("ok"))
				#expect(try read("[#?@(:jank [#cpp x 3] :default [1 2]) 9]") == [1, 2, 9])
				#expect(try read("#?(:jank #cpp x :default 2)") == 2)
				Runtime.readerFeatures = ["clj"]
				#expect(try read("#?(:cljs 1 :clj 2 :default 3)") == 2)
				#expect(Runtime.readerFeatures == ["clj"])
				// The branch a won feature makes unselected is suppressed too, even though its own feature matches.
				Runtime.readerFeatures = ["clj", "jank"]
				#expect(try read("#?(:clj 1 :jank #cpp x)") == 1)
				clj_reader_set_features(CLJ_NIL)
				#expect(try read("#?(:cljs 1 :clj 2 :default 3)") == 3)
				#expect(try read("::a") == kw("user/a"))
				#expect(try read("::rt-alias/x") == kw("clojure.core/x"))
				#expect(try read("::clojure.core/x") == kw("clojure.core/x"))
				#expect(try read("0x1F") == 31)
				#expect(try read("-0x10") == -16)
				#expect(try read("2r101") == 5)
				#expect(try read("36rZZ") == 1295)
				#expect(try read("017") == 15)
				#expect(try read("-017") == -15)
				#expect(try read("0") == 0)
				#expect(try read("00") == 0)
			}
			#expect(clj_debug_live_objects() == before)
		}

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
				#expect(try read("(1 2 3)").typeName == "list")
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
				#expect(try read("#'x") == list(sym("var"), sym("x")))
				#expect(try read("#'ns/x") == list(sym("var"), sym("ns/x")))
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
			clj_init()
			// A pattern's syntax error carries them in its ex-data, and interning is permanent; so are the cases' own keywords.
			for k in ["pattern", "offset", "a", "b", "k", "clj", "default", "jank", "a/b", "no-such/a"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			#expect(readError(text) == ReaderError(message: message, line: line, column: column), "\(text.debugDescription)")
			#expect(clj_debug_live_objects() == before)
		}

		// @ai-generated(guided)
		@Test func metadata() throws {
			for k in ["a", "b", "tag", "line", "column", "k"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				func meta(_ text: String) throws -> Value { try read(text).meta }
				#expect(try meta("^{:a 1} x") == read("{:a 1}"))
				#expect(try meta("^:a x") == read("{:a true}"))
				#expect(try meta("^Sym x") == read("{:tag Sym}"))
				#expect(try meta("^\"str\" x") == read("{:tag \"str\"}"))
				#expect(try meta("#^:a x") == read("{:a true}"))
				#expect(try read("^:a x") == sym("x"))
				#expect(try meta("^:a [1 2]") == read("{:a true}"))
				#expect(try meta("^:a {}") == read("{:a true}"))
				#expect(try meta("^:a ()") == read("{:a true}"))
				#expect(try meta("^:a (fn)") == read("{:a true :line 1 :column 5}"))
				#expect(try read("^:a (fn)") == list(sym("fn")))
				// Stacked: the outer keys win, as in LispReader.
				#expect(try meta("^:a ^:b x") == read("{:a true :b true}"))
				#expect(try meta("^{:a 1} ^{:a 2 :b 2} x") == read("{:a 1 :b 2}"))
				#expect(try meta("^{:a 1} ^:b ^Sym x") == read("{:a 1 :b true :tag Sym}"))
				#expect(try meta("^:a #_(x) y") == read("{:a true}"))
				#expect(try read("'^:a x") == list(sym("quote"), sym("x")))
				#expect(try read("'^:a x").list?[1].meta == read("{:a true}"))
				#expect(try meta("[^:a x]").isNil == true)
				#expect(try read("[^:a x]").array?[0].meta == read("{:a true}"))
				// Every non-empty list carries the position of its opening paren; an explicit :line overrides it.
				#expect(try meta("(a b)") == read("{:line 1 :column 1}"))
				#expect(try meta("\n  (a)") == read("{:line 2 :column 3}"))
				#expect(try read("(a\n (b\n  (c)))").list?[1].meta == read("{:line 2 :column 2}"))
				#expect(try read("(a\n (b\n  (c)))").list?[1].list?[1].meta == read("{:line 3 :column 3}"))
				#expect(try meta("^{:line 9} (a)") == read("{:line 9 :column 12}"))
				#expect(try meta("()").isNil == true)
				#expect(try meta("[a]").isNil == true)
				#expect(try meta("x").isNil == true)
				#expect(try meta("'x").isNil == true)
				#expect(try read("'x").list?[1].meta.isNil == true)
				#expect(try read("(1 2)") == read("(1 2)"))
				#expect(try read("(1 2)").hashValue == read("\n\n (1 2)").hashValue)
				#expect(try read("(1 2)").description == "(1 2)")
				#expect(try read("(a b)").list?.map(\.meta) == [nil, nil])
				#expect(try read("`(a b)").meta.isNil == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func truncatedInputFailsInReadAll() {
			_ = kw("a")
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
