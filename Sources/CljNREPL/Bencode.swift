// @ai-generated(solo)

/// bencode: nREPL's wire format. Integers `i<n>e`, byte strings `<len>:<bytes>`, lists `l...e`, dicts `d...e`.
public enum BValue: Equatable, Sendable {
	case int(Int)
	case bytes([UInt8])
	case list([BValue])
	case dict([String: BValue])

	public static func string(_ s: String) -> BValue { .bytes(Array(s.utf8)) }

	public var asString: String? {
		guard case .bytes(let b) = self else { return nil }
		return String(decoding: b, as: UTF8.self)
	}

	public var asStringList: [String]? {
		guard case .list(let items) = self else { return nil }
		return items.compactMap(\.asString)
	}

	public var asInt: Int? {
		guard case .int(let n) = self else { return nil }
		return n
	}

	public var asDict: [String: BValue]? {
		guard case .dict(let d) = self else { return nil }
		return d
	}
}

public struct BencodeError: Error, CustomStringConvertible {
	public let description: String
}

public enum Bencode {
	/// Decodes one complete value from the front of `buf`; `nil` means more bytes are needed to finish it.
	public static func decode(_ buf: [UInt8]) throws -> (BValue, Int)? {
		var i = 0
		guard let v = try parse(buf, &i) else { return nil }
		return (v, i)
	}

	// `i` advances only on a value fully read; an incomplete parse leaves the caller's index untouched.
	private static func parse(_ b: [UInt8], _ i: inout Int) throws -> BValue? {
		guard i < b.count else { return nil }
		switch b[i] {
		case UInt8(ascii: "i"):
			guard let e = find(b, from: i + 1, byte: UInt8(ascii: "e")) else { return nil }
			guard let n = Int(String(decoding: b[(i + 1)..<e], as: UTF8.self)) else { throw BencodeError(description: "bad integer") }
			i = e + 1
			return .int(n)
		case UInt8(ascii: "l"):
			var items: [BValue] = []
			var j = i + 1
			while true {
				guard j < b.count else { return nil }
				if b[j] == UInt8(ascii: "e") { i = j + 1; return .list(items) }
				var k = j
				guard let v = try parse(b, &k) else { return nil }
				items.append(v)
				j = k
			}
		case UInt8(ascii: "d"):
			var items: [String: BValue] = [:]
			var j = i + 1
			while true {
				guard j < b.count else { return nil }
				if b[j] == UInt8(ascii: "e") { i = j + 1; return .dict(items) }
				var k = j
				guard let keyVal = try parse(b, &k) else { return nil }
				guard case .bytes(let keyBytes) = keyVal else { throw BencodeError(description: "dict key must be a byte string") }
				guard let v = try parse(b, &k) else { return nil }
				items[String(decoding: keyBytes, as: UTF8.self)] = v
				j = k
			}
		case let d where d >= UInt8(ascii: "0") && d <= UInt8(ascii: "9"):
			guard let colon = find(b, from: i, byte: UInt8(ascii: ":")) else { return nil }
			guard let len = Int(String(decoding: b[i..<colon], as: UTF8.self)), len >= 0 else {
				throw BencodeError(description: "bad string length")
			}
			let start = colon + 1
			guard b.count >= start + len else { return nil }
			i = start + len
			return .bytes(Array(b[start..<(start + len)]))
		default:
			throw BencodeError(description: "unexpected byte \(b[i]) at offset \(i)")
		}
	}

	private static func find(_ b: [UInt8], from: Int, byte: UInt8) -> Int? {
		var j = from
		while j < b.count {
			if b[j] == byte { return j }
			j += 1
		}
		return nil
	}

	public static func encode(_ v: BValue) -> [UInt8] {
		var out: [UInt8] = []
		write(v, into: &out)
		return out
	}

	// Dict keys sorted: bencode's canonical form, and nREPL clients rely on none of it, but it costs nothing.
	private static func write(_ v: BValue, into out: inout [UInt8]) {
		switch v {
		case .int(let n):
			out.append(contentsOf: "i\(n)e".utf8)
		case .bytes(let bytes):
			out.append(contentsOf: "\(bytes.count):".utf8)
			out.append(contentsOf: bytes)
		case .list(let items):
			out.append(UInt8(ascii: "l"))
			for item in items { write(item, into: &out) }
			out.append(UInt8(ascii: "e"))
		case .dict(let items):
			out.append(UInt8(ascii: "d"))
			for key in items.keys.sorted() {
				write(.string(key), into: &out)
				write(items[key]!, into: &out)
			}
			out.append(UInt8(ascii: "e"))
		}
	}
}
