// @ai-generated(guided)
import CljCore

/// The single type that crosses the Swift ↔ core boundary.
/// Retain/release hooks arrive together with the core's RC; until then only immediates are safe to hold.
public struct Value: Sendable {
	public let raw: clj_value

	public init(raw: clj_value) {
		self.raw = raw
	}

	public static let nil_ = Value(raw: CLJ_NIL)
	public static let fixnumRange = CLJ_FIXNUM_MIN...CLJ_FIXNUM_MAX

	public init(_ b: Bool) {
		raw = clj_bool(b)
	}

	public init(_ n: Int) {
		precondition(Self.fixnumRange.contains(n), "fixnum out of range")
		raw = clj_fixnum(n)
	}

	public init(_ c: Unicode.Scalar) {
		raw = clj_char(c.value)
	}

	public var isNil: Bool { clj_is_nil(raw) }
	public var isTruthy: Bool { clj_truthy(raw) }

	public var int: Int? { clj_is_fixnum(raw) ? clj_fixnum_val(raw) : nil }
	public var bool: Bool? { clj_is_bool(raw) ? raw == CLJ_TRUE : nil }
	public var scalar: Unicode.Scalar? { clj_is_char(raw) ? Unicode.Scalar(clj_char_val(raw)) : nil }

	public var typeName: String { String(cString: clj_type_name(raw)) }
}

extension Value: ExpressibleByNilLiteral, ExpressibleByBooleanLiteral, ExpressibleByIntegerLiteral {
	public init(nilLiteral: ()) { self = .nil_ }
	public init(booleanLiteral value: Bool) { self.init(value) }
	public init(integerLiteral value: Int) { self.init(value) }
}
