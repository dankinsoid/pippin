// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval(source) }

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

private let SEQABLE = UInt64(CLJ_CORE_SEQABLE), SEQ = UInt64(CLJ_CORE_SEQ), SEQUENTIAL = UInt64(CLJ_CORE_SEQUENTIAL)
private let COLL = UInt64(CLJ_CORE_COLL), COUNTED = UInt64(CLJ_CORE_COUNTED), LOOKUP = UInt64(CLJ_CORE_LOOKUP)
private let ASSOCIATIVE = UInt64(CLJ_CORE_ASSOCIATIVE), INDEXED = UInt64(CLJ_CORE_INDEXED), FN = UInt64(CLJ_CORE_FN)
private let LIST = UInt64(CLJ_CORE_LIST), VECTOR = UInt64(CLJ_CORE_VECTOR), MAP = UInt64(CLJ_CORE_MAP)
private let ERROR = UInt64(CLJ_CORE_ERROR), META = UInt64(CLJ_CORE_META), OBJ = UInt64(CLJ_CORE_OBJ)
private let ASEQ = SEQABLE | SEQ | SEQUENTIAL | COLL
private let IOBJ = META | OBJ
private let REDUCE = UInt64(CLJ_CORE_REDUCE), EDITABLE = UInt64(CLJ_CORE_EDITABLE)

extension CoreTests {
	@Suite struct DescriptorTests {
		// Every builtin heap type with the interfaces it implements.
		private func samples() throws -> [(String, Value, UInt64)] {
			[
				("list", Value(list: [1]), ASEQ | LIST | IOBJ),
				("cons", try eval("(cons 1 [2])"), ASEQ | IOBJ),
				("empty-list", Value(list: []), ASEQ | LIST | COUNTED | IOBJ),
				("vector", [1, 2], SEQABLE | SEQUENTIAL | COLL | COUNTED | LOOKUP | ASSOCIATIVE | INDEXED | FN | VECTOR | IOBJ | REDUCE | EDITABLE),
				("map", try Value(reading: "{:a 1}"), SEQABLE | COLL | COUNTED | LOOKUP | ASSOCIATIVE | FN | MAP | IOBJ | REDUCE | EDITABLE),
				("string", "ab", SEQABLE),
				("keyword", Value(keyword: "k"), FN),
				("symbol", Value(symbol: "s"), IOBJ),
				("fn", try eval("inc"), FN | IOBJ),
				("double", 1.5, 0),
				("exception", try eval("(ex-info \"x\" {})"), ERROR),
				("var", try eval("#'inc"), FN | META),
				("namespace", Value(borrowing: clj_ns_user()), 0),
				("type", Value(borrowing: clj_from_ptr(UnsafeMutableRawPointer(mutating: clj_header_of(Value(list: []).raw).pointee.type))), 0),
				("lazy-seq", try eval("(lazy-seq [1])"), ASEQ),
				("vector-seq", try eval("(seq [1 2])"), ASEQ | COUNTED | REDUCE),
				("string-seq", try eval("(seq \"ab\")"), ASEQ),
				("range", try eval("(range 3)"), ASEQ | COUNTED | REDUCE),
				("reduced", try eval("(reduced 1)"), 0),
				("volatile", try eval("(volatile! 1)"), 0),
			]
		}

		@Test func coreBitsOfEveryBuiltinType() throws {
			clj_init()
			_ = Value(keyword: "k")
			for (name, value, bits) in try samples() {
				#expect(value.typeName == name)
				#expect(clj_core_bits(value.raw) == bits, "\(name)")
				let t = clj_type_of(value.raw).pointee
				// Slots follow the bits: a seqable has seq, a seq has first and next, IFn has invoke, a counted count.
				#expect((t.seq != nil) == (bits & SEQABLE != 0), "\(name) seq slot")
				if bits & SEQ != 0 && t.seq != nil {
					let s = Value(owning: clj_seq(value.raw))
					if !s.isNil {
						let st = clj_type_of(s.raw).pointee
						#expect(st.first != nil && st.next != nil, "\(name): seq of a seq needs first/next")
					}
				}
				#expect((t.invoke != nil) == (bits & FN != 0), "\(name) invoke slot")
				if bits & COUNTED != 0 { #expect(t.count != nil, "\(name) count slot") }
				if bits & LOOKUP != 0 { #expect(t.lookup != nil, "\(name) lookup slot") }
				if bits & COLL != 0 { #expect(t.conj != nil, "\(name) conj slot") }
				#expect((t.ex_message != nil && t.ex_data != nil && t.ex_cause != nil) == (bits & ERROR != 0), "\(name) error slots")
				#expect((t.meta != nil) == (bits & META != 0), "\(name) meta slot")
				#expect((t.with_meta != nil) == (bits & OBJ != 0), "\(name) with_meta slot")
				#expect(t.user_protos == nil)
			}
			#expect(clj_core_bits(CLJ_NIL) == 0)
			#expect(clj_core_bits(clj_fixnum(1)) == 0)
		}

		@Test func fastPathsOnlyWhereSeqIsAView() throws {
			clj_init()
			let fast = ["vector", "string", "range", "vector-seq", "string-seq", "list", "cons", "empty-list"]
			for (name, value, _) in try samples() {
				let t = clj_type_of(value.raw).pointee
				#expect((t.first != nil && t.next != nil) == fast.contains(name), "\(name)")
				#expect((t.rest != nil) == (name == "cons" || name == "list"), "\(name)")
			}
		}

		@Test func predicatesReadCoreBits() throws {
			clj_init()
			_ = Value(keyword: "k")
			let before = clj_debug_live_objects()
			do {
				let preds = "[(seqable? x) (seq? x) (sequential? x) (coll? x) (counted? x) (ifn? x) (associative? x) (indexed? x) (list? x) (vector? x) (map? x) (fn? x)]"
				func check(_ x: String, _ expected: [Bool]) throws {
					#expect(try eval("(let [x \(x)] \(preds))") == Value(expected.map { Value($0) }), Comment(rawValue: x))
				}
				//                          seqable seq   seql  coll  cntd  ifn   assoc idx   list  vec   map   fn
				try check("'(1 2)", [true, true, true, true, false, false, false, false, true, false, false, false])
				try check("()", [true, true, true, true, true, false, false, false, true, false, false, false])
				try check("[1]", [true, false, true, true, true, true, true, true, false, true, false, false])
				try check("{:k 1}", [true, false, false, true, true, true, true, false, false, false, true, false])
				try check("\"s\"", [true, false, false, false, false, false, false, false, false, false, false, false])
				try check("nil", [true, false, false, false, false, false, false, false, false, false, false, false])
				try check("1", [false, false, false, false, false, false, false, false, false, false, false, false])
				try check(":k", [false, false, false, false, false, true, false, false, false, false, false, false])
				try check("inc", [false, false, false, false, false, true, false, false, false, false, false, true])
				try check("(seq [1])", [true, true, true, true, true, false, false, false, false, false, false, false])
				try check("(seq \"a\")", [true, true, true, true, false, false, false, false, false, false, false, false])
				try check("(range 2)", [true, true, true, true, true, false, false, false, false, false, false, false])
				try check("(map inc [1])", [true, true, true, true, false, false, false, false, false, false, false, false])
				try check("(cons 1 [2])", [true, true, true, true, false, false, false, false, false, false, false, false])
				// A Cons is not an IPersistentList, so list? is false while seq? stays true.
				try check("(cons 1 '())", [true, true, true, true, false, false, false, false, false, false, false, false])
				try check("(cons 1 nil)", [true, true, true, true, false, false, false, false, true, false, false, false])
				try check("(conj '(1) 2)", [true, true, true, true, false, false, false, false, true, false, false, false])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A random seqable of every builtin kind, including the seq views a few steps in.
		private func randomSeqable(_ rng: inout SplitMix64) throws -> Value {
			let n = rng.below(6)
			let items = (0..<n).map { _ in Value(rng.below(100)) }
			switch rng.below(9) {
			case 0: return Value(items)
			case 1: return Value(list: items)
			case 2: return Value((0..<n).map { _ in ["a", "λ", "🙂", "b", "é"][rng.below(5)] }.joined())
			case 3:
				let start = rng.below(20) - 10, end = rng.below(20) - 10, step = [1, 2, 3, -1, -2][rng.below(5)]
				return try eval("(range \(start) \(end) \(step))")
			case 4: return try eval("(seq \(Value(items)))")
			case 5: return try eval("(next \(Value(items)))")
			case 6: return try eval("(map inc \(Value(items)))")
			case 7: return try eval("(cons 0 \(Value(items)))")
			default: return try eval("(next (seq \"\((0..<n).map { _ in ["x", "λ"][rng.below(2)] }.joined())\"))")
			}
		}

		// Walks to the end through the fast path and through seq, comparing every element.
		private func walkBoth(_ start: Value) -> ([Value], [Value])? {
			var fast: [Value] = [], slow: [Value] = []
			var a = start, b = start
			for _ in 0..<64 {
				let sa = Value(owning: clj_seq(a.raw)), sb = Value(owning: clj_seq(b.raw))
				if sa.raw == CLJ_THROWN || sb.raw == CLJ_THROWN { return nil }
				if sa.isNil || sb.isNil {
					guard sa.isNil && sb.isNil else { return nil }
					return (fast, slow)
				}
				let fa = Value(owning: clj_first(a.raw)), fb = Value(owning: clj_debug_first_via_seq(b.raw))
				if fa.raw == CLJ_THROWN || fb.raw == CLJ_THROWN { return nil }
				fast.append(fa)
				slow.append(fb)
				a = Value(owning: clj_next(a.raw))
				b = Value(owning: clj_debug_next_via_seq(b.raw))
				if a.raw == CLJ_THROWN || b.raw == CLJ_THROWN { return nil }
			}
			return (fast, slow)
		}

		@Test func fastPathsAgreeWithSeq() throws {
			clj_init()
			var rng = SplitMix64(state: 7)
			let before = clj_debug_live_objects()
			do {
				for _ in 0..<150 {
					let v = try randomSeqable(&rng)
					guard let (fast, slow) = walkBoth(v) else {
						Issue.record("walk failed for \(v)")
						continue
					}
					#expect(fast == slow, "\(v)")
					// count through the slot and by walking agree, as does the iterator.
					let count = Value(owning: clj_count(v.raw))
					#expect(count.int == fast.count, "\(v)")
					#expect(Int(clj_list_count(v.raw)) == fast.count, "\(v)")
					if v.isNil { continue }
					let s = Value(owning: clj_seq(v.raw))
					#expect(s.isNil == fast.isEmpty, "\(v)")
					if !s.isNil {
						#expect(s.list == fast, "\(v)")
						#expect(Value(owning: clj_rest(v.raw)).list == Array(fast.dropFirst()), "\(v)")
					}
				}
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
