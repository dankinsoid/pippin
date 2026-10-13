// @ai-generated(solo)
// A workload's result as Clojure's pr-str prints it, the form compared with the JVM's.
indirect enum EDN {
	case int(Int)
	case double(Double)
	case string(String)
	case null
	case vector([EDN])
	case map([(EDN, EDN)])

	var printed: String {
		switch self {
		case .int(let n): String(n)
		case .double(let d): javaDouble(d)
		case .string(let s): quoted(s)
		case .null: "nil"
		case .vector(let xs): "[" + xs.map(\.printed).joined(separator: " ") + "]"
		case .map(let kvs): "{" + kvs.map { "\($0.printed) \($1.printed)" }.joined(separator: ", ") + "}"
		}
	}
}

extension EDN {
	static func ints(_ xs: some Sequence<Int>) -> EDN { .vector(xs.map(EDN.int)) }
}

private func quoted(_ s: String) -> String {
	var out = "\""
	for c in s.unicodeScalars {
		switch c {
		case "\"": out += "\\\""
		case "\\": out += "\\\\"
		case "\n": out += "\\n"
		case "\t": out += "\\t"
		case "\r": out += "\\r"
		default: out.unicodeScalars.append(c)
		}
	}
	return out + "\""
}

// Java's Double.toString: plain in [1e-3, 1e7), else d.dddE±n; the digits are the shortest that round-trip.
private func javaDouble(_ d: Double) -> String {
	if d.isNaN { return "NaN" }
	if d.isInfinite { return d < 0 ? "-Infinity" : "Infinity" }
	if d == 0 { return d.sign == .minus ? "-0.0" : "0.0" }
	var text = "\(d.magnitude)"
	var exponent = 0
	if let e = text.firstIndex(of: "e") {
		exponent = Int(text[text.index(after: e)...])!
		text = String(text[..<e])
	}
	let parts = text.split(separator: ".", omittingEmptySubsequences: false)
	var digits = Array(parts[0] + (parts.count > 1 ? parts[1] : ""))
	var point = parts[0].count + exponent
	while digits.count > 1 && digits.first == "0" {
		digits.removeFirst()
		point -= 1
	}
	while digits.count > 1 && digits.last == "0" {
		digits.removeLast()
	}
	let sign = d < 0 ? "-" : ""
	if d.magnitude >= 1e-3 && d.magnitude < 1e7 {
		if point <= 0 {
			return sign + "0." + String(repeating: "0", count: -point) + String(digits)
		}
		let whole = point >= digits.count ? String(digits) + String(repeating: "0", count: point - digits.count) : String(digits[..<point])
		let fraction = point >= digits.count ? "0" : String(digits[point...])
		return sign + whole + "." + fraction
	}
	let fraction = digits.count > 1 ? String(digits[1...]) : "0"
	return sign + String(digits[0]) + "." + fraction + "E" + String(point - 1)
}
