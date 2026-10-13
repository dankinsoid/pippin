// @ai-generated(solo)
// workloads.strings: the standard library's String APIs, and Foundation's for replacing and trimming.
import Foundation

func row(_ i: Int) -> String {
	["\(i)", "name-\(i % 97)", "\(i * 3)", i.isMultiple(of: 2) ? "yes" : "no", "c\(i % 13)".uppercased()]
		.joined(separator: ",")
}

func capitalize(_ s: Substring) -> String {
	s.prefix(1).uppercased() + s.dropFirst().lowercased()
}

// Counts and offsets are UTF-16 units, Java's String length and indexes that Clojure's count and subs use.
func run() -> EDN {
	let n = 20000
	let csv = (0..<n).map(row).joined(separator: "\n")
	let lines = csv.split(separator: "\n")
	let cells = lines.map { $0.split(separator: ",") }
	let total = cells.reduce(0) { $0 + Int($1[2])! }
	let names = cells.reduce(into: [Substring: Int]()) { $0[$1[1], default: 0] += 1 }
	let upper = cells.lazy.map { $0[1].uppercased() }.filter { $0.hasPrefix("NAME-1") }.count
	let joined = (0..<n / 4).map(String.init).joined(separator: "|")
	let replaced = csv.replacingOccurrences(of: "name", with: "N")
	let regexed = String(csv.subs(0, min(csv.utf16.count, 200000))).replacingOccurrences(of: "[0-9]+", with: "#", options: .regularExpression)
	let trimmed = lines.prefix(5000).reduce(0) { $0 + ("  " + $1 + "  ").trimmingCharacters(in: .whitespacesAndNewlines).utf16.count }
	var built = ""
	for i in 0..<n / 2 {
		if built.utf16.count < 4000 {
			built += String(i)
		} else {
			built = String(built.subs(2000, built.utf16.count))
		}
	}
	let words = Array(repeating: "alpha beta gamma delta", count: 2000).joined(separator: " ").split(separator: " ")
	let caps = words.map(capitalize).joined(separator: " ")
	let rev = (0..<20000).map(String.init).filter { $0 == String($0.reversed()) }.count
	let idx = lines.prefix(10000).reduce(0) { acc, line in
		acc + (line.firstRange(of: "-5").map { line.utf16.distance(from: line.startIndex, to: $0.lowerBound) } ?? -1)
	}
	return .vector([
		.int(csv.utf16.count), .int(total), .int(names.count), .int(upper), .int(joined.utf16.count),
		.int(replaced.utf16.count), .int(regexed.utf16.count), .int(trimmed), .int(built.utf16.count),
		.int(caps.utf16.count), .int(rev), .int(idx), .string(String(caps.subs(0, 64))),
	])
}
