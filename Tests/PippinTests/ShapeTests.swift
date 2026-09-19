// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private func kw(_ s: String) -> Value { Value(keyword: s) }

private func message(_ rt: Runtime, _ source: String) -> String? {
	do {
		_ = try rt.eval(source)
		return nil
	} catch let e as ClojureError {
		return e.message
	} catch {
		return nil
	}
}

// An analyzed tree with its exec, so a test can read the keyword-lookup caches of its sites.
private final class Tree {
	let node: UnsafeMutablePointer<clj_node>
	let exec: clj_value

	init(_ source: String) throws {
		let form = try Value(reading: source)
		var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
		guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
		self.node = node
		exec = clj_exec_new(node)
	}

	deinit {
		clj_release(exec)
		clj_release(clj_from_ptr(node))
	}

	func run() throws -> Value {
		let raw = clj_exec_run(exec)
		if raw == CLJ_THROWN { throw ClojureError.takePending() }
		return Value(owning: raw)
	}

	// The keyword-lookup site number `site` of the tree, in source order.
	func siteId(_ site: UInt32 = 0) -> UInt32 { clj_debug_exec_kw_site_id(exec, site) }
	func entries(_ site: UInt32 = 0) -> UInt32 { clj_debug_exec_kw_entries(exec, siteId(site)) }
	func hits(_ site: UInt32 = 0) -> Int64 { clj_debug_exec_kw_hits(exec, siteId(site)) }
	func misses(_ site: UInt32 = 0) -> Int64 { clj_debug_exec_kw_misses(exec, siteId(site)) }
}

extension CoreTests {
	// Shapes by observation (shape.c): a keyword-keyed map is a shape map, and a lookup site caches slot indices per shape.
	@Suite struct ShapeTests {
		let rt = Runtime()

		init() {
			for k in ["a", "b", "c", "d", "e", "f", "g", "h", "id", "name", "count", "x", "y", "z", "m", "none", "k1", "k2", "k3", "k4", "k5", "k6", "k7",
			          "k8", "k9", "k10", "line", "column", "p", "q", "w", "found", "redefined", "keys", "as", "a/x", "b/x"] { _ = kw(k) }
			for i in 0..<40 { _ = kw("key\(i)") }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		private func shaped(_ source: String) throws -> Bool {
			let v = try rt.eval(source)
			return withExtendedLifetime(v) { clj_is_shape_map(v.raw) }
		}

		private func shape(_ source: String) throws -> OpaquePointer? {
			let v = try rt.eval(source)
			return withExtendedLifetime(v) { clj_map_shape(v.raw) }
		}

		@Test func representationByKeys() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try shaped("{:a 1 :b 2}"))
				#expect(try shaped("(hash-map :a 1 :b 2)"))
				#expect(try shaped("(assoc {} :a 1)"))
				#expect(try shaped("(into {} [[:a 1] [:b 2]])"))
				#expect(try shaped("(zipmap [:a :b] [1 2])"))
				#expect(try shaped("(let [m {:a 1}] (assoc m :b 2))"))
				// Anything but a keyword key is a hash map, and stays one.
				#expect(try !shaped("{}"))
				#expect(try !shaped("{1 2}"))
				#expect(try !shaped("{\"a\" 1}"))
				#expect(try !shaped("{nil 1}"))
				#expect(try !shaped("{:a 1 nil 2}"))
				#expect(try !shaped("(assoc {:a 1} 2 3)"))
				#expect(try !shaped("(dissoc (assoc {:a 1} 2 3) 2)"))
				#expect(try !shaped("(assoc {1 2} :a 1)"))
				#expect(try !shaped("(with-meta {:a 1} {:m 1})"))
				#expect(try shaped("(with-meta {:a 1} nil)"))
				#expect(try !shaped("(assoc (with-meta {} {:m 1}) :a 1)"))
				// The type is one: every map predicate and instance? answer as for the trie.
				#expect(try rt.eval("[(map? {:a 1}) (coll? {:a 1}) (associative? {:a 1}) (counted? {:a 1}) (ifn? {:a 1}) (record? {:a 1}) (sorted? {:a 1})]") == [true, true, true, true, true, false, false])
				#expect(try rt.eval("[(instance? PersistentHashMap {:a 1}) (instance? IPersistentMap {:a 1}) (instance? IEditableCollection {:a 1})]") == [true, true, true])
				#expect(try rt.eval("(= (type {:a 1}) (type {1 2}))") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func keyCountLimit() throws {
			let before = clj_debug_live_objects()
			do {
				let keys32 = (0..<32).map { ":key\($0) \($0)" }.joined(separator: " ")
				#expect(try shaped("{\(keys32)}"))
				#expect(try rt.eval("(count {\(keys32)})") == 32)
				#expect(try rt.eval("(:key31 {\(keys32)})") == 31)
				#expect(try !shaped("(assoc {\(keys32)} :key32 32)"))
				#expect(try rt.eval("(let [m (assoc {\(keys32)} :key32 32)] [(count m) (:key0 m) (:key32 m) (= m (assoc (hash-map \(keys32)) :key32 32)) (= m {\(keys32) :key32 32})])") == [33, 0, 32, true, true])
				#expect(try !shaped("{\(keys32) :key32 32}"))
				#expect(try !shaped("(dissoc {\(keys32) :key32 32} :key32)"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func transitionsAndCanonicalization() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try shape("{:a 1 :b 2}") == shape("(assoc {:a 1} :b 2)"))
				#expect(try shape("{:a 1 :b 2}") == shape("(assoc (assoc {} :a 1) :b 2)"))
				#expect(try shape("{:a 1}") == shape("(dissoc {:a 1 :b 2} :b)"))
				#expect(try shape("{:a 1}") != shape("{:a 1 :b 2}"))
				#expect(try shape("{:a 1}") != shape("{:b 1}"))
				// One shape per key set: the order of construction does not split the site.
				#expect(try shape("{:a 1 :b 2}") == shape("{:b 2 :a 1}"))
				#expect(try shape("{:a 1 :b 2}") == shape("(assoc {:b 2} :a 1)"))
				#expect(try shape("{:a 1 :b 2}") == shape("(dissoc {:b 2 :c 3 :a 1} :c)"))
				#expect(try shape("{:a 1 :b 2}") == shape("(into {} [[:b 2] [:a 1]])"))
				// The values follow the permutation into the canonical order.
				#expect(try rt.eval("(let [m (assoc {:b 2} :a 1)] [(:a m) (:b m) (vec (keys m)) (vec (vals m))])") == [1, 2, [kw("a"), kw("b")], [1, 2]])
				#expect(try rt.eval("(let [m (dissoc {:b 2 :c 3 :a 1} :c)] [(:a m) (:b m) (:c m) (count m)])") == [1, 2, nil, 2])
				#expect(try rt.eval("(let [m (assoc {:c 3 :b 2} :a 1)] [(:a m) (:b m) (:c m) (= (vec (keys m)) (vec (keys {:a 1 :b 2 :c 3})))])") == [1, 2, 3, true])
				// The canonical order of a key set is compare's, whatever order built it.
				#expect(try rt.eval("(vec (keys {:k3 1 :k2 2 :k1 3}))") == [kw("k1"), kw("k2"), kw("k3")])
				#expect(try rt.eval("(vec (keys {:k1 3 :k2 2 :k3 1}))") == [kw("k1"), kw("k2"), kw("k3")])
				#expect(try rt.eval("(vec (keys (assoc {:k3 1 :k2 2} :k1 3)))") == [kw("k1"), kw("k2"), kw("k3")])
				#expect(try rt.eval("(vec (keys {:b/x 1 :a 2 :a/x 3}))") == [kw("a"), kw("a/x"), kw("b/x")])
				// A dissoc of the last key answers an empty map that is equal to, but not the same object as, {}.
				#expect(try rt.eval("(let [e (dissoc {:a 1} :a)] [(= e {}) (identical? e {}) (count e) (map? e)])") == [true, false, 0, true])
				#expect(try shaped("(assoc (dissoc {:a 1} :a) :b 2)"))
				#expect(try rt.eval("(assoc (dissoc {:a 1} :a) :b 2)") == Value([kw("b"): 2]))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func dictionaryDetector() throws {
			let before = clj_debug_live_objects()
			do {
				// Nine distinct keys after the same first key: the shape {:p} is dictionary-like and grows no more.
				let base = "{:p 0}"
				for i in 1...9 { _ = try rt.eval("(assoc \(base) :key\(i) \(i))") }
				let p = try shape(base)
				#expect(p != nil && clj_shape_is_dictionary(p))
				#expect(clj_debug_shape_children(p) == UInt32(CLJ_SHAPE_MAX_CHILDREN))
				#expect(try !shaped("(assoc \(base) :key10 10)"))
				#expect(try !shaped("(assoc \(base) :key1 1)"))
				#expect(try rt.eval("(let [m (assoc \(base) :key10 10)] [(count m) (:p m) (:key10 m) (= m {:p 0 :key10 10})])") == [2, 0, 10, true])
				// A literal of the same keys does not go through the transition and keeps its shape.
				#expect(try shaped("{:p 0 :key10 10}"))
				// The root is exempt: every first key of the program hangs off it.
				for i in 0..<12 { #expect(try shaped("{:key\(i) 1}")) }
				#expect(!clj_shape_is_dictionary(clj_shape_root()))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func semanticsMatchTheTrie() throws {
			try declare("sh-trie")
			let before = clj_debug_live_objects()
			do {
				// The same map built as a trie (shapes off) and as a shape map.
				clj_shapes_enable(false)
				let trie = try rt.eval("(hash-map :a 1 :b [2] :c \"three\")")
				clj_shapes_enable(true)
				#expect(withExtendedLifetime(trie) { !clj_is_shape_map(trie.raw) })
				let name = Value(symbol: "sh-trie")
				withExtendedLifetime((trie, name)) { clj_var_bind_root(clj_ns_resolve(clj_ns_user(), name.raw), trie.raw) }
				#expect(try rt.eval("(let [m {:a 1 :b [2] :c \"three\"}] [(= m sh-trie) (= sh-trie m) (= (hash m) (hash sh-trie)) (= m {:c \"three\" :b [2] :a 1}) (= m {:a 1 :b [2]}) (= {:a 1 :b [2]} m)])")
					== [true, true, true, true, false, false])
				#expect(try rt.eval("(let [m {:a 1 :b [2] :c \"three\"}] [(= m (sorted-map :a 1 :b [2] :c \"three\")) (= (sorted-map :a 1 :b [2] :c \"three\") m)])") == [true, true])
				#expect(try rt.eval("(let [m {:a 1 :b [2] :c \"three\"}] [(get #{sh-trie} m) (get {sh-trie :found} m) (count (set [m sh-trie])) (contains? (hash-set m) sh-trie)])")
					== [Value([kw("a"): 1, kw("b"): [2], kw("c"): "three"]), kw("found"), 1, true])
				#expect(try rt.eval("(let [m {:a 1 :b 2 :c 3}] [(count m) (get m :b) (get m :z) (get m :z :none) (m :c) (m :z :none) (:a m) (:z m :none) (find m :b) (find m :z) (contains? m :a) (contains? m :z) (contains? m 1)])")
					== [3, 2, nil, kw("none"), 3, kw("none"), 1, kw("none"), [kw("b"), 2], nil, true, false, false])
				// The seq order is the shape's: compare's order of the keys.
				#expect(try rt.eval("(let [m {:k4 1 :k5 2 :k6 3}] [(vec (seq m)) (vec (keys m)) (vec (vals m)) (reduce-kv (fn [acc k v] (conj acc k v)) [] m) (reduce (fn [acc e] (conj acc (key e))) [] m) (into [] m)])")
					== [[[kw("k4"), 1], [kw("k5"), 2], [kw("k6"), 3]], [kw("k4"), kw("k5"), kw("k6")], [1, 2, 3], [kw("k4"), 1, kw("k5"), 2, kw("k6"), 3], [kw("k4"), kw("k5"), kw("k6")], [[kw("k4"), 1], [kw("k5"), 2], [kw("k6"), 3]]])
				#expect(try rt.eval("(let [m {:a 1 :b 2}] [(assoc m :a 9) (assoc m :c 3) (dissoc m :a) (dissoc m :z) (conj m [:c 3]) (conj m {:c 3 :a 0}) (conj m nil) (merge m {:c 3}) (select-keys m [:a :z]) (empty m) (into {} m) (update m :a inc)])")
					== [Value([kw("a"): 9, kw("b"): 2]), Value([kw("a"): 1, kw("b"): 2, kw("c"): 3]), Value([kw("b"): 2]), Value([kw("a"): 1, kw("b"): 2]), Value([kw("a"): 1, kw("b"): 2, kw("c"): 3]),
					    Value([kw("a"): 0, kw("b"): 2, kw("c"): 3]), Value([kw("a"): 1, kw("b"): 2]), Value([kw("a"): 1, kw("b"): 2, kw("c"): 3]), Value([kw("a"): 1]), Value([:]), Value([kw("a"): 1, kw("b"): 2]), Value([kw("a"): 2, kw("b"): 2])])
				#expect(try rt.eval("(let [m {:a 1 :b 2}] [(map? (empty m)) (identical? (empty m) {}) (seq {}) (vec (map key m)) (zipmap [:x :y] [1 2]) (persistent! (assoc! (transient {:a 1}) :b 2)) (dissoc! (transient {:a 1 :b 2}) :a)])")
					== [true, true, nil, [kw("a"), kw("b")], Value([kw("x"): 1, kw("y"): 2]), Value([kw("a"): 1, kw("b"): 2]), Value([kw("b"): 2])])
				#expect(try rt.eval("[(pr-str {:k7 1 :k8 \"x\"}) (str {:k8 2 :k7 1}) (pr-str {:a {:b {:c 1}}})]") == ["{:k7 1, :k8 \"x\"}", "{:k7 1, :k8 2}", "{:a {:b {:c 1}}}"])
				#expect(message(rt, "(let [k :a j :a] {k 1 j 2})") == "Duplicate key: :a")
				#expect(try rt.eval("(let [k :a] {k 1 :b 2})") == Value([kw("a"): 1, kw("b"): 2]))
				#expect(try shaped("(let [k :a] {k 1 :b 2})"))
				#expect(try rt.eval("(let [{:keys [a b] :as m} {:a 1 :b 2}] [a b (count m)])") == [1, 2, 2])
				#expect(try rt.eval("(apply hash-map [:a 1 :b 2])") == Value([kw("a"): 1, kw("b"): 2]))
				#expect(try rt.eval("(vec (for [[k v] {:a 1 :b 2}] [v k]))") == [[1, kw("a")], [2, kw("b")]])
				try unbind("sh-trie")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func metaGoesGeneric() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(meta {:a 1})") == nil)
				#expect(try rt.eval("(meta (with-meta {:a 1} {:m 1}))") == Value([kw("m"): 1]))
				#expect(try rt.eval("(let [m (with-meta {:a 1} {:m 1})] [(= m {:a 1}) (:a m) (meta (assoc m :b 2)) (meta (dissoc m :a))])") == [true, 1, Value([kw("m"): 1]), Value([kw("m"): 1])])
				#expect(try rt.eval("(meta ^{:m 1} {:a 1})") == Value([kw("m"): 1]))
				#expect(try rt.eval("(meta (vary-meta {:a 1} assoc :m 2))") == Value([kw("m"): 2]))
				#expect(try shaped("(vary-meta (with-meta {:a 1} {:m 1}) (constantly nil))") == false)
				#expect(try rt.eval("(meta (select-keys {:a 1 :b 2} [:a]))") == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Reuse at rc == 1 is what keeps the address: -DCLJ_NO_REUSE turns every step below into a copy.
		@Test(.enabled(if: clj_reuse_enabled())) func uniqueAssocWritesInPlace() throws {
			let before = clj_debug_live_objects()
			do {
				let a = clj_keyword_from_cstr("a"), g = clj_keyword_from_cstr("g")
				var m = clj_map_assoc(clj_map_empty(), a, clj_fixnum(1))
				#expect(clj_is_shape_map(m) && clj_is_unique(m))
				let p = m
				let live = clj_debug_live_objects()
				m = clj_map_assoc(m, a, clj_fixnum(2))
				#expect(m == p)
				#expect(clj_map_get(m, a, CLJ_NIL) == clj_fixnum(2))
				#expect(clj_debug_live_objects() == live)
				// A new key grows the object: same pool class, same address; otherwise it moves (the system allocator may always), still one object.
				for k in ["b", "c", "d", "e", "f"] { m = clj_map_assoc(m, clj_keyword_from_cstr(k), clj_fixnum(0)) }
				#expect(clj_debug_live_objects() == live)
				let six = m
				m = clj_map_assoc(m, g, clj_fixnum(7))
				#expect(!clj_debug_pool_enabled() || clj_debug_cell_size(24 + 6 * 8) != clj_debug_cell_size(24 + 7 * 8) || m == six)
				#expect(clj_map_count(m) == 7 && clj_debug_live_objects() == live)
				m = clj_map_dissoc(m, g)
				#expect(clj_map_count(m) == 6 && clj_debug_live_objects() == live)
				// A second reference makes a copy of the slot array and leaves the original alone.
				_ = clj_retain(m)
				let copy = clj_map_assoc(m, a, clj_fixnum(3))
				#expect(copy != m)
				#expect(clj_map_get(m, a, CLJ_NIL) == clj_fixnum(2) && clj_map_get(copy, a, CLJ_NIL) == clj_fixnum(3))
				#expect(clj_map_shape(copy) == clj_map_shape(m))
				clj_release(copy)
				#expect(clj_is_unique(m))
				clj_release(m)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func sharedMapStaysShared() throws {
			let before = clj_debug_live_objects()
			do {
				let x = clj_string_from_cstr("x")
				var m = clj_map_assoc(clj_map_assoc(clj_map_empty(), clj_keyword_from_cstr("a"), clj_fixnum(1)), clj_keyword_from_cstr("b"), x)
				clj_release(x)
				clj_share(m)
				#expect(clj_debug_all_shared(m))
				// A unique published map is updated in place, or replaced by a copy that is published too: the new value is shared.
				let s = clj_string_from_cstr("y")
				m = clj_map_assoc(m, clj_keyword_from_cstr("c"), s)
				#expect(clj_debug_all_shared(m) && clj_is_shared(s))
				m = clj_map_assoc(m, clj_keyword_from_cstr("b"), s)
				clj_release(s)
				#expect(clj_debug_all_shared(m))
				#expect(clj_map_count(m) == 3)
				// A second reference makes a copy of its own, unshared, with the original left whole.
				let copy = clj_map_assoc(clj_retain(m), clj_keyword_from_cstr("d"), clj_fixnum(4))
				#expect(!clj_is_shared(copy) && clj_debug_all_shared(m))
				clj_release(copy)
				clj_release(m)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func lookupSiteCache() throws {
			let before = clj_debug_live_objects()
			do {
				// Monomorphic: one shape, one entry, every call after the first a hit.
				let mono = try Tree("(let [f (fn [m] (:b m))] [(f {:a 1 :b 2}) (f {:a 3 :b 4}) (f {:a 5 :b 6})])")
				#expect(try mono.run() == [2, 4, 6])
				#expect(mono.entries() == 1 && mono.hits() == 2 && mono.misses() == 1)
				// The same slot index through get, at any position of the shape.
				let get = try Tree("(let [f (fn [m] (get m :e :none))] [(f {:a 1 :b 2 :c 3 :d 4 :e 5}) (f {:a 1 :b 2 :c 3 :d 4 :e 6}) (f {:a 1}) (f {:a 1}) (f nil) (f 1)])")
				#expect(try get.run() == [5, 6, kw("none"), kw("none"), kw("none"), kw("none")])
				#expect(get.entries() == 2 && get.hits() == 2 && get.misses() == 4)
				// Polymorphic up to four shapes, then megamorphic: the generic lookup with no fills.
				let poly = try Tree("(let [f (fn [m] (:a m))] (mapv f [{:a 1} {:a 2 :b 2} {:a 3 :c 3} {:a 4 :d 4} {:a 1} {:a 2 :b 2}]))")
				#expect(try poly.run() == [1, 2, 3, 4, 1, 2])
				#expect(poly.entries() == 4 && poly.hits() == 2 && poly.misses() == 4)
				let mega = try Tree("(let [f (fn [m] (:a m))] (mapv f [{:a 1} {:a 2 :b 2} {:a 3 :c 3} {:a 4 :d 4} {:a 5 :e 5} {:a 1} {:a 2 :b 2}]))")
				#expect(try mega.run() == [1, 2, 3, 4, 5, 1, 2])
				#expect(mega.entries() == UInt32(CLJ_KW_IC_MEGA) && mega.hits() == 0 && mega.misses() == 7)
				// A trie receiver is never cached; a shape map beside it is.
				let trie = try Tree("(let [f (fn [m] (:a m))] [(f (assoc {1 2} :a 7)) (f (assoc {1 2} :a 8)) (f {:a 9})])")
				#expect(try trie.run() == [7, 8, 9])
				#expect(trie.entries() == 1 && trie.hits() == 0 && trie.misses() == 3)
				// A site of a keyword the shape lacks caches the absence.
				let absent = try Tree("(let [f (fn [m] (:z m :none))] [(f {:a 1}) (f {:a 2}) (f {:a 3 :z 4})])")
				#expect(try absent.run() == [kw("none"), kw("none"), 4])
				#expect(absent.entries() == 2 && absent.hits() == 1 && absent.misses() == 2)
				// The site of a rebound get takes the generic path and answers what the new fn does.
				let bound = try Tree("(let [f (fn [m] (get m :a))] [(f {:a 1}) (with-redefs [get (fn [m k] :redefined)] (f {:a 2})) (f {:a 3})])")
				#expect(try bound.run() == [1, kw("redefined"), 3])
				#expect(bound.entries() == 1 && bound.hits() == 1 && bound.misses() == 1)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func recordReceiversShareTheCache() throws {
			try declare("SP", "->SP", "map->SP", "SQ", "->SQ", "map->SQ")
			_ = try rt.eval("(defrecord SP [x y z]) (defrecord SQ [z y x])")
			let before = clj_debug_live_objects()
			do {
				let t = try Tree("(let [f (fn [m] (:z m))] [(f (->SP 1 2 3)) (f (->SP 4 5 6)) (f (->SQ 7 8 9)) (f {:z 10}) (f (assoc (->SP 1 2 3) :w 0)) (f (->SP 1 2 3))])")
				#expect(try t.run() == [3, 6, 7, 10, 3, 3])
				#expect(t.entries() == 3 && t.hits() == 3 && t.misses() == 3)
				// A key outside the basis is the extmap's: never cached, answered as the record does.
				let ext = try Tree("(let [f (fn [m] (:w m :none))] [(f (->SP 1 2 3)) (f (assoc (->SP 1 2 3) :w 4)) (f (->SP 1 2 3))])")
				#expect(try ext.run() == [kw("none"), 4, kw("none")])
				#expect(ext.entries() == 0 && ext.misses() == 3)
			}
			#expect(clj_debug_live_objects() == before)
			// A redefined record with another basis at the same site: the guard re-reads the live descriptor's basis.
			try declare("sh-mk")
			_ = try rt.eval("(def sh-mk (fn [] (->SP 1 2 3)))")
			let site = try Tree("(let [f (fn [m] (:z m))] [(f (sh-mk)) (f (sh-mk))])")
			#expect(try site.run() == [3, 3])
			_ = try rt.eval("(defrecord SP [z]) (def sh-mk (fn [] (->SP 3)))")
			#expect(try site.run() == [3, 3])
			_ = try rt.eval("(defrecord SP [q z]) (def sh-mk (fn [] (->SP 0 3)))")
			#expect(try site.run() == [3, 3])
			_ = try rt.eval("(defrecord SP [q]) (def sh-mk (fn [] (->SP 0)))")
			#expect(try site.run() == [nil, nil])
			try unbind("sh-mk", "SP", "->SP", "map->SP", "SQ", "->SQ", "map->SQ")
		}

		@Test func literalBuildsThroughItsShape() throws {
			let before = clj_debug_live_objects()
			do {
				let t = try Tree("(let [x 1] {:a x :b (inc x)})")
				#expect(clj_debug_exec_map_shaped(t.exec, 0) == false)
				#expect(try t.run() == Value([kw("a"): 1, kw("b"): 2]))
				let literal = try Tree("{:a 1 :b (inc 1)}")
				#expect(clj_debug_exec_map_shaped(literal.exec, 0))
				#expect(try literal.run() == Value([kw("a"): 1, kw("b"): 2]))
				#expect(try shape("{:a 1 :b (inc 1)}") == shape("{:a 1 :b 2}"))
				// A literal in another order than the canonical one lands its values in the right slots.
				#expect(try rt.eval("(let [x 10] [(:k9 {:k10 (* 2 x) :k9 x}) (:k10 {:k10 (* 2 x) :k9 x}) (vec (keys {:k9 x :k10 (* 2 x)}))])") == [10, 20, [kw("k10"), kw("k9")]])
				#expect(try rt.eval("(let [x 10] (= {:k10 (* 2 x) :k9 x} (hash-map :k10 20 :k9 10) {:k9 10 :k10 20}))") == true)
				// A throwing value releases the half-built map.
				#expect(message(rt, "{:a 1 :b (throw (ex-info \"boom\" {}))}") == "boom")
				// A non-keyword key or a computed key take the generic entry.
				let mixed = try Tree("{:a 1 1 (inc 1)}"), computed = try Tree("(let [k :a] {k (inc 1)})")
				#expect(clj_debug_exec_map_shaped(mixed.exec, 0) == false)
				#expect(clj_debug_exec_map_shaped(computed.exec, 0) == false)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func codecRoundTripKeepsTheShape() throws {
			let before = clj_debug_live_objects()
			do {
				let t = try Tree("(quote {:a 1 :b [2]})")
				let data = clj_node_to_data(t.node)
				#expect(data != CLJ_THROWN)
				let text = try #require(Value(owning: clj_pr_str(data)).string)
				clj_release(data)
				let back = try Value(reading: text)
				let node = withExtendedLifetime(back) { clj_node_from_data(back.raw) }
				#expect(node != nil)
				if let node {
					let v = Value(owning: clj_eval_node(node))
					#expect(v == Value([kw("a"): 1, kw("b"): [2]]))
					#expect(withExtendedLifetime(v) { clj_is_shape_map(v.raw) })
					clj_release(clj_from_ptr(node))
				}
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func shapesOffMakesTries() throws {
			let before = clj_debug_live_objects()
			do {
				clj_shapes_enable(false)
				#expect(try !shaped("{:a 1}"))
				#expect(try !shaped("(assoc {} :a 1)"))
				#expect(try rt.eval("[(:a {:a 1}) (= {:a 1 :b 2} (assoc {:b 2} :a 1))]") == [1, true])
				clj_shapes_enable(true)
				#expect(try shaped("{:a 1}"))
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
