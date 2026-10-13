// @ai-generated(solo)
// workloads.pipelines: the source's lazy seqs and transducers as lazy sequence chains, its loops as loops.

struct Entry {
	let k: Int
	let v: Int
}

func run() -> EDN {
	let n = 300000
	let v = Array(0..<n)
	let quarter = v.prefix(n / 4)
	var runningMax = Int.min
	var runningSum = 0
	for x in quarter {
		runningSum += x
		runningMax = max(runningMax, runningSum)
	}
	var dedupeCount = 0
	var previous: Int?
	for x in v.lazy.flatMap({ [$0, $0] }) where x != previous {
		dedupeCount += 1
		previous = x
	}
	var seqSum = 0
	for x in v {
		seqSum += x
	}
	let squares: Int = (0..<n).lazy.map { $0 * $0 }.filter { $0.isMultiple(of: 2) }.map { $0 % 1000 }.reduce(0, +)
	let oddIncs: Int = v.lazy.filter { !$0.isMultiple(of: 2) }.map { $0 + 1 }.reduce(0, +)
	let partitions: Int = Array(stride(from: 0, to: n, by: 16).lazy.map { v[$0..<min($0 + 16, n)].reduce(0, +) }.filter { $0 != 0 }).count
	let byKey: Int = v.reduce(into: [Int: Int]()) { $0[$1 % 100, default: 0] += $1 }.values.reduce(0, +)
	let entries: Int = v.lazy.map { Entry(k: $0 % 10, v: $0) }.filter { !$0.v.isMultiple(of: 2) }.map(\.v).reduce(0, +)
	let iterated: Int = sequence(first: 1) { ($0 * 7) % 1000003 }.prefix(5000).reduce(0, +)
	let repeated: Int = quarter.lazy.flatMap { repeatElement($0, count: $0 % 3) }.reduce(0, +)
	let kept: Int = v.lazy.compactMap { $0.isMultiple(of: 3) ? $0 : nil }.reduce(0, +)
	let distinct: Int = Set(v.lazy.map { $0 % 1000 }).count
	let halves: Double = v.lazy.map { 0.5 * Double($0) }.reduce(0, +)
	let thirds: Double = v.lazy.map { Double($0) / 3.0 }.reduce(0.0, +)
	let products: Int = zip(v, v.reversed()).lazy.map(*).reduce(0, +)
	let rising: Int = zip(v.dropFirst(), v).lazy.filter { $0 - $1 > 0 }.count
	let indexed: Int = v.enumerated().reduce(0) { $0 + $1.offset * ($1.element % 7) }
	let groups: Int = Dictionary(grouping: quarter) { $0 % 7 }.count
	return .vector([
		.int(squares),
		.int(oddIncs),
		.int(partitions),
		.int(byKey),
		.int(entries),
		.int(iterated),
		.int(dedupeCount),
		.int(runningMax),
		.int(repeated),
		.int(kept),
		.int(distinct),
		.double(halves),
		.double(thirds),
		.int(products),
		.int(rising),
		.int(seqSum),
		.int(indexed),
		.int(groups),
	])
}
