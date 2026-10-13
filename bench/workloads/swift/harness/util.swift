// @ai-generated(solo)
// workloads.util's generator: the JVM's states (x < 2^31, so the product stays below 2^62 as in a long).

func lcg(_ x: Int) -> Int {
	(x * 1103515245 + 12345) & 0x7fff_ffff
}

func lcgVec(seed: Int, count: Int) -> [Int] {
	var x = seed
	return (0..<count).map { _ in
		x = lcg(x)
		return x
	}
}

extension StringProtocol {
	/// Clojure's `subs`: offsets in UTF-16 units, as Java's String indexes.
	func subs(_ start: Int, _ end: Int) -> SubSequence {
		let u = utf16
		return self[u.index(u.startIndex, offsetBy: start)..<u.index(u.startIndex, offsetBy: end)]
	}
}
