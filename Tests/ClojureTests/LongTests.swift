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
	try eval("(pr-str \(source))").description.trimmingCharacters(in: ["\""])
}

private let maxLong = "9223372036854775807"
private let minLong = "-9223372036854775808"

extension CoreTests {
	@Suite struct LongTests {
		// Interning is permanent and shows in clj_debug_live_objects, so it happens before any baseline.
		init() {
			clj_init()
			for k in ["a", "k", "none", "x"] { _ = Value(keyword: k) }
		}

		@Test func boxRoundTrip() {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				for i: Int64 in [.max, .min, Int64(CLJ_FIXNUM_MAX) + 1, Int64(CLJ_FIXNUM_MIN) - 1, 1 << 62] {
					let v = clj_long_new(i)
					#expect(clj_is_long(v))
					#expect(clj_long_val(v) == i)
					#expect(String(cString: clj_type_name(v)) == "long")
					var out: Int64 = 0
					#expect(clj_int64_of(v, &out) && out == i)
					clj_release(v)
				}
				// Canonical form: everything a fixnum can hold stays one.
				for i: Int64 in [0, 1, -1, Int64(CLJ_FIXNUM_MAX), Int64(CLJ_FIXNUM_MIN)] {
					#expect(clj_is_fixnum(clj_long_new(i)))
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func readsAndPrintsTheWholeRange() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[\(maxLong) \(minLong)]") == "[\(maxLong) \(minLong)]")
				#expect(try printed("[0x7FFFFFFFFFFFFFFF -0x8000000000000000 0777 2r1011]") == "[\(maxLong) \(minLong) 511 11]")
				#expect(try eval("(str \(maxLong))").string == maxLong)
				#expect(try printed("[9223372036854775808 -9223372036854775809]") == "[9223372036854775808N -9223372036854775809N]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func predicatesAndType() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(int? \(maxLong)) (integer? \(maxLong)) (number? \(maxLong)) (rational? \(maxLong))]") == [true, true, true, true])
				#expect(try eval("[(pos-int? \(maxLong)) (neg-int? \(minLong)) (int? 9223372036854775808N)]") == [true, true, false])
				#expect(try eval("(= (type 1) (type \(maxLong)) Long)") == true)
				#expect(try eval("(type \(maxLong))").description == "long")
				#expect(try eval("[(even? \(minLong)) (odd? \(maxLong)) (pos? \(maxLong)) (neg? \(minLong))]") == [true, true, true, true])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func arithmeticIsCheckedAtSixtyFourBits() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[(+ \(maxLong) 0) (- \(maxLong) 1) (* 4611686018427387903 2)]")
					== "[\(maxLong) 9223372036854775806 9223372036854775806]")
				#expect(message("(+ \(maxLong) 1)") == "integer overflow")
				#expect(message("(- \(minLong) 1)") == "integer overflow")
				#expect(message("(* \(minLong) -1)") == "integer overflow")
				#expect(message("(quot \(minLong) -1)") == "integer overflow")
				#expect(message("(inc \(maxLong))") == "integer overflow")
				#expect(message("(dec \(minLong))") == "integer overflow")
				#expect(try printed("[(quot \(maxLong) 2) (rem \(maxLong) 10) (mod \(maxLong) 10)]")
					== "[4611686018427387903 7 7]")
				// A result back inside the fixnum range comes back as a fixnum.
				#expect(try eval("[(- \(maxLong) \(maxLong)) (quot \(maxLong) \(maxLong))]") == [0, 1])
				#expect(try eval("(int? (- \(maxLong) \(maxLong)))") == true)
				#expect(try printed("[(+' \(maxLong) 1) (*' \(maxLong) 2)]") == "[9223372036854775808N 18446744073709551614N]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func equalityHashAndCompare() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(= \(maxLong) \(maxLong)) (= \(maxLong) 9223372036854775807N) (== \(maxLong) 9223372036854775807N)]") == [true, true, true])
				#expect(try eval("[(= \(maxLong) \(minLong)) (= \(maxLong) 1)]") == [false, false])
				#expect(try eval("(= (hash \(maxLong)) (hash 9223372036854775807N))") == true)
				#expect(try eval("(= (hash 1) (hash 1N))") == true)
				#expect(try eval("(get {\(maxLong) :a} 9223372036854775807N)").description == ":a")
				#expect(try eval("(count (conj #{\(maxLong)} 9223372036854775807N))") == 1)
				#expect(try eval("[(compare \(minLong) \(maxLong)) (compare \(maxLong) 0) (compare \(maxLong) \(maxLong))]") == [-1, 1, 0])
				#expect(try eval("[(< \(minLong) 0 \(maxLong)) (> \(maxLong) 9223372036854775806) (<= \(maxLong) \(maxLong))]") == [true, true, true])
				#expect(try eval("(vec (sort [\(maxLong) 0 \(minLong) 1N]))").description == "[\(minLong) 0 1N \(maxLong)]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func bitOpsCoverSixtyFourBits() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[(bit-and -1 \(maxLong)) (bit-or 0 \(minLong)) (bit-xor \(maxLong) -1)]")
					== "[\(maxLong) \(minLong) \(minLong)]")
				#expect(try printed("(bit-shift-left 1 63)") == minLong)
				#expect(try printed("[(bit-shift-right \(minLong) 63) (unsigned-bit-shift-right \(minLong) 63)]") == "[-1 1]")
				#expect(try printed("[(bit-not 0) (bit-not \(maxLong)) (bit-and-not -1 \(maxLong))]") == "[-1 \(minLong) \(minLong)]")
				#expect(try printed("[(bit-set 0 63) (bit-clear \(minLong) 63) (bit-flip 0 63)]") == "[\(minLong) 0 \(minLong)]")
				#expect(try eval("[(bit-test \(minLong) 63) (bit-test \(maxLong) 63)]") == [true, false])
				#expect(try printed("(bit-and 6148914691236517205 \(maxLong))") == "6148914691236517205")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func coercions() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[(long \(maxLong)) (long \(minLong)) (long 9223372036854775807N)]")
					== "[\(maxLong) \(minLong) \(maxLong)]")
				#expect(try printed("[(bigint \(maxLong)) (bigdec \(minLong)) (num \(maxLong))]")
					== "[9223372036854775807N -9223372036854775808M \(maxLong)]")
				#expect(try printed("(parse-long \"\(maxLong)\")") == maxLong)
				#expect(try eval("(parse-long \"9223372036854775808\")") == nil)
				#expect(try eval("(double \(maxLong))") == 9.223372036854776e18)
				#expect(message("(long 9223372036854775808N)") == "Value out of range for long: 9223372036854775808N")
				#expect(message("(int \(maxLong))") == "Value out of range for int: \(maxLong)")
				#expect(message("(long 9.3e18)") == "Value out of range for long: 9.3E18")
				#expect(try printed("(rationalize \(maxLong))") == maxLong)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func numeratorAndDenominatorAreCanonical() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("[(numerator 1/2) (denominator 1/2) (numerator -3/4) (denominator -3/4)]") == "[1 2 -3 4]")
				#expect(try eval("[(= (numerator 1/2) 1) (int? (denominator 1/2))]") == [true, true])
				#expect(try printed("(denominator (/ 1 18446744073709551616N))") == "18446744073709551616N")
				#expect(try printed("(numerator (/ \(maxLong) 2))") == maxLong)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func boxedLongsSurviveTheAnalyzer() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("((fn [] \(minLong)))").description == minLong)
				#expect(try printed("[(let [x \(maxLong)] x) (first [\(minLong)]) (get {:k \(maxLong)} :k)]")
					== "[\(maxLong) \(minLong) \(maxLong)]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func rangeSpansTheWholeInt64() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try printed("(take 3 (range (- \(maxLong) 1) \(maxLong)))") == "(9223372036854775806)")
				#expect(try printed("(take 2 (range \(maxLong)))") == "(0 1)")
				#expect(try printed("(take 3 (range \(minLong) 0))") == "(\(minLong) -9223372036854775807 -9223372036854775806)")
				#expect(try printed("(take 3 (range \(maxLong) 0 -1))") == "(\(maxLong) 9223372036854775806 9223372036854775805)")
				#expect(try printed("(vec (range (- \(maxLong) 3) \(maxLong)))")
					== "[9223372036854775804 9223372036854775805 9223372036854775806]")
				// Stepping past Long/MAX_VALUE ends the range instead of wrapping.
				#expect(try printed("(vec (range (- \(maxLong) 1) \(maxLong) 4))") == "[9223372036854775806]")
				#expect(try printed("(vec (range \(minLong) (+ \(minLong) 2) -1))") == "[]")
				#expect(try eval("(count (range (- \(maxLong) 2) \(maxLong)))") == 2)
				#expect(try eval("(count (range \(minLong) -1))") == 9223372036854775807)
				#expect(message("(count (range \(minLong) \(maxLong)))") == "range count exceeds Long/MAX_VALUE")
				#expect(try printed("(reduce +' 0 (range (- \(maxLong) 3) \(maxLong)))") == "27670116110564327415N")
				#expect(try printed("(last (range (- \(maxLong) 3) \(maxLong)))") == "9223372036854775806")
				#expect(try eval("(int? (first (range \(maxLong))))") == true)
				// A bigint bound names itself rather than reporting a missing integer.
				#expect(message("(range 0 9223372036854775808N)") == "range bound outside the 64-bit long: 9223372036854775808N")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func aBoxedIndexIsOutOfBounds() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(message("(nth [1 2] \(maxLong))") == "Index \(maxLong) out of bounds for length 2")
				#expect(message("(nth \"ab\" \(maxLong))") == "Index \(maxLong) out of bounds for length 2")
				#expect(message("(nth (list 1 2) \(minLong))") == "Index \(minLong) out of bounds for length 2")
				#expect(message("([1 2] \(maxLong))") == "Index \(maxLong) out of bounds for length 2")
				#expect(message("(assoc [1 2] \(maxLong) :x)") == "Index \(maxLong) out of bounds for length 2")
				#expect(message("(aget (long-array 2) \(maxLong))") == "Index \(maxLong) out of bounds for length 2")
				#expect(message("(aset (long-array 2) \(minLong) 1)") == "Index \(minLong) out of bounds for length 2")
				#expect(message("(subs \"ab\" 0 \(maxLong))") == "String index out of range: \(maxLong)")
				#expect(message("(subvec [1 2] 0 \(maxLong))") == "Index out of bounds: subvec 0 \(maxLong)")
				// get and contains? answer instead of throwing, as they do for any index past the end.
				#expect(try eval("[(nth [1 2] \(maxLong) :none) (get [1 2] \(maxLong)) (contains? [1 2] \(maxLong))]").description
					== "[:none nil false]")
				#expect(try eval("[(contains? (long-array 2) \(maxLong)) (contains? \"ab\" \(minLong))]") == [false, false])
				#expect(try eval("(nthnext [1 2] \(maxLong))") == nil)
				#expect(try eval("(str-index-of* \"ab\" \"b\" \(maxLong))") == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func bridgesToSwiftOverTheWholeRange() {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				for n in [0, 1, -1, Int(Int64.max), Int(Int64.min), Int(CLJ_FIXNUM_MAX) + 1] {
					let v = Value(n)
					#expect(v.int == n)
					#expect(v.typeName == "long")
				}
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
