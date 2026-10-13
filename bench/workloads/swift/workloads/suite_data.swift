// @ai-generated(solo)
// workloads.suite-data: a protocol over structs for the records, an enum for the multimethod's maps, stdlib
// collections for the rest.

protocol Shape {
	func area() -> Int
	func scaled(by k: Int) -> any Shape
}

struct Circle: Shape {
	let r: Int
	func area() -> Int { 3 * r * r }
	func scaled(by k: Int) -> any Shape { Circle(r: r * k) }
}

struct Rect: Shape {
	let w: Int
	let h: Int
	func area() -> Int { w * h }
	func scaled(by k: Int) -> any Shape { Rect(w: w * k, h: h * k) }
}

struct Tri: Shape {
	let b: Int
	let h: Int
	func area() -> Int { (b * h) / 2 }
	func scaled(by k: Int) -> any Shape { Tri(b: b * k, h: h * k) }
}

enum Kind: Hashable { case point, segment, other }

enum Figure: Hashable {
	case point(x: Int, y: Int)
	case segment(x1: Int, x2: Int)
	case other

	var kind: Kind {
		switch self {
		case .point: .point
		case .segment: .segment
		case .other: .other
		}
	}

	/// postwalk's inc of every number in the map.
	func incremented() -> Figure {
		switch self {
		case let .point(x, y): .point(x: x + 1, y: y + 1)
		case let .segment(x1, x2): .segment(x1: x1 + 1, x2: x2 + 1)
		case .other: .other
		}
	}
}

func describe(_ f: Figure) -> Int {
	switch f {
	case let .point(x, y): x + y
	case let .segment(x1, x2): x2 - x1
	case .other: 1
	}
}

/// The first field by name, (or (:r s) (:w s) (:b s)).
func firstField(_ s: any Shape) -> Int? {
	switch s {
	case let c as Circle: c.r
	case let r as Rect: r.w
	case let t as Tri: t.b
	default: nil
	}
}

indirect enum Nested {
	case leaf(Int)
	case list([Nested])

	func flatten(into out: inout [Int]) {
		switch self {
		case .leaf(let x): out.append(x)
		case .list(let xs): for x in xs { x.flatten(into: &out) }
		}
	}
}

func run() -> EDN {
	let n = 30000
	let shapes: [any Shape] = (0..<n).map { i in
		switch i % 3 {
		case 0: Circle(r: i % 10)
		case 1: Rect(w: i % 7, h: i % 5)
		default: Tri(b: i % 9, h: i % 4)
		}
	}
	let areas = shapes.reduce(0) { $0 + $1.area() }
	let scaled = shapes.reduce(0) { $0 + $1.scaled(by: 2).area() }
	let fields = shapes.reduce(0) { $0 + firstField($1)! }
	// record?/satisfies? as a runtime conformance check of the boxed value.
	let records = shapes.filter { ($0 as Any) is any Shape }.count
	let figures: [Figure] = (0..<n).map { i in
		switch i % 3 {
		case 0: .point(x: i, y: 2 * i)
		case 1: .segment(x1: i, x2: i + 5)
		default: .other
		}
	}
	let described = figures.reduce(0) { $0 + describe($1) }
	let s1 = Set(stride(from: 0, to: n, by: 2))
	let s2 = Set(stride(from: 0, to: n, by: 3))
	let index = figures.prefix(300).reduce(into: [Kind: Set<Figure>]()) { $0[$1.kind, default: []].insert($1) }
	let sets = [s1.union(s2).count, s1.intersection(s2).count, s1.subtracting(s2).count,
	            s2.filter { $0.isMultiple(of: 2) }.count, index.count]
	let by13 = (0..<n).reduce(into: [Int: Int]()) { $0[$1 % 13, default: 0] += 1 }
	let by17 = (0..<n).reduce(into: [Int: Int]()) { $0[$1 % 17, default: 0] += 1 }
	let merged = by13.merging(by17, uniquingKeysWith: +)
	let zm = Dictionary(uniqueKeysWithValues: zip(0..<1000, (0..<1000).map(String.init)))
	let threes = 0..<3000
	let parts = (0...threes.count - 3).map { threes[$0..<$0 + 3] }.count
	let inter = zip(0..<3000, 3000..<6000).lazy.flatMap { [$0, $1] }.reduce(0, +)
	let sorted = (0..<n).map { ($0 * 7919) % 10007 }.sorted(by: >).prefix(5)
	// The standard library has no sorted map: a Dictionary, its keys sorted once at the end.
	var sm: [Int: Int] = [:]
	for i in 0..<n {
		sm[(i * 31) % 1009] = i
	}
	let smKeys = sm.keys.sorted()
	var stack: [Int] = []
	for i in 0..<n {
		if i.isMultiple(of: 3) {
			_ = stack.popLast()
		} else {
			stack.append(i)
		}
	}
	var tr: [Int] = []
	for i in 0..<n {
		tr.append(i)
	}
	var trm: [Int: Int] = [:]
	for i in 0..<n {
		trm[i % 777] = i
	}
	let walked = figures.prefix(200).map { $0.incremented() }
	let walkedX = walked.reduce(0) { acc, f in
		if case let .point(x, _) = f { acc + x } else { acc }
	}
	var flat: [Int] = []
	for i in 0..<3000 {
		Nested.list([.leaf(i), .list([.leaf(i), .list([.leaf(i)])])]).flatten(into: &flat)
	}
	return .vector([
		.int(areas), .int(scaled), .int(fields), .int(records), .int(described), .ints(sets),
		.int(merged.values.reduce(0, +)), .int(zm.count), .int(parts), .int(inter), .ints(sorted),
		.ints([smKeys.first!, sm[smKeys.first!]!]), .ints([smKeys.last!, sm[smKeys.last!]!]), .int(stack.count),
		.int(tr.count), .int(trm.count), .int(walkedX), .int(flat.reduce(0, +)),
	])
}
