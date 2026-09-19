// @ai-generated(solo)
import CljCompiler
import CljCore
import Pippin
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

func benchThrew() -> Never {
	let trace = clj_pr_str(clj_take_pending_trace())
	let text = clj_pr_str(clj_take_pending())
	fatalError("bench call threw: \(text == CLJ_THROWN ? "?" : String(cString: clj_string_bytes(text))) at \(trace == CLJ_THROWN ? "?" : String(cString: clj_string_bytes(trace)))")
}

func cljCall0(_ f: clj_value) -> UInt64 {
	let r = clj_invoke(f, nil, 0)
	if r == CLJ_THROWN { benchThrew() }
	let v = UInt64(bitPattern: Int64(clj_fixnum_val(r)))
	clj_release(r)
	return v
}

func cljCall(_ f: clj_value, _ arg: clj_value) -> UInt64 {
	var a = arg
	let r = withUnsafePointer(to: &a) { clj_invoke(f, $0, 1) }
	if r == CLJ_THROWN { benchThrew() }
	let v = UInt64(bitPattern: Int64(clj_fixnum_val(r)))
	clj_release(r)
	return v
}

func cljCall2(_ f: clj_value, _ a: clj_value, _ b: clj_value) -> UInt64 {
	let args = [a, b]
	let r = args.withUnsafeBufferPointer { clj_invoke(f, $0.baseAddress, 2) }
	if r == CLJ_THROWN { benchThrew() }
	let v = UInt64(bitPattern: Int64(clj_fixnum_val(r)))
	clj_release(r)
	return v
}

// A vector of n doubles, i + 0.5 each.
func cDoubleVecBuild(_ n: Int) -> clj_value {
	var v = clj_vector_empty()
	for i in 0..<n {
		let d = clj_double_new(Double(i) + 0.5)
		v = clj_vector_conj(v, d)
		clj_release(d)
	}
	return v
}

// CLJ_BENCH_ONLY=boot: clj_init wall time, peak resident memory and live objects after it; the compiled core
// against the interpreted one (bench/RESULTS.md, "Compiler v0").
if ProcessInfo.processInfo.environment["CLJ_BENCH_ONLY"] == "boot" {
	var before = rusage()
	getrusage(RUSAGE_SELF, &before)
	let t0 = DispatchTime.now().uptimeNanoseconds
	clj_init()
	let t1 = DispatchTime.now().uptimeNanoseconds
	var after = rusage()
	getrusage(RUSAGE_SELF, &after)
	print("boot: clj_init \(String(format: "%.2f", Double(t1 - t0) / 1e6)) ms, peak rss \(after.ru_maxrss / 1024 / 1024) MB (\(before.ru_maxrss / 1024 / 1024) MB before), live objects \(clj_debug_live_objects())")
	exit(0)
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

// CLJ_BENCH_NO_SPECIALIZE=1: the interpreter without the specialized arithmetic nodes, the control for those rows.
if ProcessInfo.processInfo.environment["CLJ_BENCH_NO_SPECIALIZE"] != nil { clj_specialize_enable(false) }
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
// The counting loop with a second variable it adds into: two slot rebinds per iteration.
let accFn = cljEval("(fn [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc i)) acc)))")
// The same two loops as def'd fns whose only caller is another def'd fn, in one form: the caller join gives the
// bound a type, where the host's argument above gives it none (NOTES.md "Facts", the reverse index).
let joinedCountFn = cljEval(
	"(let [] (defn bench-count-to [n] (loop [i 0] (if (< i n) (recur (inc i)) i))) (defn bench-count-run [] (bench-count-to 100000))) bench-count-run")
let joinedAccFn = cljEval(
	"(let [] (defn bench-acc-to [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc i)) acc))) (defn bench-acc-run [] (bench-acc-to 100000))) bench-acc-run")
// The accumulating loop with the square written out, the reference for the helper rows below (compileUnitRow).
let sqInlineFn = cljEval(
	"(let [] (defn bench-sqi-to [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (* i i))) acc))) (defn bench-sqi-run [] (bench-sqi-to 100000))) bench-sqi-run")
// A double accumulator per iteration, and a dot product over two vectors of doubles through nth (whose element has
// no fact: the products stay generic and the sum with them).
let dblAccFn = cljEval("(fn [n] (loop [i 0 x 0.0] (if (< i n) (recur (inc i) (+ x 0.5)) x)))")
let dotFn = cljEval("(fn [a b] (let [n (count a)] (loop [i 0 s 0.0] (if (< i n) (recur (inc i) (+ s (* (nth a i) (nth b i)))) s))))")
// The bound from (count v): a let around the loop, and the loop variable itself fed by the count (NOTES.md
// "Compiler", entry-checked frames).
let countBoundFn = cljEval("(fn [v] (let [n (count v)] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc i)) acc))))")
let countDownFn = cljEval("(fn [v] (loop [i (count v) acc 0] (if (pos? i) (recur (dec i) (+ acc i)) acc)))")
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
// The same call on a protocol with one implementor: the receiver known from a def'd caller in the same form (the
// join names the deftype, so a closed unit takes the direct arm) and a captured one (top inside the body: the cache).
_ = cljEval("(defprotocol BenchKP (bench-km [x])) (deftype BenchKT [] BenchKP (bench-km [x] 1))")
let protoKnownFn = cljEval(
	"(let [] (defn bench-proto-to [t n] (loop [i 0] (if (< i n) (recur (+ i (bench-km t))) i))) (defn bench-proto-run [] (bench-proto-to (->BenchKT) 100000))) bench-proto-run")
let protoTopFn = cljEval("(let [t (->BenchKT)] (fn [n] (loop [i 0] (if (< i n) (recur (+ i (bench-km t))) i))))")

// The deftype and its caller compiled as one unit from a file, the way clj-compile emits a namespace: the arm is a
// static call clang inlines, which a compiled-eval form cannot be (it is compiled before its deftype runs). Only
// under CLJ_EVAL_ROOT, since building the unit needs clang and the package root; the JIT hook is re-armed after.
func compileUnitRow(name: String, closed: Bool, body: String = """
	(defprotocol BenchUP (bench-um [x]))
	(deftype BenchUT [] BenchUP (bench-um [x] 1))
	(defn bench-unit-to [t n] (loop [i 0] (if (< i n) (recur (+ i (bench-um t))) i)))
	(defn bench-unit-run [] (bench-unit-to (->BenchUT) 100000))
	""") -> clj_value? {
	guard let root = ProcessInfo.processInfo.environment["CLJ_EVAL_ROOT"] else { return nil }
	let dir = "\(root)/.build/compiled-eval"
	let ns = "bench.\(name)"
	let source = "(ns \(ns))\n" + body
	let path = "\(dir)/\(name).clj"
	try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
	try? source.write(toFile: path, atomically: true, encoding: .utf8)
	var opts = cljc_options()
	opts.closed = closed
	opts.skip_embedded = true
	let c = cljc_new(&opts)!
	cljc_begin(c)
	var bytes = Array(source.utf8)
	let file = clj_string_from_cstr(path)
	let loaded = bytes.withUnsafeMutableBufferPointer { buf in
		buf.withMemoryRebound(to: CChar.self) { chars in clj_load_source(chars.baseAddress, chars.count, file) }
	}
	if loaded == CLJ_THROWN { benchThrew() }
	clj_release(loaded)
	cljc_end(c)
	let text = cljc_unit_text(c, 0, nil)!
	cljc_free(c)
	var o = cljc_eval_options()
	o.root = UnsafePointer(strdup(root))
	o.dir = UnsafePointer(strdup(dir))
	if let opt = ProcessInfo.processInfo.environment["CLJ_EVAL_OPT"] { o.opt = UnsafePointer(strdup(opt)) }
	o.closed = closed
	guard let unit = cljc_load_dylib(&o, "bench_\(name)", text) else { benchThrew() }
	free(text)
	clj_compiled_register(unit.pointee.path, unit.pointee.`init`)
	let ran = clj_load_file(file)
	if ran == CLJ_THROWN { benchThrew() }
	clj_release(ran)
	clj_release(file)
	clj_compiled_eval_boot()
	return cljEval("\(ns)/bench-unit-run")
}

let protoUnitClosedFn = compileUnitRow(name: "unit_closed", closed: true)
let protoUnitDevFn = compileUnitRow(name: "unit_dev", closed: false)
// The accumulating loop calling a tiny pure helper def'd in the same unit: the closed unit calls it by name and
// inlines its body (NOTES.md "Compiler", leaf inlining); the dev unit goes through the var and the dispatcher.
let sqUnit = """
	(defn bench-sq [x] (* x x))
	(defn bench-sq-to [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (bench-sq i))) acc)))
	(defn bench-unit-run [] (bench-sq-to 100000))
	"""
let sqUnitClosedFn = compileUnitRow(name: "sq_closed", closed: true, body: sqUnit)
let sqUnitDevFn = compileUnitRow(name: "sq_dev", closed: false, body: sqUnit)
// The same shape with a helper that can throw on its primitive path (quot: a zero divisor) and with a double helper,
// each beside the loop with the operation written out (NOTES.md "Compiler", the primitive entry).
let qrUnit = """
	(defn bench-qr [a b] (quot a b))
	(defn bench-qr-to [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (bench-qr i 3))) acc)))
	(defn bench-unit-run [] (bench-qr-to 100000))
	"""
let qrInlineUnit = """
	(defn bench-qr-to [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (quot i 3))) acc)))
	(defn bench-unit-run [] (bench-qr-to 100000))
	"""
let halfUnit = """
	(defn bench-half [x] (/ x 2.0))
	(defn bench-half-to [n] (loop [i 0 x 0.0 acc 0.0] (if (< i n) (recur (inc i) (+ x 1.0) (+ acc (bench-half x))) acc)))
	(defn bench-unit-run [] (bench-half-to 100000))
	"""
let halfInlineUnit = """
	(defn bench-half-to [n] (loop [i 0 x 0.0 acc 0.0] (if (< i n) (recur (inc i) (+ x 1.0) (+ acc (/ x 2.0))) acc)))
	(defn bench-unit-run [] (bench-half-to 100000))
	"""
let qrUnitClosedFn = compileUnitRow(name: "qr_closed", closed: true, body: qrUnit)
let qrInlineClosedFn = compileUnitRow(name: "qr_inline_closed", closed: true, body: qrInlineUnit)
let halfUnitClosedFn = compileUnitRow(name: "half_closed", closed: true, body: halfUnit)
let halfInlineClosedFn = compileUnitRow(name: "half_inline_closed", closed: true, body: halfInlineUnit)
// Self-recursive numeric fns as closed units: the join of a fn's own site rests on the entry's own join (NOTES.md
// "Facts", the caller join), so the recursion runs worker to worker. (fact 20) 5000 times is 100000 recursive calls;
// (fib 25) once is 242785 calls.
let factUnit = """
	(defn bench-fact [n] (if (<= n 1) 1 (* n (bench-fact (dec n)))))
	(defn bench-fact-to [k] (loop [i 0 r 0] (if (< i k) (recur (inc i) (bench-fact 20)) r)))
	(defn bench-unit-run [] (bench-fact-to 5000))
	"""
let fibUnit = """
	(defn bench-fib [n] (if (< n 2) n (+ (bench-fib (- n 1)) (bench-fib (- n 2)))))
	(defn bench-unit-run [] (bench-fib 25))
	"""
let factUnitClosedFn = compileUnitRow(name: "fact_closed", closed: true, body: factUnit)
let fibUnitClosedFn = compileUnitRow(name: "fib_closed", closed: true, body: fibUnit)

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
	callRows.append(CallRow(scenario: "loop accumulating into a local", n: n, c: measure(ops: n) { cCountLoop(accFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "counting loop, bound known from the caller", n: n, c: measure(ops: n) { cljCall0(joinedCountFn) }, swift: nil))
	callRows.append(CallRow(scenario: "accumulating loop, bound known from the caller", n: n, c: measure(ops: n) { cljCall0(joinedAccFn) }, swift: nil))
	callRows.append(CallRow(scenario: "accumulating loop with (* i i) written out", n: n, c: measure(ops: n) { cljCall0(sqInlineFn) }, swift: nil))
	if let f = sqUnitClosedFn { callRows.append(CallRow(scenario: "accumulating loop calling (defn sq [x] (* x x)), one closed unit", n: n, c: measure(ops: n) { cljCall0(f) }, swift: nil)) }
	if let f = sqUnitDevFn { callRows.append(CallRow(scenario: "accumulating loop calling (defn sq [x] (* x x)), one dev unit", n: n, c: measure(ops: n) { cljCall0(f) }, swift: nil)) }
	if let f = qrInlineClosedFn { callRows.append(CallRow(scenario: "accumulating loop with (quot i 3) written out, one closed unit", n: n, c: measure(ops: n) { cljCall0(f) }, swift: nil)) }
	if let f = qrUnitClosedFn { callRows.append(CallRow(scenario: "accumulating loop calling (defn qr [a b] (quot a b)), one closed unit", n: n, c: measure(ops: n) { cljCall0(f) }, swift: nil)) }
	if let f = halfInlineClosedFn { callRows.append(CallRow(scenario: "double accumulating loop with (/ x 2.0) written out, one closed unit", n: n, c: measure(ops: n) { cljCall0(f) }, swift: nil)) }
	if let f = halfUnitClosedFn { callRows.append(CallRow(scenario: "double accumulating loop calling (defn half [x] (/ x 2.0)), one closed unit", n: n, c: measure(ops: n) { cljCall0(f) }, swift: nil)) }
	if let f = factUnitClosedFn { callRows.append(CallRow(scenario: "(fact 20) x 5000, per recursive call, one closed unit", n: n, c: measure(ops: n) { cljCall0(f) }, swift: nil)) }
	if let f = fibUnitClosedFn { callRows.append(CallRow(scenario: "(fib 25), per call, one closed unit", n: 242_785, c: measure(ops: 242_785) { cljCall0(f) }, swift: nil)) }
	let fixnums = cVecBuild(n), da = cDoubleVecBuild(n), db = cDoubleVecBuild(n)
	callRows.append(CallRow(scenario: "double accumulating loop", n: n, c: measure(ops: n) { cCountLoop(dblAccFn, n) }, swift: nil))
	callRows.append(CallRow(scenario: "dot product of two double vectors via nth", n: n, c: measure(ops: n) { cljCall2(dotFn, da, db) }, swift: nil))
	callRows.append(CallRow(scenario: "accumulating loop, bound (count v) in a let", n: n, c: measure(ops: n) { cljCall(countBoundFn, fixnums) }, swift: nil))
	callRows.append(CallRow(scenario: "accumulating loop down from (count v)", n: n, c: measure(ops: n) { cljCall(countDownFn, fixnums) }, swift: nil))
	clj_release(fixnums)
	clj_release(da)
	clj_release(db)
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
	callRows.append(CallRow(scenario: "protocol call, receiver known from the caller", n: n, c: measure(ops: n) { cljCall0(protoKnownFn) }, swift: nil))
	callRows.append(CallRow(scenario: "protocol call, captured receiver", n: n, c: measure(ops: n) { cClosureCallLoop(protoTopFn, n) }, swift: nil))
	if let f = protoUnitClosedFn { callRows.append(CallRow(scenario: "protocol call, known receiver, one closed unit", n: n, c: measure(ops: n) { cljCall0(f) }, swift: nil)) }
	if let f = protoUnitDevFn { callRows.append(CallRow(scenario: "protocol call, known receiver, one dev unit", n: n, c: measure(ops: n) { cljCall0(f) }, swift: nil)) }
}
clj_release(countFn)
clj_release(accFn)
clj_release(joinedCountFn)
clj_release(joinedAccFn)
clj_release(sqInlineFn)
clj_release(dblAccFn)
clj_release(dotFn)
clj_release(countBoundFn)
clj_release(countDownFn)
clj_release(callFn)
clj_release(nativeCallFn)
clj_release(hostCallFn)
clj_release(letFn)
clj_release(helperFn)
clj_release(protoTypeFn)
clj_release(protoBuiltinFn)
clj_release(protoKnownFn)
clj_release(protoTopFn)
if let f = protoUnitClosedFn { clj_release(f) }
if let f = protoUnitDevFn { clj_release(f) }
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

// MARK: - Records

// The §10 step 1b targets: field access ≤2 ns, unique update ≤10 ns.
_ = cljEval("(defrecord BenchPoint [id name count x y])")
let benchPointType = cljEval("BenchPoint")
let benchKeys = ["id", "name", "count", "x", "y"].map { name in name.withCString { clj_keyword_from_cstr($0) } }
let benchCountKey = benchKeys[2]

func recBuild() -> clj_value {
	let vals = (0..<5).map { clj_fixnum($0) }
	return vals.withUnsafeBufferPointer { clj_record_new(benchPointType, $0.baseAddress, 5) }
}

func mapBuild5() -> clj_value {
	var m = clj_map_empty()
	for (i, k) in benchKeys.enumerated() { m = clj_map_assoc(m, k, clj_fixnum(i)) }
	return m
}

func recGet(_ coll: clj_value, _ k: clj_value, _ n: Int) -> UInt64 {
	var sum: UInt64 = 0
	for _ in 0..<n {
		let v = clj_get(coll, k, CLJ_NIL)
		sum &+= UInt64(bitPattern: Int64(clj_fixnum_val(v)))
		clj_release(v)
	}
	return sum
}

// The assoc slot consumes its collection, so a unique one is rewritten in place.
func assocUnique(_ build: () -> clj_value, _ n: Int) -> UInt64 {
	var c = build()
	for i in 0..<n { c = clj_type_of(c).pointee.assoc(c, benchCountKey, clj_fixnum(i)) }
	let r = clj_count(c)
	clj_release(c)
	return UInt64(bitPattern: Int64(clj_fixnum_val(r)))
}

func assocShared(_ base: clj_value, _ n: Int) -> UInt64 {
	var sum: UInt64 = 0
	for i in 0..<n {
		let out = clj_assoc3(base, benchCountKey, clj_fixnum(i))
		sum &+= UInt64(clj_is_ptr(out) ? 1 : 0)
		clj_release(out)
	}
	return sum
}

func updateUnique(_ build: () -> clj_value, _ n: Int) -> UInt64 {
	var c = build()
	for _ in 0..<n {
		let v = clj_get(c, benchCountKey, CLJ_NIL)
		c = clj_type_of(c).pointee.assoc(c, benchCountKey, clj_fixnum(clj_fixnum_val(v) + 1))
		clj_release(v)
	}
	let r = clj_get(c, benchCountKey, CLJ_NIL)
	clj_release(c)
	let out = UInt64(bitPattern: Int64(clj_fixnum_val(r)))
	clj_release(r)
	return out
}

struct RecordRow {
	let scenario: String
	let record: Double
	let map: Double
}

var recordRows: [RecordRow] = []
do {
	let n = 100_000
	let rec = recBuild(), hmap = mapBuild5()
	let recShared = clj_retain(rec), mapShared = clj_retain(hmap)
	recordRows.append(RecordRow(scenario: "(:count r), field 3 of 5",
		record: measure(ops: n) { recGet(rec, benchCountKey, n) },
		map: measure(ops: n) { recGet(hmap, benchCountKey, n) }))
	recordRows.append(RecordRow(scenario: "(:id r), field 1 of 5",
		record: measure(ops: n) { recGet(rec, benchKeys[0], n) },
		map: measure(ops: n) { recGet(hmap, benchKeys[0], n) }))
	recordRows.append(RecordRow(scenario: "(assoc r :count v), unique",
		record: measure(ops: n) { assocUnique(recBuild, n) },
		map: measure(ops: n) { assocUnique(mapBuild5, n) }))
	recordRows.append(RecordRow(scenario: "(assoc r :count v), shared",
		record: measure(ops: n) { assocShared(recShared, n) },
		map: measure(ops: n) { assocShared(mapShared, n) }))
	recordRows.append(RecordRow(scenario: "(update r :count inc), unique",
		record: measure(ops: n) { updateUnique(recBuild, n) },
		map: measure(ops: n) { updateUnique(mapBuild5, n) }))
	for v in [rec, hmap, recShared, mapShared] { clj_release(v) }
}
clj_release(benchPointType)

print("\n| scenario | n | record | hash map | map / record |")
print("|---|---:|---:|---:|---:|")
for r in recordRows {
	print("| \(r.scenario) | 100000 | \(fmt(r.record)) | \(fmt(r.map)) | \(ratio(r.record, r.map)) |")
}
print("\nns per op; record = a 5-field defrecord through clj_get and the assoc slot, hash map = the same five keywords and values through clj_map_get / clj_map_assoc; unique = the only reference, shared = a second one held so every assoc copies")

// MARK: - Shapes

// The Records rows for a record, a shape map and the trie (shapes off), then the sites (NOTES.md "Shapes").
_ = cljEval("(defrecord BenchShapePoint [id name count x y])")
let shapePointType = cljEval("BenchShapePoint")

func shapeRecBuild() -> clj_value {
	let vals = (0..<5).map { clj_fixnum($0) }
	return vals.withUnsafeBufferPointer { clj_record_new(shapePointType, $0.baseAddress, 5) }
}

func trieBuild5() -> clj_value {
	clj_shapes_enable(false)
	defer { clj_shapes_enable(true) }
	return mapBuild5()
}

struct ShapeRow {
	let scenario: String
	let record: Double?
	let shape: Double
	let trie: Double
}

var shapeRows: [ShapeRow] = []
do {
	let n = 100_000
	let rec = shapeRecBuild(), smap = mapBuild5(), tmap = trieBuild5()
	precondition(clj_is_shape_map(smap) && !clj_is_shape_map(tmap))
	let recShared = clj_retain(rec), smapShared = clj_retain(smap), tmapShared = clj_retain(tmap)
	// The shape's slots are in compare's order (count id name x y), the record's in the basis's (id name count x y).
	shapeRows.append(ShapeRow(scenario: "(:count r), record field 3 / shape slot 1, clj_get",
		record: measure(ops: n) { recGet(rec, benchCountKey, n) },
		shape: measure(ops: n) { recGet(smap, benchCountKey, n) },
		trie: measure(ops: n) { recGet(tmap, benchCountKey, n) }))
	shapeRows.append(ShapeRow(scenario: "(:id r), record field 1 / shape slot 2, clj_get",
		record: measure(ops: n) { recGet(rec, benchKeys[0], n) },
		shape: measure(ops: n) { recGet(smap, benchKeys[0], n) },
		trie: measure(ops: n) { recGet(tmap, benchKeys[0], n) }))
	shapeRows.append(ShapeRow(scenario: "(assoc r :count v), unique",
		record: measure(ops: n) { assocUnique(shapeRecBuild, n) },
		shape: measure(ops: n) { assocUnique(mapBuild5, n) },
		trie: measure(ops: n) { assocUnique(trieBuild5, n) }))
	shapeRows.append(ShapeRow(scenario: "(assoc r :count v), shared",
		record: measure(ops: n) { assocShared(recShared, n) },
		shape: measure(ops: n) { assocShared(smapShared, n) },
		trie: measure(ops: n) { assocShared(tmapShared, n) }))
	shapeRows.append(ShapeRow(scenario: "(update r :count inc), unique",
		record: measure(ops: n) { updateUnique(shapeRecBuild, n) },
		shape: measure(ops: n) { updateUnique(mapBuild5, n) },
		trie: measure(ops: n) { updateUnique(trieBuild5, n) }))
	for v in [rec, smap, tmap, recShared, smapShared, tmapShared] { clj_release(v) }
}

// The counting loop plus one operation; receivers are built once outside, the trie ones with shapes off.
let shapeLiteralFn = cljEval("(fn [n] (loop [i 0 acc 0] (if (< i n) (let [m {:id i :name \"x\" :count i}] (recur (inc i) (+ acc (count m)))) acc)))")
let shapeAssocNewFn = cljEval("(fn [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (count (assoc {:id i} :name \"x\")))) acc)))")
let shapeGetIdFn = cljEval("(fn [m n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (:id m))) acc)))")
let shapeGetCountFn = cljEval("(fn [m n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (:count m))) acc)))")
let shapeGetYFn = cljEval("(fn [m n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (:y m))) acc)))")
let shapeGetFormFn = cljEval("(fn [m n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (get m :count))) acc)))")
let shapePolyFn = cljEval("(fn [a b n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (:count (if (even? i) a b)))) acc)))")
let shapeCycleFn = cljEval("(fn [ms n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (:count (nth ms (bit-and i 7))))) acc)))")
let shapeFoldFn = cljEval("(fn [maps] (reduce (fn [acc m] (+ acc (:count m))) 0 maps))")
let shapeEqFn = cljEval("(fn [a b n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (if (= a b) (inc acc) acc)) acc)))")
let shapeIntoFn = cljEval("(fn [pairs n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (count (into {} pairs)))) acc)))")
let shapeZipmapFn = cljEval("(fn [ks vs n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (count (zipmap ks vs)))) acc)))")

func shapeReceivers() -> (shape: clj_value, trie: clj_value, record: clj_value) {
	let s = cljEval("{:id 1 :name \"x\" :count 3 :x 4 :y 5}")
	clj_shapes_enable(false)
	let t = cljEval("(hash-map :id 1 :name \"x\" :count 3 :x 4 :y 5)")
	clj_shapes_enable(true)
	let r = cljEval("(->BenchShapePoint 1 \"x\" 3 4 5)")
	precondition(clj_is_shape_map(s) && !clj_is_shape_map(t) && clj_is_record(r))
	return (s, t, r)
}

func cljCall3(_ f: clj_value, _ a: clj_value, _ b: clj_value, _ c: clj_value) -> UInt64 {
	let args = [a, b, c]
	let r = args.withUnsafeBufferPointer { clj_invoke(f, $0.baseAddress, 3) }
	if r == CLJ_THROWN { benchThrew() }
	let v = UInt64(bitPattern: Int64(clj_fixnum_val(r)))
	clj_release(r)
	return v
}

struct SiteRow {
	let scenario: String
	let n: Int
	let shape: Double
	let trie: Double?
	let record: Double?
}

var siteRows: [SiteRow] = []
do {
	let n = 100_000
	let recv = shapeReceivers()
	let fix = clj_fixnum(n)
	siteRows.append(SiteRow(scenario: "literal {:id i :name \"x\" :count i} per iteration", n: n, shape: measure(ops: n) { cljCall(shapeLiteralFn, fix) }, trie: nil, record: nil))
	siteRows.append(SiteRow(scenario: "(assoc {:id i} :name \"x\"), a transition per iteration", n: n, shape: measure(ops: n) { cljCall(shapeAssocNewFn, fix) }, trie: nil, record: nil))
	for (name, f) in [("(:id m), record field 1 / shape slot 2", shapeGetIdFn), ("(:count m), record field 3 / shape slot 1", shapeGetCountFn), ("(:y m), field 5 of 5", shapeGetYFn), ("(get m :count), record field 3 / shape slot 1", shapeGetFormFn)] {
		siteRows.append(SiteRow(scenario: name, n: n,
			shape: measure(ops: n) { cljCall2(f, recv.shape, fix) },
			trie: measure(ops: n) { cljCall2(f, recv.trie, fix) },
			record: measure(ops: n) { cljCall2(f, recv.record, fix) }))
	}
	// The polymorphic site alternates two shapes; the trie column alternates a shape map with a trie (a miss every other call).
	let other = cljEval("{:count 7 :z 8}")
	siteRows.append(SiteRow(scenario: "(:count m), site over 2 shapes", n: n,
		shape: measure(ops: n) { cljCall3(shapePolyFn, recv.shape, other, fix) },
		trie: measure(ops: n) { cljCall3(shapePolyFn, recv.shape, recv.trie, fix) },
		record: measure(ops: n) { cljCall3(shapePolyFn, recv.shape, recv.record, fix) }))
	// Eight receivers through nth: one shape, four (the cache's capacity), eight (megamorphic), eight tries.
	let cycleOne = cljEval("(vec (repeat 8 {:id 1 :name \"x\" :count 3 :x 4 :y 5}))")
	let cycleFour = cljEval("[{:count 1 :a 1} {:count 2 :b 2} {:count 3 :c 3} {:count 4 :d 4} {:count 1 :a 1} {:count 2 :b 2} {:count 3 :c 3} {:count 4 :d 4}]")
	let cycleEight = cljEval("[{:count 1 :a 1} {:count 2 :b 2} {:count 3 :c 3} {:count 4 :d 4} {:count 5 :e 5} {:count 6 :f 6} {:count 7 :g 7} {:count 8 :h 8}]")
	clj_shapes_enable(false)
	let cycleTries = cljEval("(mapv (fn [m] (into (hash-map 1 1) m)) [{:count 1 :a 1} {:count 2 :b 2} {:count 3 :c 3} {:count 4 :d 4} {:count 5 :e 5} {:count 6 :f 6} {:count 7 :g 7} {:count 8 :h 8}])")
	clj_shapes_enable(true)
	siteRows.append(SiteRow(scenario: "(:count (nth ms i)), 8 receivers of one shape", n: n, shape: measure(ops: n) { cljCall2(shapeCycleFn, cycleOne, fix) }, trie: nil, record: nil))
	siteRows.append(SiteRow(scenario: "(:count (nth ms i)), 4 shapes", n: n, shape: measure(ops: n) { cljCall2(shapeCycleFn, cycleFour, fix) }, trie: nil, record: nil))
	siteRows.append(SiteRow(scenario: "(:count (nth ms i)), 8 shapes (megamorphic)", n: n, shape: measure(ops: n) { cljCall2(shapeCycleFn, cycleEight, fix) }, trie: measure(ops: n) { cljCall2(shapeCycleFn, cycleTries, fix) }, record: nil))
	// The JSON-shaped fold: 100k maps of one shape, as tries and as records.
	let foldShapes = cljEval("(mapv (fn [i] {:id i :name \"x\" :count i}) (range 100000))")
	clj_shapes_enable(false)
	let foldTries = cljEval("(mapv (fn [i] (hash-map :id i :name \"x\" :count i)) (range 100000))")
	clj_shapes_enable(true)
	let foldRecords = cljEval("(mapv (fn [i] (->BenchShapePoint i \"x\" i 0 0)) (range 100000))")
	precondition(!clj_is_shape_map(clj_vector_nth(foldTries, 0)))
	siteRows.append(SiteRow(scenario: "(reduce (fn [acc m] (+ acc (:count m))) 0 maps), 100k maps", n: n,
		shape: measure(ops: n) { cljCall(shapeFoldFn, foldShapes) },
		trie: measure(ops: n) { cljCall(shapeFoldFn, foldTries) },
		record: measure(ops: n) { cljCall(shapeFoldFn, foldRecords) }))
	// Equality of two equal 5-key maps: same shape (slot compare), a shape map against a trie, two tries.
	let eqA = cljEval("{:id 1 :name \"x\" :count 3 :x 4 :y 5}"), eqB = cljEval("{:id 1 :name \"x\" :count 3 :x 4 :y 5}")
	clj_shapes_enable(false)
	let eqT1 = cljEval("(hash-map :id 1 :name \"x\" :count 3 :x 4 :y 5)"), eqT2 = cljEval("(hash-map :id 1 :name \"x\" :count 3 :x 4 :y 5)")
	clj_shapes_enable(true)
	siteRows.append(SiteRow(scenario: "(= a b), two equal 5-key maps", n: n,
		shape: measure(ops: n) { cljCall3(shapeEqFn, eqA, eqB, fix) },
		trie: measure(ops: n) { cljCall3(shapeEqFn, eqT1, eqT2, fix) },
		record: measure(ops: n) { cljCall3(shapeEqFn, eqA, eqT1, fix) }))
	let pairs = cljEval("[[:id 1] [:name \"x\"] [:count 3] [:x 4] [:y 5]]")
	let ks = cljEval("[:id :name :count :x :y]"), vs = cljEval("[1 \"x\" 3 4 5]")
	let intoShape = measure(ops: n) { cljCall2(shapeIntoFn, pairs, fix) }
	let zipShape = measure(ops: n) { cljCall3(shapeZipmapFn, ks, vs, fix) }
	clj_shapes_enable(false)
	let intoTrie = measure(ops: n) { cljCall2(shapeIntoFn, pairs, fix) }
	let zipTrie = measure(ops: n) { cljCall3(shapeZipmapFn, ks, vs, fix) }
	clj_shapes_enable(true)
	siteRows.append(SiteRow(scenario: "(into {} pairs), 5 pairs", n: n, shape: intoShape, trie: intoTrie, record: nil))
	siteRows.append(SiteRow(scenario: "(zipmap ks vs), 5 keys", n: n, shape: zipShape, trie: zipTrie, record: nil))
	for v in [recv.shape, recv.trie, recv.record, other, cycleOne, cycleFour, cycleEight, cycleTries, foldShapes, foldTries, foldRecords, eqA, eqB, eqT1, eqT2, pairs, ks, vs] { clj_release(v) }
}
for v in [shapeLiteralFn, shapeAssocNewFn, shapeGetIdFn, shapeGetCountFn, shapeGetYFn, shapeGetFormFn, shapePolyFn, shapeCycleFn, shapeFoldFn, shapeEqFn, shapeIntoFn, shapeZipmapFn] { clj_release(v) }
clj_release(shapePointType)

// Pool cells handed out per map, with the trie's nodes included.
func mapFootprint(_ build: () -> clj_value, count: Int) -> Double {
	var held: [clj_value] = []
	held.reserveCapacity(count)
	let before = clj_debug_pool_used_bytes()
	for _ in 0..<count { held.append(build()) }
	let after = clj_debug_pool_used_bytes()
	for v in held { clj_release(v) }
	return Double(after - before) / Double(count)
}

let footprintCount = 200_000
let shapeFootprint = mapFootprint(mapBuild5, count: footprintCount)
let trieFootprint = mapFootprint(trieBuild5, count: footprintCount)
let shapeCell = clj_debug_cell_size(24 + 5 * 8), trieWrapperCell = clj_debug_cell_size(40), trieNodeCell = clj_debug_cell_size(24 + 10 * 8)

print("\n| scenario | n | record | shape map | trie | trie / shape |")
print("|---|---:|---:|---:|---:|---:|")
for r in shapeRows {
	print("| \(r.scenario) | 100000 | \(fmt(r.record)) | \(fmt(r.shape)) | \(fmt(r.trie)) | \(ratio(r.shape, r.trie)) |")
}
print("\nns per op through the C API, as the Records section: record = a 5-field defrecord, shape map = {:id :name :count :x :y} as a shape map, trie = the same map built with shapes off")
print("\n| scenario | n | shape map | trie | record |")
print("|---|---:|---:|---:|---:|")
for r in siteRows {
	print("| \(r.scenario) | \(r.n) | \(fmt(r.shape)) | \(fmt(r.trie)) | \(fmt(r.record)) |")
}
print("\nns per iteration, interpreted: the counting loop plus the operation; the receiver columns are the same fn over a shape map, the same map as a trie (shapes off while building) and a 5-field record; the polymorphic row's trie column alternates the shape map with a trie, its record column with a record; the equality row's record column compares the shape map with the trie")
print("\nbytes per 5-key map, pool cells over \(footprintCount) maps: shape map \(String(format: "%.0f", shapeFootprint)) (one \(shapeCell)-byte cell), trie \(String(format: "%.0f", trieFootprint)) (a \(trieWrapperCell)-byte wrapper and a \(trieNodeCell)-byte node, more with a hash collision at the root); shapes so far \(clj_debug_shape_count()), \(clj_debug_shape_bytes()) bytes of shapes and transitions")

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

// MARK: - Regex

// The pattern is compiled once outside the measured fn, as a literal in a closure is.
let reFindFn = cljEval(#"(let [p #"\d+"] (fn [s] (count (re-find p s))))"#)
let reSplitFn = cljEval(#"(do (require 'clojure.string) (let [p #","] (fn [s] (count (clojure.string/split s p)))))"#)
let reReplaceFn = cljEval(#"(let [p #"(\w+)@"] (fn [s] (count (clojure.string/replace s p "$1 at "))))"#)
let reFindArg = cljEval(#""order 1147 shipped on 2026-09-16 by van 7""#)
let reSplitArg = cljEval(#""a,bb,ccc,dddd,e,ff,ggg,h,ii,jjj""#)
let reReplaceArg = cljEval(#""ann@example.com and bob@example.com""#)

let regexRows: [(String, Double)] = [
	("re-find #\"\\d+\" over a 40-char string", measure(ops: 1) { cljCall(reFindFn, reFindArg) }),
	("split #\",\" of a 10-field line", measure(ops: 1) { cljCall(reSplitFn, reSplitArg) }),
	("replace #\"(\\w+)@\" with $1", measure(ops: 1) { cljCall(reReplaceFn, reReplaceArg) }),
]
for v in [reFindFn, reSplitFn, reReplaceFn, reFindArg, reSplitArg, reReplaceArg] { clj_release(v) }

print("\n| scenario | ns/op |")
print("|---|---:|")
for r in regexRows {
	print("| \(r.0) | \(fmt(r.1)) |")
}
print("\nns per call; the measured fn is interpreted and the pattern is already compiled")
