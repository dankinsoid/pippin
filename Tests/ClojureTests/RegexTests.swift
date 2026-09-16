// @ai-generated(solo)
import CljCore
import Testing
@testable import Clojure

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func printed(_ source: String) throws -> String {
	try eval("(pr-str \(source))").string!
}

// One expected result per form, derived from what JVM Clojure answers for it.
struct RegexCase: CustomStringConvertible, Sendable {
	let form: String
	let want: String
	var description: String { form }
}

private func c(_ form: String, _ want: String) -> RegexCase { RegexCase(form: form, want: want) }

// The differential table: `(pr-str form)` against the JVM's answer, one row per syntax element.
private let cases: [RegexCase] = [
	// literals, escapes and .
	c(#"(re-find #"abc" "xxabcxx")"#, #""abc""#),
	c(#"(re-find #"a\.c" "a.c")"#, #""a.c""#),
	c(#"(re-find #"a\.c" "abc")"#, "nil"),
	c(#"(re-find #"a.c" "abc")"#, #""abc""#),
	c(#"(re-find #"a.c" "a\nc")"#, "nil"),
	c(#"(re-find #"(?s)a.c" "a\nc")"#, #""a\nc""#),
	c(#"(re-find #"\t" "a\tb")"#, #""\t""#),
	c(#"(re-find #"\n" "a\nb")"#, #""\n""#),
	c(#"(re-find #"\r" "a\rb")"#, #""\r""#),
	c(#"(re-find #"\f" "a\fb")"#, #""\f""#),
	c(#"(re-find #"\x41" "xAy")"#, #""A""#),
	c(#"(re-find #"\x{1F600}" "a😀b")"#, #""😀""#),
	c(#"(re-find (re-pattern "\\u0041") "xAy")"#, #""A""#),
	c(#"(re-find #"\Qa+b\E" "xa+by")"#, #""a+b""#),
	c(#"(re-find #"\Qa+b\Ec" "a+bc")"#, #""a+bc""#),
	c(#"(re-find #"a\"b" "xa\"by")"#, #""a\"b""#),
	// classes
	c(#"(re-find #"[abc]+" "xxbcax")"#, #""bca""#),
	c(#"(re-find #"[a-c]+" "xxbcax")"#, #""bca""#),
	c(#"(re-find #"[^a-c]+" "aabxxcc")"#, #""xx""#),
	c(#"(re-find #"[a-c[x-z]]+" "qazx")"#, #""azx""#),
	c(#"(re-find #"[a-z&&[^aeiou]]+" "aeiobcd")"#, #""bcd""#),
	c(#"(re-find #"[\d\s]+" "ab 12 cd")"#, #"" 12 ""#),
	c(#"(re-find #"[\D]+" "12ab34")"#, #""ab""#),
	c(#"(re-find #"[-a]+" "x-ay")"#, #""-a""#),
	c(#"(re-find #"[a-]+" "xa-y")"#, #""a-""#),
	// predefined classes
	c(#"(re-find #"\d+" "ab123cd")"#, #""123""#),
	c(#"(re-find #"\D+" "12ab34")"#, #""ab""#),
	c(#"(re-find #"\w+" " a_1 ")"#, #""a_1""#),
	c(#"(re-find #"\W+" "ab  cd")"#, #""  ""#),
	c(#"(re-find #"\s+" "ab \t cd")"#, #"" \t ""#),
	c(#"(re-find #"\S+" "  ab  ")"#, #""ab""#),
	c(#"(re-find #"\bcat\b" "a cat here")"#, #""cat""#),
	c(#"(re-find #"\bcat\b" "concatenate")"#, "nil"),
	c(#"(re-find #"\Bat" "concatenate")"#, #""at""#),
	c(#"(re-find #"\p{L}+" "12ab34")"#, #""ab""#),
	c(#"(re-find #"\p{Lu}+" "abCDef")"#, #""CD""#),
	c(#"(re-find #"\p{N}+" "ab12")"#, #""12""#),
	c(#"(re-find #"\p{Alpha}+" "12ab")"#, #""ab""#),
	c(#"(re-find #"\p{Digit}+" "ab12")"#, #""12""#),
	c(#"(re-find #"\p{Space}+" "ab cd")"#, #"" ""#),
	c(#"(re-find #"\P{Digit}+" "12ab")"#, #""ab""#),
	// anchors
	c(#"(re-find #"^ab" "abc")"#, #""ab""#),
	c(#"(re-find #"^bc" "abc")"#, "nil"),
	c(#"(re-find #"bc$" "abc")"#, #""bc""#),
	c(#"(re-find #"(?m)^b" "a\nbc")"#, #""b""#),
	c(#"(re-find #"(?m)b$" "ab\nc")"#, #""b""#),
	c(#"(re-find #"\Aab" "abc")"#, #""ab""#),
	c(#"(re-find #"bc\z" "abc")"#, #""bc""#),
	c(#"(re-find #"bc\z" "abc\n")"#, "nil"),
	c(#"(re-find #"bc\Z" "abc\n")"#, #""bc""#),
	// groups
	c(#"(re-find #"(a)(b)" "xaby")"#, #"["ab" "a" "b"]"#),
	c(#"(re-find #"(?:ab)+" "xababy")"#, #""abab""#),
	c(#"(re-find #"(a)|(b)" "b")"#, #"["b" nil "b"]"#),
	c(#"(re-find #"(?<d>\d+)" "x42")"#, #"["42" "42"]"#),
	c(#"(re-find #"a(?=b)" "ab ac")"#, #""a""#),
	c(#"(re-find #"a(?!b)" "ab ac")"#, #""a""#),
	c(#"(re-find #"(?<=a)b" "cb ab")"#, #""b""#),
	c(#"(re-find #"(?<!a)b" "ab cb")"#, #""b""#),
	c(#"(re-find #"(?>a+)b" "aaab")"#, #""aaab""#),
	// alternation and quantifiers
	c(#"(re-find #"ab|cd|ef" "xxefxx")"#, #""ef""#),
	c(#"(re-find #"a*" "aaa")"#, #""aaa""#),
	c(#"(re-find #"a*" "b")"#, #""""#),
	c(#"(re-find #"a+" "aaa")"#, #""aaa""#),
	c(#"(re-find #"a?b" "ab")"#, #""ab""#),
	c(#"(re-find #"a{3}" "aaaa")"#, #""aaa""#),
	c(#"(re-find #"a{2,}" "aaaa")"#, #""aaaa""#),
	c(#"(re-find #"a{2,3}" "aaaa")"#, #""aaa""#),
	c(#"(re-find #"a{2,3}?" "aaaa")"#, #""aa""#),
	c(#"(re-find #"a+?b" "aaab")"#, #""aaab""#),
	c(#"(re-find #"a*?" "aaa")"#, #""""#),
	c(#"(re-find #"a++b" "aaab")"#, #""aaab""#),
	c(#"(re-find #"a?+a" "aa")"#, #""aa""#),
	c(#"(re-matches #"(a*)*" "aaa")"#, #"["aaa" "aaa"]"#),
	c(#"(re-find #"a{,2}" "a{,2}")"#, #""a{,2}""#),
	// backreferences
	c(#"(re-find #"(ab)\1" "xabab")"#, #"["abab" "ab"]"#),
	c(#"(re-find #"(ab)\1" "xabx")"#, "nil"),
	c(#"(re-find #"(?<x>ab)\k<x>" "abab")"#, #"["abab" "ab"]"#),
	c(#"(re-find #"(?i)(ab)\1" "abAB")"#, #"["abAB" "ab"]"#),
	// flags
	c(#"(re-find #"(?i)ABC" "xabc")"#, #""abc""#),
	c(#"(re-find #"(?i)[a-z]+" "XY")"#, #""XY""#),
	c(#"(re-find #"(?i)[^a]" "A")"#, "nil"),
	c(#"(re-find #"a(?i:BC)" "abc")"#, #""abc""#),
	c(#"(re-find #"a(?i:BC)d" "aBCD")"#, "nil"),
	c(#"(re-find #"(?i)a(?-i:b)c" "ABC")"#, "nil"),
	c(#"(re-find #"(?x) a b  # trailing\#n c" "abc")"#, #""abc""#),
	// re-matches and re-seq
	c(#"(re-matches #"a+" "aaa")"#, #""aaa""#),
	c(#"(re-matches #"a+" "aaab")"#, "nil"),
	c(#"(re-matches #"(a+)(b+)" "aabb")"#, #"["aabb" "aa" "bb"]"#),
	c(#"(re-matches #"a|ab" "ab")"#, #""ab""#),
	c(#"(vec (re-seq #"\d+" "1 22 333"))"#, #"["1" "22" "333"]"#),
	c(#"(vec (re-seq #"(\d)(\d)" "1234"))"#, #"[["12" "1" "2"] ["34" "3" "4"]]"#),
	c(#"(vec (re-seq #"" "ab"))"#, #"["" "" ""]"#),
	c(#"(count (re-seq #"a" "bbb"))"#, "0"),
	// code points, not UTF-16 units
	c(#"(re-find #"." "😀")"#, #""😀""#),
	c(#"(re-matches #"^.$" "😀")"#, #""😀""#),
	c(#"(re-find #"😀+" "x😀😀y")"#, #""😀😀""#),
	// pattern values
	c(#"(pr-str #"a+\d")"#, ##""#\"a+\\d\"""##),
	c(#"(str #"a+")"#, #""a+""#),
	c(#"[(regex? #"a") (regex? "a") (regex? nil)]"#, "[true false false]"),
	c(#"(= #"ab" #"ab" (re-pattern "ab"))"#, "true"),
	c(#"(= #"ab" #"ba")"#, "false"),
	c(#"(= (hash #"ab") (hash (re-pattern "ab")))"#, "true"),
	c(#"(count (set [#"a" #"a" #"b"]))"#, "2"),
	c(#"(type #"a")"#, "regex"),
	c(#"(= Pattern (type #"a"))"#, "true"),
	// clojure.string
	c(#"(str/split "a,b,c" #",")"#, #"["a" "b" "c"]"#),
	c(#"(str/split "a1b22c" #"\d+")"#, #"["a" "b" "c"]"#),
	c(#"(str/split "a,b,c" #"," 2)"#, #"["a" "b,c"]"#),
	c(#"(str/split "a,b,,," #",")"#, #"["a" "b"]"#),
	c(#"(str/split "a,b,,," #"," -1)"#, #"["a" "b" "" "" ""]"#),
	c(#"(str/split "abc" #"")"#, #"["a" "b" "c"]"#),
	c(#"(str/split "" #",")"#, #"[""]"#),
	c(#"(str/split "abc" #"x")"#, #"["abc"]"#),
	c(#"(str/split-lines "a\nb\r\nc")"#, #"["a" "b" "c"]"#),
	c(#"(str/replace "The color is red" #"red" "blue")"#, #""The color is blue""#),
	c(#"(str/replace "fabulous fodder foo food" #"f(o+)(\S+)" "$2$1")"#, #""fabulous ddero oo doo""#),
	c(#"(str/replace "a b a" #"a|b" {"a" "1" "b" "2"})"#, #""1 2 1""#),
	c(#"(str/replace "hello world" #"\b." str/upper-case)"#, #""Hello World""#),
	c(#"(str/replace "hello world" #"\b(.)" (comp str/upper-case first))"#, #""Hello World""#),
	c(#"(str/replace "/my/dir/path/" #".$" "")"#, #""/my/dir/path""#),
	c(#"(str/replace "xxx" #".*" "")"#, #""""#),
	c(#"(str/replace "x" #"" "y")"#, #""yxy""#),
	c(#"(str/replace "a-b" #"-" (str/re-quote-replacement "$1"))"#, #""a$1b""#),
	c(#"(str/replace "x42y" #"(?<n>\d+)" "[${n}]")"#, #""x[42]y""#),
	c(#"(str/replace-first "aaa" #"a" "b")"#, #""baa""#),
	c(#"(str/replace-first "a1b2" #"\d" (fn [m] (str "<" m ">")))"#, #""a<1>b2""#),
	c(#"(str/replace "Vegeta" #"Goku" "Gohan")"#, #""Vegeta""#),
	c(#"(str/re-quote-replacement "a$1\\b")"#, #""a\\$1\\\\b""#),
	// the literal-string fast path is unchanged
	c(#"(str/split "a,b" ",")"#, #"["a" "b"]"#),
	c(#"(str/split "a,b" \,)"#, #"["a" "b"]"#),
	c(#"(str/replace "xx" "x" "y")"#, #""yy""#),
	c(#"(str/replace "xx" \x \y)"#, #""yy""#),
	c(#"(str/replace-first "xx" "x" "y")"#, #""yx""#),
	c(#"(str/replace ":foo" "foo" "bar")"#, #"":bar""#),
	c(#"(str/replace :foo "foo" "bar")"#, #"":bar""#),
]

extension CoreTests {
	@Suite struct RegexTests {
		init() {
			clj_init()
			// Interning is permanent and shows in clj_debug_live_objects, so it happens before any baseline.
			for k in ["a", "b", "n", "pattern", "offset", "d", "x"] { _ = Value(keyword: k) }
			_ = try? cljEval("(require '[clojure.string :as str])")
		}

		@Test(arguments: cases) func differential(row: RegexCase) throws {
			clj_init()
			_ = try eval("(require '[clojure.string :as str])")
			#expect(try printed(row.form) == row.want, "\(row.form)")
		}

		@Test func patternsAreLiveObjects() {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				let text = clj_string_from_cstr("a(b)c")
				let p = clj_regex_new(text)
				clj_release(text)
				#expect(clj_is_regex(p))
				#expect(String(cString: clj_type_name(p)) == "regex")
				#expect(clj_regex_group_count(p) == 1)
				#expect(String(cString: clj_string_bytes(clj_regex_pattern(p))) == "a(b)c")
				let input = clj_string_from_cstr("xa bc abc")
				let m = clj_matcher_new(p, input)
				clj_release(input)
				#expect(clj_is_matcher(m))
				let found = clj_matcher_find(m)
				#expect(Value(owning: found).description == #"["abc" "b"]"#)
				#expect(Value(owning: clj_matcher_group(m, 1)).description == #""b""#)
				#expect(Value(owning: clj_matcher_find(m)) == nil)
				clj_release(m)
				clj_release(p)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func theMatcherIsStatefulAndIndexable() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("""
				(let [m (re-matcher #"(\\d+),(\\d+),(\\d+)" "123,456,789")]
				  (re-find m)
				  [(nth m 0) (nth m 2) (nth m 3) (re-groups m)])
				""").description == #"["123,456,789" "456" "789" ["123,456,789" "123" "456" "789"]]"#)
				#expect(message("(let [m (re-matcher #\"(\\d+)\" \"1\")] (re-find m) (nth m 10))") == "Index 10 out of bounds for length 2")
				#expect(message("(re-groups (re-matcher #\"a\" \"a\"))") == "No match found")
				// A matcher walks its input once: the second pass over the same one finds nothing.
				#expect(try eval("""
				(let [m (re-matcher #"a" "aa")] [(re-find m) (re-find m) (re-find m) (re-find m)])
				""").description == #"["a" "a" nil nil]"#)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func compileErrorsCarryThePatternAndOffset() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(message("(re-pattern \"a(b\")") == "Unclosed group near index 3\\na(b")
				#expect(message("(re-pattern \"a)\")") == "Unmatched closing ')' near index 1\\na)")
				#expect(message("(re-pattern \"[a\")") == "Unclosed character class near index 2\\n[a")
				#expect(message("(re-pattern \"*a\")") == "Dangling meta character near index 0\\n*a")
				#expect(message("(re-pattern \"a{2,1}\")") == "Illegal repetition range near index 1\\na{2,1}")
				#expect(message("(re-pattern \"\\\\p{Nope}\")") == "Unknown character property name near index 3\\n\\\\p{Nope}")
				#expect(message("(re-pattern \"a\\\\q\")") == "Illegal/unsupported escape sequence near index 1\\na\\\\q")
				#expect(message("(re-pattern \"(?<=a+)b\")") == "Look-behind group does not have an obvious maximum length near index 7\\n(?<=a+)b")
				#expect(message("(re-pattern \"\\\\k<none>\")") == "Unknown group name near index 3\\n\\\\k<none>")
				#expect(message("(re-pattern \"\\\\1\")") == "No such group near index 0\\n\\\\1")
				#expect(message("(re-pattern \"(?i)*\")") == "Dangling meta character near index 5\\n(?i)*")
				#expect(message("(re-pattern \"a{5001}\")") == "Repetition count too large near index 1\\na{5001}")
				#expect(message("(re-pattern \"(?z)a\")") == "Unknown inline modifier near index 2\\n(?z)a")
				#expect(try eval("(ex-data (try (re-pattern \"a(b\") (catch :default e e)))").description
					== "{:offset 3, :pattern \"a(b\"}")
				#expect(message("(re-pattern 1)") == "long cannot be cast to a pattern")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func argumentErrors() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(message("(re-find \"a\" \"a\")") == "re-find expects a pattern, got: string")
				#expect(message("(re-find #\"a\" 1)") == "re-find expects a string, got: long")
				#expect(message("(re-matches #\"a\" nil)") == "re-matches expects a string, got: nil")
				#expect(message("(re-matcher \"a\" \"a\")") == "re-matcher expects a pattern, got: string")
				#expect(message("(re-groups \"a\")") == "re-groups expects a matcher, got: string")
				_ = try eval("(require '[clojure.string :as str])")
				#expect(message("(str/replace nil \"x\" \"y\")") == "Cannot convert nil to a string")
				#expect(message("(str/replace \"\" \"x\" \\y)") == "replace: invalid replacement arg: \\\\y")
				#expect(message("(str/replace \"\" \\x \"y\")") == "replace: invalid replacement arg: \\\"y\\\"")
				#expect(message("(str/replace \"\" 1 \"y\")") == "replace: invalid match arg: 1")
				#expect(message("(str/replace \"a\" #\".\" (constantly \\1))") == "char cannot be cast to a string")
				#expect(message("(str/replace \"a\" #\".\" 1)") == "replace expects a function, got: long")
				#expect(message("(str/replace \"a\" #\"a\" \"$1\")") == "No group 1")
				#expect(message("(str/replace \"a\" #\"a\" \"${none}\")") == "No group with name {none}")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func aPatternIsAConstantTheNodeCodecCarries() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("((fn [] #\"a+\")) ").description == "#\"a+\"")
				#expect(try printed("(let [p #\"a+\"] p)") == "#\"a+\"")
				#expect(try eval("(= #\"a+\" (read-string (pr-str #\"a+\")))") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A runaway pattern is stopped by the deadline, not by a memo table (NOTES.md, "Regex").
		@Test func catastrophicBacktrackingHitsTheDeadline() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				clj_deadline_set_ms(50)
				defer { clj_deadline_set_ms(0) }
				#expect(message("(re-find #\"(a+)+b\" \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\")") == "Execution timed out")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
