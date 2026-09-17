// @ai-generated(guided)
import CljCore

/// The single type that crosses the Swift ↔ core boundary.
/// Immediates live inline; heap values are held through `Ref`, whose deinit releases them.
public struct Value: Sendable {
	public let raw: clj_value
	private let ref: Ref?

	/// Swift may drop the reference on any thread, so the core object must already be atomic.
	private final class Ref: @unchecked Sendable {
		let raw: clj_value
		init(owning raw: clj_value) { self.raw = raw }
		deinit { clj_release(raw) }
	}

	/// Takes over a +1 reference and marks the reachable graph shared.
	public init(owning raw: clj_value) {
		self.raw = raw
		if clj_is_ptr(raw) {
			clj_share(raw)
			ref = Ref(owning: raw)
		} else {
			ref = nil
		}
	}

	public init(borrowing raw: clj_value) {
		self.init(owning: clj_retain(raw))
	}

	public static let nil_ = Value(owning: CLJ_NIL)
	public static let fixnumRange = CLJ_FIXNUM_MIN...CLJ_FIXNUM_MAX

	public init(_ b: Bool) {
		self.init(owning: clj_bool(b))
	}

	public init(_ n: Int) {
		self.init(owning: clj_long_new(Int64(n)))
	}

	public init(_ c: Unicode.Scalar) {
		self.init(owning: clj_char(c.value))
	}

	public init(_ s: String) {
		var s = s
		self.init(owning: s.withUTF8 { clj_string_new($0.baseAddress, $0.count) })
	}

	public init(_ d: Double) {
		self.init(owning: clj_double_new(d))
	}

	/// "ns/name" splits at the first slash, as the reader would.
	public init(keyword text: String) {
		self.init(owning: clj_keyword_from_cstr(text))
	}

	public init(symbol text: String) {
		self.init(owning: clj_symbol_from_cstr(text))
	}

	/// A persistent vector of the elements.
	public init(_ array: [Value]) {
		// The raw words are borrowed from `array`, which must outlive the C call.
		self.init(owning: withExtendedLifetime(array) {
			array.map(\.raw).withUnsafeBufferPointer { clj_vector_from_array($0.baseAddress, UInt32($0.count)) }
		})
	}

	/// A list of the elements, ending in the empty list.
	public init(list elements: [Value]) {
		self.init(owning: withExtendedLifetime(elements) {
			elements.map(\.raw).withUnsafeBufferPointer { clj_list_from_array($0.baseAddress, $0.count) }
		})
	}

	/// A persistent map of the entries.
	public init(_ dictionary: [Value: Value]) {
		self.init(owning: withExtendedLifetime(dictionary) {
			dictionary.reduce(clj_map_empty()) { clj_map_assoc($0, $1.key.raw, $1.value.raw) }
		})
	}

	public var isNil: Bool { clj_is_nil(raw) }
	public var isTruthy: Bool { clj_truthy(raw) }

	public var int: Int? {
		var i: Int64 = 0
		return withExtendedLifetime(self) { clj_int64_of(raw, &i) ? Int(i) : nil }
	}
	public var bool: Bool? { clj_is_bool(raw) ? raw == CLJ_TRUE : nil }
	public var scalar: Unicode.Scalar? { clj_is_char(raw) ? Unicode.Scalar(clj_char_val(raw)) : nil }
	public var double: Double? { clj_is_double(raw) ? clj_double_val(raw) : nil }

	public var string: String? {
		guard clj_is_string(raw) else { return nil }
		return withExtendedLifetime(self) { Self.decode(raw) }
	}

	// `s` must be a string the caller keeps alive.
	private static func decode(_ s: clj_value) -> String {
		String(decoding: UnsafeRawBufferPointer(start: clj_string_bytes(s), count: Int(clj_string_len(s))), as: UTF8.self)
	}

	private static func qualifiedName(ns: clj_value, name: clj_value) -> String {
		clj_is_nil(ns) ? decode(name) : decode(ns) + "/" + decode(name)
	}

	/// The elements of a vector, in order; nil for any other value.
	public var array: [Value]? {
		guard clj_is_vector(raw) else { return nil }
		var out: [Value] = []
		out.reserveCapacity(Int(clj_vector_count(raw)))
		withExtendedLifetime(self) {
			withUnsafeMutablePointer(to: &out) { p in
				clj_vector_each(raw, { item, ctx in
					ctx!.assumingMemoryBound(to: [Value].self).pointee.append(Value(borrowing: item))
					return true
				}, p)
			}
		}
		return out
	}

	/// The elements of any seq (a list, a lazy seq, a range, a seq view), in order; nil for any other value.
	/// Walking realizes a lazy seq; a thunk that throws ends the walk early.
	public var list: [Value]? {
		guard clj_is_seq(raw) else { return nil }
		var out: [Value] = []
		withExtendedLifetime(self) {
			var it = clj_seq_iter_start(raw)
			var item: clj_value = CLJ_NIL
			while clj_seq_iter_next(&it, &item) { out.append(Value(borrowing: item)) }
		}
		return out
	}

	/// The entries of a map; nil for any other value.
	public var dictionary: [Value: Value]? {
		guard clj_is_map(raw) else { return nil }
		var out: [Value: Value] = [:]
		out.reserveCapacity(Int(clj_map_count(raw)))
		withExtendedLifetime(self) {
			withUnsafeMutablePointer(to: &out) { p in
				clj_map_each(raw, { key, val, ctx in
					ctx!.assumingMemoryBound(to: [Value: Value].self).pointee[Value(borrowing: key)] = Value(borrowing: val)
					return true
				}, p)
			}
		}
		return out
	}

	public var typeName: String { String(cString: clj_type_name(raw)) }
}

extension Value {
	/// A `switch`-friendly view. Scalars come out unboxed, collections copied out;
	/// keywords and symbols carry their qualified name so string patterns match them.
	public enum Kind: Sendable {
		case `nil`
		case bool(Bool)
		case int(Int)
		case char(Unicode.Scalar)
		case double(Double)
		case string(String)
		/// "ns/name" or "name", without the leading colon.
		case keyword(String)
		case symbol(String)
		case vector([Value])
		case list([Value])
		case map([Value: Value])
		/// fn, var, namespace, exception or a user type; `typeName` tells which.
		case object(Value)
	}

	public var kind: Kind {
		if let n = int { return .int(n) }
		if isNil { return .nil }
		if let b = bool { return .bool(b) }
		if let c = scalar { return .char(c) }
		if let d = double { return .double(d) }
		if let s = string { return .string(s) }
		if let a = array { return .vector(a) }
		if let l = list { return .list(l) }
		if let m = dictionary { return .map(m) }
		return withExtendedLifetime(self) {
			if clj_is_keyword(raw) { return .keyword(Self.qualifiedName(ns: clj_keyword_ns(raw), name: clj_keyword_name(raw))) }
			if clj_is_symbol(raw) { return .symbol(Self.qualifiedName(ns: clj_symbol_ns(raw), name: clj_symbol_name(raw))) }
			return .object(self)
		}
	}
}

/// A syntax error from the reader; line and column are 1-based, the column counts code points.
public struct ReaderError: Error, Equatable, CustomStringConvertible {
	public let message: String
	public let line: Int
	public let column: Int

	public init(message: String, line: Int, column: Int) {
		self.message = message
		self.line = line
		self.column = column
	}

	public var description: String { "\(line):\(column): \(message)" }
}

extension Value {
	/// Reads exactly one form; anything but whitespace and comments after it is an error.
	public init(reading text: String) throws {
		var first: Value?
		try Self.read(text) { value, reader in
			if first == nil {
				first = value
				return true
			}
			throw ReaderError(message: "Unexpected trailing input", line: Int(reader.form_line), column: Int(reader.form_col))
		}
		guard let first else { throw ReaderError(message: "EOF while reading", line: 1, column: 1) }
		self = first
	}

	/// Every form in the text, in order.
	public static func readAll(_ text: String) throws -> [Value] {
		var out: [Value] = []
		try read(text) { value, _ in
			out.append(value)
			return true
		}
		return out
	}

	// The body returns false to stop; a thrown error propagates after the reader is torn down.
	private static func read(_ text: String, _ body: (Value, clj_reader) throws -> Bool) throws {
		var bytes = Array(text.utf8)
		try bytes.withUnsafeMutableBufferPointer { buf in
			try buf.withMemoryRebound(to: CChar.self) { chars in
				var reader = clj_reader()
				clj_reader_init(&reader, chars.baseAddress, chars.count)
				clj_reader_use_namespaces(&reader)
				while true {
					var raw: clj_value = CLJ_NIL
					switch clj_read(&reader, &raw) {
					case CLJ_READ_EOF:
						return
					case CLJ_READ_ERROR:
						throw ReaderError(
							message: String(cString: clj_reader_message(&reader)),
							line: Int(reader.error_line), column: Int(reader.error_col))
					default:
						if try !body(Value(owning: raw), reader) { return }
					}
				}
			}
		}
	}
}

extension Value: CustomStringConvertible {
	/// Clojure `pr-str`. Printing realizes lazy seqs; when a thunk throws, the exception's text stands in.
	public var description: String {
		withExtendedLifetime(self) {
			let s = clj_pr_str(raw)
			if s == CLJ_THROWN { return "#<lazy-seq threw: \(Value(owning: clj_take_pending()))>" }
			defer { clj_release(s) }
			return Self.decode(s)
		}
	}
}

extension Value: ExpressibleByNilLiteral, ExpressibleByBooleanLiteral, ExpressibleByIntegerLiteral,
	ExpressibleByFloatLiteral, ExpressibleByStringLiteral, ExpressibleByArrayLiteral, ExpressibleByDictionaryLiteral
{
	public init(nilLiteral: ()) { self = .nil_ }
	public init(booleanLiteral value: Bool) { self.init(value) }
	public init(integerLiteral value: Int) { self.init(value) }
	public init(floatLiteral value: Double) { self.init(value) }
	public init(stringLiteral value: String) { self.init(value) }
	public init(arrayLiteral elements: Value...) { self.init(elements) }
	public init(dictionaryLiteral elements: (Value, Value)...) { self.init(Dictionary(uniqueKeysWithValues: elements)) }
}

// Clojure `=` and `hash`: Value(1) != Value(1.0), NaN != NaN, as with Swift's Double.
extension Value: Hashable {
	public static func == (lhs: Value, rhs: Value) -> Bool {
		withExtendedLifetime((lhs, rhs)) { clj_equals(lhs.raw, rhs.raw) }
	}

	public func hash(into hasher: inout Hasher) {
		withExtendedLifetime(self) { hasher.combine(clj_hash(raw)) }
	}
}
