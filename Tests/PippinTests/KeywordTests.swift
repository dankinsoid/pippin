// @ai-generated(solo)
import CljCore
import Dispatch
import Testing
@testable import Pippin

private func text(_ s: clj_value) -> String? {
	guard clj_is_string(s) else { return nil }
	return String(cString: clj_string_bytes(s))
}

extension CoreTests {
	@Suite struct KeywordTests {
		// The first intern of a name allocates for the life of the process; a repeat must not.
		@Test func internIsIdentityAndImmortal() {
			let k1 = clj_keyword_from_cstr("app/state")
			let before = clj_debug_live_objects()
			let k2 = clj_keyword_from_cstr("app/state")
			let ns = clj_string_from_cstr("app")
			let name = clj_string_from_cstr("state")
			let k3 = clj_keyword_intern(ns, name)
			clj_release(ns)
			clj_release(name)
			#expect(k1 == k2 && k2 == k3)
			#expect(clj_is_keyword(k1))
			#expect(String(cString: clj_type_name(k1)) == "keyword")
			#expect(text(clj_keyword_ns(k1)) == "app")
			#expect(text(clj_keyword_name(k1)) == "state")
			#expect(clj_is_shared(k1))
			#expect(!clj_is_unique(k1))
			#expect(clj_debug_all_shared(clj_keyword_of(k1).pointee.sym))
			_ = clj_retain(k1)
			clj_release(k1)
			clj_release(k1)
			#expect(clj_equals(k1, k2))
			#expect(clj_hash(k1) == clj_hash(k2))
			#expect(clj_debug_live_objects() == before)
		}

		@Test func distinctNamesAreDistinct() {
			let a = clj_keyword_from_cstr("a")
			let nsA = clj_keyword_from_cstr("ns/a")
			let b = clj_keyword_from_cstr("b")
			let slash = clj_keyword_from_cstr("/")
			#expect(a != nsA && a != b && a != slash)
			#expect(!clj_equals(a, nsA))
			#expect(!clj_equals(a, b))
			#expect(clj_is_nil(clj_keyword_ns(a)))
			#expect(clj_is_nil(clj_keyword_ns(slash)))
			#expect(text(clj_keyword_name(slash)) == "/")
			#expect(clj_hash(a) != clj_hash(nsA))
		}

		@Test func keywordSymbolStringWithSameTextDiffer() {
			let before = clj_debug_live_objects()
			let k = clj_keyword_from_cstr("x/y")
			let live = clj_debug_live_objects()
			let s = clj_symbol_from_cstr("x/y")
			let str = clj_string_from_cstr("x/y")
			#expect(!clj_equals(k, s) && !clj_equals(s, k))
			#expect(!clj_equals(k, str) && !clj_equals(str, k))
			#expect(!clj_equals(s, str) && !clj_equals(str, s))
			#expect(clj_hash(k) == clj_hash(s) &+ 0x9e37_79b9)
			#expect(clj_hash(k) != clj_hash(str))
			clj_release(s)
			clj_release(str)
			#expect(clj_debug_live_objects() == live)
			#expect(before <= live)
		}

		@Test func hashIsCachedOnce() {
			let k = clj_keyword_from_cstr("hash-cache/kw")
			// A previous test run in this process may have hashed it already.
			let h = clj_hash(k)
			#expect(h != 0)
			#expect(clj_debug_cached_hash(k) == h)
			#expect(clj_hash(k) == h)
			#expect(clj_debug_cached_hash(k) == h)
		}

		@Test func concurrentInternYieldsOnePointer() {
			let threads = 8
			let names = (0..<64).map { "race/kw-\($0)" }
			let firstRound = internAll(names, threads: threads)
			#expect(firstRound.allSatisfy { $0 == firstRound[0] })
			let after = clj_debug_live_objects()
			let secondRound = internAll(names, threads: threads)
			#expect(secondRound[0] == firstRound[0])
			#expect(secondRound.allSatisfy { $0 == firstRound[0] })
			#expect(clj_debug_live_objects() == after)
		}

		// Each thread writes its own slot, so the raw pointer is safe to share.
		private struct Slots: @unchecked Sendable {
			let base: UnsafeMutablePointer<[clj_value]>
		}

		// One entry per thread, the keywords in `names` order.
		private func internAll(_ names: [String], threads: Int) -> [[clj_value]] {
			var results = [[clj_value]](repeating: [], count: threads)
			results.withUnsafeMutableBufferPointer { buf in
				let slots = Slots(base: buf.baseAddress!)
				DispatchQueue.concurrentPerform(iterations: threads) { t in
					var mine: [clj_value] = []
					for n in names { mine.append(clj_keyword_from_cstr(n)) }
					(slots.base + t).pointee = mine
				}
			}
			return results
		}
	}
}
