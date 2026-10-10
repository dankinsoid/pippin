// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private func printed(_ source: String) throws -> String {
	try cljEval("(pr-str \(source))").string!
}

// Design §4 «Строки» divergence, written by hand: the JVM-checked rest is fuzz/regressions/strings-unicode.clj.
struct AstralCase: CustomStringConvertible, Sendable {
	let form: String
	let want: String
	let jvm: String
	var description: String { form }
}

private func d(_ form: String, _ want: String, jvm: String) -> AstralCase { AstralCase(form: form, want: want, jvm: jvm) }

private let astral: [AstralCase] = [
	d(#"(count "😀")"#, "1", jvm: "2"),
	d(#"(count "👨‍👩‍👧")"#, "5", jvm: "8"),
	d(#"(count "🇺🇸")"#, "2", jvm: "4"),
	d(#"(subs "a😀b" 1 2)"#, #""😀""#, jvm: "a lone high surrogate"),
	d(#"(subs "a😀b" 2)"#, #""b""#, jvm: #""\uDE00b""#),
	d(#"(int (nth "a😀b" 1))"#, "128512", jvm: "55357"),
	d(#"(int (get "a😀b" 2))"#, "98", jvm: "56832"),
	d(#"(count (seq "🇺🇸"))"#, "2", jvm: "4"),
	d(#"(mapv int "a😀")"#, "[97 128512]", jvm: "[97 55357 56832]"),
	d(#"(str/index-of "😀a😀b" "b")"#, "3", jvm: "5"),
	d(#"(str/index-of "😀a😀b" "😀" 1)"#, "2", jvm: "3"),
	d(#"(str/last-index-of "😀a😀b" "😀")"#, "2", jvm: "3"),
	d(#"(contains? "😀" 1)"#, "false", jvm: "true"),
	d(#"(format "%3s|" "😀")"#, #""  😀|""#, jvm: #"" 😀|""#),
	d(#"(format "%.1s" "😀a")"#, #""😀""#, jvm: "a lone high surrogate"),
	// Past CLJ_STRING_CRUMB_BYTES the index goes through the crumbs: 7 bytes and 3 code points per repeat.
	d(#"(let [s (apply str (repeat 100 "é😀a"))] [(count s) (int (nth s 64)) (subs s 150 153) (str/index-of s "a" 200) (str/last-index-of s "😀") (count (subs s 63 129)) (count (rest s))])"#,
	  #"[300 128512 "é😀a" 200 298 66 299]"#, jvm: #"[400 …]"#),
	d(#"(let [s (apply str (repeat 128 "😀"))] [(count s) (subs s 127) (subs s 128) (nth s 128 :none) (str/index-of (str s "x") "x")])"#,
	  #"[128 "😀" "" :none 128]"#, jvm: #"[256 …]"#),
]

extension CoreTests {
	@Suite struct UnicodeIndexTests {
		init() {
			clj_init()
			_ = try? cljEval("(require '[clojure.string :as str])")
		}

		@Test(arguments: astral) func codePointsNotUTF16Units(row: AstralCase) throws {
			clj_init()
			_ = try cljEval("(require '[clojure.string :as str])")
			#expect(try printed(row.form) == row.want, "\(row.form), the JVM answers \(row.jvm)")
		}

		@Test func asciiFlagAndIndexUnit() {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				let ascii = clj_string_from_cstr("hello, world")
				let wide = clj_string_from_cstr("héllo")
				#expect(clj_string_is_ascii(ascii))
				#expect(!clj_string_is_ascii(wide))
				#expect(clj_string_count(ascii) == 12 && clj_string_count(wide) == 5)
				#expect(clj_string_offset(wide, 2) == 3 && clj_string_index_at(wide, 3) == 2)
				let empty = clj_string_new(nil, 0)
				#expect(clj_string_is_ascii(empty) && clj_string_count(empty) == 0)
				for s in [ascii, wide, empty] { clj_release(s) }
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every code point of a long mixed string, both directions, across every crumb boundary.
		@Test func crumbsAgreeWithAScan() {
			clj_init()
			let text = String(repeating: "aé日😀", count: 50) + "z"
			let scalars = Array(text.unicodeScalars)
			var offsets: [Int] = []
			var at = 0
			for u in scalars {
				offsets.append(at)
				at += String(u).utf8.count
			}
			let s = Array(text.utf8).withUnsafeBufferPointer { buf in
				buf.withMemoryRebound(to: CChar.self) { clj_string_new($0.baseAddress, $0.count) }
			}
			#expect(clj_string_count(s) == scalars.count)
			for (i, u) in scalars.enumerated() {
				#expect(clj_string_offset(s, i) == offsets[i], "offset of \(i)")
				#expect(clj_string_index_at(s, offsets[i]) == i, "index at \(offsets[i])")
				var cp: UInt32 = 0
				#expect(clj_string_char_at(s, i, &cp) && cp == u.value, "char \(i)")
			}
			#expect(clj_string_offset(s, scalars.count) == Int(clj_string_len(s)))
			#expect(clj_string_index_at(s, Int(clj_string_len(s))) == scalars.count)
			var cp: UInt32 = 0
			#expect(!clj_string_char_at(s, scalars.count, &cp))
			clj_release(s)
		}

		@Test func integersPrintThemselves() throws {
			clj_init()
			#expect(try printed("(str 0 -1 9 10 99 100 -100 4611686018427387903 -4611686018427387904)") == #""0-191099100-1004611686018427387903-4611686018427387904""#)
			#expect(try printed("[Long/MAX_VALUE Long/MIN_VALUE (str Long/MIN_VALUE)]") == #"[9223372036854775807 -9223372036854775808 "-9223372036854775808"]"#)
			#expect(try printed(#"(format "%d|%05d|%,d" Long/MIN_VALUE -42 -1234567)"#) == #""-9223372036854775808|-0042|-1,234,567""#)
		}
	}
}
