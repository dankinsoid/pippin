// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private let missing = clj_fixnum(-7777)

private struct Rng {
	var state: UInt64

	mutating func below(_ n: Int) -> Int {
		state &+= 0x9e3779b97f4a7c15
		var z = state
		z = (z ^ (z >> 30)) &* 0xbf58476d1ce4e5b9
		z = (z ^ (z >> 27)) &* 0x94d049bb133111eb
		return Int((z ^ (z >> 31)) % UInt64(n))
	}
}

private func pairs(_ map: clj_value) -> [(clj_value, clj_value)] {
	var out: [(clj_value, clj_value)] = []
	withUnsafeMutablePointer(to: &out) { p in
		clj_map_each(map, { k, v, ctx in
			ctx!.assumingMemoryBound(to: [(clj_value, clj_value)].self).pointee.append((k, v))
			return true
		}, p)
	}
	return out
}

// The trie of the same entries, built without the flat layout.
private func trieOf(_ ref: [(clj_value, clj_value)]) -> clj_value {
	var m = clj_map_empty()
	for (k, v) in ref { m = clj_hash_map_assoc(m, k, v) }
	return m
}

// Insertion order with a re-assoc keeping its place and a dissoc removing it: what a flat map iterates in.
private struct Ordered {
	var items: [(clj_value, clj_value)] = []

	mutating func assoc(_ k: clj_value, _ v: clj_value) {
		if let i = items.firstIndex(where: { clj_equals($0.0, k) }) { items[i].1 = v } else { items.append((k, v)) }
	}

	mutating func dissoc(_ k: clj_value) { items.removeAll { clj_equals($0.0, k) } }
}

private func same(_ map: clj_value, _ ref: Ordered) -> Bool {
	let seen = pairs(map)
	return clj_map_count(map) == UInt32(ref.items.count) && seen.count == ref.items.count &&
		zip(seen, ref.items).allSatisfy { $0.0 == $1.0 && $0.1 == $1.1 } &&
		ref.items.allSatisfy { clj_map_get(map, $0.0, missing) == $0.1 && clj_map_contains(map, $0.0) }
}

extension CoreTests {
	// The flat layout (map.c, NOTES "Map"): what a transient map builds, kept by persistent! under the threshold.
	@Suite(.serialized) struct FlatMapTests {
		let rt = Runtime()

		@Test func transientEditsInPlaceAtAnyCount() {
			let before = clj_debug_live_objects()
			var t = clj_map_transient(clj_map_empty())
			#expect(clj_map_is_flat(t) && clj_map_is_transient(t))
			let first = t
			for i in 0..<3 {
				// a second reference, as a builder's borrowed accumulator has
				_ = clj_retain(t)
				t = clj_map_assoc(t, clj_fixnum(i), clj_fixnum(10 * i))
				#expect(t == first)
				clj_release(t)
			}
			#expect(clj_map_count(t) == 3)
			let p = clj_map_persistent(t)
			#expect(p == t && clj_map_is_flat(p) && !clj_map_is_transient(p))
			clj_release(t)
			// persistent now: a shared one copies
			_ = clj_retain(p)
			let q = clj_map_assoc(p, clj_fixnum(0), clj_fixnum(99))
			#expect(q != p && clj_map_get(p, clj_fixnum(0), missing) == clj_fixnum(0) && clj_map_get(q, clj_fixnum(0), missing) == clj_fixnum(99))
			clj_release(q)
			clj_release(p)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func orderEqualityAndHashAgainstTheTrie() {
			let before = clj_debug_live_objects()
			for mode in HashMode.allCases {
				clj_debug_set_hash_override(mode.override)
				defer { clj_debug_set_hash_override(nil) }
				var rng = Rng(state: 0xf1a7)
				var ref = Ordered()
				var t = clj_map_transient(clj_map_empty())
				for _ in 0..<2000 {
					let k = clj_fixnum(rng.below(300))
					if rng.below(4) == 0 {
						t = clj_map_dissoc(t, k)
						ref.dissoc(k)
					} else {
						let v = clj_fixnum(rng.below(50))
						t = clj_map_assoc(t, k, v)
						ref.assoc(k, v)
					}
				}
				#expect(same(t, ref), "\(mode)")
				let trie = trieOf(ref.items)
				#expect(clj_equals(t, trie) && clj_equals(trie, t), "\(mode)")
				// the override hashes the maps themselves too
				if mode == .normal { #expect(clj_hash(t) == clj_hash(trie)) }
				let p = clj_map_persistent(t)
				#expect(!clj_map_is_flat(p) && clj_equals(p, trie), "\(mode)")
				for x in [t, p, trie] { clj_release(x) }
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The JVM throws on a transient used after persistent!; here persistent! clears the flag, so the object is a
		// persistent value and the stale handle's edits copy while the value has another holder.
		@Test func useAfterPersistentLeavesTheValue() {
			let before = clj_debug_live_objects()
			var t = clj_map_transient(clj_map_empty())
			t = clj_map_assoc(t, clj_fixnum(1), clj_fixnum(1))
			let p = clj_map_persistent(t)
			#expect(p == t)
			for op in 0..<3 {
				_ = clj_retain(t)
				let r = op == 0 ? clj_map_assoc(t, clj_fixnum(2), clj_fixnum(2))
					: op == 1 ? clj_map_dissoc(t, clj_fixnum(1)) : clj_map_assoc(t, clj_fixnum(1), clj_fixnum(9))
				#expect(r != p)
				clj_release(r)
				#expect(clj_map_count(p) == 1 && clj_map_get(p, clj_fixnum(1), missing) == clj_fixnum(1))
			}
			clj_release(t)
			clj_release(p)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func thresholdIsOneWay() {
			let before = clj_debug_live_objects()
			clj_debug_flat_max(8)
			defer { clj_debug_flat_max(0) }
			var t = clj_map_transient(clj_map_empty())
			for i in 0..<8 { t = clj_map_assoc(t, clj_fixnum(i), clj_fixnum(i)) }
			var p = clj_map_persistent(t)
			clj_release(t)
			#expect(clj_map_is_flat(p))
			// unique: in place, then the ninth key turns it into a trie
			let address = p
			p = clj_map_assoc(p, clj_fixnum(0), clj_fixnum(100))
			#expect(p == address || !clj_reuse_enabled())
			p = clj_map_assoc(p, clj_fixnum(8), clj_fixnum(8))
			#expect(!clj_map_is_flat(p) && clj_map_count(p) == 9 && clj_map_get(p, clj_fixnum(0), missing) == clj_fixnum(100))
			p = clj_map_dissoc(p, clj_fixnum(8))
			#expect(!clj_map_is_flat(p))
			clj_release(p)

			// past the threshold at persistent!: a trie
			t = clj_map_transient(clj_map_empty())
			for i in 0..<9 { t = clj_map_assoc(t, clj_fixnum(i), clj_fixnum(i)) }
			p = clj_map_persistent(t)
			#expect(!clj_map_is_flat(p) && clj_map_count(p) == 9)
			clj_release(p)
			clj_release(t)

			// a trie past it is its own transient
			let big = trieOf((0..<9).map { (clj_fixnum($0), clj_fixnum($0)) })
			t = clj_map_transient(big)
			#expect(t == big && !clj_map_is_flat(t))
			clj_release(t)
			clj_release(big)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func dissocLeavesHolesTheRebuildDrops() {
			let before = clj_debug_live_objects()
			var t = clj_map_transient(clj_map_empty())
			var ref = Ordered()
			for round in 0..<40 {
				for i in 0..<6 {
					let k = clj_fixnum(round * 6 + i)
					t = clj_map_assoc(t, k, k)
					ref.assoc(k, k)
				}
				for i in 0..<5 {
					let k = clj_fixnum(round * 6 + i)
					t = clj_map_dissoc(t, k)
					ref.dissoc(k)
				}
				#expect(same(t, ref))
			}
			// 40 live entries after 240 appended: the table compacts rather than doubling past what it holds
			#expect(clj_debug_flat_capacity(t) <= 128)
			clj_release(t)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func keywordKeysGoBackToAShape() throws {
			let shaped = try rt.eval("(persistent! (-> (transient {}) (assoc! :a 1) (assoc! :b 2)))")
			#expect(withExtendedLifetime(shaped) { clj_is_shape_map(shaped.raw) })
			let mixed = try rt.eval("(persistent! (-> (transient {}) (assoc! :a 1) (assoc! \"b\" 2)))")
			#expect(withExtendedLifetime(mixed) { clj_map_is_flat(mixed.raw) && !clj_map_is_transient(mixed.raw) })
			// a shape map's transient is a copy; the source keeps its layout and contents
			let src = try rt.eval("(def flat-src {:a 1 :b 2}) (persistent! (assoc! (transient flat-src) :c 3))")
			#expect(withExtendedLifetime(src) { clj_is_shape_map(src.raw) && clj_map_count(src.raw) == 3 })
			#expect(try rt.eval("flat-src").description == "{:a 1, :b 2}")
		}

		@Test func printsInInsertionOrder() throws {
			#expect(try rt.eval("(persistent! (-> (transient {}) (assoc! 3 :a) (assoc! 1 :b) (assoc! \"x\" :c) (dissoc! 1) (assoc! 1 :d)))")
				.description == "{3 :a, \"x\" :c, 1 :d}")
			#expect(try rt.eval("(group-by odd? (range 6))").description == "{false [0 2 4], true [1 3 5]}")
			#expect(try rt.eval("(frequencies [3 1 3 2])").description == "{3 2, 1 1, 2 1}")
			// two keys of one hash, against the trie of the literal
			#expect(try rt.eval("""
				(let [m (persistent! (-> (transient {}) (assoc! "k15599" 1) (assoc! "k97211" 2) (assoc! nil 3)))
				      t {"k97211" 2 nil 3 "k15599" 1}]
				  [(hash "k15599") (hash "k97211") (= m t) (= t m) (= (hash m) (hash t)) (get m "k97211") (get m nil)])
				""").description.hasSuffix(" true true true 2 3]"))
		}

		@Test func iterationSurvivesItsOwnDissoc() throws {
			// reduce-kv over a transient that its fn empties: each entry is held across the call
			let v = try rt.eval("""
				(let [t (transient {})
				      t (reduce (fn [t i] (assoc! t (str "k" i) (str "v" i))) t (range 6))]
				  (reduce-kv (fn [n k v] (dissoc! t k) (+ n (count k) (count v))) 0 t))
				""")
			#expect(v.description == "24")
		}

		@Test func metaSelfInsertionAndPublication() throws {
			// with-meta gives the layout up; a map with meta is its own transient
			#expect(try rt.eval("(meta (with-meta (persistent! (assoc! (transient {}) 1 2)) {:m 1}))").description == "{:m 1}")
			#expect(try rt.eval("(let [m (with-meta {1 2} {:m 1})] (meta (persistent! (assoc! (transient m) 3 4))))").description == "{:m 1}")
			// an entry that is the transient itself copies rather than closing a cycle
			#expect(try rt.eval("(let [t (transient {}) u (assoc! t 1 t)] [(identical? t u) (count t) (count u)])").description == "[false 0 1]")
			// published, then edited: a copy, the published value unchanged
			#expect(try rt.eval("""
				(let [t (transient {}) a (atom t) u (assoc! t 1 2)]
				  [(identical? t u) (count @a) (count u)])
				""").description == "[false 0 1]")
		}

		@Test func controlOffKeepsTheTrie() throws {
			clj_flat_enable(false)
			defer { clj_flat_enable(true) }
			let v = try rt.eval("(frequencies [1 2 1])")
			#expect(withExtendedLifetime(v) { !clj_map_is_flat(v.raw) && clj_map_count(v.raw) == 2 })
		}
	}
}
