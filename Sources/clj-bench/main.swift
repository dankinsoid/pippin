// @ai-generated(solo)
import CljCore
import Clojure
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

// CLJ_BENCH_ONLY=rc-share on a debug binary: the share of retain/release pairs on the shared (atomic) path
// with the application state in one atom (design §4, "Проверка, закрывающая вопрос"); release builds count nothing.
if ProcessInfo.processInfo.environment["CLJ_BENCH_ONLY"] == "rc-share" {
	clj_init()
	func rcOps() -> [Int64] {
		var out = [Int64](repeating: 0, count: 3)
		clj_debug_rc_ops(&out)
		return out
	}
	if rcOps()[0] < 0 {
		print("rc-share needs a debug build (swift build --product clj-bench)")
		exit(1)
	}
	// One tick: two nested writes through swap! and two reads through deref + get-in/get, the shape of a UI
	// state update; the same over a local map (never published) is the non-shared baseline.
	let tickAtom = cljEval("""
	(fn [n]
	  (let [state (atom {:users {} :counter 0})]
	    (loop [i 0 s 0]
	      (if (< i n)
	        (do (swap! state assoc-in [:users i] {:id i :name "x"})
	            (swap! state update :counter inc)
	            (recur (inc i) (+ s (count (get-in @state [:users (- i 1) :name] "")) (get @state :counter))))
	        s))))
	""")
	let tickWatched = cljEval("""
	(fn [n]
	  (let [state (atom {:users {} :counter 0})]
	    (add-watch state :k (fn [k r o n] nil))
	    (loop [i 0 s 0]
	      (if (< i n)
	        (do (swap! state assoc-in [:users i] {:id i :name "x"})
	            (swap! state update :counter inc)
	            (recur (inc i) (+ s (count (get-in @state [:users (- i 1) :name] "")) (get @state :counter))))
	        s))))
	""")
	let tickLocal = cljEval("""
	(fn [n]
	  (loop [i 0 s 0 state {:users {} :counter 0}]
	    (if (< i n)
	      (let [state (assoc-in state [:users i] {:id i :name "x"})
	            state (update state :counter inc)]
	        (recur (inc i) (+ s (count (get-in state [:users (- i 1) :name] "")) (get state :counter)) state))
	      s)))
	""")
	print("| workload | n | plain | shared | immortal | shared share |")
	print("|---|---:|---:|---:|---:|---:|")
	for (name, fn) in [("state in an atom", tickAtom), ("state in a watched atom", tickWatched), ("state in a loop local", tickLocal)] {
		for n in [1_000, 10_000] {
			_ = cljCall(fn, clj_fixnum(n))
			let before = rcOps()
			_ = cljCall(fn, clj_fixnum(n))
			let after = rcOps()
			let plain = after[0] - before[0], shared = after[1] - before[1], immortal = after[2] - before[2]
			let share = Double(shared) / Double(plain + shared) * 100
			print("| \(name) | \(n) | \(plain) | \(shared) | \(immortal) | \(String(format: "%.1f", share)) % |")
		}
	}
	print("\nretain+release ops per run of n ticks; shared share = shared / (plain + shared), immortal ops (keywords, core roots) aside")
	for fn in [tickAtom, tickWatched, tickLocal] { clj_release(fn) }
	exit(0)
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

// (loop [v [] i 0] (if (< i n) (recur (conj v i) (inc i)) v)), the same with (assoc m i i) into a map, and
// (reduce conj [] (range n)): a collection grown one element per step, in place when the step owns it.
func cGrowLoop(_ f: clj_value, _ n: Int) -> UInt64 { cljCall(f, clj_fixnum(n)) }

func aAppendLoop(_ n: Int) -> UInt64 {
	var out: [Int] = []
	for i in 0..<n { out.append(i) }
	return UInt64(out.count)
}

func dInsertLoop(_ n: Int) -> UInt64 {
	var out: [Int: Int] = [:]
	for i in 0..<n { out[i] = i }
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
let loopConjFn = cljEval("(fn [n] (count (loop [v [] i 0] (if (< i n) (recur (conj v i) (inc i)) v))))")
let loopAssocFn = cljEval("(fn [n] (count (loop [m {} i 0] (if (< i n) (recur (assoc m i i) (inc i)) m))))")
let reduceConjFn = cljEval("(fn [n] (count (reduce conj [] (range n))))")
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
	seqRows.append(SeqRow(scenario: "loop conj into a vector", n: n,
		c: measure(ops: n) { cGrowLoop(loopConjFn, n) },
		iterator: nil,
		swift: measure(ops: n) { aAppendLoop(n) }))
	seqRows.append(SeqRow(scenario: "loop assoc into a map", n: n,
		c: measure(ops: n) { cGrowLoop(loopAssocFn, n) },
		iterator: nil,
		swift: measure(ops: n) { dInsertLoop(n) }))
	seqRows.append(SeqRow(scenario: "reduce conj [] range", n: n,
		c: measure(ops: n) { cGrowLoop(reduceConjFn, n) },
		iterator: nil,
		swift: measure(ops: n) { aAppendLoop(n) }))
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
clj_release(loopConjFn)
clj_release(loopAssocFn)
clj_release(reduceConjFn)
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

// The same call with the fn bound by a let around the loop, and a helper bound inside the loop body that
// reads a loop variable: a closure per iteration unless the optimizer calls it directly.
// The same call through a var bound to a C builtin (`inc`, a plain native called from the site) and to a fn
// made by Runtime.define (a context native through clj_invoke and the Value bridge): the Swift↔C transition.
let rt = Runtime()
rt.define("bench-host-inc", arity: 1...1) { args in Value(args[0].int! + 1) }
let nativeCallFn = cljEval("(def bench-c-inc inc) (fn [n] (loop [i 0] (if (< i n) (recur (bench-c-inc i)) i)))")
let hostCallFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (bench-host-inc i)) i)))")

let letFn = cljEval("(fn [n] (let [f (fn [x] (inc x))] (loop [i 0] (if (< i n) (recur (f i)) i))))")
let helperFn = cljEval("(fn [n] (loop [i 0 acc 0] (if (< i n) (let [add (fn [x] (+ acc x))] (recur (inc i) (add i))) acc)))")

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
	callRows.append(CallRow(scenario: "C builtin call in a loop", n: n, c: measure(ops: n) { cClosureCallLoop(nativeCallFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "host fn call in a loop", n: n, c: measure(ops: n) { cClosureCallLoop(hostCallFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "let-bound fn called in a loop", n: n, c: measure(ops: n) { cClosureCallLoop(letFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "loop with a local helper", n: n, c: measure(ops: n) { cClosureCallLoop(helperFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "protocol call, deftype receiver", n: n, c: measure(ops: n) { cClosureCallLoop(protoTypeFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "protocol call, fixnum receiver", n: n, c: measure(ops: n) { cClosureCallLoop(protoBuiltinFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "protocol call, bi-morphic", n: n, c: measure(ops: n) { cClosureCallLoop(protoBiFn, n) }, swift: nil))
}
clj_release(countFn)
clj_release(callFn)
clj_release(nativeCallFn)
clj_release(hostCallFn)
clj_release(letFn)
clj_release(helperFn)
clj_release(protoTypeFn)
clj_release(protoBuiltinFn)
clj_release(protoBiFn)

// (sort v) over shuffled fixnums: the Swift primitive, the Clojure merge sort that is its specification, and
// Swift's own sort of the same numbers.
_ = cljEval("(def bench-sort-spec \(Runtime.sortSpecification))")
let sortFn = cljEval("(fn [v] (first (sort v)))")
let sortSpecFn = cljEval("(fn [v] (first (bench-sort-spec v)))")

struct SortRow {
	let n: Int
	let primitive: Double
	let spec: Double
	let swift: Double
}

var sortRows: [SortRow] = []
for n in [1_000] {
	let a = shuffled(n, seed: 6)
	let v = a.map(clj_fixnum).withUnsafeBufferPointer { clj_vector_from_array($0.baseAddress, UInt32($0.count)) }
	sortRows.append(SortRow(n: n,
		primitive: measure(ops: n) { cljCall(sortFn, v) },
		spec: measure(ops: n) { cljCall(sortSpecFn, v) },
		swift: measure(ops: n) { UInt64(a.sorted()[0]) }))
	clj_release(v)
}
clj_release(sortFn)
clj_release(sortSpecFn)

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
print("\nns per iteration; counting loop = (loop [i 0] (if (< i n) (recur (inc i)) i)), closure call = the same with (f i) for (def f (fn [x] (inc x))), C builtin call = (def f inc), host fn call = f made by Runtime.define, let-bound = f bound by a let around the loop, local helper = (let [add (fn [x] (+ acc x))] ...) inside the loop body, protocol call = the same with (+ i (m x)) for a one-method protocol extended to a deftype and to Long")

print("\n| scenario | n | Swift primitive | Clojure spec | Swift sorted | spec / primitive |")
print("|---|---:|---:|---:|---:|---:|")
for r in sortRows {
	print("| sort, shuffled fixnums | \(r.n) | \(fmt(r.primitive)) | \(fmt(r.spec)) | \(fmt(r.swift)) | \(ratio(r.primitive, r.spec)) |")
}
print("\nns per element; Swift primitive = (sort v) bound by Runtime.define, Clojure spec = Runtime.sortSpecification evaluated in user, Swift sorted = [Int].sorted()")

// MARK: - Sorted collections and multimethods

// The red-black tree of sorted.c against the CHAMT of map.c, same keys and probes. The comparator is the
// default one, so every comparison is clj_compare in C — what (sorted-map) builds with.
func sBuild(_ keys: [Int]) -> clj_value {
	var m = clj_sorted_map_new(CLJ_NIL)
	for k in keys { m = clj_sorted_assoc(m, clj_fixnum(k), clj_fixnum(k)) }
	return m
}

func sAssocReuse(_ keys: [Int]) -> UInt64 {
	let m = sBuild(keys)
	let c = clj_sorted_count(m)
	clj_release(m)
	return UInt64(c)
}

func sGet(_ m: clj_value, _ probes: [Int]) -> UInt64 {
	var sum: UInt64 = 0
	for k in probes {
		let v = clj_sorted_get(m, clj_fixnum(k), CLJ_NIL)
		if clj_is_fixnum(v) { sum &+= UInt64(bitPattern: Int64(clj_fixnum_val(v))) }
	}
	return sum
}

struct SortedRow {
	let scenario: String
	let n: Int
	let sorted: Double
	let hash: Double
}

var sortedRows: [SortedRow] = []
for n in sizes {
	let keys = shuffled(n, seed: 11)
	let hits = randomKeys(lookups, below: n, seed: 12)
	sortedRows.append(SortedRow(scenario: "assoc, old version dropped", n: n,
		sorted: measure(ops: n) { sAssocReuse(keys) },
		hash: measure(ops: n) { cAssocReuse(keys) }))
	let sm = sBuild(keys), hm = cBuild(keys)
	sortedRows.append(SortedRow(scenario: "get, hit", n: n,
		sorted: measure(ops: hits.count) { sGet(sm, hits) },
		hash: measure(ops: hits.count) { cGet(hm, hits) }))
	clj_release(sm)
	clj_release(hm)
}

// One multimethod call per iteration: the dispatch value is the method's own key (the = hit the cache
// answers), a child of it through the global hierarchy (the isa? hit, cached the same way), and a value
// only :default matches. The last two rows are the same loop through a one-method protocol and a plain fn.
_ = cljEval("""
(defmulti bench-mm identity)
(defmethod bench-mm :bench/leaf [_] 1)
(defmethod bench-mm :default [_] 1)
(derive :bench/child :bench/leaf)
(def bench-plain-fn (fn [_] 1))
(defprotocol BenchMP (bench-mp [x]))
(extend-type Keyword BenchMP (bench-mp [x] 1))
(deftype BenchOldMF [dispatch-fn default table]
  IFn
  (invoke [_ & args]
    (let [dv (apply dispatch-fn args) m @table f (get m dv (get m default))] (apply f args))))
(def bench-old-mm (->BenchOldMF identity :default (atom {:bench/leaf (fn [_] 1)})))
""")
let mmEqFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (+ i (bench-mm :bench/leaf))) i)))")
let mmIsaFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (+ i (bench-mm :bench/child))) i)))")
let mmDefaultFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (+ i (bench-mm :bench/other))) i)))")
let mmProtoFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (+ i (bench-mp :bench/leaf))) i)))")
let mmPlainFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (+ i (bench-plain-fn :bench/leaf))) i)))")
let mmOldFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (+ i (bench-old-mm :bench/leaf))) i)))")

struct DispatchRow {
	let scenario: String
	let n: Int
	let c: Double
}

var dispatchRows: [DispatchRow] = []
do {
	let n = 100_000
	dispatchRows.append(DispatchRow(scenario: "multimethod, = hit", n: n, c: measure(ops: n) { cljCall(mmEqFn, clj_fixnum(n)) }))
	dispatchRows.append(DispatchRow(scenario: "multimethod, = hit, pre-hierarchy shape", n: n, c: measure(ops: n) { cljCall(mmOldFn, clj_fixnum(n)) }))
	dispatchRows.append(DispatchRow(scenario: "multimethod, isa? hit", n: n, c: measure(ops: n) { cljCall(mmIsaFn, clj_fixnum(n)) }))
	dispatchRows.append(DispatchRow(scenario: "multimethod, :default hit", n: n, c: measure(ops: n) { cljCall(mmDefaultFn, clj_fixnum(n)) }))
	dispatchRows.append(DispatchRow(scenario: "protocol call, keyword receiver", n: n, c: measure(ops: n) { cljCall(mmProtoFn, clj_fixnum(n)) }))
	dispatchRows.append(DispatchRow(scenario: "plain fn call through a var", n: n, c: measure(ops: n) { cljCall(mmPlainFn, clj_fixnum(n)) }))
}
clj_release(mmEqFn)
clj_release(mmIsaFn)
clj_release(mmDefaultFn)
clj_release(mmProtoFn)
clj_release(mmPlainFn)
clj_release(mmOldFn)

print("\n| scenario | n | sorted map | hash map | sorted / hash |")
print("|---|---:|---:|---:|---:|")
for r in sortedRows {
	print("| \(r.scenario) | \(r.n) | \(fmt(r.sorted)) | \(fmt(r.hash)) | \(ratio(r.hash, r.sorted)) |")
}
print("\nns per op; sorted map = clj_sorted_assoc / clj_sorted_get with the default C comparator, hash map = clj_map_assoc / clj_map_get over the same keys")

print("\n| scenario | n | ns/op |")
print("|---|---:|---:|")
for r in dispatchRows {
	print("| \(r.scenario) | \(r.n) | \(fmt(r.c)) |")
}
print("\nns per iteration; one call per iteration inside an interpreted loop, the dispatch fn is `identity`")

// MARK: - Arrays

// The same summing loop over 1000 elements: aget on a long-array, nth on a vector, and the reduce slot of
// each, so the row separates the element read from the loop around it.
let arrayRows: [(String, Double)] = {
	let n = 1000
	let agetLoop = cljEval("(fn [a] (loop [i 0 s 0] (if (< i (alength a)) (recur (inc i) (+ s (aget a i))) s)))")
	let nthLoop = cljEval("(fn [v] (loop [i 0 s 0] (if (< i (count v)) (recur (inc i) (+ s (nth v i))) s)))")
	let reduceCall = cljEval("(fn [c] (reduce + c))")
	let arr = cljEval("(long-array (range 1000))")
	let vec = cljEval("(vec (range 1000))")
	let rows: [(String, Double)] = [
		("loop + aget over a long-array", measure(ops: n) { cljCall(agetLoop, arr) }),
		("loop + nth over a vector", measure(ops: n) { cljCall(nthLoop, vec) }),
		("reduce + over a long-array", measure(ops: n) { cljCall(reduceCall, arr) }),
		("reduce + over a vector", measure(ops: n) { cljCall(reduceCall, vec) }),
	]
	for v in [agetLoop, nthLoop, reduceCall, arr, vec] { clj_release(v) }
	return rows
}()

print("\n| scenario | n | ns/element |")
print("|---|---:|---:|")
for r in arrayRows {
	print("| \(r.0) | 1000 | \(fmt(r.1)) |")
}
print("\nns per element; the loops are interpreted, the arrays and vectors hold the same 1000 fixnums")

// MARK: - Atoms

// (swap! a assoc k v) over an atom holding a map of K keys, k cycling through them and v new every call (an
// assoc of the value already there copies nothing); the atom is the map's only holder, so the row measures
// swap!'s own handling of the value (NOTES.md, "Atoms");
// (swap! a assoc i i) growing a map on an atom with a no-op watch; (swap! a inc); (get @a :k) per iteration
// against the same through a volatile; and four threads doing (swap! a inc) / (swap! a assoc k i) on one atom.
let atomAssocSizedFn = cljEval("(fn [a n K salt] (loop [i 0 k 0] (if (< i n) (do (swap! a assoc k (+ i salt)) (recur (inc i) (if (< (inc k) K) (inc k) 0))) (count @a))))")
let atomAssocWatchedFn = cljEval("(fn [n] (let [a (atom {})] (add-watch a :k (fn [k r o n] nil)) (loop [i 0] (if (< i n) (do (swap! a assoc i i) (recur (inc i))) (count @a)))))")
let atomIncFn = cljEval("(fn [n] (let [a (atom 0)] (loop [i 0] (if (< i n) (do (swap! a inc) (recur (inc i))) @a))))")
let atomDerefFn = cljEval("(fn [n] (let [a (atom {:k 1})] (loop [i 0 s 0] (if (< i n) (recur (inc i) (+ s (get @a :k))) s))))")
let volatileDerefFn = cljEval("(fn [n] (let [v (volatile! {:k 1})] (loop [i 0 s 0] (if (< i n) (recur (inc i) (+ s (get @v :k))) s))))")
let loopAssocRefFn = cljEval("(fn [n] (count (loop [m {} i 0] (if (< i n) (recur (assoc m i i) (inc i)) m))))")
let countRefFn = cljEval("(fn [n] (loop [i 0] (if (< i n) (recur (inc i)) i)))")
let contended = cljEval("(let [a (atom 0) m (atom {})] [(fn [n] (dotimes [i n] (swap! a inc)) @a) (fn [t] (dotimes [i 25000] (swap! m assoc (+ (* t 1000000) i) i)) (count @m)) a m])")
clj_share(contended)
let contendedIncFn = clj_vector_nth(contended, 0), contendedAssocFn = clj_vector_nth(contended, 1)
let contendedAssocAtom = clj_vector_nth(contended, 3)

// Swift under an os_unfair_lock: a Dictionary insert and an Int increment, alone and on four threads.
final class LockedState: @unchecked Sendable {
	var lock = os_unfair_lock()
	var dict: [Int: Int] = [:]
	var count = 0
}

func cContended(_ f: clj_value, threads: Int, each: clj_value) -> UInt64 {
	let s = LockedState()
	DispatchQueue.concurrentPerform(iterations: threads) { t in
		let r = cljCall(f, each == CLJ_NIL ? clj_fixnum(t) : each)
		os_unfair_lock_lock(&s.lock)
		s.count &+= Int(truncatingIfNeeded: r)
		os_unfair_lock_unlock(&s.lock)
	}
	return UInt64(bitPattern: Int64(s.count))
}

func cAtomAssocSized(_ a: clj_value, n: Int, keys: Int, salt: Int) -> UInt64 {
	let args = [a, clj_fixnum(n), clj_fixnum(keys), clj_fixnum(salt)]
	let r = args.withUnsafeBufferPointer { clj_invoke(atomAssocSizedFn, $0.baseAddress, 4) }
	if r == CLJ_THROWN { fatalError("bench call threw") }
	let v = UInt64(bitPattern: Int64(clj_fixnum_val(r)))
	clj_release(r)
	return v
}

func aLockedInsert(_ s: LockedState, n: Int, keys: Int, salt: Int) -> UInt64 {
	var k = 0
	for i in 0..<n {
		os_unfair_lock_lock(&s.lock)
		s.dict[k] = i + salt
		os_unfair_lock_unlock(&s.lock)
		k = k + 1 < keys ? k + 1 : 0
	}
	return UInt64(s.dict.count)
}

func aLockedInc(_ n: Int, threads: Int) -> UInt64 {
	let s = LockedState()
	DispatchQueue.concurrentPerform(iterations: threads) { _ in
		for _ in 0..<(n / threads) {
			os_unfair_lock_lock(&s.lock)
			s.count += 1
			os_unfair_lock_unlock(&s.lock)
		}
	}
	return UInt64(s.count)
}

func aLockedRead(_ n: Int) -> UInt64 {
	let s = LockedState()
	s.dict[1] = 1
	var sum = 0
	for _ in 0..<n {
		os_unfair_lock_lock(&s.lock)
		sum &+= s.dict[1]!
		os_unfair_lock_unlock(&s.lock)
	}
	return UInt64(sum)
}

struct AtomRow {
	let scenario: String
	let n: Int
	let c: Double
	let swift: Double?
}

var atomRows: [AtomRow] = []
do {
	let n = 100_000
	for keys in [16, 1_000, 100_000] {
		let m = cBuild(Array(0..<keys)), a = clj_atom_new(m, CLJ_NIL, CLJ_NIL), s = LockedState()
		clj_release(m)
		for i in 0..<keys { s.dict[i] = i }
		var salt = 0
		atomRows.append(AtomRow(scenario: "swap! assoc, map of \(keys) keys", n: n, c: measure(ops: n) { salt += 1; return cAtomAssocSized(a, n: n, keys: keys, salt: salt) }, swift: measure(ops: n) { salt += 1; return aLockedInsert(s, n: n, keys: keys, salt: salt) }))
		clj_release(a)
	}
	atomRows.append(AtomRow(scenario: "swap! assoc, watched", n: n, c: measure(ops: n) { cljCall(atomAssocWatchedFn, clj_fixnum(n)) }, swift: nil))
	atomRows.append(AtomRow(scenario: "loop assoc into a map (no atom)", n: n, c: measure(ops: n) { cGrowLoop(loopAssocRefFn, n) }, swift: nil))
	atomRows.append(AtomRow(scenario: "swap! inc", n: n, c: measure(ops: n) { cljCall(atomIncFn, clj_fixnum(n)) }, swift: measure(ops: n) { aLockedInc(n, threads: 1) }))
	atomRows.append(AtomRow(scenario: "get @atom :k", n: n, c: measure(ops: n) { cljCall(atomDerefFn, clj_fixnum(n)) }, swift: measure(ops: n) { aLockedRead(n) }))
	atomRows.append(AtomRow(scenario: "get @volatile :k", n: n, c: measure(ops: n) { cljCall(volatileDerefFn, clj_fixnum(n)) }, swift: nil))
	atomRows.append(AtomRow(scenario: "counting loop", n: n, c: measure(ops: n) { cCountLoop(countRefFn, n) }, swift: nil))
	atomRows.append(AtomRow(scenario: "swap! inc, 4 threads", n: n, c: measure(ops: n) { cContended(contendedIncFn, threads: 4, each: clj_fixnum(n / 4)) }, swift: measure(ops: n) { aLockedInc(n, threads: 4) }))
	atomRows.append(AtomRow(scenario: "swap! assoc, 4 threads", n: n, c: measure(ops: n) { cContended(contendedAssocFn, threads: 4, each: CLJ_NIL) }, swift: nil))
}
clj_release(atomAssocSizedFn)
clj_release(atomAssocWatchedFn)
clj_release(atomIncFn)
clj_release(atomDerefFn)
clj_release(volatileDerefFn)
clj_release(loopAssocRefFn)
clj_release(countRefFn)
clj_release(contended)
_ = contendedAssocAtom

print("\n| scenario | n | interpreted | Swift locked | interpreted / Swift |")
print("|---|---:|---:|---:|---:|")
for r in atomRows {
	print("| \(r.scenario) | \(r.n) | \(fmt(r.c)) | \(fmt(r.swift)) | \(r.swift.map { ratio($0, r.c) } ?? "—") |")
}
print("\nns per iteration; swap! assoc, map of K keys = (swap! a assoc k v) with k cycling through the keys of a prebuilt map the atom alone holds and v new every call, watched = (swap! a assoc i i) growing a map on an atom with a no-op watch, swap! inc / get @atom :k = the same loops, 4 threads = four DispatchQueue.concurrentPerform workers sharing one atom (n ops in total); Swift locked = an os_unfair_lock around a Dictionary insert / an Int increment / a Dictionary read")
