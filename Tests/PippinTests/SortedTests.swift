// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }
private func m(_ d: [String: Value]) -> Value { Value(Dictionary(uniqueKeysWithValues: d.map { (kw($0.key), $0.value) })) }

private let missing = clj_char(0xFFFF)

// SplitMix64, so the edit sequence is the same on every run.
private struct Rng {
	var state: UInt64
	mutating func next(_ below: Int) -> Int {
		state &+= 0x9E37_79B9_7F4A_7C15
		var z = state
		z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
		z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
		return Int((z ^ (z >> 31)) % UInt64(below))
	}
}

extension CoreTests {
	@Suite struct SortedTests {
		init() {
			clj_init()
			for k in ["a", "b", "c", "d", "m", "x", "y", "z", "nf", "found", "bad"] { _ = kw(k) }
		}

		private func comparator() throws -> Value { try eval("compare") }

		private func declare(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func constructorsAndPredicates() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(count (sorted-map)) (count (sorted-map :a 1 :b 2)) (count (sorted-set)) (count (sorted-set 1 1 2))]") == [0, 2, 0, 2])
				#expect(try eval("[(sorted? (sorted-map)) (sorted? (sorted-set)) (sorted? {}) (sorted? #{}) (sorted? [1]) (sorted? nil)]") == [true, true, false, false, false, false])
				#expect(try eval("[(map? (sorted-map)) (set? (sorted-set)) (map? (sorted-set)) (coll? (sorted-map)) (counted? (sorted-set)) (associative? (sorted-map))]") == [true, true, false, true, true, true])
				#expect(try eval("(pr-str [(type (sorted-map)) (type (sorted-set))])") == "[sorted-map sorted-set]")
				#expect(try eval("(= (sorted-map) {})") == true)
				#expect(try eval("(= (sorted-set) #{})") == true)
				#expect(message("(sorted-map :a)") == "No value supplied for key: :a")
				#expect(message("(sorted-map-by 7 :a 1)") == "comparator must be a function, got: long")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func lookupAndUpdate() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [m (sorted-map :a 1 :b 2)] [(get m :a) (get m :z) (get m :z :nf) (m :b) (:a m) (contains? m :a) (contains? m :z)])") == Value([1, nil, kw("nf"), 2, 1, true, false]))
				#expect(try eval("(let [s (sorted-set 1 2)] [(get s 1) (get s 9) (s 2) (s 9 :nf) (contains? s 1) (contains? s 9)])") == Value([1, nil, 2, kw("nf"), true, false]))
				#expect(try eval("(keys (assoc (sorted-map :b 2) :a 1 :c 3))") == Value([kw("a"), kw("b"), kw("c")]))
				#expect(try eval("(vals (assoc (sorted-map :b 2) :a 1 :c 3))") == [1, 2, 3])
				#expect(try eval("(keys (dissoc (sorted-map :a 1 :b 2 :c 3) :b))") == Value([kw("a"), kw("c")]))
				#expect(try eval("(count (dissoc (sorted-map :a 1) :a :b))") == 0)
				#expect(try eval("(seq (disj (sorted-set 1 2 3) 2))") == [1, 3])
				#expect(try eval("(seq (conj (sorted-set 3 1) 2))") == [1, 2, 3])
				#expect(try eval("(seq (conj (sorted-map :b 2) [:a 1]))") == Value([Value([kw("a"), 1]), Value([kw("b"), 2])]))
				#expect(try eval("(keys (conj (sorted-map :b 2) {:a 1 :c 3}))") == Value([kw("a"), kw("b"), kw("c")]))
				#expect(try eval("(keys (into (sorted-map) {:c 3 :a 1 :b 2}))") == Value([kw("a"), kw("b"), kw("c")]))
				#expect(try eval("(seq (into (sorted-set) [3 1 2 1]))") == [1, 2, 3])
				#expect(try eval("(seq (sorted-map :a 1))") == Value([Value([kw("a"), 1])]))
				#expect(try eval("[(seq (sorted-map)) (seq (sorted-set))]") == Value([nil, nil]))
				#expect(message("(dissoc (sorted-set 1) 1)") == "dissoc not supported on this type: sorted-set")
				#expect(message("(disj (sorted-map :a 1) :a)") == "disj not supported on this type: sorted-map")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func orderAndPrinting() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(pr-str (apply sorted-map (interleave [:c :a :b] [3 1 2])))") == "{:a 1, :b 2, :c 3}")
				#expect(try eval("(pr-str (sorted-set 3 1 2))") == "#{1 2 3}")
				#expect(try eval("[(pr-str (sorted-map)) (pr-str (sorted-set))]") == Value(["{}", "#{}"]))
				#expect(try eval("(pr-str (sorted-map-by > 1 :a 2 :b))") == "{2 :b, 1 :a}")
				#expect(try eval("(str (sorted-set \"a\"))") == "#{\"a\"}")
				#expect(try eval("[(first (sorted-map :b 2 :a 1)) (last (sorted-map :b 2 :a 1))]") == Value([Value([kw("a"), 1]), Value([kw("b"), 2])]))
				#expect(try eval("[(first (sorted-set 3 1 2)) (last (sorted-set 3 1 2))]") == [1, 3])
				#expect(try eval("(seq (apply sorted-set (reverse (range 40))))") == Value(Array(0..<40).map { Value($0) }))
				// APersistentVector.compareTo: by count first, then item by item.
				#expect(try eval("(pr-str (sorted-set [1 2] [2] [1 1] []))") == "#{[] [2] [1 1] [1 2]}")
				#expect(try eval("(pr-str (sort [[1 2] [2] [1 1] []]))") == "([] [2] [1 1] [1 2])")
				#expect(try eval("(pr-str (sorted-map [1 :a] 1 [0] 2))") == "{[0] 2, [1 :a] 1}")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func equalityAndHash() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(= (sorted-map :a 1) {:a 1}) (= {:a 1} (sorted-map :a 1)) (= (sorted-map :a 1) (sorted-map :a 1))]") == [true, true, true])
				#expect(try eval("[(= (sorted-map :a 1) {:a 2}) (= (sorted-map :a 1) {:a 1 :b 2}) (= (sorted-map :a 1) #{:a})]") == [false, false, false])
				#expect(try eval("[(= (sorted-set 1 2) #{2 1}) (= #{2 1} (sorted-set 1 2)) (= (sorted-set 1) #{1 2}) (= (sorted-set 1) {1 1})]") == [true, true, false, false])
				#expect(try eval("[(= (hash (sorted-map :a 1 :b 2)) (hash {:b 2 :a 1})) (= (hash (sorted-set 1 2)) (hash #{2 1}))]") == [true, true])
				#expect(try eval("[(= (hash (sorted-map)) (hash {})) (= (hash (sorted-set)) (hash #{}))]") == [true, true])
				// A map keyed by a sorted map finds it again, so hash and equality agree.
				#expect(try eval("(get {(sorted-map :a 1) :found} {:a 1})") == kw("found"))
				#expect(try eval("(get {{:a 1} :found} (sorted-map :a 1))") == kw("found"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func customComparator() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(keys (sorted-map-by > 1 :a 3 :c 2 :b))") == [3, 2, 1])
				#expect(try eval("(seq (sorted-set-by > 1 3 2))") == [3, 2, 1])
				// A predicate comparator answers only "a first", as a fn comparator does for sort.
				#expect(try eval("(seq (sorted-set-by (fn [a b] (< (count a) (count b))) \"ccc\" \"a\" \"bb\"))") == Value(["a", "bb", "ccc"]))
				#expect(try eval("(count (sorted-set-by (fn [a b] (< (count a) (count b))) \"aa\" \"bb\"))") == 1)
				#expect(try eval("(keys (empty (sorted-map-by > 1 :a)))") == nil)
				#expect(try eval("(keys (assoc (empty (sorted-map-by > 1 :a)) 1 :a 2 :b))") == [2, 1])
				#expect(try eval("(seq (conj (empty (sorted-set-by >)) 3 1))") == [3, 1])
				#expect(try eval("(count (empty (sorted-map-by > 1 :a)))") == 0)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func comparatorThrowPropagates() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(message("(sorted-map-by (fn [_ _] (throw (ex-info \"boom\" {}))) 1 :a 2 :b)") == "boom")
				#expect(message("(get (sorted-map-by (fn [a b] (if (= a :bad) (throw (ex-info \"boom\" {})) (compare a b))) :a 1 :b 2) :bad)") == "boom")
				#expect(message("(contains? (sorted-set-by (fn [a b] (if (= a :bad) (throw (ex-info \"boom\" {})) (compare a b))) :a :b) :bad)") == "boom")
				#expect(message("(sorted-map-by compare [1] 1 :a 2)") != nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func reduceAndTransduce() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(reduce (fn [a e] (conj a (key e))) [] (sorted-map :c 3 :a 1 :b 2))") == Value([kw("a"), kw("b"), kw("c")]))
				#expect(try eval("(reduce + 0 (sorted-set 3 1 2))") == 6)
				#expect(try eval("(reduce-kv (fn [a k v] (conj a k v)) [] (sorted-map :b 2 :a 1))") == Value([kw("a"), 1, kw("b"), 2]))
				#expect(try eval("(into [] (map inc) (sorted-set 3 1 2))") == [2, 3, 4])
				#expect(try eval("(transduce (map key) conj [] (sorted-map :b 2 :a 1))") == Value([kw("a"), kw("b")]))
				#expect(try eval("(reduce (fn [_ x] (reduced x)) nil (sorted-set 1 2 3))") == 1)
				#expect(try eval("[(count (sorted-map :a 1)) (empty? (sorted-map)) (empty? (sorted-set 1))]") == Value([1, true, false]))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func metadata() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(meta (with-meta (sorted-map :a 1) {:m 1}))") == m(["m": 1]))
				#expect(try eval("(meta (with-meta (sorted-set 1) {:m 1}))") == m(["m": 1]))
				#expect(try eval("(let [s (with-meta (sorted-map :a 1) {:m 1})] [(meta (assoc s :b 2)) (meta (dissoc s :a)) (= s (assoc (sorted-map) :a 1))])") == Value([m(["m": 1]), m(["m": 1]), true]))
				#expect(try eval("(meta (empty (with-meta (sorted-map :a 1) {:m 1})))") == m(["m": 1]))
				#expect(try eval("(= (with-meta (sorted-map :a 1) {:m 1}) (sorted-map :a 1))") == true)
				#expect(try eval("(meta (sorted-map))") == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func reverseAndRangeSeqs() throws {
			clj_init()
			try declare("ss")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(rseq (sorted-set 1 2 3))") == [3, 2, 1])
				#expect(try eval("(rseq (sorted-map :a 1 :b 2))") == Value([Value([kw("b"), 2]), Value([kw("a"), 1])]))
				#expect(try eval("[(rseq (sorted-map)) (rseq (sorted-set))]") == Value([nil, nil]))
				#expect(try eval("(rseq [1 2 3])") == [3, 2, 1])
				#expect(message("(rseq {:a 1})") == "rseq not supported on this type: map")

				#expect(try eval("(def ss (apply sorted-set (range 10))) (subseq ss > 5)") == [6, 7, 8, 9])
				#expect(try eval("(subseq ss >= 5)") == [5, 6, 7, 8, 9])
				#expect(try eval("(subseq ss < 5)") == [0, 1, 2, 3, 4])
				#expect(try eval("(subseq ss <= 5)") == [0, 1, 2, 3, 4, 5])
				#expect(try eval("(subseq ss > 2 < 6)") == [3, 4, 5])
				#expect(try eval("(subseq ss >= 2 <= 6)") == [2, 3, 4, 5, 6])
				#expect(try eval("(rsubseq ss > 5)") == [9, 8, 7, 6])
				#expect(try eval("(rsubseq ss >= 5)") == [9, 8, 7, 6, 5])
				#expect(try eval("(rsubseq ss < 5)") == [4, 3, 2, 1, 0])
				#expect(try eval("(rsubseq ss <= 5)") == [5, 4, 3, 2, 1, 0])
				#expect(try eval("(rsubseq ss >= 2 <= 6)") == [6, 5, 4, 3, 2])
				#expect(try eval("[(subseq ss > 20) (rsubseq ss < 0)]") == Value([nil, nil]))
				#expect(try eval("(seq (subseq ss < 0))") == nil)
				#expect(try eval("(subseq (apply sorted-map (interleave (range 4) (range 4))) >= 2)") == Value([Value([2, 2]), Value([3, 3])]))
				// A custom comparator bounds subseq as it orders the tree: 3 comes before 2 here.
				#expect(try eval("(subseq (sorted-map-by > 3 :c 2 :b 1 :a) < 2)") == Value([Value([3, kw("c")])]))
				#expect(try eval("(subseq (sorted-map-by > 3 :c 2 :b 1 :a) > 1)") == nil)
				try unbind("ss")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func structuralSharingKeepsOldVersions() throws {
			let cmp = try comparator()
			let before = clj_debug_live_objects()
			do {
				var m1 = clj_sorted_map_new(cmp.raw)
				for i in 0..<200 { m1 = clj_sorted_assoc(m1, clj_fixnum(i), clj_fixnum(i)) }
				let m2 = clj_sorted_assoc(clj_retain(m1), clj_fixnum(5), clj_fixnum(-5))
				let m3 = clj_sorted_dissoc(clj_retain(m1), clj_fixnum(7))
				let m4 = clj_sorted_assoc(clj_retain(m1), clj_fixnum(1000), clj_fixnum(1000))
				#expect(m2 != m1 && m3 != m1 && m4 != m1)
				#expect(clj_sorted_get(m1, clj_fixnum(5), missing) == clj_fixnum(5))
				#expect(clj_sorted_get(m2, clj_fixnum(5), missing) == clj_fixnum(-5))
				#expect(clj_sorted_get(m1, clj_fixnum(7), missing) == clj_fixnum(7))
				#expect(clj_sorted_get(m3, clj_fixnum(7), missing) == missing)
				#expect(clj_sorted_count(m1) == 200 && clj_sorted_count(m3) == 199 && clj_sorted_count(m4) == 201)
				for v in [m1, m2, m3, m4] { #expect(clj_debug_sorted_valid(v)) }
				clj_release(m1)
				#expect(clj_sorted_get(m2, clj_fixnum(199), missing) == clj_fixnum(199))
				#expect(clj_sorted_get(m4, clj_fixnum(199), missing) == clj_fixnum(199))
				clj_release(m2)
				clj_release(m3)
				clj_release(m4)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Reuse at rc == 1 is what keeps the address: -DCLJ_NO_REUSE turns every step below into a copy.
		@Test(.enabled(if: clj_reuse_enabled())) func uniqueSortedMapIsUpdatedInPlace() throws {
			let cmp = try comparator()
			let before = clj_debug_live_objects()
			do {
				var m = clj_sorted_assoc(clj_sorted_map_new(cmp.raw), clj_fixnum(1), clj_fixnum(1))
				#expect(clj_is_unique(m) == clj_reuse_enabled())
				let wrapper = m
				let root = clj_debug_sorted_root(m)
				let live = clj_debug_live_objects()

				m = clj_sorted_assoc(m, clj_fixnum(1), clj_fixnum(2))
				#expect(m == wrapper)
				#expect(clj_debug_sorted_root(m) == root)
				#expect(clj_debug_live_objects() == live)

				for i in 2..<100 { m = clj_sorted_assoc(m, clj_fixnum(i), clj_fixnum(i)) }
				#expect(m == wrapper)
				let grownLive = clj_debug_live_objects()
				m = clj_sorted_dissoc(m, clj_fixnum(50))
				#expect(m == wrapper)
				#expect(clj_debug_live_objects() < grownLive)

				_ = clj_retain(m)
				let copy = clj_sorted_assoc(m, clj_fixnum(1), clj_fixnum(3))
				#expect(copy != wrapper)
				#expect(clj_sorted_get(m, clj_fixnum(1), missing) == clj_fixnum(2))
				#expect(clj_sorted_get(copy, clj_fixnum(1), missing) == clj_fixnum(3))
				clj_release(copy)
				#expect(clj_is_unique(m) == clj_reuse_enabled())
				clj_release(m)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func treeInvariantsHoldUnderRandomEdits() throws {
			let cmp = try comparator()
			let before = clj_debug_live_objects()
			do {
				var rng = Rng(state: 0x5EED)
				var reference: [Int: Int] = [:]
				var m = clj_sorted_map_new(cmp.raw)
				for step in 0..<1500 {
					let k = rng.next(150)
					if rng.next(3) == 0 {
						m = clj_sorted_dissoc(m, clj_fixnum(k))
						reference[k] = nil
					} else {
						m = clj_sorted_assoc(m, clj_fixnum(k), clj_fixnum(step))
						reference[k] = step
					}
					if !clj_debug_sorted_valid(m) {
						Issue.record("tree invariants broken at step \(step)")
						break
					}
					if Int(clj_sorted_count(m)) != reference.count {
						Issue.record("count diverged at step \(step)")
						break
					}
				}
				for (k, v) in reference { #expect(clj_sorted_get(m, clj_fixnum(k), missing) == clj_fixnum(v)) }
				clj_release(m)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every child of a shared tree stays shared, so a published sorted map keeps the RC invariant.
		// -DCLJ_NO_REUSE copies instead of editing in place, and a copy is a fresh unshared object.
		@Test(.enabled(if: clj_reuse_enabled())) func sharedTreeKeepsChildrenShared() throws {
			let cmp = try comparator()
			let before = clj_debug_live_objects()
			do {
				var m = clj_sorted_map_new(cmp.raw)
				for i in 0..<50 { m = clj_sorted_assoc(m, clj_fixnum(i), clj_fixnum(i)) }
				clj_share(m)
				#expect(clj_debug_all_shared(m))
				for i in 50..<200 {
					let key = clj_cons_new(clj_fixnum(i), CLJ_NIL)
					m = clj_sorted_assoc(m, clj_fixnum(i), key)
					clj_release(key)
					#expect(clj_debug_all_shared(m))
				}
				m = clj_sorted_dissoc(m, clj_fixnum(100))
				#expect(clj_debug_all_shared(m))
				#expect(clj_debug_sorted_valid(m))
				clj_release(m)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
