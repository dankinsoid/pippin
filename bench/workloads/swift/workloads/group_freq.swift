// @ai-generated(solo)
// workloads.group-freq: grouping and counting over words and person structs.

let syllables = ["ka", "lo", "mi", "ne", "ru", "sa", "ti", "vo", "ze", "pa", "qu", "do"]
let cities = ["Yerevan", "Gyumri", "Vanadzor", "Dilijan", "Goris"]

func word(_ x: Int) -> String {
	(0..<1 + x % 3).map { syllables[(x / (1 + $0 * 12)) % 12] }.joined()
}

struct Person {
	let id: Int
	let dept: String
	let city: String
	let age: Int
	let salary: Int
}

struct CityDecade: Hashable {
	let city: String
	let decade: Int
}

func people(_ xs: [Int]) -> [Person] {
	zip(0..., xs).map { i, x in
		Person(id: i, dept: "d\(x % 23)", city: cities[x % 5], age: 20 + x % 45, salary: 100 * (x % 997))
	}
}

func run() -> EDN {
	let xs = lcgVec(seed: 3, count: 50000)
	let words = xs.map(word)
	let freqs = words.reduce(into: [String: Int]()) { $0[$1, default: 0] += 1 }
	let top = freqs.sorted { $0.value != $1.value ? $0.value > $1.value : $0.key < $1.key }.prefix(10)
	// Clojure's count of a string is Java's length, UTF-16 units.
	let byLen = Dictionary(grouping: words, by: \.utf16.count).mapValues(\.count)
	let ps = people(xs)
	let byDept = Dictionary(grouping: ps, by: \.dept)
	let payroll = byDept.mapValues { $0.reduce(0) { $0 + $1.salary } }
	let oldest = byDept.mapValues { $0.max { 1_000_000 * $0.age + $0.id < 1_000_000 * $1.age + $1.id }!.id }
	let byCityAge = ps.reduce(into: [CityDecade: Int]()) { $0[CityDecade(city: $1.city, decade: $1.age / 10), default: 0] += 1 }
	let merged = ps.reduce(into: [String: Int]()) { $0[$1.city, default: 0] += $1.salary }
	let ranked = ps.sorted { $0.salary != $1.salary ? $0.salary > $1.salary : $0.id < $1.id }.prefix(100)
	let buckets = ps.reduce(into: [String: Set<String>]()) { $0[$1.dept, default: []].insert($1.city) }
	return .vector([
		.int(freqs.count),
		.vector(top.map { .vector([.string($0.key), .int($0.value)]) }),
		.map(byLen.sorted { $0.key < $1.key }.map { (.int($0.key), .int($0.value)) }),
		.int(payroll.values.reduce(0, +)),
		.int(oldest.values.reduce(0, +)),
		.int(byCityAge.count),
		.map(merged.sorted { $0.key < $1.key }.map { (.string($0.key), .int($0.value)) }),
		.ints(ranked.map(\.id)),
		.int(buckets.values.reduce(0) { $0 + $1.count }),
	])
}
