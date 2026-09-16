// @ai-generated(solo)
import CljCore
import Testing
@testable import Clojure

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

extension CoreTests {
	// defrecord: a named shape whose descriptor carries the basis keywords and the hash map's slots (record.c).
	@Suite struct RecordTests {
		let rt = Runtime()

		init() {
			// Interning a keyword leaves an immortal object behind, so every one an expectation uses comes first.
			for k in ["a", "b", "c", "x", "y", "z", "count", "nf", "none", "found", "m", "k", "v", "extra", "one", "nope", "keys"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		private func definePoint() throws {
			try declare("Point", "->Point", "map->Point")
			_ = try rt.eval("(defrecord Point [x y])")
		}

		private func dropPoint() throws { try unbind("Point", "->Point", "map->Point") }

		@Test func constructorsLookupAndPredicates() throws {
			try definePoint()
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(:x (->Point 1 2)) (:y (->Point 1 2)) (get (->Point 1 2) :x) (get (->Point 1 2) :z :none)]") == [1, 2, 1, kw("none")])
				#expect(try rt.eval("((->Point 1 2) :y)") == 2)
				#expect(try rt.eval("[(count (->Point 1 2)) (:x (map->Point {:x 5})) (:y (map->Point {:x 5}))]") == [2, 5, nil])
				#expect(try rt.eval("[(record? (->Point 1 2)) (record? {:x 1}) (map? (->Point 1 2)) (associative? (->Point 1 2)) (coll? (->Point 1 2)) (counted? (->Point 1 2))]")
					== [true, false, true, true, true, true])
				#expect(try rt.eval("[(seq? (->Point 1 2)) (vector? (->Point 1 2)) (set? (->Point 1 2)) (ifn? (->Point 1 2))]") == [false, false, false, true])
				#expect(try rt.eval("(instance? Point (->Point 1 2))") == true)
				#expect(try rt.eval("[(instance? IRecord (->Point 1 2)) (instance? IPersistentMap (->Point 1 2)) (instance? IRecord {:x 1})]") == [true, true, false])
				#expect(try rt.eval("(pr-str (type (->Point 1 2)))") == "user.Point")
				// A basis field holding nil is still a key, unlike a missing one.
				#expect(try rt.eval("[(contains? (->Point nil 2) :x) (contains? (->Point 1 2) :z) (find (->Point 1 2) :x)]") == [true, false, [kw("x"), 1]])
			}
			#expect(clj_debug_live_objects() == before)
			try dropPoint()
		}

		@Test func assocDissocAndExtmap() throws {
			try definePoint()
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(:x (assoc (->Point 1 2) :x 9))") == 9)
				#expect(try rt.eval("(record? (assoc (->Point 1 2) :x 9))") == true)
				// A key outside the basis lands in the extmap and the value stays a record.
				#expect(try rt.eval("[(record? (assoc (->Point 1 2) :z 3)) (:z (assoc (->Point 1 2) :z 3)) (count (assoc (->Point 1 2) :z 3))]") == [true, 3, 3])
				#expect(try rt.eval("(pr-str (assoc (->Point 1 2) :z 3))") == "#user.Point{:x 1, :y 2, :z 3}")
				// dissoc of a basis key gives up the shape; of an ext key it does not.
				#expect(try rt.eval("[(record? (dissoc (->Point 1 2) :x)) (map? (dissoc (->Point 1 2) :x)) (dissoc (->Point 1 2) :x)]") == [false, true, Value([kw("y"): 2])])
				#expect(try rt.eval("[(record? (dissoc (assoc (->Point 1 2) :z 3) :z)) (count (dissoc (assoc (->Point 1 2) :z 3) :z))]") == [true, 2])
				// The extmap round trip leaves a record equal to the one it started from.
				#expect(try rt.eval("(= (dissoc (assoc (->Point 1 2) :z 3) :z) (->Point 1 2))") == true)
				#expect(try rt.eval("(= (->Point 1 2) (dissoc (assoc (->Point 1 2) :z 3) :z))") == true)
				#expect(try rt.eval("(dissoc (->Point 1 2) :nope)") == rt.eval("(->Point 1 2)"))
				#expect(try rt.eval("(:count (update (->Point 1 2) :x inc))") == nil)
				#expect(try rt.eval("(:x (update (->Point 1 2) :x inc))") == 2)
				#expect(try rt.eval("(:x (conj (->Point 1 2) [:x 7]))") == 7)
				#expect(try rt.eval("[(record? (merge (->Point 1 2) {:x 9 :z 3})) (:x (merge (->Point 1 2) {:x 9 :z 3})) (:z (merge (->Point 1 2) {:x 9 :z 3}))]") == [true, 9, 3])
				#expect(try rt.eval("(record? (merge {:x 9} (->Point 1 2)))") == false)
				#expect(message(rt, "(empty (->Point 1 2))") == "Can't create empty: user.Point")
			}
			#expect(clj_debug_live_objects() == before)
			try dropPoint()
		}

		@Test func seqCountAndReduction() throws {
			try definePoint()
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(vec (seq (->Point 1 2)))") == [[kw("x"), 1], [kw("y"), 2]])
				#expect(try rt.eval("[(vec (keys (->Point 1 2))) (vec (vals (->Point 1 2)))]") == [[kw("x"), kw("y")], [1, 2]])
				// Basis order first, the extmap after it.
				#expect(try rt.eval("(vec (keys (assoc (->Point 1 2) :z 3)))") == [kw("x"), kw("y"), kw("z")])
				#expect(try rt.eval("(into {} (->Point 1 2))") == Value([kw("x"): 1, kw("y"): 2]))
				#expect(try rt.eval("(reduce-kv (fn [acc k v] (conj acc k v)) [] (->Point 1 2))") == [kw("x"), 1, kw("y"), 2])
				#expect(try rt.eval("(reduce (fn [acc e] (conj acc (nth e 1))) [] (->Point 1 2))") == [1, 2])
				#expect(try rt.eval("(select-keys (->Point 1 2) [:x])") == Value([kw("x"): 1]))
				#expect(try rt.eval("(record? (select-keys (->Point 1 2) [:x]))") == false)
				#expect(try rt.eval("(let [{:keys [x y]} (->Point 1 2)] [x y])") == [1, 2])
				#expect(try rt.eval("(map key (->Point 1 2))") == Value(list: [kw("x"), kw("y")]))
			}
			#expect(clj_debug_live_objects() == before)
			try dropPoint()
		}

		@Test func recordWithoutFields() throws {
			try declare("Nullary", "->Nullary", "map->Nullary")
			_ = try rt.eval("(defrecord Nullary [])")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(seq (->Nullary)) (count (->Nullary)) (pr-str (->Nullary)) (record? (->Nullary))]") == [nil, 0, "#user.Nullary{}", true])
				#expect(try rt.eval("(= (->Nullary) (->Nullary))") == true)
				#expect(try rt.eval("[(record? (assoc (->Nullary) :z 1)) (:z (assoc (->Nullary) :z 1))]") == [true, 1])
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Nullary", "->Nullary", "map->Nullary")
		}

		@Test func equalityHashAndMeta() throws {
			try definePoint()
			try declare("Other", "->Other", "map->Other")
			_ = try rt.eval("(defrecord Other [x y])")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(= (->Point 1 2) (->Point 1 2)) (= (->Point 1 2) (->Point 1 3)) (= (->Point 1 2) (->Other 1 2))]") == [true, false, false])
				// A record is no map's equal, in either direction.
				#expect(try rt.eval("[(= (->Point 1 2) {:x 1 :y 2}) (= {:x 1 :y 2} (->Point 1 2)) (= (->Point 1 2) (sorted-map :x 1 :y 2))]") == [false, false, false])
				#expect(try rt.eval("(= (hash (->Point 1 2)) (hash {:x 1 :y 2}))") == true)
				#expect(try rt.eval("(= (hash (->Point 1 2)) (hash (->Point 1 3)))") == false)
				#expect(try rt.eval("(get {(->Point 1 2) :found} (->Point 1 2))") == kw("found"))
				#expect(try rt.eval("(get {(->Point 1 2) :found} {:x 1 :y 2} :none)") == kw("none"))
				#expect(try rt.eval("(count (set [(->Point 1 2) (->Point 1 2) (->Point 1 3)]))") == 2)
				// with-meta survives an assoc of a basis key and of an ext key alike.
				#expect(try rt.eval("(meta (with-meta (->Point 1 2) {:m 1}))") == Value([kw("m"): 1]))
				#expect(try rt.eval("(meta (assoc (with-meta (->Point 1 2) {:m 1}) :x 9))") == Value([kw("m"): 1]))
				#expect(try rt.eval("(meta (assoc (with-meta (->Point 1 2) {:m 1}) :z 9))") == Value([kw("m"): 1]))
				#expect(try rt.eval("(meta (dissoc (with-meta (->Point 1 2) {:m 1}) :x))") == Value([kw("m"): 1]))
				// Equality and hash ignore the meta, as everywhere else.
				#expect(try rt.eval("(= (with-meta (->Point 1 2) {:m 1}) (->Point 1 2))") == true)
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Other", "->Other", "map->Other")
			try dropPoint()
		}

		@Test func protocolMethodsSeeFieldsAsLocals() throws {
			try declare("Scaled", "->Scaled", "map->Scaled", "Sizer", "sized", "scaled-by", "Bad", "->Bad", "map->Bad")
			_ = try rt.eval("""
			(defprotocol Sizer (sized [this k]) (scaled-by [this]))
			(defrecord Scaled [n factor]
			  Sizer
			  (sized [this k] [n (get this k) (:factor this)])
			  (scaled-by [_] (* n factor)))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(scaled-by (->Scaled 3 4))") == 12)
				#expect(try rt.eval("(sized (->Scaled 3 4) :n)") == [3, 3, 4])
				#expect(try rt.eval("[(satisfies? Sizer (->Scaled 1 1)) (satisfies? Sizer {:n 1})]") == [true, false])
				// A core interface in the body is refused: the map slots are the record's own.
				#expect(message(rt, "(defrecord Bad [x] Counted (count [_] 1))")
					== "Counted cannot be implemented by defrecord: the map interfaces are the record's own")
				try unbind("Bad", "->Bad", "map->Bad")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Scaled", "->Scaled", "map->Scaled", "Sizer", "sized", "scaled-by")
		}

		@Test func mapConstructorAndPrinting() throws {
			try definePoint()
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(= (map->Point {:x 1 :y 2}) (->Point 1 2))") == true)
				#expect(try rt.eval("(pr-str (map->Point {:x 1 :y 2 :z 3}))") == "#user.Point{:x 1, :y 2, :z 3}")
				#expect(try rt.eval("(pr-str (->Point \"s\" nil))") == "#user.Point{:x \"s\", :y nil}")
				#expect(try rt.eval("(str (->Point 1 2))") == "#user.Point{:x 1, :y 2}")
				#expect(try rt.eval("(pr-str [(->Point 1 2)])") == "[#user.Point{:x 1, :y 2}]")
				#expect(try rt.eval("(= (map->Point (->Point 1 2)) (->Point 1 2))") == true)
				#expect(message(rt, "(map->Point 7)") == "map->Name expects a map, got: long")
				#expect(message(rt, "(->Point 1)") == "Wrong number of args (1) passed to: user/->Point")
			}
			#expect(clj_debug_live_objects() == before)
			try dropPoint()
		}

		@Test func editableMatchesTheJvm() throws {
			try definePoint()
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(instance? clojure.lang.IEditableCollection {}) (instance? clojure.lang.IEditableCollection []) (instance? clojure.lang.IEditableCollection #{})]")
					== [true, true, true])
				#expect(try rt.eval("[(instance? clojure.lang.IEditableCollection (->Point 1 2)) (instance? clojure.lang.IEditableCollection (sorted-map)) (instance? clojure.lang.IEditableCollection '()) (instance? clojure.lang.IEditableCollection \"s\")]")
					== [false, false, false, false])
			}
			#expect(clj_debug_live_objects() == before)
			try dropPoint()
		}

		@Test func recordsMixWithTheOtherMapRepresentations() throws {
			try definePoint()
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(conj {} (->Point 1 2))") == Value([kw("x"): 1, kw("y"): 2]))
				#expect(try rt.eval("(conj {} (assoc (->Point 1 2) :z 3))") == Value([kw("x"): 1, kw("y"): 2, kw("z"): 3]))
				#expect(try rt.eval("(:x (merge (->Point 1 2) (sorted-map :x 9)))") == 9)
				#expect(try rt.eval("(= (map->Point (sorted-map :x 1 :y 2)) (->Point 1 2))") == true)
				#expect(try rt.eval("(pr-str (conj (sorted-map) (->Point 1 2)))") == "{:x 1, :y 2}")
			}
			#expect(clj_debug_live_objects() == before)
			try dropPoint()
		}

		// extend on a core interface is never retired here, so the table lives past the baseline.
		@Test func protocolOnTheMapInterfaceReachesARecord() throws {
			try definePoint()
			try declare("Mapper", "mapped")
			_ = try rt.eval("(defprotocol Mapper (mapped [x]))")
			_ = try rt.eval("(extend-type IPersistentMap Mapper (mapped [x] (count x)))")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(mapped (->Point 1 2)) (mapped {:a 1})]") == [2, 1])
				#expect(try rt.eval("(satisfies? Mapper (->Point 1 2))") == true)
			}
			#expect(clj_debug_live_objects() == before)
			try dropPoint()
		}

		// Storing into a shared record has to share the new child: every child of a shared object is shared.
		@Test func sharedRecordKeepsChildrenShared() throws {
			try definePoint()
			let before = clj_debug_live_objects()
			do {
				let type = try rt.eval("Point")
				let vals = [clj_fixnum(1), clj_fixnum(2)]
				var cur = vals.withUnsafeBufferPointer { clj_record_new(type.raw, $0.baseAddress, 2) }
				clj_share(cur)
				#expect(clj_debug_all_shared(cur))
				// rc stays 1, so every step below rewrites the shared record in place.
				for i in 0..<20 {
					let value = clj_cons_new(clj_fixnum(i), CLJ_NIL)
					cur = clj_type_of(cur).pointee.assoc(cur, Value(keyword: i % 2 == 0 ? "z" : "x").raw, value)
					clj_release(value)
					#expect(clj_debug_all_shared(cur))
				}
				clj_release(cur)
			}
			#expect(clj_debug_live_objects() == before)
			try dropPoint()
		}

		// Reuse at rc == 1 is what keeps the address: -DCLJ_NO_REUSE turns every step below into a copy.
		@Test(.enabled(if: clj_reuse_enabled())) func uniqueRecordIsUpdatedInPlace() throws {
			try definePoint()
			let before = clj_debug_live_objects()
			do {
				let type = try rt.eval("Point")
				var vals = [clj_fixnum(1), clj_fixnum(2)]
				var r = vals.withUnsafeBufferPointer { clj_record_new(type.raw, $0.baseAddress, 2) }
				#expect(clj_is_unique(r) == clj_reuse_enabled())
				let wrapper = r
				let live = clj_debug_live_objects()

				let x = Value(keyword: "x").raw
				r = clj_type_of(r).pointee.assoc(r, x, clj_fixnum(3))
				#expect(r == wrapper)
				#expect(clj_record_field(r, 0) == clj_fixnum(3))
				#expect(clj_debug_live_objects() == live)

				_ = clj_retain(r)
				let copy = clj_type_of(r).pointee.assoc(r, x, clj_fixnum(4))
				#expect(copy != wrapper)
				#expect(clj_record_field(r, 0) == clj_fixnum(3))
				#expect(clj_record_field(copy, 0) == clj_fixnum(4))
				clj_release(copy)
				#expect(clj_is_unique(r) == clj_reuse_enabled())
				clj_release(r)
				vals.removeAll()
			}
			#expect(clj_debug_live_objects() == before)
			try dropPoint()
		}
	}
}
