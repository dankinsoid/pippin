// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private struct SplitMix64 {
	var state: UInt64

	mutating func next() -> UInt64 {
		state &+= 0x9e3779b97f4a7c15
		var z = state
		z = (z ^ (z >> 30)) &* 0xbf58476d1ce4e5b9
		z = (z ^ (z >> 27)) &* 0x94d049bb133111eb
		return z ^ (z >> 31)
	}

	mutating func below(_ n: Int) -> Int { Int(next() % UInt64(n)) }
	mutating func chance(_ percent: Int) -> Bool { below(100) < percent }
}

private let keywordPool = ["a", "b", "k", "key", "ns/name", "a.b/c-d?", "/", "λ", "x1"]
private let symbolPool = ["a", "b", "+", "-", "/", "..", "ns/name", "clojure.core//", "λ→", "a#", "a'b", "%", "x/y/z"]
private let stringAlphabet: [String] = ["a", "b", "z", " ", "\"", "\\", "\n", "\t", "\r", "\u{8}", "\u{C}", "λ", "→", "🙂", "\u{1}", "\u{7F}", "é", "0", ";", "(", "#"]
private let charPool: [Unicode.Scalar] = ["a", "λ", "🙂", "\n", " ", "\t", "\u{8}", "\u{C}", "\r", "\u{1}", "\u{7F}", "(", "\\", "\"", ";", "é"]

private func randomString(_ rng: inout SplitMix64) -> String {
	(0..<rng.below(8)).map { _ in stringAlphabet[rng.below(stringAlphabet.count)] }.joined()
}

private func randomDouble(_ rng: inout SplitMix64) -> Double {
	switch rng.below(6) {
	case 0: return Double(rng.below(2000) - 1000)
	case 1: return Double(rng.below(1_000_000)) / 1000
	case 2: return Double(bitPattern: rng.next() & 0x7FEF_FFFF_FFFF_FFFF) // finite, positive
	case 3: return -Double(bitPattern: rng.next() & 0x7FEF_FFFF_FFFF_FFFF)
	case 4: return [Double.infinity, -.infinity, 0.0, -0.0, .leastNonzeroMagnitude, .greatestFiniteMagnitude, 1e7, 1e-3, 9999999.0, 0.001, 123456789.0][rng.below(11)]
	default: return Double(rng.below(100)) * 0.1
	}
}

private func randomTree(_ rng: inout SplitMix64, depth: Int) -> Value {
	let roll = depth == 0 ? rng.below(8) : rng.below(11)
	switch roll {
	case 0: return nil
	case 1: return Value(rng.chance(50))
	case 2: return Value(rng.chance(20) ? [Value.fixnumRange.lowerBound, Value.fixnumRange.upperBound, 0, -1][rng.below(4)] : rng.below(2_000_000) - 1_000_000)
	case 3: return Value(randomDouble(&rng))
	case 4: return Value(randomString(&rng))
	case 5: return Value(keyword: keywordPool[rng.below(keywordPool.count)])
	case 6: return Value(symbol: symbolPool[rng.below(symbolPool.count)])
	case 7: return Value(charPool[rng.below(charPool.count)])
	case 8: return Value(list: (0..<rng.below(5)).map { _ in randomTree(&rng, depth: depth - 1) })
	case 9: return Value((0..<rng.below(5)).map { _ in randomTree(&rng, depth: depth - 1) })
	default:
		var m = clj_map_empty()
		for _ in 0..<rng.below(5) {
			let k = randomTree(&rng, depth: depth - 1), v = randomTree(&rng, depth: depth - 1)
			withExtendedLifetime((k, v)) { m = clj_map_assoc(m, k.raw, v.raw) }
		}
		return Value(owning: m)
	}
}

// Text that Clojure itself prints back unchanged, one form per entry.
private let canonical: [String] = [
	"nil", "true", "false", "0", "1", "-1", "42", "4611686018427387903", "-4611686018427387904",
	"9223372036854775807", "-9223372036854775808",
	"0.0", "-0.0", "1.0", "-1.0", "0.5", "100000.0", "9999999.0", "1.0E7", "1.0E10", "0.001", "1.0E-4", "1.5E-4", "0.0015",
	"123.456", "1.7976931348623157E308", "4.9E-324", "3.141592653589793", "1.0E23", "0.002", "2.0E-4", "##Inf", "##-Inf", "##NaN",
	"\"\"", "\"hello\"", "\"a\\\"b\\\\c\\nd\\te\\rf\\bg\\fh\"", "\"λ→🙂\"", "\"\\u0001\\u007f\"",
	"\\a", "\\λ", "\\🙂", "\\newline", "\\space", "\\tab", "\\backspace", "\\formfeed", "\\return", "\\u0001", "\\(", "\\\\",
	":a", ":ns/name", ":a.b/c-d?", ":/", "a", "ns/name", "/", "clojure.core//", "+", "-", ".", "..", "a#", "λ→",
	"()", "(1)", "(1 2 3)", "(1 (2 (3)))", "[]", "[1]", "[1 [2 [3]]]", "{}", "{:a 1}", "{:a [1 2]}", "{[1 2] (3 4)}",
	"(quote x)", "(clojure.core/deref x)", "(nil true false)", "[\"s\" \\c :k sym 1.5 -2]",
]

extension CoreTests {
	@Suite struct PrinterTests {
		@Test func doublesFollowJavaToString() {
			let before = clj_debug_live_objects()
			let cases: [(Double, String)] = [
				(1, "1.0"), (100000, "100000.0"), (1e7, "1.0E7"), (1e10, "1.0E10"), (0.001, "0.001"), (0.0001, "1.0E-4"),
				(1.5e-3, "0.0015"), (1.5e-4, "1.5E-4"), (123.456, "123.456"), (-2.5, "-2.5"), (0.1, "0.1"), (0.1 + 0.2, "0.30000000000000004"),
				(1e23, "1.0E23"), (9007199254740992, "9.007199254740992E15"), (Double.pi, "3.141592653589793"),
				(.greatestFiniteMagnitude, "1.7976931348623157E308"), (.leastNonzeroMagnitude, "4.9E-324"),
				(.infinity, "##Inf"), (-.infinity, "##-Inf"), (.nan, "##NaN"), (0, "0.0"), (-0.0, "-0.0"), (9999999, "9999999.0"),
				(12345678.9, "1.23456789E7"), (1e-3 - 1e-20, "0.001"), (0.00099, "9.9E-4"),
			]
			for (d, text) in cases { #expect(Value(d).description == text, "\(d)") }
			#expect(clj_debug_live_objects() == before)
		}

		@Test func scalarsAndCollections() {
			for k in keywordPool { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				#expect(Value.nil_.description == "nil")
				#expect(Value(true).description == "true")
				#expect(Value(-7).description == "-7")
				#expect(Value("a\"b\\c\nd\te\rf\u{8}g\u{C}h\u{1}λ").description == "\"a\\\"b\\\\c\\nd\\te\\rf\\bg\\fh\\u0001λ\"")
				#expect(Value("\n" as Unicode.Scalar).description == "\\newline")
				#expect(Value("\u{7F}" as Unicode.Scalar).description == "\\u007f")
				#expect(Value(keyword: "ns/name").description == ":ns/name")
				#expect(Value(symbol: "ns/name").description == "ns/name")
				#expect(Value(list: []).description == "()")
				#expect(Value(list: [1, "a", nil]).description == "(1 \"a\" nil)")
				#expect(Value([1, [2, []], Value(list: [3])]).description == "[1 [2 []] (3)]")
				let tail: Value = [2, 3]
				let consVec = withExtendedLifetime(tail) { Value(owning: clj_cons_new(clj_fixnum(1), tail.raw)) }
				#expect(consVec.description == "(1 2 3)")
				let consNil = Value(owning: clj_cons_new(clj_fixnum(1), CLJ_NIL))
				#expect(consNil.description == "(1)")
				let m = try? Value(reading: "{:a 1, :b 2}")
				#expect(["{:a 1, :b 2}", "{:b 2, :a 1}"].contains(m?.description ?? ""))
				#expect(Value(owning: clj_map_empty()).description == "{}")
				// A type descriptor prints as its name, as a class does in Clojure.
				#expect(Value(borrowing: clj_from_ptr(UnsafeMutableRawPointer(mutating: clj_header_of(consNil.raw).pointee.type))).description == "cons")
				#expect("\(Value(list: [Value(symbol: "quote"), Value(symbol: "x")]))" == "(quote x)")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test(arguments: canonical) func canonicalTextRoundTrips(text: String) throws {
			for k in keywordPool { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				let v = try Value(reading: text)
				#expect(v.description == text)
				#expect(try Value(reading: v.description) == v || v.double?.isNaN == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func randomTreesRoundTrip() throws {
			for k in keywordPool { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				var rng = SplitMix64(state: 0xC10)
				for i in 0..<2000 {
					let v = randomTree(&rng, depth: 4)
					let text = v.description
					let back = try Value(reading: text)
					guard back == v, back.hashValue == v.hashValue, back.description == text else {
						Issue.record("round trip \(i): \(text) -> \(back.description)")
						break
					}
				}
				var forms: [Value] = []
				for _ in 0..<50 { forms.append(randomTree(&rng, depth: 3)) }
				let text = forms.map(\.description).joined(separator: "\n")
				#expect(try Value.readAll(text) == forms)
				#expect(try Value.readAll(forms.map(\.description).joined(separator: " ; comment\n ")) == forms)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
