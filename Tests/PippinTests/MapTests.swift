// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private let missing = clj_fixnum(-7777)

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
}

private struct RefMap {
	var pairs: [(key: clj_value, value: clj_value)] = []

	func get(_ key: clj_value) -> clj_value? {
		pairs.first { clj_equals($0.key, key) }?.value
	}

	mutating func assoc(_ key: clj_value, _ value: clj_value) {
		if let i = pairs.firstIndex(where: { clj_equals($0.key, key) }) {
			pairs[i].value = value
		} else {
			pairs.append((key, value))
		}
	}

	mutating func dissoc(_ key: clj_value) {
		pairs.removeAll { clj_equals($0.key, key) }
	}
}

private func entries(_ map: clj_value) -> [(clj_value, clj_value)] {
	var out: [(clj_value, clj_value)] = []
	withUnsafeMutablePointer(to: &out) { p in
		clj_map_each(map, { k, v, ctx in
			ctx!.assumingMemoryBound(to: [(clj_value, clj_value)].self).pointee.append((k, v))
			return true
		}, p)
	}
	return out
}

private func matches(_ map: clj_value, _ ref: RefMap) -> Bool {
	guard clj_map_count(map) == UInt32(ref.pairs.count) else { return false }
	for (k, v) in ref.pairs where clj_map_get(map, k, missing) != v || !clj_map_contains(map, k) {
		return false
	}
	let seen = entries(map)
	return seen.count == ref.pairs.count && Set(seen.map(\.0)).count == seen.count
}

private func build(_ ref: RefMap) -> clj_value {
	var m = clj_map_empty()
	for (k, v) in ref.pairs { m = clj_map_assoc(m, k, v) }
	return m
}

enum HashMode: CaseIterable {
	case normal, low6Bits, high7Bits, mod7

	var override: (@convention(c) (clj_value) -> UInt32)? {
		switch self {
		case .normal: return nil
		case .low6Bits: return { v in UInt32(truncatingIfNeeded: v >> 1) & 0x3F }
		case .high7Bits: return { v in (UInt32(truncatingIfNeeded: v >> 1) & 0x7F) << 25 }
		case .mod7: return { v in UInt32(truncatingIfNeeded: v >> 1) % 7 }
		}
	}
}

extension CoreTests {
	@Suite struct MapTests {
		@Test func emptyMap() {
			let before = clj_debug_live_objects()
			let e = clj_map_empty()
			#expect(clj_map_count(e) == 0)
			#expect(clj_map_get(e, clj_fixnum(1), missing) == missing)
			#expect(!clj_map_contains(e, CLJ_NIL))
			#expect(entries(e).isEmpty)
			#expect(!clj_is_unique(e))
			#expect(String(cString: clj_type_name(e)) == "map")
			_ = clj_retain(e)
			clj_release(e)
			clj_release(e)
			#expect(clj_map_empty() == e)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func assocGetDissoc() {
			let before = clj_debug_live_objects()
			var m = clj_map_empty()
			m = clj_map_assoc(m, clj_fixnum(1), clj_fixnum(10))
			m = clj_map_assoc(m, CLJ_NIL, clj_fixnum(20))
			m = clj_map_assoc(m, CLJ_TRUE, CLJ_FALSE)
			m = clj_map_assoc(m, clj_char(97), CLJ_NIL)
			#expect(clj_map_count(m) == 4)
			#expect(clj_map_get(m, clj_fixnum(1), missing) == clj_fixnum(10))
			#expect(clj_map_get(m, CLJ_NIL, missing) == clj_fixnum(20))
			#expect(clj_map_get(m, CLJ_TRUE, missing) == CLJ_FALSE)
			#expect(clj_map_get(m, clj_char(97), missing) == CLJ_NIL)
			#expect(clj_map_contains(m, clj_char(97)))
			#expect(clj_map_get(m, clj_fixnum(2), missing) == missing)
			#expect(!clj_map_contains(m, CLJ_FALSE))

			m = clj_map_assoc(m, clj_fixnum(1), clj_fixnum(11))
			#expect(clj_map_count(m) == 4)
			#expect(clj_map_get(m, clj_fixnum(1), missing) == clj_fixnum(11))

			let same = clj_map_dissoc(m, clj_fixnum(99))
			#expect(same == m)
			m = clj_map_dissoc(m, CLJ_NIL)
			#expect(clj_map_count(m) == 3)
			#expect(!clj_map_contains(m, CLJ_NIL))
			#expect(clj_map_get(m, CLJ_NIL, missing) == missing)

			for k in [clj_fixnum(1), CLJ_TRUE, clj_char(97)] { m = clj_map_dissoc(m, k) }
			#expect(clj_map_count(m) == 0)
			#expect(entries(m).isEmpty)
			clj_release(m)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func structuralSharingKeepsOldVersion() {
			let before = clj_debug_live_objects()
			var m1 = clj_map_empty()
			for i in 0..<200 { m1 = clj_map_assoc(m1, clj_fixnum(i), clj_fixnum(i)) }
			let m2 = clj_map_assoc(clj_retain(m1), clj_fixnum(5), clj_fixnum(-5))
			let m3 = clj_map_dissoc(clj_retain(m1), clj_fixnum(7))
			let m4 = clj_map_assoc(clj_retain(m1), clj_fixnum(1000), clj_fixnum(1000))
			#expect(m2 != m1 && m3 != m1 && m4 != m1)
			#expect(clj_map_get(m1, clj_fixnum(5), missing) == clj_fixnum(5))
			#expect(clj_map_get(m2, clj_fixnum(5), missing) == clj_fixnum(-5))
			#expect(clj_map_contains(m1, clj_fixnum(7)))
			#expect(!clj_map_contains(m3, clj_fixnum(7)))
			#expect(clj_map_count(m1) == 200 && clj_map_count(m3) == 199 && clj_map_count(m4) == 201)
			#expect(!clj_map_contains(m1, clj_fixnum(1000)))
			clj_release(m1)
			#expect(clj_map_get(m2, clj_fixnum(199), missing) == clj_fixnum(199))
			#expect(clj_map_get(m4, clj_fixnum(199), missing) == clj_fixnum(199))
			clj_release(m2)
			clj_release(m3)
			clj_release(m4)
			#expect(clj_debug_live_objects() == before)
		}

		// Reuse at rc == 1 is what keeps the address: -DCLJ_NO_REUSE turns every step below into a copy.
		@Test(.enabled(if: clj_reuse_enabled())) func uniqueMapIsUpdatedInPlace() {
			let before = clj_debug_live_objects()
			var m = clj_map_assoc(clj_map_empty(), clj_fixnum(1), clj_fixnum(1))
			#expect(clj_is_unique(m) == clj_reuse_enabled())
			let wrapper = m
			let root = clj_debug_map_root(m)
			let live = clj_debug_live_objects()

			m = clj_map_assoc(m, clj_fixnum(1), clj_fixnum(2))
			#expect(m == wrapper)
			#expect(clj_debug_map_root(m) == root)
			#expect(clj_debug_live_objects() == live)

			for i in 2..<100 { m = clj_map_assoc(m, clj_fixnum(i), clj_fixnum(i)) }
			#expect(m == wrapper)
			let grownRoot = clj_debug_map_root(m)
			let grownLive = clj_debug_live_objects()
			m = clj_map_dissoc(m, clj_fixnum(50))
			#expect(m == wrapper)
			#expect(clj_debug_live_objects() <= grownLive)

			_ = clj_retain(m)
			let copy = clj_map_assoc(m, clj_fixnum(1), clj_fixnum(3))
			#expect(copy != wrapper)
			#expect(clj_debug_map_root(copy) != grownRoot)
			#expect(clj_map_get(m, clj_fixnum(1), missing) == clj_fixnum(2))
			#expect(clj_map_get(copy, clj_fixnum(1), missing) == clj_fixnum(3))
			clj_release(copy)
			#expect(clj_is_unique(m) == clj_reuse_enabled())
			clj_release(m)
			#expect(clj_debug_live_objects() == before)
		}

		// Reuse at rc == 1 is what keeps the address: -DCLJ_NO_REUSE turns every step below into a copy.
		@Test(.enabled(if: clj_reuse_enabled())) func sharedMapKeepsChildrenShared() {
			let before = clj_debug_live_objects()
			var m = clj_map_empty()
			for i in 0..<50 { m = clj_map_assoc(m, clj_fixnum(i), clj_fixnum(i)) }
			clj_share(m)
			#expect(clj_debug_all_shared(m))
			let wrapper = m
			for i in 50..<300 {
				let key = clj_cons_new(clj_fixnum(i), CLJ_NIL)
				m = clj_map_assoc(m, clj_fixnum(i), key)
				clj_release(key)
			}
			for i in stride(from: 0, to: 300, by: 3) { m = clj_map_dissoc(m, clj_fixnum(i)) }
			#expect(m == wrapper)
			#expect(clj_debug_all_shared(m))
			#expect(clj_map_count(m) == 200)

			let copy = clj_map_assoc(clj_retain(m), clj_fixnum(1), CLJ_TRUE)
			#expect(!clj_is_shared(copy))
			#expect(clj_debug_all_shared(m))
			clj_release(copy)
			clj_release(m)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func mapEqualityAndHash() {
			let before = clj_debug_live_objects()
			var a = clj_map_empty()
			var b = clj_map_empty()
			for i in 0..<100 { a = clj_map_assoc(a, clj_fixnum(i), clj_fixnum(i * 2)) }
			for i in (0..<100).reversed() { b = clj_map_assoc(b, clj_fixnum(i), clj_fixnum(i * 2)) }
			#expect(clj_equals(a, b))
			#expect(clj_hash(a) == clj_hash(b))
			#expect(clj_debug_map_same_shape(a, b))

			let c = clj_map_assoc(clj_retain(b), clj_fixnum(0), clj_fixnum(1))
			#expect(!clj_equals(a, c))
			#expect(!clj_equals(a, clj_map_empty()))
			#expect(!clj_equals(a, clj_fixnum(1)))
			#expect(!clj_equals(clj_fixnum(1), a))

			var outer = clj_map_assoc(clj_map_empty(), a, clj_fixnum(42))
			#expect(clj_map_get(outer, b, missing) == clj_fixnum(42))
			#expect(clj_map_get(outer, c, missing) == missing)
			outer = clj_map_dissoc(outer, b)
			#expect(clj_map_count(outer) == 0)
			clj_release(outer)
			clj_release(a)
			clj_release(b)
			clj_release(c)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func heapKeysRoundTrip() {
			let kw = clj_keyword_from_cstr("map-key/kw")
			let before = clj_debug_live_objects()
			let str = clj_string_from_cstr("key")
			let dbl = clj_double_new(-0.0)
			let sym = clj_symbol_from_cstr("map-key/sym")
			var m = clj_map_empty()
			m = clj_map_assoc(m, kw, clj_fixnum(1))
			m = clj_map_assoc(m, str, clj_fixnum(2))
			m = clj_map_assoc(m, dbl, clj_fixnum(3))
			m = clj_map_assoc(m, sym, clj_fixnum(4))
			m = clj_map_assoc(m, clj_fixnum(0), clj_fixnum(5))
			#expect(clj_map_count(m) == 5)
			#expect(clj_map_get(m, clj_keyword_from_cstr("map-key/kw"), missing) == clj_fixnum(1))
			let str2 = clj_string_from_cstr("key")
			let dbl2 = clj_double_new(0.0)
			let sym2 = clj_symbol_from_cstr("map-key/sym")
			#expect(clj_map_get(m, str2, missing) == clj_fixnum(2))
			#expect(clj_map_get(m, dbl2, missing) == clj_fixnum(3))
			#expect(clj_map_get(m, sym2, missing) == clj_fixnum(4))
			#expect(clj_map_get(m, clj_fixnum(0), missing) == clj_fixnum(5))
			#expect(clj_map_get(m, clj_fixnum(3), missing) == missing)
			m = clj_map_dissoc(m, str2)
			m = clj_map_dissoc(m, dbl2)
			m = clj_map_dissoc(m, sym2)
			m = clj_map_dissoc(m, kw)
			#expect(clj_map_count(m) == 1)
			for v in [str, dbl, sym, str2, dbl2, sym2, m] { clj_release(v) }
			#expect(clj_debug_live_objects() == before)
		}

		@Test func hashIsCachedAndResetInPlace() {
			let before = clj_debug_live_objects()
			var m = clj_map_assoc(clj_map_empty(), clj_fixnum(1), clj_fixnum(1))
			#expect(clj_debug_cached_hash(m) == 0)
			let h1 = clj_hash(m)
			#expect(clj_debug_cached_hash(m) == h1)
			#expect(clj_hash(m) == h1)

			let same = clj_map_assoc(m, clj_fixnum(1), clj_fixnum(1))
			#expect(same == m)
			#expect(clj_debug_cached_hash(m) == h1)

			m = clj_map_assoc(m, clj_fixnum(2), clj_fixnum(2))
			#expect(clj_debug_cached_hash(m) == 0)
			let h2 = clj_hash(m)
			#expect(h2 != h1)
			m = clj_map_dissoc(m, clj_fixnum(2))
			#expect(clj_debug_cached_hash(m) == 0)
			#expect(clj_hash(m) == h1)

			let copy = clj_map_assoc(clj_retain(m), clj_fixnum(3), clj_fixnum(3))
			#expect(copy != m)
			#expect(clj_debug_cached_hash(m) == h1)
			#expect(clj_debug_cached_hash(copy) == 0)
			clj_release(copy)
			clj_release(m)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func heapValuesAreOwned() {
			let before = clj_debug_live_objects()
			let v = clj_cons_new(clj_fixnum(1), CLJ_NIL)
			var m = clj_map_assoc(clj_map_empty(), clj_fixnum(1), v)
			#expect(!clj_is_unique(v))
			clj_release(v)
			#expect(clj_is_unique(v) == clj_reuse_enabled())
			#expect(clj_map_get(m, clj_fixnum(1), missing) == v)
			let live = clj_debug_live_objects()
			m = clj_map_assoc(m, clj_fixnum(1), CLJ_NIL)
			#expect(clj_debug_live_objects() == live - 1)
			clj_release(m)
			#expect(clj_debug_live_objects() == before)
		}

		@Test(arguments: HashMode.allCases) func randomOpsMatchReference(mode: HashMode) {
			let before = clj_debug_live_objects()
			clj_debug_set_hash_override(mode.override)
			defer { clj_debug_set_hash_override(nil) }

			var keys: [clj_value] = (0..<500).map { clj_fixnum($0) }
			keys += [CLJ_NIL, CLJ_TRUE, CLJ_FALSE, clj_fixnum(-1), clj_fixnum(Int(CLJ_FIXNUM_MAX))]
			keys += (97..<107).map { clj_char(UInt32($0)) }

			var rng = SplitMix64(state: 0x5eed)
			var ref = RefMap()
			var m = clj_map_empty()
			var snapshots: [(map: clj_value, ref: RefMap)] = []

			for step in 0..<12_000 {
				let key = keys[rng.below(keys.count)]
				switch rng.below(10) {
				case 0..<5:
					let value = clj_fixnum(rng.below(40))
					m = clj_map_assoc(m, key, value)
					ref.assoc(key, value)
				case 5..<9:
					m = clj_map_dissoc(m, key)
					ref.dissoc(key)
				default:
					#expect(clj_map_get(m, key, missing) == (ref.get(key) ?? missing))
				}
				guard matches(m, ref) else {
					Issue.record("mismatch at step \(step) (\(mode))")
					break
				}
				if step % 400 == 399 {
					snapshots.append((clj_retain(m), ref))
					if snapshots.count > 3 {
						let old = snapshots.removeFirst()
						#expect(matches(old.map, old.ref))
						clj_release(old.map)
					}
				}
				if step % 1500 == 1499 {
					let rebuilt = build(ref)
					#expect(clj_debug_map_same_shape(m, rebuilt), "shape at step \(step) (\(mode))")
					#expect(clj_equals(m, rebuilt))
					if mode == .normal { #expect(clj_hash(m) == clj_hash(rebuilt)) }
					clj_release(rebuilt)
				}
			}
			for s in snapshots {
				#expect(matches(s.map, s.ref))
				clj_release(s.map)
			}
			for (k, _) in ref.pairs { m = clj_map_dissoc(m, k) }
			#expect(clj_map_count(m) == 0)
			clj_release(m)
			#expect(clj_debug_live_objects() == before)
		}
	}
}
