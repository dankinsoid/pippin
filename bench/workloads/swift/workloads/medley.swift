// @ai-generated(solo)
// workloads.medley: medley.core's functions as a Swift dev writes them over typed records.

enum Tag: Hashable { case a, b, c }

// The source's #{:a :b} and #{:c} are literals: one value each, shared by every record.
let evenTags: Set<Tag> = [.a, .b]
let oddTags: Set<Tag> = [.c]

struct Record {
	let id: Int
	let group: Int
	let score: Int
	let tags: Set<Tag>
	let parent: Int?
}

struct GroupSummary {
	var n: Int
	var tags: [Int: Bool]

	/// deep-merge of {:n .. :tags {..}}: :n is not a map and is overwritten, :tags are maps and merge.
	mutating func deepMerge(_ other: GroupSummary) {
		n = other.n
		tags.merge(other.tags) { $1 }
	}
}

struct Stats {
	var n: Int?
	var isEmpty: Bool { n == nil }
}

func records(_ n: Int) -> [Record] {
	zip(0..<n, lcgVec(seed: 42, count: n)).map { i, x in
		Record(id: i, group: x % 37, score: x % 1000, tags: x.isMultiple(of: 2) ? evenTags : oddTags,
		       parent: i > 0 ? x % i : nil)
	}
}

func interleaveAll<S: Sequence>(_ seqs: [S]) -> [S.Element] {
	var live = seqs.map { $0.makeIterator() }
	var out: [S.Element] = []
	while !live.isEmpty {
		var next: [S.Iterator] = []
		for var it in live {
			if let x = it.next() {
				out.append(x)
				next.append(it)
			}
		}
		live = next
	}
	return out
}

func run() -> EDN {
	let rs = records(20000)
	let byId = Dictionary(rs.map { ($0.id, $0) }, uniquingKeysWith: { $1 })
	let scores = byId.mapValues(\.score)
	let high = scores.filter { $0.value > 500 }
	let keyed = Dictionary(high.map { (String($0.key), $0.value) }, uniquingKeysWith: { $1 })
	let oddIds = scores.filter { !$0.key.isMultiple(of: 2) }
	let dropped = scores.filter { $0.value != 0 }
	let totals = Dictionary(grouping: rs, by: \.group).mapValues { $0.reduce(0) { $0 + $1.score } }
	var collated: [Int: Int] = [:]
	for r in rs {
		collated[r.group] = collated[r.group].map { $0 + r.score } ?? r.score
	}
	var merged: [Int: GroupSummary] = [:]
	for r in rs.prefix(2000) {
		let incoming = GroupSummary(n: r.id, tags: [r.score: true])
		if merged[r.group]?.deepMerge(incoming) == nil {
			merged[r.group] = incoming
		}
	}
	var seen = Set<Int>()
	let distinctScores = rs.filter { seen.insert($0.score).inserted }
	var prior: Int?
	let deduped = rs.filter { r in
		let k = r.score / 100
		defer { prior = k }
		return k != prior
	}
	let found = rs.first { $0.score == 999 }
	let upto = rs.firstIndex { $0.score > 990 }.map { rs[...$0] } ?? rs[...]
	var nested = Dictionary(uniqueKeysWithValues: (0..<37).map { ($0, Stats(n: 0)) })
	for r in rs {
		nested[r.group]?.n? += r.score
	}
	var pruned = nested
	for r in rs.prefix(20) {
		if var stats = pruned[r.group] {
			stats.n = nil
			pruned[r.group] = stats.isEmpty ? nil : stats
		}
	}
	var someAssoc: [Int: Int] = [:]
	for r in rs.prefix(5000) {
		if let parent = r.parent {
			someAssoc[r.id] = parent
		}
	}
	let interleaved = interleaveAll([0..<1000, 0..<500, 0..<1500]).count
	let indexed = rs.prefix(3000).enumerated().reduce(0) { $0 + $1.offset * $1.element.score }
	// The source calls greatest-by with one argument, the seq, which it returns unrealized; its :score is nil.
	let bestScore: Int? = nil
	return .vector([
		.int(high.count), .int(keyed.count), .int(oddIds.count), .int(dropped.count),
		.int(totals.values.reduce(0, +)), .int(collated.values.reduce(0, +)), .int(merged.count),
		.int(merged.values.reduce(0) { $0 + $1.tags.count }), .int(distinctScores.count), .int(deduped.count),
		bestScore.map(EDN.int) ?? .null, found.map { .int($0.id) } ?? .null, .int(upto.count),
		.int(nested.values.reduce(0) { $0 + $1.n! }), .int(pruned.count), .int(someAssoc.count), .int(interleaved),
		.int(indexed),
	])
}
