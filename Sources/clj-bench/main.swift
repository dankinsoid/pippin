// @ai-generated(solo)
import CljCore
import Dispatch
import Foundation
import HashTreeCollections

// Sizes chosen to hit a single root node, a shallow trie and a deep one.
let sizes = [10, 1_000, 100_000]
let lookups = 1_000_000
let reps = 5

struct SplitMix64 {
	var state: UInt64
	mutating func next() -> UInt64 {
		state &+= 0x9E37_79B9_7F4A_7C15
		var z = state
		z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
		z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
		return z ^ (z >> 31)
	}
}

func shuffled(_ n: Int, seed: UInt64) -> [Int] {
	var rng = SplitMix64(state: seed)
	var a = Array(0..<n)
	for i in stride(from: n - 1, to: 0, by: -1) {
		a.swapAt(i, Int(rng.next() % UInt64(i + 1)))
	}
	return a
}

func randomKeys(_ count: Int, below: Int, offset: Int = 0, seed: UInt64) -> [Int] {
	var rng = SplitMix64(state: seed)
	return (0..<count).map { _ in Int(rng.next() % UInt64(below)) + offset }
}

// Median of `reps` runs, ns per op. Each run repeats the body until ~targetOps operations are done,
// so a 10-op scenario is not a 1 µs measurement dominated by the timer. One untimed warmup run.
// The checksum keeps the optimizer from dropping the work.
let targetOps = 1_000_000
func measure(ops: Int, _ body: () -> UInt64) -> Double {
	let iters = max(1, targetOps / ops)
	var times: [Double] = []
	var sink: UInt64 = body()
	for _ in 0..<reps {
		let t0 = DispatchTime.now().uptimeNanoseconds
		for _ in 0..<iters { sink &+= body() }
		let t1 = DispatchTime.now().uptimeNanoseconds
		times.append(Double(t1 - t0) / Double(ops * iters))
	}
	blackHole(sink)
	return times.sorted()[reps / 2]
}

@inline(never) func blackHole(_ x: UInt64) { if x == 0xDEAD_BEEF_0BAD_F00D { print("") } }

// MARK: - C HAMT

func cBuild(_ keys: [Int]) -> clj_value {
	var m = clj_map_empty()
	for k in keys { m = clj_map_assoc(m, clj_fixnum(k), clj_fixnum(k)) }
	return m
}

func cAssocReuse(_ keys: [Int]) -> UInt64 {
	let m = cBuild(keys)
	let c = clj_map_count(m)
	clj_release(m)
	return UInt64(c)
}

func cAssocPersist(_ keys: [Int]) -> UInt64 {
	var versions: [clj_value] = []
	versions.reserveCapacity(keys.count)
	var m = clj_map_empty()
	for k in keys {
		versions.append(clj_retain(m))
		m = clj_map_assoc(m, clj_fixnum(k), clj_fixnum(k))
	}
	let c = clj_map_count(m)
	clj_release(m)
	for v in versions { clj_release(v) }
	return UInt64(c)
}

func cGet(_ m: clj_value, _ probes: [Int]) -> UInt64 {
	var sum: UInt64 = 0
	for k in probes {
		let v = clj_map_get(m, clj_fixnum(k), CLJ_NIL)
		if clj_is_fixnum(v) { sum &+= UInt64(bitPattern: Int64(clj_fixnum_val(v))) }
	}
	return sum
}

func cDissocReuse(_ keys: [Int], _ order: [Int]) -> UInt64 {
	var m = cBuild(keys)
	for k in order { m = clj_map_dissoc(m, clj_fixnum(k)) }
	let c = clj_map_count(m)
	clj_release(m)
	return UInt64(c)
}

// MARK: - TreeDictionary

func tBuild(_ keys: [Int]) -> TreeDictionary<Int, Int> {
	var d = TreeDictionary<Int, Int>()
	for k in keys { d[k] = k }
	return d
}

func tAssocPersist(_ keys: [Int]) -> UInt64 {
	var versions: [TreeDictionary<Int, Int>] = []
	versions.reserveCapacity(keys.count)
	var d = TreeDictionary<Int, Int>()
	for k in keys {
		versions.append(d)
		d[k] = k
	}
	return UInt64(d.count &+ versions.count)
}

func tGet(_ d: TreeDictionary<Int, Int>, _ probes: [Int]) -> UInt64 {
	var sum: UInt64 = 0
	for k in probes { if let v = d[k] { sum &+= UInt64(v) } }
	return sum
}

func tDissocReuse(_ keys: [Int], _ order: [Int]) -> UInt64 {
	var d = tBuild(keys)
	for k in order { d.removeValue(forKey: k) }
	return UInt64(d.count)
}

// MARK: - Dictionary

func dBuild(_ keys: [Int]) -> [Int: Int] {
	var d: [Int: Int] = [:]
	for k in keys { d[k] = k }
	return d
}

func dAssocPersist(_ keys: [Int]) -> UInt64 {
	var versions: [[Int: Int]] = []
	versions.reserveCapacity(keys.count)
	var d: [Int: Int] = [:]
	for k in keys {
		versions.append(d)
		d[k] = k
	}
	return UInt64(d.count &+ versions.count)
}

func dGet(_ d: [Int: Int], _ probes: [Int]) -> UInt64 {
	var sum: UInt64 = 0
	for k in probes { if let v = d[k] { sum &+= UInt64(v) } }
	return sum
}

func dDissocReuse(_ keys: [Int], _ order: [Int]) -> UInt64 {
	var d = dBuild(keys)
	for k in order { d.removeValue(forKey: k) }
	return UInt64(d.count)
}

// MARK: - C vector

func cVecBuild(_ n: Int) -> clj_value {
	var v = clj_vector_empty()
	for i in 0..<n { v = clj_vector_conj(v, clj_fixnum(i)) }
	return v
}

func cConjReuse(_ n: Int) -> UInt64 {
	let v = cVecBuild(n)
	let c = clj_vector_count(v)
	clj_release(v)
	return UInt64(c)
}

func cConjPersist(_ n: Int) -> UInt64 {
	var versions: [clj_value] = []
	versions.reserveCapacity(n)
	var v = clj_vector_empty()
	for i in 0..<n {
		versions.append(clj_retain(v))
		v = clj_vector_conj(v, clj_fixnum(i))
	}
	let c = clj_vector_count(v)
	clj_release(v)
	for old in versions { clj_release(old) }
	return UInt64(c)
}

func cNth(_ v: clj_value, _ probes: [Int]) -> UInt64 {
	var sum: UInt64 = 0
	for i in probes { sum &+= UInt64(bitPattern: Int64(clj_fixnum_val(clj_vector_nth(v, UInt32(i))))) }
	return sum
}

func cPopReuse(_ n: Int) -> UInt64 {
	var v = cVecBuild(n)
	for _ in 0..<n { v = clj_vector_pop(v) }
	let c = clj_vector_count(v)
	clj_release(v)
	return UInt64(c)
}

// MARK: - Array

func aBuild(_ n: Int) -> [Int] {
	var a: [Int] = []
	for i in 0..<n { a.append(i) }
	return a
}

// Every version copies the whole buffer: O(n²), a reference point for small n only.
func aAppendPersist(_ n: Int) -> UInt64 {
	var versions: [[Int]] = []
	versions.reserveCapacity(n)
	var a: [Int] = []
	for i in 0..<n {
		versions.append(a)
		a.append(i)
	}
	return UInt64(a.count &+ versions.count)
}

func aNth(_ a: [Int], _ probes: [Int]) -> UInt64 {
	var sum: UInt64 = 0
	for i in probes { sum &+= UInt64(a[i]) }
	return sum
}

func aPopReuse(_ n: Int) -> UInt64 {
	var a = aBuild(n)
	for _ in 0..<n { a.removeLast() }
	return UInt64(a.count)
}

// MARK: - Seqs through the interpreter

// Reads and evaluates every form; the last value is owned by the caller.
func cljEval(_ source: String) -> clj_value {
	var bytes = Array(source.utf8)
	var last: clj_value = CLJ_NIL
	bytes.withUnsafeMutableBufferPointer { buf in
		buf.withMemoryRebound(to: CChar.self) { chars in
			var reader = clj_reader()
			clj_reader_init(&reader, chars.baseAddress, chars.count)
			reader.resolve = clj_syntax_quote_resolve
			var form: clj_value = CLJ_NIL
			while clj_read(&reader, &form) == CLJ_READ_OK {
				clj_release(last)
				last = clj_eval(form, nil)
				clj_release(form)
				if last == CLJ_THROWN {
					let ex = clj_take_pending()
					let text = clj_pr_str(ex)
					fatalError("bench eval failed: \(String(cString: clj_string_bytes(text)))")
				}
			}
		}
	}
	return last
}

func cljCall(_ f: clj_value, _ arg: clj_value) -> UInt64 {
	var a = arg
	let r = withUnsafePointer(to: &a) { clj_invoke(f, $0, 1) }
	if r == CLJ_THROWN { fatalError("bench call threw") }
	let v = UInt64(bitPattern: Int64(clj_fixnum_val(r)))
	clj_release(r)
	return v
}

// (reduce + (map inc (range n))), (reduce + (map inc (filter even? (range n)))), (count (vec (map inc (range n)))):
// pipelines the optimizer fuses into their transducer form.
func cReduceMapRange(_ f: clj_value, _ n: Int) -> UInt64 { cljCall(f, clj_fixnum(n)) }

// (transduce (map inc) + (range n)), (reduce + (range n)), (reduce + v), (into [] (map inc) (range n)): the
// range and the vector through their reduce slots, no seq objects.
func cReduceFn(_ f: clj_value, _ arg: clj_value) -> UInt64 { cljCall(f, arg) }

func aReduceRange(_ a: [Int]) -> UInt64 {
	var sum = 0
	for x in a { sum &+= x }
	return UInt64(sum)
}

func aIntoMapRange(_ a: [Int]) -> UInt64 {
	var out: [Int] = []
	out.reserveCapacity(0)
	for x in a { out.append(x + 1) }
	return UInt64(out.count)
}

// Over a materialized array: a range loop folds to a closed form under -O.
func aReduceMapRange(_ a: [Int]) -> UInt64 {
	var sum = 0
	for x in a { sum &+= x + 1 }
	return UInt64(sum)
}

func aReduceMapFilterRange(_ a: [Int]) -> UInt64 {
	var sum = 0
	for x in a where x & 1 == 0 { sum &+= x + 1 }
	return UInt64(sum)
}

// first/next over (seq v) in an interpreted loop, and the same walk through the C iterator.
func cSeqWalkInterpreted(_ f: clj_value, _ v: clj_value) -> UInt64 { cljCall(f, v) }

func cSeqWalkIterator(_ v: clj_value) -> UInt64 {
	var it = clj_seq_iter_start(v)
	var item: clj_value = CLJ_NIL
	var sum = 0
	while clj_seq_iter_next(&it, &item) { sum &+= clj_fixnum_val(item) }
	return UInt64(sum)
}

func aSeqWalk(_ a: [Int]) -> UInt64 {
	var sum = 0
	for x in a { sum &+= x }
	return UInt64(sum)
}

// MARK: - Driver

struct Row {
	let scenario: String
	let n: Int
	let c: Double
	let tree: Double
	let dict: Double?
}

func fmt(_ ns: Double?) -> String {
	guard let ns else { return "—" }
	return String(format: "%.1f", ns)
}

func ratio(_ a: Double, _ b: Double) -> String { String(format: "%.1f×", b / a) }

var rows: [Row] = []
for n in sizes {
	let keys = shuffled(n, seed: 1)
	let order = shuffled(n, seed: 2)
	let hits = randomKeys(lookups, below: n, seed: 3)
	let misses = randomKeys(lookups, below: n, offset: n, seed: 4)

	rows.append(Row(scenario: "assoc, old version dropped", n: n,
		c: measure(ops: n) { cAssocReuse(keys) },
		tree: measure(ops: n) { UInt64(tBuild(keys).count) },
		dict: measure(ops: n) { UInt64(dBuild(keys).count) }))

	// Dictionary copies the whole table per version: O(n²), pointless past 1k.
	rows.append(Row(scenario: "assoc, all versions kept", n: n,
		c: measure(ops: n) { cAssocPersist(keys) },
		tree: measure(ops: n) { tAssocPersist(keys) },
		dict: n <= 1_000 ? measure(ops: n) { dAssocPersist(keys) } : nil))

	let cm = cBuild(keys)
	let tm = tBuild(keys)
	let dm = dBuild(keys)
	rows.append(Row(scenario: "get, hit", n: n,
		c: measure(ops: lookups) { cGet(cm, hits) },
		tree: measure(ops: lookups) { tGet(tm, hits) },
		dict: measure(ops: lookups) { dGet(dm, hits) }))
	rows.append(Row(scenario: "get, miss", n: n,
		c: measure(ops: lookups) { cGet(cm, misses) },
		tree: measure(ops: lookups) { tGet(tm, misses) },
		dict: measure(ops: lookups) { dGet(dm, misses) }))
	clj_release(cm)

	rows.append(Row(scenario: "dissoc to empty", n: n,
		c: measure(ops: n) { cDissocReuse(keys, order) },
		tree: measure(ops: n) { tDissocReuse(keys, order) },
		dict: measure(ops: n) { dDissocReuse(keys, order) }))
}

let mode = clj_debug_pool_enabled() ? "pool" : "system malloc"
print("C allocator: \(mode)\n")
print("| scenario | n | C HAMT | TreeDictionary | Dictionary | tree / C |")
print("|---|---:|---:|---:|---:|---:|")
for r in rows {
	print("| \(r.scenario) | \(r.n) | \(fmt(r.c)) | \(fmt(r.tree)) | \(fmt(r.dict)) | \(ratio(r.c, r.tree)) |")
}
print("\nns per op, median of \(reps) runs; get = \(lookups) random probes")

struct VectorRow {
	let scenario: String
	let n: Int
	let c: Double
	let array: Double?
}

var vectorRows: [VectorRow] = []
for n in sizes {
	let probes = randomKeys(lookups, below: n, seed: 5)
	vectorRows.append(VectorRow(scenario: "conj, old version dropped", n: n,
		c: measure(ops: n) { cConjReuse(n) },
		array: measure(ops: n) { UInt64(aBuild(n).count) }))
	vectorRows.append(VectorRow(scenario: "conj, all versions kept", n: n,
		c: measure(ops: n) { cConjPersist(n) },
		array: n <= 1_000 ? measure(ops: n) { aAppendPersist(n) } : nil))
	let cv = cVecBuild(n)
	let av = aBuild(n)
	vectorRows.append(VectorRow(scenario: "nth, random", n: n,
		c: measure(ops: lookups) { cNth(cv, probes) },
		array: measure(ops: lookups) { aNth(av, probes) }))
	clj_release(cv)
	vectorRows.append(VectorRow(scenario: "pop to empty", n: n,
		c: measure(ops: n) { cPopReuse(n) },
		array: measure(ops: n) { aPopReuse(n) }))
}

print("\n| scenario | n | C vector | Array | array / C |")
print("|---|---:|---:|---:|---:|")
for r in vectorRows {
	print("| \(r.scenario) | \(r.n) | \(fmt(r.c)) | \(fmt(r.array)) | \(r.array.map { ratio(r.c, $0) } ?? "—") |")
}
print("\nns per op; Array is mutable and in place, the persistent column copies the buffer per version")

clj_init()
let sumFn = cljEval("(fn [n] (reduce + (map inc (range n))))")
let sumFilterFn = cljEval("(fn [n] (reduce + (map inc (filter even? (range n)))))")
let vecFn = cljEval("(fn [n] (count (vec (map inc (range n)))))")
let transduceFn = cljEval("(fn [n] (transduce (map inc) + (range n)))")
let reduceRangeFn = cljEval("(fn [n] (reduce + (range n)))")
let reduceVecFn = cljEval("(fn [v] (reduce + v))")
let intoFn = cljEval("(fn [n] (count (into [] (map inc) (range n))))")
let walkFn = cljEval("(fn [v] (loop [s (seq v) acc 0] (if s (recur (next s) (+ acc (first s))) acc)))")

struct SeqRow {
	let scenario: String
	let n: Int
	let c: Double
	let iterator: Double?
	let swift: Double
}

var seqRows: [SeqRow] = []
// n = 10 shows the per-form cost of a fused pipeline: the transducer stack is built on every evaluation.
for n in [10, 1_000, 100_000] {
	let a = aBuild(n)
	seqRows.append(SeqRow(scenario: "reduce + map inc range", n: n,
		c: measure(ops: n) { cReduceMapRange(sumFn, n) },
		iterator: nil,
		swift: measure(ops: n) { aReduceMapRange(a) }))
	seqRows.append(SeqRow(scenario: "reduce + map inc (filter even?) range", n: n,
		c: measure(ops: n) { cReduceMapRange(sumFilterFn, n) },
		iterator: nil,
		swift: measure(ops: n) { aReduceMapFilterRange(a) }))
	seqRows.append(SeqRow(scenario: "vec (map inc range)", n: n,
		c: measure(ops: n) { cReduceMapRange(vecFn, n) },
		iterator: nil,
		swift: measure(ops: n) { aIntoMapRange(a) }))
}
for n in [1_000, 100_000] {
	let a = aBuild(n)
	seqRows.append(SeqRow(scenario: "transduce (map inc) + range", n: n,
		c: measure(ops: n) { cReduceFn(transduceFn, clj_fixnum(n)) },
		iterator: nil,
		swift: measure(ops: n) { aReduceMapRange(a) }))
	seqRows.append(SeqRow(scenario: "reduce + range", n: n,
		c: measure(ops: n) { cReduceFn(reduceRangeFn, clj_fixnum(n)) },
		iterator: nil,
		swift: measure(ops: n) { aReduceRange(a) }))
	let v = cVecBuild(n)
	seqRows.append(SeqRow(scenario: "reduce + vector", n: n,
		c: measure(ops: n) { cReduceFn(reduceVecFn, v) },
		iterator: measure(ops: n) { cSeqWalkIterator(v) },
		swift: measure(ops: n) { aReduceRange(a) }))
	clj_release(v)
	seqRows.append(SeqRow(scenario: "into [] (map inc) range", n: n,
		c: measure(ops: n) { cReduceFn(intoFn, clj_fixnum(n)) },
		iterator: nil,
		swift: measure(ops: n) { aIntoMapRange(a) }))
}
do {
	let n = 1_000
	let v = cVecBuild(n)
	let a = aBuild(n)
	seqRows.append(SeqRow(scenario: "seq walk of a vector", n: n,
		c: measure(ops: n) { cSeqWalkInterpreted(walkFn, v) },
		iterator: measure(ops: n) { cSeqWalkIterator(v) },
		swift: measure(ops: n) { aSeqWalk(a) }))
	clj_release(v)
}
clj_release(sumFn)
clj_release(sumFilterFn)
clj_release(vecFn)
clj_release(transduceFn)
clj_release(reduceRangeFn)
clj_release(reduceVecFn)
clj_release(intoFn)
clj_release(walkFn)

// (loop [i 0] (if (< i n) (recur (inc i)) i)): one rebind, a comparison and an increment per iteration.
func cCountLoop(_ f: clj_value, _ n: Int) -> UInt64 { cljCall(f, clj_fixnum(n)) }

// Same loop with the increment behind a closure call through a var: one interpreted call per iteration.
func cClosureCallLoop(_ f: clj_value, _ n: Int) -> UInt64 { cljCall(f, clj_fixnum(n)) }

@inline(never) func incBox(_ x: Int) -> Int { x + 1 }

func aClosureCallLoop(_ n: Int) -> UInt64 {
	var i = 0
	while i < n { i = incBox(i) }
	return UInt64(i)
}

let countFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (inc i)) i)))")
let callFn = cljEval("(def bench-inc (fn [x] (inc x))) (fn [n] (loop [i 0] (if (< i n) (recur (bench-inc i)) i)))")

// The same loop with a protocol method call per iteration: the receiver is a deftype instance held in a
// local (mono), a fixnum (mono, a builtin type's table), or alternating between the two (bi-morphic).
_ = cljEval("(defprotocol BenchP (bench-m [x])) (deftype BenchT [] BenchP (bench-m [x] 1)) (extend-type Long BenchP (bench-m [x] 1))")
let protoTypeFn = cljEval("(fn [n] (let [t (->BenchT)] (loop [i 0] (if (< i n) (recur (+ i (bench-m t))) i))))")
let protoBuiltinFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (+ i (bench-m i))) i)))")
let protoBiFn = cljEval("(fn [n] (let [t (->BenchT)] (loop [i 0] (if (< i n) (recur (+ i (bench-m (if (even? i) t i)))) i))))")

struct CallRow {
	let scenario: String
	let n: Int
	let c: Double
	let swift: Double?
}

var callRows: [CallRow] = []
do {
	let n = 100_000
	// A Swift counting loop folds to a closed form under -O, so it has no reference column.
	callRows.append(CallRow(scenario: "counting loop", n: n, c: measure(ops: n) { cCountLoop(countFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "closure call in a loop", n: n,
		c: measure(ops: n) { cClosureCallLoop(callFn, n) },
		swift: measure(ops: n) { aClosureCallLoop(n) }))
	callRows.append(CallRow(scenario: "protocol call, deftype receiver", n: n, c: measure(ops: n) { cClosureCallLoop(protoTypeFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "protocol call, fixnum receiver", n: n, c: measure(ops: n) { cClosureCallLoop(protoBuiltinFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "protocol call, bi-morphic", n: n, c: measure(ops: n) { cClosureCallLoop(protoBiFn, n) }, swift: nil))
}
clj_release(countFn)
clj_release(callFn)
clj_release(protoTypeFn)
clj_release(protoBuiltinFn)
clj_release(protoBiFn)

print("\n| scenario | n | interpreted | C iterator | Swift for | interpreted / Swift |")
print("|---|---:|---:|---:|---:|---:|")
for r in seqRows {
	print("| \(r.scenario) | \(r.n) | \(fmt(r.c)) | \(fmt(r.iterator)) | \(fmt(r.swift)) | \(ratio(r.swift, r.c)) |")
}
print("\nns per element; interpreted = a core.clj fn called through clj_invoke, C iterator = clj_seq_iter over the same vector")

print("\n| scenario | n | interpreted | Swift while | interpreted / Swift |")
print("|---|---:|---:|---:|---:|")
for r in callRows {
	print("| \(r.scenario) | \(r.n) | \(fmt(r.c)) | \(fmt(r.swift)) | \(r.swift.map { ratio($0, r.c) } ?? "—") |")
}
print("\nns per iteration; counting loop = (loop [i 0] (if (< i n) (recur (inc i)) i)), closure call = the same with (f i) for (def f (fn [x] (inc x))), protocol call = the same with (+ i (m x)) for a one-method protocol extended to a deftype and to Long")
