// @ai-generated(solo)
// workloads.combinatorics: clojure.math.combinatorics' algorithms for the cases the workload calls (distinct or
// sorted numeric items), as iterators over arrays.

func weigh(_ xs: some Sequence<Int>) -> Int {
	zip(xs, 1..<100).reduce(0) { $0 + $1.0 * $1.1 }
}

extension Array where Element: Comparable {
	/// The library's iter-perm (Knuth's algorithm L); false past the last permutation.
	mutating func formNextPermutation() -> Bool {
		guard count > 1, let j = indices.dropLast().last(where: { self[$0] < self[$0 + 1] }) else { return false }
		let l = indices.last { self[j] < self[$0] }!
		swapAt(j, l)
		self[(j + 1)...].reverse()
		return true
	}
}

/// lex-permutations: every permutation of the sorted items, duplicates once each.
func permutations<T: Comparable>(_ items: [T]) -> some Sequence<[T]> {
	sequence(first: items.sorted()) { previous in
		var next = previous
		return next.formNextPermutation() ? next : nil
	}
}

/// index-combinations: the library's order of t-combinations of cnt indices, each newest index first.
struct IndexCombinations: Sequence, IteratorProtocol {
	let n: Int
	var c: [Int]
	var j = 1
	var done = false

	init(_ n: Int, _ cnt: Int) {
		self.n = n
		c = [0] + (1...n).map { $0 + cnt - (n + 1) }
	}

	mutating func next() -> [Int]? {
		if done { return nil }
		let out = Array(c[1...].reversed())
		if j > n {
			done = true
		} else {
			c[j] -= 1
			if c[j] < j {
				j += 1
			} else {
				while j != 1 {
					c[j - 1] = c[j] - 1
					j -= 1
				}
			}
		}
		return out
	}
}

func combinations<T>(_ items: [T], _ t: Int) -> [[T]] {
	if t == 0 { return [[]] }
	if t > items.count { return [] }
	if t == 1 { return items.map { [$0] } }
	if t == items.count { return [items] }
	let reversedItems = Array(items.reversed())
	return IndexCombinations(t, items.count).map { $0.map { reversedItems[$0] } }
}

func subsets<T>(_ items: [T]) -> some Sequence<[T]> {
	(0...items.count).lazy.flatMap { combinations(items, $0) }
}

/// Knuth's algorithm H as the library writes it: growth strings with at most r and at least s parts.
struct GrowthStrings: Sequence, IteratorProtocol {
	let n: Int
	let r: Int?
	let s: Int?
	var a: [Int]
	var b: [Int]
	var done = false

	init(n: Int, r: Int?, s: Int?) {
		self.n = n
		self.r = r
		self.s = s
		a = s.map { s in (1...n).map { Swift.max(0, $0 - (n - s + 1)) } } ?? Array(repeating: 0, count: n)
		b = Array(repeating: 1, count: n)
	}

	mutating func next() -> [Int]? {
		if done { return nil }
		let out = a
		advance()
		return out
	}

	private mutating func advance() {
		if b[n - 1] > a[n - 1] && r.map({ a[n - 1] < $0 - 1 }) ?? true {
			a[n - 1] += 1
			return
		}
		var j = n - 2
		while !(a[j] < b[j] && r.map({ a[j] < $0 - 1 }) ?? true
			&& s.map({ $0 - b[j] - (a[j] + 1 == b[j] ? 1 : 0) <= n - j }) ?? true) {
			j -= 1
		}
		if j == 0 {
			done = true
			return
		}
		a[j] += 1
		let x = s.map { $0 - (b[j] + (a[j] == b[j] ? 1 : 0)) }
		var currentMax = b[j] + (b[j] == a[j] ? 1 : 0)
		for i in (j + 1)..<n {
			if let s, let x, i > n - x - 1 {
				let newA = i - n + s
				a[i] = newA
				b[i] = currentMax
				currentMax = Swift.max(currentMax, newA + 1)
			} else {
				a[i] = 0
				b[i] = currentMax
			}
		}
	}
}

/// partitions-H: every partition of distinct items, with :min and :max parts.
func partitions<T>(_ items: [T], min: Int? = nil, max: Int? = nil) -> [[[T]]] {
	let n = items.count
	if n == 0 { return (min ?? 0) <= 0 && 0 <= (max ?? 0) ? [[]] : [] }
	let from = min.flatMap { $0 <= 1 ? nil : $0 }
	let to = max.flatMap { $0 >= n ? nil : $0 }
	guard 1 <= (from ?? 1), (from ?? 1) <= (to ?? n), (to ?? n) <= n else { return [] }
	if n == 1 { return [[items]] }
	if to == 1 { return [[items]] }
	return GrowthStrings(n: n, r: to, s: from).map { growth in
		let groups = Dictionary(grouping: 0..<n) { growth[$0] }
		return (0..<groups.count).map { groups[$0]!.map { items[$0] } }
	}
}

/// cartesian-product: the nested-for order, the last sequence varying fastest.
struct CartesianProduct<T>: Sequence, IteratorProtocol {
	let seqs: [[T]]
	var positions: [Int]?

	init(_ seqs: [[T]]) {
		self.seqs = seqs
		positions = seqs.allSatisfy { !$0.isEmpty } ? Array(repeating: 0, count: seqs.count) : nil
	}

	mutating func next() -> [T]? {
		guard var p = positions else { return nil }
		let out = zip(seqs, p).map { $0[$1] }
		var i = seqs.count - 1
		while i >= 0 && p[i] + 1 == seqs[i].count {
			p[i] = 0
			i -= 1
		}
		if i < 0 {
			positions = nil
		} else {
			p[i] += 1
			positions = p
		}
		return out
	}
}

enum Letter { case a, b, c }

func factorial(_ n: Int) -> Int {
	(0..<n).reduce(1) { $0 * ($1 + 1) }
}

/// nth-permutation-distinct: through the factorial number system.
func nthPermutation<T>(_ items: [T], _ n: Int) -> [T] {
	precondition(n < factorial(items.count), "\(n) is too large")
	var digits: [Int] = []
	var q = n
	var divisor = 1
	while q != 0 {
		digits.append(q % divisor)
		q /= divisor
		divisor += 1
	}
	var rest = items
	let indices = Array(repeating: 0, count: items.count - digits.count) + digits.reversed()
	return indices.map { rest.remove(at: $0) }
}

func nTakeK(_ n: Int, _ k: Int) -> Int {
	if k < 0 || k > n { return 0 }
	if k == 0 { return 1 }
	if k == 1 { return n }
	if k > n / 2 { return nTakeK(n, n - k) }
	return (n - k + 1...n).reduce(1, *) / (1...k).reduce(1, *)
}

/// nth-combination-distinct.
func nthCombination<T>(_ items: [T], _ t: Int, _ n: Int) -> [T] {
	precondition(n < nTakeK(items.count, t), "\(n) is too large")
	var comb: [T] = []
	var rest = items[...]
	var t = t
	var n = n
	while n != 0 && !rest.isEmpty {
		let dcDt = nTakeK(rest.count - 1, t - 1)
		if n < dcDt {
			comb.append(rest.removeFirst())
			t -= 1
		} else {
			rest.removeFirst()
			n -= dcDt
		}
	}
	return comb + rest.prefix(t)
}

func run() -> EDN {
	let k = 8
	let range = Array(0..<k)
	return .ints([
		permutations(range).reduce(0) { $0 + weigh($1) },
		permutations([1, 1, 2, 2, 3, 3, 4]).reduce(0) { $0 + weigh($1) },
		combinations(Array(0..<3 * k), 4).reduce(0) { $0 + weigh($1) },
		subsets(Array(0..<k + 6)).reduce(0) { $0 + $1.count },
		partitions(Array(0...k)).count,
		partitions(range, min: 2, max: 3).count,
		CartesianProduct(Array(repeating: Array(0..<9), count: 4)).reduce(0) { $0 + weigh($1) },
		CartesianProduct(Array(repeating: [Letter.a, .b, .c], count: k)).reduce(0) { n, _ in n + 1 },
		factorial(k + 4),
		stride(from: 0, to: 3628800, by: 9973).reduce(0) { $0 + weigh(nthPermutation(Array(0..<10), $1)) },
		stride(from: 0, to: 142506, by: 211).reduce(0) { $0 + weigh(nthCombination(Array(0..<30), 5, $1)) },
	])
}
