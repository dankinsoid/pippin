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
}

private func items(_ vec: clj_value, limit: Int = .max) -> [clj_value] {
	var out: (items: [clj_value], limit: Int) = ([], limit)
	withUnsafeMutablePointer(to: &out) { p in
		clj_vector_each(vec, { item, ctx in
			let out = ctx!.assumingMemoryBound(to: ([clj_value], Int).self)
			out.pointee.0.append(item)
			return out.pointee.0.count < out.pointee.1
		}, p)
	}
	return out.items
}

private func matches(_ vec: clj_value, _ ref: [clj_value]) -> Bool {
	guard clj_vector_count(vec) == UInt32(ref.count) else { return false }
	for (i, x) in ref.enumerated() where clj_vector_nth(vec, UInt32(i)) != x { return false }
	return items(vec) == ref && clj_vector_peek(vec) == (ref.last ?? CLJ_NIL)
}

private func build(_ n: Int) -> clj_value {
	var v = clj_vector_empty()
	for i in 0..<n { v = clj_vector_conj(v, clj_fixnum(i)) }
	return v
}

// Tree heights: ≤ 1056 elements shift 5, ≤ 32800 shift 10, then 15. 33 * 1024 crosses into 15.
private let boundarySizes = [0, 1, 31, 32, 33, 64, 1024, 1025, 1056, 1057, 32768, 32769, 32800, 32801, 33 * 1024]

extension CoreTests {
	@Suite struct VectorTests {
		@Test func emptyVector() {
			let before = clj_debug_live_objects()
			let e = clj_vector_empty()
			#expect(clj_vector_count(e) == 0)
			#expect(clj_vector_peek(e) == CLJ_NIL)
			#expect(items(e).isEmpty)
			#expect(!clj_is_unique(e))
			#expect(clj_is_vector(e))
			#expect(!clj_is_vector(clj_fixnum(1)))
			#expect(String(cString: clj_type_name(e)) == "vector")
			_ = clj_retain(e)
			clj_release(e)
			clj_release(e)
			#expect(clj_vector_empty() == e)
			let popped = clj_vector_pop(clj_vector_conj(e, clj_fixnum(1)))
			#expect(popped == e)
			#expect(clj_debug_live_objects() == before)
		}

		@Test(arguments: boundarySizes) func conjAndNthAcrossBoundaries(n: Int) {
			let before = clj_debug_live_objects()
			let v = build(n)
			#expect(clj_vector_count(v) == UInt32(n))
			for i in 0..<n where clj_vector_nth(v, UInt32(i)) != clj_fixnum(i) {
				Issue.record("nth \(i) of \(n)")
				break
			}
			#expect(clj_vector_peek(v) == (n == 0 ? CLJ_NIL : clj_fixnum(n - 1)))
			#expect(items(v) == (0..<n).map { clj_fixnum($0) })
			let raw = (0..<n).map { clj_fixnum($0) }
			let fromArray = raw.withUnsafeBufferPointer { clj_vector_from_array($0.baseAddress, UInt32(n)) }
			#expect(clj_equals(v, fromArray))
			#expect(clj_vector_count(fromArray) == UInt32(n))
			clj_release(fromArray)
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func popBackDownThroughHeightShrink() {
			let before = clj_debug_live_objects()
			let n = 33 * 1024 + 5
			var v = build(n)
			var count = n
			while count > 0 {
				v = clj_vector_pop(v)
				count -= 1
				#expect(clj_vector_count(v) == UInt32(count))
				#expect(clj_vector_peek(v) == (count == 0 ? CLJ_NIL : clj_fixnum(count - 1)))
				if boundarySizes.contains(count) || count % 997 == 0 {
					#expect(matches(v, (0..<count).map { clj_fixnum($0) }), "contents at \(count)")
				}
			}
			#expect(v == clj_vector_empty())
			for i in 0..<100 { v = clj_vector_conj(v, clj_fixnum(i)) }
			#expect(matches(v, (0..<100).map { clj_fixnum($0) }))
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		// PersistentVector.pop keeps the vector's meta, down to the empty vector, and is O(1): core's pop reaches clj_vector_pop.
		// @ai-generated(solo)
		@Test func corePopKeepsMetaAndShape() throws {
			clj_init()
			_ = Value(keyword: "m")
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("(pop [1 2 3])") == [1, 2])
				#expect(try cljEval("(= (pop (vec (range 40))) (vec (range 39)))") == true)
				#expect(try cljEval("[(meta (pop (with-meta [1 2] {:m 1}))) (meta (pop (with-meta [1] {:m 1}))) (meta (pop (with-meta (vec (range 40)) {:m 1})))]").description == "[{:m 1} {:m 1} {:m 1}]")
				#expect(try cljEval("(loop [v (vec (range 50000)) n 0] (if (seq v) (recur (pop v) (+ n (peek v))) n))") == Value(50000 * 49999 / 2))
				#expect(cljEvalError("(pop [])")?.contains("Can't pop empty vector") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func assocAtEveryPosition() {
			let before = clj_debug_live_objects()
			let n = 1025
			var v = build(n)
			for i in 0..<n { v = clj_vector_assoc(v, UInt32(i), clj_fixnum(-i)) }
			#expect(matches(v, (0..<n).map { clj_fixnum(-$0) }))
			v = clj_vector_assoc(v, UInt32(n), clj_fixnum(n))
			#expect(clj_vector_count(v) == UInt32(n + 1))
			#expect(clj_vector_peek(v) == clj_fixnum(n))
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func structuralSharingKeepsOldVersion() {
			let before = clj_debug_live_objects()
			let n = 2000
			let v1 = build(n)
			let ref = (0..<n).map { clj_fixnum($0) }
			let v2 = clj_vector_conj(clj_retain(v1), clj_fixnum(n))
			let v3 = clj_vector_assoc(clj_retain(v1), 5, clj_fixnum(-5))
			let v4 = clj_vector_assoc(clj_retain(v1), UInt32(n - 1), clj_fixnum(-1))
			let v5 = clj_vector_pop(clj_retain(v1))
			#expect(v2 != v1 && v3 != v1 && v4 != v1 && v5 != v1)
			#expect(matches(v1, ref))
			#expect(matches(v2, ref + [clj_fixnum(n)]))
			#expect(clj_vector_nth(v3, 5) == clj_fixnum(-5))
			#expect(clj_vector_nth(v4, UInt32(n - 1)) == clj_fixnum(-1))
			#expect(clj_vector_count(v5) == UInt32(n - 1))
			clj_release(v1)
			#expect(matches(v2, ref + [clj_fixnum(n)]))
			#expect(matches(v5, Array(ref.dropLast())))
			#expect(clj_vector_nth(v3, 1999) == clj_fixnum(1999))
			for v in [v2, v3, v4, v5] { clj_release(v) }
			#expect(clj_debug_live_objects() == before)
		}

		// Reuse at rc == 1 is what keeps the address: -DCLJ_NO_REUSE turns every step below into a copy.
		@Test(.enabled(if: clj_reuse_enabled())) func uniqueVectorIsUpdatedInPlace() {
			let before = clj_debug_live_objects()
			var v = build(40)
			#expect(clj_is_unique(v) == clj_reuse_enabled())
			let wrapper = v
			let root = clj_debug_vector_root(v)
			let tail = clj_debug_vector_tail(v)
			let live = clj_debug_live_objects()

			v = clj_vector_assoc(v, 39, clj_fixnum(-39))
			v = clj_vector_assoc(v, 3, clj_fixnum(-3))
			#expect(v == wrapper && clj_debug_vector_root(v) == root && clj_debug_vector_tail(v) == tail)
			#expect(clj_debug_live_objects() == live)

			// A resized tail may move between size classes; the object count is the stable signal.
			v = clj_vector_conj(v, clj_fixnum(40))
			#expect(v == wrapper && clj_debug_vector_root(v) == root)
			#expect(clj_debug_live_objects() == live)

			for i in 41..<2000 { v = clj_vector_conj(v, clj_fixnum(i)) }
			#expect(v == wrapper)
			let grownRoot = clj_debug_vector_root(v)
			let grownLive = clj_debug_live_objects()
			v = clj_vector_assoc(v, 100, clj_fixnum(-100))
			v = clj_vector_pop(v)
			#expect(v == wrapper && clj_debug_vector_root(v) == grownRoot)
			#expect(clj_debug_live_objects() == grownLive)
			let grownTail = clj_debug_vector_tail(v)

			let copy = clj_vector_assoc(clj_retain(v), 100, clj_fixnum(100))
			let copy2 = clj_vector_conj(clj_retain(v), clj_fixnum(-1))
			let copy3 = clj_vector_pop(clj_retain(v))
			#expect(copy != wrapper && copy2 != wrapper && copy3 != wrapper)
			#expect(clj_debug_vector_root(copy) != grownRoot)
			#expect(clj_debug_vector_tail(copy) == grownTail)
			#expect(clj_debug_vector_root(copy2) == grownRoot)
			#expect(clj_debug_vector_tail(copy2) != grownTail)
			#expect(clj_debug_vector_root(copy3) == grownRoot)
			#expect(clj_debug_vector_tail(copy3) != grownTail)
			#expect(v == wrapper && clj_debug_vector_root(v) == grownRoot && clj_debug_vector_tail(v) == grownTail)
			#expect(clj_vector_nth(v, 100) == clj_fixnum(-100))
			#expect(clj_vector_nth(copy, 100) == clj_fixnum(100))
			#expect(clj_vector_count(copy2) == 2000 && clj_vector_count(copy3) == 1998)
			clj_release(copy)
			clj_release(copy2)
			clj_release(copy3)
			#expect(clj_is_unique(v) == clj_reuse_enabled())
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		// Reuse at rc == 1 is what keeps the address: -DCLJ_NO_REUSE turns every step below into a copy.
		@Test(.enabled(if: clj_reuse_enabled())) func sharedVectorKeepsChildrenShared() {
			let before = clj_debug_live_objects()
			var v = build(50)
			clj_share(v)
			#expect(clj_debug_all_shared(v))
			let wrapper = v
			for i in 50..<1100 {
				let cell = clj_cons_new(clj_fixnum(i), CLJ_NIL)
				v = clj_vector_conj(v, cell)
				clj_release(cell)
			}
			let cell = clj_cons_new(clj_fixnum(7), CLJ_NIL)
			v = clj_vector_assoc(v, 7, cell)
			clj_release(cell)
			for _ in 0..<40 { v = clj_vector_pop(v) }
			#expect(v == wrapper)
			#expect(clj_debug_all_shared(v))
			#expect(clj_vector_count(v) == 1060)

			let copy = clj_vector_conj(clj_retain(v), CLJ_TRUE)
			#expect(!clj_is_shared(copy))
			#expect(clj_debug_all_shared(v))
			clj_release(copy)
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func equalityAndHash() {
			let before = clj_debug_live_objects()
			let a = build(1100)
			let raw = (0..<1100).map { clj_fixnum($0) }
			let b = raw.withUnsafeBufferPointer { clj_vector_from_array($0.baseAddress, 1100) }
			#expect(clj_equals(a, b))
			#expect(clj_hash(a) == clj_hash(b))
			#expect(clj_hash(a) != clj_hash(clj_vector_empty()))

			let c = clj_vector_assoc(clj_retain(b), 0, clj_fixnum(1))
			let d = clj_vector_pop(clj_retain(b))
			#expect(!clj_equals(a, c))
			#expect(!clj_equals(a, d))
			#expect(!clj_equals(a, clj_vector_empty()))
			#expect(!clj_equals(a, clj_fixnum(1)))
			#expect(!clj_equals(clj_fixnum(1), a))
			#expect(clj_hash(a) != clj_hash(c))

			let e = clj_vector_conj(clj_vector_empty(), clj_string_from_cstr("x"))
			clj_release(clj_vector_nth(e, 0))
			let f = clj_vector_conj(clj_vector_empty(), clj_string_from_cstr("x"))
			clj_release(clj_vector_nth(f, 0))
			#expect(clj_equals(e, f))
			#expect(clj_hash(e) == clj_hash(f))
			#expect(clj_equals(clj_vector_empty(), clj_vector_empty()))
			for v in [a, b, c, d, e, f] { clj_release(v) }
			#expect(clj_debug_live_objects() == before)
		}

		@Test func hashIsCachedAndResetInPlace() {
			let before = clj_debug_live_objects()
			var v = build(3)
			#expect(clj_debug_cached_hash(v) == 0)
			let h1 = clj_hash(v)
			#expect(clj_debug_cached_hash(v) == h1)
			#expect(clj_hash(v) == h1)

			v = clj_vector_conj(v, clj_fixnum(3))
			#expect(clj_debug_cached_hash(v) == 0)
			let h2 = clj_hash(v)
			#expect(h2 != h1)
			v = clj_vector_pop(v)
			#expect(clj_debug_cached_hash(v) == 0)
			#expect(clj_hash(v) == h1)

			let copy = clj_vector_assoc(clj_retain(v), 0, clj_fixnum(9))
			#expect(copy != v)
			#expect(clj_debug_cached_hash(v) == h1)
			#expect(clj_debug_cached_hash(copy) == 0)
			clj_release(copy)
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func vectorsAsMapKeys() {
			let before = clj_debug_live_objects()
			let k1 = build(40)
			let k2 = build(40)
			let k3 = build(41)
			var m = clj_map_assoc(clj_map_empty(), k1, clj_fixnum(1))
			m = clj_map_assoc(m, k3, clj_fixnum(3))
			#expect(clj_map_count(m) == 2)
			#expect(clj_map_get(m, k2, CLJ_NIL) == clj_fixnum(1))
			#expect(clj_map_get(m, clj_vector_empty(), CLJ_NIL) == CLJ_NIL)
			m = clj_map_assoc(m, k2, clj_fixnum(2))
			#expect(clj_map_count(m) == 2)
			#expect(clj_map_get(m, k1, CLJ_NIL) == clj_fixnum(2))
			m = clj_map_dissoc(m, k2)
			#expect(!clj_map_contains(m, k1))
			for v in [k1, k2, k3, m] { clj_release(v) }
			#expect(clj_debug_live_objects() == before)
		}

		@Test func heapValuesAreOwned() {
			let before = clj_debug_live_objects()
			let cell = clj_cons_new(clj_fixnum(1), CLJ_NIL)
			var v = clj_vector_conj(clj_vector_empty(), cell)
			#expect(!clj_is_unique(cell))
			clj_release(cell)
			#expect(clj_is_unique(cell) == clj_reuse_enabled())
			#expect(clj_vector_nth(v, 0) == cell)
			let live = clj_debug_live_objects()
			v = clj_vector_assoc(v, 0, CLJ_NIL)
			#expect(clj_debug_live_objects() == live - 1)
			let other = clj_cons_new(clj_fixnum(2), CLJ_NIL)
			v = clj_vector_conj(v, other)
			clj_release(other)
			v = clj_vector_pop(v)
			#expect(clj_debug_live_objects() == live - 1)
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func eachStopsEarly() {
			let before = clj_debug_live_objects()
			let v = build(100)
			#expect(items(v, limit: 1) == [clj_fixnum(0)])
			#expect(items(v, limit: 33) == (0..<33).map { clj_fixnum($0) })
			#expect(items(v, limit: 100).count == 100)
			#expect(items(clj_vector_empty(), limit: 1).isEmpty)
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func randomOpsMatchReference() {
			let before = clj_debug_live_objects()
			var rng = SplitMix64(state: 0xbeef)
			var ref: [clj_value] = []
			var v = clj_vector_empty()
			var snapshots: [(vec: clj_value, ref: [clj_value])] = []

			for step in 0..<12_000 {
				let value = clj_fixnum(rng.below(1000))
				// Growth-biased so the trie gains height; pop dominates in a late phase to shrink it.
				let popWeight = step > 9000 ? 6 : 3
				let roll = rng.below(10)
				if roll < 9 - popWeight {
					v = clj_vector_conj(v, value)
					ref.append(value)
				} else if roll == 9 - popWeight, !ref.isEmpty {
					let i = rng.below(ref.count)
					v = clj_vector_assoc(v, UInt32(i), value)
					ref[i] = value
				} else if !ref.isEmpty {
					let i = rng.below(ref.count)
					#expect(clj_vector_nth(v, UInt32(i)) == ref[i])
					v = clj_vector_pop(v)
					ref.removeLast()
				}
				guard clj_vector_count(v) == UInt32(ref.count),
					clj_vector_peek(v) == (ref.last ?? CLJ_NIL)
				else {
					Issue.record("mismatch at step \(step)")
					break
				}
				if step % 500 == 499 {
					#expect(matches(v, ref), "contents at step \(step)")
					snapshots.append((clj_retain(v), ref))
					if snapshots.count > 3 {
						let old = snapshots.removeFirst()
						#expect(matches(old.vec, old.ref))
						clj_release(old.vec)
					}
				}
				if step % 3000 == 2999 {
					let rebuilt = ref.withUnsafeBufferPointer { clj_vector_from_array($0.baseAddress, UInt32(ref.count)) }
					#expect(clj_equals(v, rebuilt))
					#expect(clj_hash(v) == clj_hash(rebuilt))
					clj_release(rebuilt)
				}
			}
			for s in snapshots {
				#expect(matches(s.vec, s.ref))
				clj_release(s.vec)
			}
			while !ref.isEmpty {
				v = clj_vector_pop(v)
				ref.removeLast()
			}
			#expect(v == clj_vector_empty())
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}
	}

	// Tuples (vector.c, design §4 «Tuples»): clj_vector_from_array of one to six items is the inline layout.
	@Suite struct TupleTests {
		private func fromArray(_ xs: [clj_value]) -> clj_value {
			xs.withUnsafeBufferPointer { clj_vector_from_array($0.baseAddress, UInt32(xs.count)) }
		}

		@Test func layoutBySize() {
			let before = clj_debug_live_objects()
			for n in 1...6 {
				let t = fromArray((0..<n).map { clj_fixnum($0) })
				#expect(clj_vector_is_tuple(t))
				#expect(clj_debug_live_objects() == before + 1, "a tuple of \(n) is one object")
				#expect(matches(t, (0..<n).map { clj_fixnum($0) }))
				#expect(clj_debug_vector_root(t) == CLJ_NIL && clj_debug_vector_tail(t) == CLJ_NIL)
				clj_release(t)
			}
			let seven = fromArray((0..<7).map { clj_fixnum($0) })
			#expect(!clj_vector_is_tuple(seven))
			#expect(fromArray([]) == clj_vector_empty())
			let conjed = clj_vector_conj(clj_vector_empty(), clj_fixnum(1))
			#expect(!clj_vector_is_tuple(conjed))
			clj_tuples_enable(false)
			let off = fromArray([clj_fixnum(1), clj_fixnum(2)])
			clj_tuples_enable(true)
			#expect(!clj_vector_is_tuple(off))
			for v in [seven, conjed, off] { clj_release(v) }
			#expect(clj_debug_live_objects() == before)
		}

		// Every operation on a tuple of each size against the same operation on a trie of the same elements.
		@Test(arguments: 1...6) func operationsMatchTheTrie(n: Int) {
			let before = clj_debug_live_objects()
			var rng = SplitMix64(state: UInt64(n))
			var ref = (0..<n).map { clj_fixnum($0) }
			var t = fromArray(ref)
			var trie = build(n)
			#expect(clj_equals(t, trie) && clj_equals(trie, t))
			#expect(clj_hash(t) == clj_hash(trie))
			for step in 0..<600 {
				let value = clj_fixnum(rng.below(1000))
				let roll = rng.below(3)
				// Every other step a second reference forces the copy path and must keep its old contents.
				let kept = step % 2 == 0 ? clj_retain(t) : CLJ_NIL
				let keptRef = ref
				if roll == 0 || ref.isEmpty {
					t = clj_vector_conj(t, value)
					trie = clj_vector_conj(trie, value)
					ref.append(value)
				} else if roll == 1 {
					let i = rng.below(ref.count)
					t = clj_vector_assoc(t, UInt32(i), value)
					trie = clj_vector_assoc(trie, UInt32(i), value)
					ref[i] = value
				} else {
					t = clj_vector_pop(t)
					trie = clj_vector_pop(trie)
					ref.removeLast()
				}
				if ref.count > 6 { #expect(!clj_vector_is_tuple(t)) }
				guard matches(t, ref), clj_equals(t, trie), clj_equals(trie, t), clj_hash(t) == clj_hash(trie),
					kept == CLJ_NIL || matches(kept, keptRef)
				else {
					Issue.record("mismatch at step \(step)")
					clj_release(kept)
					break
				}
				clj_release(kept)
				if ref.count > 9 {
					while ref.count > n {
						t = clj_vector_pop(t)
						trie = clj_vector_pop(trie)
						ref.removeLast()
					}
				}
				// A promoted tuple stays a trie, and so does a pop to empty: start over from a tuple.
				if !clj_vector_is_tuple(t), (1...6).contains(ref.count) {
					clj_release(t)
					t = fromArray(ref)
				}
			}
			clj_release(t)
			clj_release(trie)
			#expect(clj_debug_live_objects() == before)
		}

		@Test(.enabled(if: clj_reuse_enabled())) func uniqueTupleIsUpdatedInPlace() {
			let before = clj_debug_live_objects()
			var t = fromArray([clj_fixnum(1), clj_fixnum(2)])
			let cell = t
			t = clj_vector_assoc(t, 0, clj_fixnum(-1))
			#expect(t == cell)
			t = clj_vector_pop(t)
			#expect(t == cell && clj_vector_count(t) == 1)
			// A conj may move the cell to the next size class; the object count is the stable signal.
			for i in 2...6 { t = clj_vector_conj(t, clj_fixnum(i)) }
			#expect(clj_vector_is_tuple(t) && clj_debug_live_objects() == before + 1)
			let kept = clj_retain(t)
			let copy = clj_vector_assoc(kept, 0, clj_fixnum(0))
			#expect(copy != t && clj_vector_nth(t, 0) == clj_fixnum(-1) && clj_vector_nth(copy, 0) == clj_fixnum(0))
			let promoted = clj_vector_conj(t, clj_fixnum(7))
			#expect(!clj_vector_is_tuple(promoted) && clj_vector_count(promoted) == 7)
			clj_release(copy)
			clj_release(promoted)
			#expect(clj_debug_live_objects() == before)
		}

		@Test(.enabled(if: clj_reuse_enabled())) func sharedTupleKeepsChildrenShared() {
			let before = clj_debug_live_objects()
			let first = clj_cons_new(clj_fixnum(0), CLJ_NIL)
			var t = fromArray([first])
			clj_release(first)
			clj_share(t)
			for i in 1..<4 {
				let c = clj_cons_new(clj_fixnum(i), CLJ_NIL)
				t = clj_vector_conj(t, c)
				clj_release(c)
			}
			let c = clj_cons_new(clj_fixnum(9), CLJ_NIL)
			t = clj_vector_assoc(t, 1, c)
			clj_release(c)
			t = clj_vector_pop(t)
			#expect(clj_vector_is_tuple(t) && clj_vector_count(t) == 3)
			#expect(clj_debug_all_shared(t))
			let copy = clj_vector_conj(clj_retain(t), CLJ_TRUE)
			#expect(!clj_is_shared(copy) && clj_debug_all_shared(t))
			clj_release(copy)
			clj_release(t)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func metaSurvivesEveryOperation() {
			let m = Value([Value(keyword: "m"): Value(1)])
			let before = clj_debug_live_objects()
			withExtendedLifetime(m) {
				var t = clj_with_meta(fromArray([clj_fixnum(1)]), m.raw)
				#expect(clj_vector_is_tuple(t))
				for i in 2...7 { t = clj_vector_conj(t, clj_fixnum(i)) }
				#expect(!clj_vector_is_tuple(t))
				var meta = clj_meta(t)
				#expect(clj_equals(meta, m.raw))
				clj_release(meta)
				clj_release(t)
				t = clj_vector_pop(clj_with_meta(fromArray([clj_fixnum(1)]), m.raw))
				#expect(clj_vector_count(t) == 0 && t != clj_vector_empty())
				meta = clj_meta(t)
				#expect(clj_equals(meta, m.raw))
				clj_release(meta)
				#expect(clj_equals(t, clj_vector_empty()))
				clj_release(t)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func mapEntriesAreTuples() throws {
			let rt = Runtime()
			let sources = ["(first {:a 1})", "(first (hash-map 1 2))", "(first (sorted-map 1 2))", "(reduce (fn [_ e] (reduced e)) nil {:a 1})", "[1 2]"]
			let destructure = "(let [[k v] (first {:a 1})] [k v (key (first {:a 1})) (val (first {:a 1}))])"
			// The first run interns what the forms read.
			for source in sources + [destructure] { _ = try rt.eval(source) }
			let before = clj_debug_live_objects()
			do {
				for source in sources {
					let e = try rt.eval(source)
					#expect(withExtendedLifetime(e) { clj_vector_is_tuple(e.raw) }, "\(source)")
				}
				#expect(try rt.eval(destructure) == [Value(keyword: "a"), 1, Value(keyword: "a"), 1])
			}
			#expect(clj_debug_live_objects() == before)
		}
	}

	@Suite struct VectorValueTests {
		@Test func arrayRoundTrip() {
			let before = clj_debug_live_objects()
			do {
				let v = Value([1, "two", nil, [3.0, true]])
				let lit: Value = [1, "two", nil, [3.0, true]]
				withExtendedLifetime((v, lit)) {
					#expect(v.typeName == "vector")
					#expect(clj_vector_count(v.raw) == 4)
					#expect(clj_debug_all_shared(v.raw))
					#expect(v.array == [1, "two", nil, [3.0, true]])
					#expect(v.array?[3].array == [3.0, true])
					#expect(v == lit)
					#expect(v.hashValue == lit.hashValue)
					#expect(v != [1, "two", nil])
					#expect(Value(1).array == nil)
					#expect(([] as Value).array == [])
					#expect(([] as Value).raw == clj_vector_empty())
				}
				var d: [Value: Int] = [:]
				d[[1, 2]] = 1
				d[Value([Value(1), Value(2)])] = 2
				#expect(d.count == 1 && d[[1, 2]] == 2)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func largeArrayRoundTrip() {
			let before = clj_debug_live_objects()
			do {
				let source = (0..<5000).map { Value($0) }
				let v = Value(source)
				#expect(v.array == source)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
