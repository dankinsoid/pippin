// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

// (format fmt args...) against what java.util.Formatter answers.
struct FormatCase: CustomStringConvertible, Sendable {
	let form: String
	let want: String
	var description: String { form }
}

private func c(_ form: String, _ want: String) -> FormatCase { FormatCase(form: form, want: want) }

private let cases: [FormatCase] = [
	// %s and %S: str of anything, null for nil, precision truncates, width pads
	c(#"(format "%s" "a")"#, "a"),
	c(#"(format "%s" nil)"#, "null"),
	c(#"(format "%s %s %s" :k 'sym 1.5)"#, ":k sym 1.5"),
	c(#"(format "%s" [1 "a" :b])"#, #"[1 "a" :b]"#),
	c(#"(format "%s" {:a 1})"#, "{:a 1}"),
	c(#"(format "%s" \c)"#, "c"),
	c(#"(format "%S" "abc")"#, "ABC"),
	c(#"(format "%5s|%-5s|" "ab" "cd")"#, "   ab|cd   |"),
	c(#"(format "%.2s" "abcdef")"#, "ab"),
	c(#"(format "%5.1s|" "abc")"#, "    a|"),
	c(#"(format "%3s" "λ→🙂")"#, "λ→🙂"),
	c(#"(format "%4s|%.2s" "λ→" "λ→🙂")"#, "  λ→|λ→"),
	c(#"(format "no specs")"#, "no specs"),
	c(#"(format "")"#, ""),
	c(#"(format "100%%")"#, "100%"),
	c(#"(format "a%nb")"#, "a\nb"),
	// %d family
	c(#"(format "%d" 42)"#, "42"),
	c(#"(format "%d" -42)"#, "-42"),
	c(#"(format "%5d|%-5d|" 42 42)"#, "   42|42   |"),
	c(#"(format "%05d" 42)"#, "00042"),
	c(#"(format "%05d" -42)"#, "-0042"),
	c(#"(format "%+d %+d" 42 -42)"#, "+42 -42"),
	c(#"(format "% d|% d" 42 -42)"#, " 42|-42"),
	c(#"(format "%,d" 1234567)"#, "1,234,567"),
	c(#"(format "%,d" -1234567)"#, "-1,234,567"),
	c(#"(format "%,d" 123)"#, "123"),
	c(#"(format "%,12d|" 1234567)"#, "   1,234,567|"),
	c(#"(format "%d" 9223372036854775807)"#, "9223372036854775807"),
	c(#"(format "%d" -9223372036854775808)"#, "-9223372036854775808"),
	c(#"(format "%d" 123456789012345678901234567890N)"#, "123456789012345678901234567890"),
	c(#"(format "%,d" -123456789012345678901234567890N)"#, "-123,456,789,012,345,678,901,234,567,890"),
	c(#"(format "%x %X %o" 255 255 8)"#, "ff FF 10"),
	c(#"(format "%x" -1)"#, "ffffffffffffffff"),
	c(#"(format "%08x" 255)"#, "000000ff"),
	c(#"(format "%1$s %1$s %s %s" "a" "b")"#, "a a a b"),
	c(#"(format "%2$s %1$s" "a" "b")"#, "b a"),
	// %f %e %g
	c(#"(format "%f" 3.14159)"#, "3.141590"),
	c(#"(format "%.2f" 3.14159)"#, "3.14"),
	c(#"(format "%.0f" 2.5)"#, "3"),
	c(#"(format "%.0f" 3.5)"#, "4"),
	c(#"(format "%8.3f|%-8.3f|" 3.14159 3.14159)"#, "   3.142|3.142   |"),
	c(#"(format "%08.3f" -3.14159)"#, "-003.142"),
	c(#"(format "%+.1f" 2.0)"#, "+2.0"),
	c(#"(format "%,.2f" 1234567.891)"#, "1,234,567.89"),
	c(#"(format "%.2f" 1.005)"#, "1.01"),
	c(#"(format "%.2f" 0.005)"#, "0.01"),
	c(#"(format "%.2f" 0.0049)"#, "0.00"),
	c(#"(format "%.1f" 9.95)"#, "10.0"),
	c(#"(format "%.0f" 0.5)"#, "1"),
	c(#"(format "%.0f" 0.4)"#, "0"),
	c(#"(format "%.3f" 0.0)"#, "0.000"),
	c(#"(format "%.2f" 1e20)"#, "100000000000000000000.00"),
	c(#"(format "%.2f" 123456789.125)"#, "123456789.13"),
	c(#"(format "%f" 1e-7)"#, "0.000000"),
	c(#"(format "%.10f" 1e-7)"#, "0.0000001000"),
	c(#"(format "%.3f" 1M)"#, "1.000"),
	c(#"(format "%.2f" 2.675M)"#, "2.68"),
	c(#"(format "%f %f %f" ##NaN ##Inf ##-Inf)"#, "NaN Infinity -Infinity"),
	c(#"(format "%5.1f|" ##NaN)"#, "  NaN|"),
	c(#"(format "%e" 12345.678)"#, "1.234568e+04"),
	c(#"(format "%e" 0.0)"#, "0.000000e+00"),
	c(#"(format "%.0e" 9.5)"#, "1e+01"),
	c(#"(format "%.1e" 1e100)"#, "1.0e+100"),
	c(#"(format "%.2E" 0.000123)"#, "1.23E-04"),
	c(#"(format "%g" 12345.678)"#, "12345.7"),
	c(#"(format "%g" 1.0)"#, "1.00000"),
	c(#"(format "%g" 0.0)"#, "0.00000"),
	c(#"(format "%g" 1234567.0)"#, "1.23457e+06"),
	c(#"(format "%g" 0.0001)"#, "0.000100000"),
	c(#"(format "%g" 0.00001)"#, "1.00000e-05"),
	c(#"(format "%.3g" 1234.0)"#, "1.23e+03"),
	c(#"(format "%.3g" 123.0)"#, "123"),
	c(#"(format "%G" 0.00001)"#, "1.00000E-05"),
	// %b %c
	c(#"(format "%b %b %b %b %b" true false nil 0 "x")"#, "true false false true true"),
	c(#"(format "%B" true)"#, "TRUE"),
	c(#"(format "%c%c%c" \a 98 \λ)"#, "abλ"),
	c(#"(format "%c" nil)"#, "null"),
	c(#"(format "%C" \a)"#, "A"),
	c(#"(format "%3c|" \a)"#, "  a|"),
	// mixed
	c(#"(format "%s=%d (%.1f%%)" "x" 3 50.0)"#, "x=3 (50.0%)"),
	c(#"(format "%-10s%5d%n" "name" 7)"#, "name          7\n"),
]

extension CoreTests {
	// java.util.Formatter's subset (builtins_format.c) and printf.
	@Suite struct FormatTests {
		init() {
			clj_init()
			for k in ["k", "b", "a"] { _ = Value(keyword: k) }
		}

		@Test(arguments: cases) func formatsLikeTheJVM(_ c: FormatCase) throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval(c.form) == Value(c.want))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func errorsNameTheSpec() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(message(#"(format "%q" 1)"#) == "format: unsupported conversion in %q")
				#expect(message(#"(format "%tY" 1)"#) == "format: unsupported conversion in %t")
				#expect(message(#"(format "%h" 1)"#) == "format: unsupported conversion in %h")
				#expect(message(#"(format "%#x" 1)"#) == "format: unsupported flag in %#")
				#expect(message(#"(format "%(d" 1)"#) == "format: unsupported flag in %(")
				#expect(message(#"(format "abc %")"#) == "format: missing conversion in %")
				#expect(message(#"(format "%5.f" 1.0)"#) == "format: missing precision in %5.")
				#expect(message(#"(format "%s %s" 1)"#) == "format: missing argument for %s")
				#expect(message(#"(format "%3$s" 1 2)"#) == "format: missing argument for %3$s")
				#expect(message(#"(format "%d" "1")"#) == "format: d != string in %d")
				#expect(message(#"(format "%d" 1.5)"#) == "format: d != double in %d")
				#expect(message(#"(format "%x" 12N)"#) == "format: x != bigint in %x")
				#expect(message(#"(format "%f" 1)"#) == "format: f != long in %f")
				#expect(message(#"(format "%.2f" 1/2)"#) == "format: f != ratio in %.2f")
				#expect(message(#"(format "%c" "ab")"#) == "format: c != string in %c")
				#expect(message(#"(format "%.1d" 1)"#) == "format: precision not allowed in %.1d")
				#expect(message(#"(format "%05s" "a")"#) == "format: flag not allowed in %05s")
				#expect(message(#"(format "%+x" 1)"#) == "format: flag not allowed in %+x")
				#expect(message(#"(format "%,e" 1.0)"#) == "format: flag not allowed in %,e")
				#expect(message(#"(format 1)"#) == "format expects a string, got: long")
				#expect(message(#"(format nil)"#) == "format expects a string, got: nil")
				#expect(message(#"(format)"#)?.hasPrefix("Wrong number of args (0)") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func printfWritesToOut() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval(#"(with-out-str (printf "%s=%d%n" "x" 1))"#) == "x=1\n")
				#expect(try cljEval(#"(with-out-str (printf "plain"))"#) == "plain")
				#expect(try cljEval(#"(printf "%s" "x")"#) == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
