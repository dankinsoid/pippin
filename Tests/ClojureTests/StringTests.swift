// @ai-generated(solo)
import CljCore
import Testing
@testable import Clojure

private func bytes(_ s: clj_value) -> [UInt8] {
	Array(UnsafeRawBufferPointer(start: clj_string_bytes(s), count: Int(clj_string_len(s))))
}

extension CoreTests {
	@Suite struct StringTests {
		@Test func constructionAndAccessors() {
			let before = clj_debug_live_objects()
			let s = clj_string_from_cstr("hello")
			#expect(clj_is_string(s))
			#expect(String(cString: clj_type_name(s)) == "string")
			#expect(clj_string_len(s) == 5)
			#expect(String(cString: clj_string_bytes(s)) == "hello")
			#expect(clj_is_unique(s) == clj_reuse_enabled())
			#expect(clj_debug_live_objects() == before + 1)
			clj_release(s)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func emptyString() {
			let before = clj_debug_live_objects()
			let a = clj_string_from_cstr("")
			let b = clj_string_new(nil, 0)
			#expect(clj_string_len(a) == 0 && clj_string_len(b) == 0)
			#expect(clj_string_bytes(a).pointee == 0)
			#expect(clj_string_bytes(b).pointee == 0)
			#expect(clj_equals(a, b))
			#expect(clj_hash(a) == clj_hash(b))
			clj_release(a)
			clj_release(b)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nonASCIIAndEmbeddedNUL() {
			let before = clj_debug_live_objects()
			let text = "λ→🙂"
			let s = Array(text.utf8).withUnsafeBufferPointer { buf in
				buf.withMemoryRebound(to: CChar.self) { clj_string_new($0.baseAddress, $0.count) }
			}
			#expect(Int(clj_string_len(s)) == text.utf8.count)
			#expect(bytes(s) == Array(text.utf8))
			#expect(clj_string_bytes(s)[Int(clj_string_len(s))] == 0)

			let raw: [UInt8] = [0x61, 0x00, 0x62]
			let n = raw.withUnsafeBufferPointer { buf in
				buf.withMemoryRebound(to: CChar.self) { clj_string_new($0.baseAddress, $0.count) }
			}
			let a = clj_string_from_cstr("a")
			#expect(clj_string_len(n) == 3)
			#expect(bytes(n) == raw)
			#expect(!clj_equals(n, a))
			#expect(clj_hash(n) != clj_hash(a))
			clj_release(s)
			clj_release(n)
			clj_release(a)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func equalityAndHashOfSeparateCopies() {
			let before = clj_debug_live_objects()
			let a = clj_string_from_cstr("clojure")
			let b = clj_string_new("clojure!", 7)
			let c = clj_string_from_cstr("clojurf")
			#expect(a != b)
			#expect(clj_equals(a, b))
			#expect(clj_equals(b, a))
			#expect(clj_hash(a) == clj_hash(b))
			#expect(!clj_equals(a, c))
			#expect(clj_hash(a) != clj_hash(c))
			#expect(!clj_equals(a, clj_fixnum(1)))
			#expect(!clj_equals(clj_fixnum(1), a))
			#expect(!clj_equals(a, CLJ_NIL))
			clj_release(a)
			clj_release(b)
			clj_release(c)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func murmur3KnownVectors() {
			// MurmurHash3_x86_32 with seed 0 over the raw bytes.
			let vectors: [(String, UInt32)] = [
				("", 0),
				("a", 0x3c2569b2),
				("abc", 0xb3dd93fa),
				("Hello, world!", 0xc0363e43),
				("The quick brown fox jumps over the lazy dog", 0x2e4ff723),
			]
			for (text, expected) in vectors {
				let s = clj_string_from_cstr(text)
				#expect(clj_hash(s) == (expected == 0 ? 1 : expected), "\(text)")
				clj_release(s)
			}
		}

		@Test func hashIsCachedOnce() {
			let before = clj_debug_live_objects()
			let s = clj_string_from_cstr("cache me")
			#expect(clj_debug_cached_hash(s) == 0)
			let h = clj_hash(s)
			#expect(h != 0)
			#expect(clj_debug_cached_hash(s) == h)
			#expect(clj_hash(s) == h)
			#expect(clj_debug_cached_hash(s) == h)
			clj_release(s)
			#expect(clj_debug_live_objects() == before)
		}
	}
}
