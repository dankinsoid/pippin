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

// Median of `reps` runs, ns per op. The checksum keeps the optimizer from dropping the work.
func measure(ops: Int, _ body: () -> UInt64) -> Double {
	var times: [Double] = []
	var sink: UInt64 = 0
	for _ in 0..<reps {
		let t0 = DispatchTime.now().uptimeNanoseconds
		sink &+= body()
		let t1 = DispatchTime.now().uptimeNanoseconds
		times.append(Double(t1 - t0) / Double(ops))
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

print("| scenario | n | C HAMT | TreeDictionary | Dictionary | tree / C |")
print("|---|---:|---:|---:|---:|---:|")
for r in rows {
	print("| \(r.scenario) | \(r.n) | \(fmt(r.c)) | \(fmt(r.tree)) | \(fmt(r.dict)) | \(ratio(r.c, r.tree)) |")
}
print("\nns per op, median of \(reps) runs; get = \(lookups) random probes")
