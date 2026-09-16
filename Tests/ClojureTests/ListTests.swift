// @ai-generated(solo)
import CljCore
import Testing
@testable import Clojure

private func items(_ seq: clj_value) -> [clj_value] {
	var out: [clj_value] = []
	var it = clj_seq_iter_start(seq)
	var item: clj_value = CLJ_NIL
	while clj_seq_iter_next(&it, &item) { out.append(item) }
	return out
}

private func list(_ raw: [clj_value]) -> clj_value {
	raw.withUnsafeBufferPointer { clj_list_from_array($0.baseAddress, $0.count) }
}

private func vector(_ raw: [clj_value]) -> clj_value {
	raw.withUnsafeBufferPointer { clj_vector_from_array($0.baseAddress, UInt32($0.count)) }
}

extension CoreTests {
	@Suite struct ListTests {
		@Test func emptyListSingleton() {
			let before = clj_debug_live_objects()
			let e = clj_list_empty()
			#expect(clj_is_list(e))
			#expect(clj_is_empty_list(e))
			#expect(!clj_is_list(CLJ_NIL))
			#expect(!clj_is_list(clj_vector_empty()))
			#expect(clj_list_count(e) == 0)
			#expect(items(e).isEmpty)
			#expect(!clj_is_unique(e))
			#expect(String(cString: clj_type_name(e)) == "empty-list")
			_ = clj_retain(e)
			clj_release(e)
			clj_release(e)
			#expect(clj_list_empty() == e)
			#expect(list([]) == e)
			#expect(!clj_equals(e, CLJ_NIL))
			#expect(clj_equals(e, clj_vector_empty()))
			#expect(clj_equals(clj_vector_empty(), e))
			#expect(clj_hash(e) == clj_hash(clj_vector_empty()))
			#expect(clj_debug_live_objects() == before)
		}

		@Test func fromArrayBuildsConsChain() {
			let before = clj_debug_live_objects()
			let raw = (1...5).map { clj_fixnum($0) }
			let l = list(raw)
			#expect(clj_is_list(l))
			#expect(!clj_is_empty_list(l))
			#expect(String(cString: clj_type_name(l)) == "list")
			#expect(clj_list_count(l) == 5)
			#expect(items(l) == raw)
			#expect(clj_cons_of(l).pointee.first == clj_fixnum(1))
			var tail = l
			for _ in 0..<5 { tail = clj_cons_of(tail).pointee.rest }
			#expect(tail == clj_list_empty())
			#expect(clj_debug_live_objects() == before + 5)
			clj_release(l)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func sequentialEqualityAndHash() {
			let before = clj_debug_live_objects()
			let raw = [clj_fixnum(1), clj_fixnum(2)]
			let l = list(raw)
			let v = vector(raw)
			let consNil = clj_cons_new(clj_fixnum(1), clj_cons_new(clj_fixnum(2), CLJ_NIL))
			clj_release(clj_cons_of(consNil).pointee.rest)
			let consVec = clj_cons_new(clj_fixnum(1), vector([clj_fixnum(2)]))
			clj_release(clj_cons_of(consVec).pointee.rest)
			let longer = list(raw + [clj_fixnum(3)])
			let other = list([clj_fixnum(1), clj_fixnum(3)])

			#expect(clj_equals(l, v))
			#expect(clj_equals(v, l))
			#expect(clj_equals(l, consNil))
			#expect(clj_equals(consNil, v))
			#expect(clj_equals(l, consVec))
			#expect(clj_list_count(consVec) == 2)
			#expect(clj_hash(l) == clj_hash(v))
			#expect(clj_hash(l) == clj_hash(consNil))
			#expect(clj_hash(l) == clj_hash(consVec))
			#expect(!clj_equals(l, longer))
			#expect(!clj_equals(v, longer))
			#expect(!clj_equals(longer, v))
			#expect(!clj_equals(l, other))
			#expect(!clj_equals(l, clj_fixnum(1)))
			#expect(!clj_equals(l, CLJ_NIL))
			#expect(!clj_equals(l, clj_list_empty()))
			#expect(clj_hash(l) != clj_hash(other))

			var m = clj_map_assoc(clj_map_empty(), l, clj_fixnum(1))
			#expect(clj_map_get(m, v, CLJ_NIL) == clj_fixnum(1))
			m = clj_map_assoc(m, v, clj_fixnum(2))
			#expect(clj_map_count(m) == 1)
			#expect(clj_map_get(m, consNil, CLJ_NIL) == clj_fixnum(2))
			for x in [l, v, consNil, consVec, longer, other, m] { clj_release(x) }
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nestedElementsAreOwned() {
			let before = clj_debug_live_objects()
			let s = clj_string_from_cstr("x")
			let inner = list([s])
			let outer = list([inner, s])
			#expect(!clj_is_unique(s))
			clj_release(s)
			clj_release(inner)
			#expect(clj_debug_live_objects() == before + 4)
			#expect(clj_equals(clj_cons_of(clj_cons_of(outer).pointee.rest).pointee.first, clj_cons_of(inner).pointee.first))
			clj_release(outer)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func valueListRoundTrip() {
			let before = clj_debug_live_objects()
			do {
				let l = Value(list: [1, "two", nil, [3.0, true]])
				withExtendedLifetime(l) {
					#expect(l.typeName == "list")
					#expect(clj_debug_all_shared(l.raw))
					#expect(l.list == [1, "two", nil, [3.0, true]])
					#expect(l.array == nil)
					#expect(Value([1, "two", nil, [3.0, true]]).list == nil)
					#expect(l == Value([1, "two", nil, [3.0, true]]))
					#expect(l.hashValue == Value([1, "two", nil, [3.0, true]]).hashValue)
					#expect(Value(list: []).list == [])
					#expect(Value(list: []).raw == clj_list_empty())
					#expect(Value(list: []) == Value([]))
					#expect(Value(list: []) != nil)
				}
				var d: [Value: Int] = [:]
				d[[1, 2]] = 1
				d[Value(list: [1, 2])] = 2
				#expect(d.count == 1 && d[[1, 2]] == 2)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A Cons is not an IPersistentList; RT.cons over nil and PersistentList.cons are.
		@Test func consIsNotAList() throws {
			clj_init()
			_ = Value(keyword: "m")
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("[(list? (cons 1 '())) (seq? (cons 1 '())) (sequential? (cons 1 '()))]") == [false, true, true])
				#expect(try cljEval("[(list? (cons 1 nil)) (list? (cons 1 [2])) (list? (cons 1 (lazy-seq [2])))]") == [true, false, false])
				#expect(try cljEval("[(list? '(1 2)) (list? ()) (list? (list 1)) (list? (rest '(1 2))) (list? (pop '(1 2)))]") == [true, true, true, true, true])
				#expect(try cljEval("[(list? (conj '(1) 2)) (list? (reverse [1 2])) (list? (seq '(1 2)))]") == [true, true, true])
				#expect(try cljEval("[(list? (map inc [1])) (list? (range 2)) (list? [1]) (list? nil)]") == [false, false, false, false])
				#expect(try cljEval("[(type (cons 1 '())) (type '(1 2))]").description == "[cons list]")
				#expect(try cljEval("[(instance? Cons (cons 1 '())) (instance? PersistentList '(1 2))]") == [true, true])
				#expect(cljEvalError("(peek (cons 1 '()))") != nil)
				#expect(try cljEval("[(= (cons 1 '()) '(1)) (= (hash (cons 1 '())) (hash '(1)))]") == [true, true])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// PersistentList.cons carries the list's meta onto the new head; pop of the last cell keeps it too.
		@Test func conjOnAListKeepsMeta() throws {
			clj_init()
			_ = Value(keyword: "m")
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("(meta (conj (with-meta '() {:m 1}) 2))").description == "{:m 1}")
				#expect(try cljEval("(meta (conj (with-meta '(1) {:m 1}) 2))").description == "{:m 1}")
				#expect(try cljEval("(meta (conj (conj (with-meta '() {:m 1}) 2) 3))").description == "{:m 1}")
				#expect(try cljEval("(conj (with-meta '(1) {:m 1}) 2)").description == "(2 1)")
				#expect(try cljEval("(meta (pop (with-meta '(1) {:m 1})))").description == "{:m 1}")
				// ASeq.cons drops meta, so conj onto a Cons or a lazy seq does not carry it.
				#expect(try cljEval("[(meta (conj (with-meta (cons 1 '()) {:m 1}) 2)) (meta (rest (with-meta '(1 2) {:m 1})))]") == [nil, nil])
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
