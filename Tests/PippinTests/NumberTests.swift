// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval(source) }

// cljEvalError reports only evaluation failures, and a malformed literal fails in the reader.
private func rejects(_ source: String) -> Bool {
	do {
		_ = try cljEval(source)
		return false
	} catch {
		return true
	}
}

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func printed(_ source: String) throws -> String {
	try eval("(pr-str \(source))").description.trimmingCharacters(in: ["\""])
}

extension CoreTests {
	@Suite struct NumberTests {
		@Test func boxRoundTrip() {
			let before = clj_debug_live_objects()
			for d in [0.0, 1.5, -2.25, .greatestFiniteMagnitude, .leastNonzeroMagnitude, .infinity, -.infinity] {
				let v = clj_double_new(d)
				#expect(clj_is_double(v))
				#expect(String(cString: clj_type_name(v)) == "double")
				#expect(clj_double_val(v) == d)
				#expect(clj_is_unique(v) == clj_reuse_enabled())
				clj_release(v)
			}
			let nan = clj_double_new(.nan)
			#expect(clj_double_val(nan).isNaN)
			clj_release(nan)
			#expect(!clj_is_double(clj_fixnum(1)))
			#expect(!clj_is_double(CLJ_NIL))
			#expect(clj_debug_live_objects() == before)
		}

		@Test func zeroSigns() {
			let before = clj_debug_live_objects()
			let pos = clj_double_new(0.0)
			let neg = clj_double_new(-0.0)
			#expect(clj_double_val(neg).sign == .minus)
			#expect(clj_equals(pos, neg))
			#expect(clj_equals(neg, pos))
			#expect(clj_hash(pos) == clj_hash(neg))
			clj_release(pos)
			clj_release(neg)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nanNeverEqualsAnotherBox() {
			let before = clj_debug_live_objects()
			let a = clj_double_new(.nan)
			let b = clj_double_new(.nan)
			#expect(!clj_equals(a, b))
			#expect(!clj_equals(b, a))
			let one = clj_double_new(1.0)
			#expect(!clj_equals(a, one))
			clj_release(a)
			clj_release(b)
			clj_release(one)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func typeStrictAgainstFixnum() {
			let before = clj_debug_live_objects()
			let one = clj_double_new(1.0)
			#expect(!clj_equals(one, clj_fixnum(1)))
			#expect(!clj_equals(clj_fixnum(1), one))
			#expect(clj_hash(one) != clj_hash(clj_fixnum(1)))
			#expect(!clj_equals(one, CLJ_TRUE))
			let str = clj_string_from_cstr("1.0")
			#expect(!clj_equals(one, str) && !clj_equals(str, one))
			clj_release(one)
			clj_release(str)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func equalValuesHashEqual() {
			let before = clj_debug_live_objects()
			for d in [3.25, -1e300, 6.02e23, .infinity] {
				let a = clj_double_new(d)
				let b = clj_double_new(d)
				#expect(a != b)
				#expect(clj_equals(a, b))
				#expect(clj_hash(a) == clj_hash(b))
				clj_release(a)
				clj_release(b)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// ---- bigint at the C level

		@Test func bigintRoundTripsInt64() {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				let samples: [Int64] = [0, 1, -1, 42, -42, 2147483648, -2147483648, .max, .min, 4611686018427387903]
				for i in samples {
					let v = clj_bigint_from_i64(i)
					#expect(clj_is_bigint(v))
					var out: Int64 = 0
					#expect(clj_bigint_to_i64(v, &out))
					#expect(out == i)
					#expect(clj_bigint_to_double(v) == Double(i))
					clj_release(v)
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func bigintArithmeticMatchesInt64() {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				var state: UInt64 = 0x243f_6a88_85a3_08d3
				func next() -> Int64 {
					state = state &* 6364136223846793005 &+ 1442695040888963407
					return Int64(bitPattern: state >> 34) - (1 << 29)
				}
				for _ in 0..<400 {
					let x = next(), y = next()
					let a = clj_bigint_from_i64(x), b = clj_bigint_from_i64(y)
					var out: Int64 = 0
					for (v, expected) in [(clj_bigint_add(a, b), x + y), (clj_bigint_sub(a, b), x - y), (clj_bigint_mul(a, b), x * y)] {
						#expect(clj_bigint_to_i64(v, &out))
						#expect(out == expected)
						clj_release(v)
					}
					if y != 0 {
						var rem: clj_value = CLJ_NIL
						let q = clj_bigint_quot(a, b, &rem)
						#expect(clj_bigint_to_i64(q, &out))
						#expect(out == x / y)
						#expect(clj_bigint_to_i64(rem, &out))
						#expect(out == x % y)
						clj_release(q)
						clj_release(rem)
					}
					#expect(clj_bigint_cmp(a, b) == (x < y ? -1 : x > y ? 1 : 0))
					clj_release(a)
					clj_release(b)
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func bigintTextAndGcd() {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				let big = "123456789012345678901234567890".withCString { clj_bigint_parse($0, 30, 10) }
				#expect(clj_is_bigint(big))
				var fits: Int64 = 0
				#expect(!clj_bigint_to_i64(big, &fits))
				let text = Value(owning: clj_bigint_to_string(big))
				#expect(text == "123456789012345678901234567890")
				let squared = clj_bigint_mul(big, big)
				let back = Value(owning: clj_bigint_to_string(squared))
				#expect(back == "15241578753238836750495351562536198787501905199875019052100")
				let g = clj_bigint_gcd(squared, big)
				#expect(clj_bigint_cmp(g, big) == 0)
				clj_release(g)
				clj_release(squared)
				clj_release(big)
				#expect(clj_is_nil("12x".withCString { clj_bigint_parse($0, 3, 10) }))
			}
			#expect(clj_debug_live_objects() == before)
		}

		// ---- the reader

		@Test func readsLiterals() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[0N 1N -1N 123N]") == "[0N 1N -1N 123N]")
				#expect(try printed("[1/2 -1/2 111/7]") == "[1/2 -1/2 111/7]")
				#expect(try printed("[0/2 12/12 -12/12]") == "[0 1 -1]")
				#expect(try printed("[0.0M 1.5M -123.456M 1M 440M]") == "[0.0M 1.5M -123.456M 1M 440M]")
				#expect(try printed("10000000000000000000000000N") == "10000000000000000000000000N")
				// A literal is a long while it fits 64 bits, whatever the radix, and a bigint past them.
				#expect(try printed("[0x7FFFFFFFFFFFFFFF -0x8000000000000000 9223372036854775807]")
					== "[9223372036854775807 -9223372036854775808 9223372036854775807]")
				#expect(try printed("[0xFFN 2r1011 -9223372036854775809]") == "[255N 11 -9223372036854775809N]")
				#expect(try printed("[1e10M 1E-10M]") == "[1E+10M 1E-10M]")
				#expect(try eval("[(type 1N) (type 1/2) (type 1.0M)]").description == "[bigint ratio decimal]")
				#expect(try eval("(str 1N \" \" 1/2 \" \" 1.5M)") == "1 1/2 1.5")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func rejectsMalformedLiterals() {
			clj_init()
			for text in ["1.5N", "1/2N", "1/0", "1/", "0x1M", "1MM", "1NN"] {
				#expect(rejects(text), "\(text) should not read")
			}
		}

		// ---- equality, hash and compare

		@Test func equalityFollowsCategories() throws {
			clj_init()
			// Interning a keyword grows the table for the process, so the baseline is taken after it.
			_ = try eval(":a")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(= 1N 1) (= 1 1N) (= 1N 1N) (= -1N -1)]") == [true, true, true, true])
				#expect(try eval("[(= 1N 1.0) (= 1M 1) (= 1/2 0.5) (= 1/2 1N)]") == [false, false, false, false])
				#expect(try eval("[(== 1N 1) (== 1N 1.0) (== 1/2 0.5) (== 1M 1) (== 1 1N 1.0 1M)]") == [true, true, true, true, true])
				#expect(try eval("[(= 0.0M 0M) (= 1.0M 1.00M) (= 1/2 2/4)]") == [true, true, true])
				#expect(try eval("[(= (hash 1N) (hash 1)) (= (hash 0N) (hash 0)) (= (hash -7N) (hash -7))]") == [true, true, true])
				#expect(try eval("[(get {1N :a} 1) (get {1 :a} 1N) (count (set [1 1N])) (count (set [1/2 2/4]))]") == [Value(keyword: "a"), Value(keyword: "a"), 1, 1])
				#expect(try eval("[(= (hash 1.0M) (hash 1.00M)) (= (hash 1/2) (hash 2/4))]") == [true, true])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func compareOrdersAcrossKinds() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(compare 1N 2) (compare 2 1N) (compare 1N 1) (compare 1/2 0.5) (compare 1 1.0)]") == [-1, 1, 0, 0, 0])
				#expect(try eval("[(compare 1/2 1) (compare 1.0M 2N) (compare 1N ##NaN)]") == [-1, -1, 0])
				#expect(try printed("(sort [3 1/2 1N 0.25 2.5M])") == "(0.25 1/2 1N 2.5M 3)")
				#expect(try eval("[(< 1N 2) (< 2 1N) (<= 1N 1) (> 1/2 1/3) (>= 1.0M 1N)]") == [true, false, true, true, true])
				#expect(try eval("[(< 1N ##NaN) (> 1N ##NaN) (<= 1N ##NaN)]") == [false, false, false])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// ---- arithmetic

		@Test func promotingOperators() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(+' 1 2) (*' 2 3) (-' 5 1) (inc' 1) (dec' 1)]") == [3, 6, 4, 2, 0])
				#expect(try printed("[(+' 9223372036854775807 1) (inc' 9223372036854775807) (dec' -9223372036854775808)]")
					== "[9223372036854775808N 9223372036854775808N -9223372036854775809N]")
				#expect(try printed("(*' 4611686018427387903 4611686018427387903)") == "21267647932558653957237540927630737409N")
				#expect(message("(+ 9223372036854775807 1)") == "integer overflow")
				#expect(message("(inc 9223372036854775807)") == "integer overflow")
				#expect(try eval("[(type (+' 1 1)) (type (+' 1N 1))]").description == "[long bigint]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func bigintArithmeticStaysBigint() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[(+ 1N 1) (- 1N 1) (* 2N 3) (inc 1N) (dec 1N)]") == "[2N 0N 6N 2N 0N]")
				#expect(try printed("[(quot 10N 3) (rem 10N 3) (mod -7N 3) (quot -10N 3) (rem -10 3N)]") == "[3N 1N 2N -3N -1N]")
				#expect(try printed("[(/ 6N 3) (/ 1N 2)]") == "[2N 1/2]")
				#expect(try printed("(+ 1N 1.5)") == "2.5")
				#expect(try eval("[(even? 2N) (odd? 3N) (zero? 0N) (pos? 1N) (neg? -1N)]") == [true, true, true, true, true])
				#expect(message("(/ 1N 0)") == "Divide by zero")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func ratioArithmetic() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[(/ 1 2) (/ 4 2) (/ 1 2 3) (/ 10 4)]") == "[1/2 2 1/6 5/2]")
				#expect(try printed("[(+ 1/2 1/3) (- 1/2 1/3) (* 1/2 2/3) (/ 1/2 2/3)]") == "[5/6 1/6 1/3 3/4]")
				#expect(try printed("[(+ 1/2 1/2) (* 2/3 3) (+ 1/2 1)]") == "[1N 2N 3/2]")
				#expect(try printed("[(+ 1/2 0.5) (quot 7/2 1/2) (rem 7/2 1/2)]") == "[1.0 7N 0N]")
				#expect(try printed("[(numerator 1/2) (denominator 1/2) (numerator -3/4)]") == "[1 2 -3]")
				#expect(try eval("[(ratio? 1/2) (ratio? 0/2) (rational? 1/2) (integer? 1/2)]") == [true, false, true, false])
				#expect(message("(numerator 1)") == "long cannot be cast to a ratio")
				// A ratio whose arithmetic overflows the fixnum stays exact.
				#expect(try printed("(* 4611686018427387903/2 4611686018427387903)") == "21267647932558653957237540927630737409/2")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func decimalArithmetic() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[(+ 1.5M 1.5M) (- 1.5M 0.5M) (* 1.5M 2M) (+ 1M 1)]") == "[3.0M 1.0M 3.0M 2M]")
				#expect(try printed("[(/ 1M 2M) (/ 1.5M 0.5M)]") == "[0.5M 3M]")
				#expect(try printed("(+ 1M 1.5)") == "2.5")
				#expect(try eval("[(decimal? 1.0M) (decimal? 1N) (rational? 1.0M) (integer? 1.0M)]") == [true, false, true, false])
				#expect(message("(/ 1M 3M)") == "Non-terminating decimal expansion; no exact representable decimal result.")
				#expect(message("(/ 1M 0M)") == "Divide by zero")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every expected value is what JVM Clojure 1.12.6 prints for the same form.
		@Test func withPrecision() throws {
			clj_init()
			_ = try eval("[:precision :rounding :UP :DOWN :CEILING :FLOOR :HALF_UP :HALF_DOWN :HALF_EVEN :UNNECESSARY]")
			_ = try eval("(with-precision 1 (+ 1M 1M))")
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[(with-precision 10 (/ 1M 3M)) (with-precision 10 (/ 1M 3)) (with-precision 50 (/ 22M 7M))]") == "[0.3333333333M 0.3333333333M 3.1428571428571428571428571428571428571428571428571M]")
				#expect(try printed("[(with-precision 1 :rounding UP (* 1.1M 1M)) (with-precision 1 :rounding CEILING (* -1.1M 1M)) (with-precision 1 :rounding DOWN (* -1.9M 1M)) (with-precision 1 :rounding FLOOR (* -1.9M 1M))]") == "[2M -1M -1M -2M]")
				#expect(try printed("[(with-precision 1 :rounding HALF_EVEN (* 2.5M 1M)) (with-precision 1 :rounding HALF_EVEN (* 1.5M 1M)) (with-precision 1 :rounding HALF_DOWN (* -1.5M 1M)) (with-precision 1 :rounding HALF_UP (* -1.5M 1M))]") == "[2M 2M -1M -2M]")
				#expect(try printed("[(with-precision 4 (+ 3.5555555M 1)) (with-precision 6 :rounding FLOOR (+ 3.5555555M 1)) (with-precision 6 :rounding UNNECESSARY (+ 3.5555M 1))]") == "[4.556M 4.55555M 4.5555M]")
				#expect(message("(with-precision 1 :rounding UNNECESSARY (* 1.5M 1M))") == "Rounding necessary")
				// An exact quotient keeps the scale the operands prefer while it fits; past the precision it is rounded.
				#expect(try printed("[(with-precision 2 (/ 1M 8M)) (with-precision 5 (/ 1M 4M)) (with-precision 3 (/ 1.000M 1M)) (with-precision 2 (/ 100.00M 1M)) (with-precision 1 (/ 100M 4M))]") == "[0.13M 0.25M 1.00M 1.0E+2M 3E+1M]")
				#expect(try printed("[(with-precision 3 (+ 999.6M 0)) (with-precision 3 (inc 9.9999M)) (with-precision 3 (* 0.0001234567M 1)) (with-precision 3 (/ 123456M 0.001M))]") == "[1.00E+3M 11.0M 0.000123M 1.23E+8M]")
				// minus is add of the negated operand, which rounds first: the JVM's (- 100M 99.99M) under 2 digits is 0M.
				#expect(try printed("[(with-precision 2 (- 100M 99.99M)) (with-precision 2 (- 5M 5.000M)) (with-precision 2 (+ 5M -5.000M)) (with-precision 3 (- 1.2345M)) (with-precision 3 (- 1E+5M)) (with-precision 3 (- 0 1E+5M))]") == "[0M 0.0M 0.000M -1.23M -1E+5M -1.00E+5M]")
				#expect(try printed("[(with-precision 5 (bigdec 1/3)) (with-precision 5 (+ 1/3 1M)) (with-precision 0 (+ 1.23456789M 1)) (with-precision 2 (rem 7M 2M))]") == "[0.33333M 1.3333M 2.23456789M 1M]")
				#expect(message("(with-precision 2 (quot 12345M 1M))") == "Division impossible")
				#expect(message("(with-precision -1 1)") == "Digits < 0")
				#expect(message("(with-precision 0 (/ 1M 3M))") == "Non-terminating decimal expansion; no exact representable decimal result.")
				// The binding is the call's, not the analysis's: a constant operand is not folded ahead of it.
				#expect(try printed("[*math-context* (with-precision 7 :rounding FLOOR *math-context*) ((fn [] (with-precision 1 (* 1.5M 1M))))]") == "[nil {:precision 7, :rounding :FLOOR} 2M]")
				#expect(try printed("(binding [*math-context* {:precision 2 :rounding :DOWN}] (/ 2M 3M))") == "0.66M")
				#expect(message("(binding [*math-context* {:precision 2}] (/ 2M 3M))") == "*math-context* must be nil or {:precision n :rounding mode}, not {:precision 2}")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// ---- coercions and predicates

		@Test func coercions() throws {
			clj_init()
			_ = Value(keyword: "a")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(long 1.9) (long -1.9) (long 1N) (long 3/2) (long -3/2) (long 1/10) (long 1.1M)]") == [1, -1, 1, 1, -1, 0, 1])
				#expect(try eval("[(int 1.9) (int -2147483648) (int 2147483647) (short 1.9) (byte -128) (byte 1.1M)]") == [1, -2147483648, 2147483647, 1, -128, 1])
				#expect(try eval("[(double 1) (double 1N) (double 1/2) (double 1.5M)]") == [1.0, 1.0, 0.5, 1.5])
				#expect(try eval("[(float 1) (float 1N) (float 0.5)]") == [1.0, 1.0, 0.5])
				#expect(try eval("[(num 1) (num 1.5) (num nil)]") == [1, 1.5, nil])
				for bad in ["(int 2147483648)", "(int -2147483649)", "(int 2147483647.000001)", "(short 32768)", "(byte -129)",
				            "(byte 127.000001)", "(long \"0\")", "(long nil)", "(long [0])", "(float ##Inf)", "(num \\a)", "(num \"1\")"] {
					#expect(rejects(bad), "\(bad) should throw")
				}
				#expect(try printed("[(bigint 1) (bigint 1.0) (bigint \"1\") (bigint -1.0M) (bigint 12/12) (biginteger 7)]")
					== "[1N 1N 1N -1N 1N 7N]")
				#expect(try printed("[(bigdec 1) (bigdec 1N) (bigdec 1.0) (bigdec \"0.5\") (bigdec 1/2) (bigdec \"1e10\")]")
					== "[1M 1M 1.0M 0.5M 0.5M 1E+10M]")
				#expect(try printed("[(rationalize 1) (rationalize 1.0) (rationalize 1.5) (rationalize 1.1) (rationalize 1.5M)]")
					== "[1 1N 3/2 11/10 3/2]")
				#expect(try printed("(rationalize (/ 1.0 3.0))") == "3333333333333333/10000000000000000")
				for bad in ["(bigdec ##Inf)", "(bigdec ##NaN)", "(bigdec nil)", "(bigdec \"abc\")", "(bigdec true)", "(bigdec :a)", "(bigint ##Inf)"] {
					#expect(rejects(bad), "\(bad) should throw")
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func predicates() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(number? 1N) (number? 1/2) (number? 1.0M) (number? \\a) (number? nil)]") == [true, true, true, false, false])
				#expect(try eval("[(integer? 1N) (integer? 1/2) (integer? 1.0M) (integer? 1.0)]") == [true, false, false, false])
				#expect(try eval("[(int? 1) (int? 1N) (int? 0/2) (int? 1/2) (int? 1.0)]") == [true, false, true, false, false])
				#expect(try eval("[(double? 1.0) (double? 1) (double? 1N) (double? 1.0M) (double? 1/2)]") == [true, false, false, false, false])
				#expect(try eval("[(rational? 1) (rational? 1N) (rational? 1/2) (rational? 0.0M) (rational? 1.0)]") == [true, true, true, true, false])
				#expect(try eval("[(decimal? 0.0M) (decimal? 1N) (decimal? 1/2) (ratio? 1/2) (ratio? 1N)]") == [true, false, false, true, false])
				#expect(try eval("[(nat-int? 1) (nat-int? 1N) (pos-int? 1N) (neg-int? -1N)]") == [true, false, false, false])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func parsing() throws {
			clj_init()
			_ = try eval(":key")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(parse-long \"0\") (parse-long \"42\") (parse-long \"+12\") (parse-long \"-1000\")]") == [0, 42, 12, -1000])
				#expect(try eval("[(parse-long \"\") (parse-long \"1L\") (parse-long \"0.0\") (parse-long \"+-5\") (parse-long \"1e3\")]") == [nil, nil, nil, nil, nil])
				#expect(try eval("[(parse-double \"1\") (parse-double \"1.000\") (parse-double \"+5.6\") (parse-double \"56851E-2\")]") == [1.0, 1.0, 5.6, 568.51])
				#expect(try eval("[(parse-double \"Infinity\") (parse-double \"-Infinity\")]").description == "[##Inf ##-Inf]")
				#expect(try eval("[(parse-double \"\") (parse-double \"foo\") (parse-double \"2.6e8E5\") (parse-double \"##Inf\")]") == [nil, nil, nil, nil])
				for bad in ["(parse-long 1000)", "(parse-long :key)", "(parse-double 0.0)"] {
					#expect(rejects(bad), "\(bad) should throw")
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func uncheckedWrapsAtSixtyFourBits() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(unchecked-add 1 2) (unchecked-subtract 5 1) (unchecked-multiply 3 4) (unchecked-inc 1) (unchecked-dec 1) (unchecked-negate 3)]")
					== [3, 4, 12, 2, 0, -3])
				#expect(try printed("[(unchecked-inc 9223372036854775807) (unchecked-negate -9223372036854775808) (unchecked-dec -9223372036854775808)]")
					== "[-9223372036854775808 -9223372036854775808 9223372036854775807]")
				#expect(try printed("[(unchecked-add 9223372036854775807 1) (unchecked-subtract -9223372036854775808 1) (unchecked-multiply 4611686018427387904 2)]")
					== "[-9223372036854775808 9223372036854775807 -9223372036854775808]")
				// The fixnum boundary is a representation change, not a wrap point.
				#expect(try printed("(unchecked-inc 4611686018427387903)") == "4611686018427387904")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
