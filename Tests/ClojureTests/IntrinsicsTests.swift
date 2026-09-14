// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

// What a call produced: the printed value, or the thrown message. Two calls agree when these agree, or the
// values are `=` (a NaN prints the same and compares unequal to itself).
private enum Outcome: Equatable {
	case value(Value)
	case thrown(String)

	init(_ raw: clj_value) {
		if raw == CLJ_THROWN {
			self = .thrown(Value(owning: clj_take_pending()).description)
		} else {
			self = .value(Value(owning: raw))
		}
	}

	static func == (a: Outcome, b: Outcome) -> Bool {
		switch (a, b) {
		case let (.value(x), .value(y)): return x == y || x.description == y.description
		case let (.thrown(x), .thrown(y)): return x == y
		default: return false
		}
	}
}

extension CoreTests {
	@Suite struct IntrinsicsTests {
		init() {
			clj_init()
			for k in ["k", "a", "b", "intrinsic"] { _ = Value(keyword: k) }
		}

		// Every table entry crossed with sample values of every type, by arity: the C function called directly
		// must agree with the var's fn through clj_invoke, in value or in thrown message.
		@Test func intrinsicsMatchTheirBuiltins() throws {
			_ = try cljEval("(def DiffBox) (def ->DiffBox) (deftype DiffBox [v])")
			let samples = try cljEval("""
			[nil true false 0 1 -1 7 \(Int.max >> 1) \(Int.min >> 1) 1.5 -0.5 0.0 1e300 \\a "" "str" :k :ns/k 'sym 'ns/sym
			 [] [1 2 3] {} {:a 1 :b 2} '() '(1 2) (let [l (lazy-seq [1 2])] (seq l) l) (range 3) inc (->DiffBox 1)]
			""")
			let before = clj_debug_live_objects()
			do {
				var n = 0
				let table = try #require(clj_intrinsic_table(&n))
				#expect(n >= 40)
				let count = Int(clj_vector_count(samples.raw))
				let items = (0..<count).map { clj_vector_nth(samples.raw, UInt32($0)) }
				var calls = 0
				for i in 0..<n {
					let op = table + i
					let arity = Int(op.pointee.arity)
					let name = String(cString: op.pointee.name)
					#expect(arity == Int(op.pointee.kind.rawValue), Comment(rawValue: name))
					let fn = Value(owning: clj_var_deref(clj_intrinsic_var(op)))
					#expect(fn.raw == clj_intrinsic_builtin(op), Comment(rawValue: "\(name): core.clj rebinds the var"))
					var args = [clj_value](repeating: CLJ_NIL, count: arity)
					var index = [Int](repeating: 0, count: arity)
					tuples: while true {
						for k in 0..<arity { args[k] = items[index[k]] }
						let direct = Outcome(args.withUnsafeBufferPointer { clj_intrinsic_call(op, $0.baseAddress) })
						let generic = Outcome(args.withUnsafeBufferPointer { clj_invoke(fn.raw, $0.baseAddress, arity) })
						#expect(direct == generic, Comment(rawValue: "\(name) on \(args.map { Value(borrowing: $0).description })"))
						calls += 1
						var k = arity - 1
						while k >= 0 {
							index[k] += 1
							if index[k] < count { continue tuples }
							index[k] = 0
							k -= 1
						}
						break
					}
				}
				#expect(calls > 30_000)
			}
			#expect(clj_debug_live_objects() == before)
			_ = try cljEval("(def DiffBox nil) (def ->DiffBox nil)")
		}
	}
}
