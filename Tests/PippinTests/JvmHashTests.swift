// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'jvm-hash-tests) " + source) }

// Every expected number is what (hash x) answers on JVM Clojure 1.12.6 for the same x; the fuzzer compares the rest.
extension CoreTests {
	@Suite struct JvmHashTests {
		init() throws {
			clj_init()
			_ = try cljEvalScoped("""
			(ns jvm-hash-tests)
			(defrecord R [a b])
			(deftype T [])
			(defn msg [f] (try (f) (catch :default e (ex-message e))))
			""")
		}

		@Test func scalars() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(mapv jvm-hash [nil true false 0 1 -1 42 9223372036854775807 -9223372036854775808])") == [
					0, 1231, 1237, 0, 1_392_991_556, 1_651_860_712, 1_871_679_806, -2_106_506_049, 1_366_273_829,
				])
				#expect(try eval("(mapv jvm-hash [1.0 -0.0 0.0 ##NaN ##Inf ##-Inf 1.5 0.1])") == [1_072_693_248, 0, 0, 2_146_959_360, 2_146_435_072, -1_048_576, 1_073_217_536, -1_507_852_285])
				#expect(try eval("(mapv jvm-hash [1N 12345678901234567890N -12345678901234567890N 1/3 -2/7 (/ 12345678901234567890N 7)])") == [1_392_991_556, -1_436_577_082, 1_436_577_082, 2, -7, -1_436_577_087])
				#expect(try eval("(mapv jvm-hash [0M 1M 1.0M 1.50M 100M -2.5M 0.000M 123456789012345678901234567890.123M])") == [0, 31, 31, 466, 29, -774, 0, -285_907_324])
				#expect(try eval("(mapv jvm-hash [\\a \\space (char 233) \\A])") == [97, 32, 233, 65])
				// The JVM hashes UTF-16 units: a code point past the BMP is two of them.
				#expect(try eval("(mapv jvm-hash [\"\" \"a\" \"ab\" \"abc\" (str \"h\" (char 233) \"llo\") (str (char 0x65e5) (char 0x672c)) (str (char 0x1D11E))])") == [
					0, 1_455_541_201, 1_707_819_681, 74_834_163, 162_369_042, 1_762_503_263, -857_155_403,
				])
				#expect(try eval("(mapv jvm-hash [:a :ns/a 'a 'ns/a :abc/def-ghi])") == [-2_123_407_586, -2_123_428_585, -482_876_059, -482_897_058, -522_790_300])
				#expect(try eval("(mapv jvm-hash [#uuid \"550e8400-e29b-41d4-a716-446655440000\" #inst \"2020-01-01T00:00:00.123-00:00\" #inst \"1960-01-01\"])") == [1_170_702_770, 1_583_802_644, 2_086_587_318])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func collections() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(mapv jvm-hash [[] [1 2] '(1 2) () {} {:a 1} #{} #{1 2} (first {:a 1}) (seq [1 2]) (range 3) (lazy-seq nil)])") == [
					-2_017_569_654, 156_247_261, 156_247_261, -2_017_569_654, -15_128_758, 1_772_842_048, -15_128_758, 460_223_544, -1_809_393_901,
					156_247_261, -1_475_093_015, -2_017_569_654,
				])
				#expect(try eval("(mapv jvm-hash [(sorted-map 1 2) (sorted-set 3 1) clojure.lang.PersistentQueue/EMPTY (conj clojure.lang.PersistentQueue/EMPTY 1 2)])") == [
					1_952_097_988, 1_763_362_998, -2_017_569_654, 156_247_261,
				])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// defrecord xors in the hash of the class name, the namespace's dashes munged: jvm_hash_tests.R.
		@Test func recordsCarryTheirTypeName() throws {
			#expect(try eval("[(jvm-hash (->R 1 2)) (jvm-hash (assoc (->R 1 2) :c 3)) (= (jvm-hash (->R 1 2)) (jvm-hash {:a 1 :b 2}))]") == [-1_113_982_419, -801_084_744, false])
		}

		@Test func valuesTheJvmHashesByIdentityAreRefused() throws {
			#expect(try eval("[(msg #(jvm-hash (->T))) (msg #(jvm-hash (atom 1))) (msg #(jvm-hash inc)) (msg #(jvm-hash #\"a\")) (msg #(jvm-hash (parse-uri \"a:b\"))) (msg #(jvm-hash (char 0x1F600)))]") == [
				"jvm-hash of type jvm-hash-tests.T: a deftype hashes by identity on the JVM",
				"jvm-hash of type atom: the JVM hashes it by identity, or it has no JVM counterpart",
				"jvm-hash of type fn: the JVM hashes it by identity, or it has no JVM counterpart",
				"jvm-hash of type regex: the JVM hashes it by identity, or it has no JVM counterpart",
				"jvm-hash of type URI: equal here by its text, where java.net.URI folds case and escapes",
				"jvm-hash of type char: past the BMP, where a JVM char is one UTF-16 unit",
			])
			#expect(try eval("(msg #(jvm-hash [1 [2 (atom 3)]]))") == "jvm-hash of type atom: the JVM hashes it by identity, or it has no JVM counterpart")
		}
	}
}
