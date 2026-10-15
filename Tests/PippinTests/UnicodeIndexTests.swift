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

// In StringTests rather than a suite of its own: a new suite reshuffles the committed shard deal (scripts/test-times.json).
extension CoreTests.StringTests {
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

	// The index hint must answer as the scan in any order of access, on a shared string too.
	@Test(arguments: [false, true]) func hintAgreesWithAScan(shared: Bool) {
		clj_init()
		let text = "é" + String(repeating: "aé日😀", count: 80) + "z"
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
		if shared { clj_share(s) }
		let n = scalars.count
		var order = Array(0..<n) + Array((0..<n).reversed()) + Array(stride(from: 0, to: n, by: 63)) + [n - 1, 0, 70, 66, 130, 3]
		var x: UInt64 = 0x9E3779B97F4A7C15
		for _ in 0..<2000 {
			x = x &* 6364136223846793005 &+ 1442695040888963407
			let base = Int(x >> 33) % n
			order.append(base)
			order.append(max(0, min(n - 1, base + Int(x >> 60) - 8)))
		}
		for i in order {
			#expect(clj_string_offset(s, i) == offsets[i], "offset of \(i)")
			var cp: UInt32 = 0
			#expect(clj_string_char_at(s, i, &cp) && cp == scalars[i].value, "char \(i)")
		}
		#expect(clj_string_offset(s, n) == Int(clj_string_len(s)))
		clj_release(s)
	}

	// An in-place append must read as a fresh string, and never reach a holder of the old one.
	@Test func appendInPlaceIsAFreshString() throws {
		clj_init()
		#expect(try printed(#"(reduce str "" (range 12))"#) == #""01234567891011""#)
		#expect(try printed(#"(reduce str "" [1 {:a 1} [2] 'x :k nil \é])"#) == #""1{:a 1}[2]x:ké""#)
		#expect(try printed(#"(let [v (volatile! "")] (dotimes [i 3] (vswap! v str i \é nil "ü")) @v)"#) == #""0éü1éü2éü""#)
		let walk = """
		(let [v (volatile! "")]
		  (loop [i 0 ok true]
		    (if (< i 300)
		      (let [c (if (zero? (mod i 3)) \\é (char (+ 97 (mod i 26))))]
		        (vswap! v str c)
		        (let [s @v fresh (apply str (seq s))]
		          (recur (inc i) (and ok (= (count s) (inc i)) (= c (nth s i)) (= (hash s) (hash fresh)) (= s fresh)
		                              (= (subs s (quot i 2)) (subs fresh (quot i 2)))))))
		      [ok (count @v)])))
		"""
		#expect(try printed(walk) == "[true 300]")
		#expect(try printed(#"(let [v (volatile! (str "ab" 1)) keep @v] (vswap! v str "c") [keep @v])"#) == #"["ab1" "ab1c"]"#)
		#expect(try printed(#"(let [a (str "ab" 1) b (str a "c")] [a b (str b b)])"#) == #"["ab1" "ab1c" "ab1cab1c"]"#)
		#expect(try printed(#"(let [s (apply str (repeat 70 "é"))] [(count (reduce str s ["a" \ü 1])) (nth (reduce str s ["a" \ü 1]) 71)])"#) == #"[73 \ü]"#)
		#expect(try printed(#"(let [v (volatile! "x")] (vswap! v str {:a 1}) (vswap! v str [1 "é"]) @v)"#) == #""x{:a 1}[1 \"é\"]""#)
	}

	@Test func integersPrintThemselves() throws {
		clj_init()
		#expect(try printed("(str 0 -1 9 10 99 100 -100 4611686018427387903 -4611686018427387904)") == #""0-191099100-1004611686018427387903-4611686018427387904""#)
		#expect(try printed("[Long/MAX_VALUE Long/MIN_VALUE (str Long/MIN_VALUE)]") == #"[9223372036854775807 -9223372036854775808 "-9223372036854775808"]"#)
		#expect(try printed(#"(format "%d|%05d|%,d" Long/MIN_VALUE -42 -1234567)"#) == #""-9223372036854775808|-0042|-1,234,567""#)
	}
}

