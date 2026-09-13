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
		precondition(Self.fixnumRange.contains(n), "fixnum out of range")
		self.init(owning: clj_fixnum(n))
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

	public var isNil: Bool { clj_is_nil(raw) }
	public var isTruthy: Bool { clj_truthy(raw) }

	public var int: Int? { clj_is_fixnum(raw) ? clj_fixnum_val(raw) : nil }
	public var bool: Bool? { clj_is_bool(raw) ? raw == CLJ_TRUE : nil }
	public var scalar: Unicode.Scalar? { clj_is_char(raw) ? Unicode.Scalar(clj_char_val(raw)) : nil }
	public var double: Double? { clj_is_double(raw) ? clj_double_val(raw) : nil }

	public var string: String? {
		guard clj_is_string(raw) else { return nil }
		let bytes = UnsafeRawBufferPointer(start: clj_string_bytes(raw), count: Int(clj_string_len(raw)))
		return String(decoding: bytes, as: UTF8.self)
	}

	public var typeName: String { String(cString: clj_type_name(raw)) }
}

extension Value: ExpressibleByNilLiteral, ExpressibleByBooleanLiteral, ExpressibleByIntegerLiteral,
	ExpressibleByFloatLiteral, ExpressibleByStringLiteral
{
	public init(nilLiteral: ()) { self = .nil_ }
	public init(booleanLiteral value: Bool) { self.init(value) }
	public init(integerLiteral value: Int) { self.init(value) }
	public init(floatLiteral value: Double) { self.init(value) }
	public init(stringLiteral value: String) { self.init(value) }
}
