// @ai-generated(solo)
import CljCore
import Foundation

/// A Swift type a typed adapter passes into Clojure; unrelated to `Encodable`.
public protocol ValueEncodable: SendableMetatype {
	var asValue: Value { get }
}

/// A Swift type a typed adapter reads a Clojure result as; a value of another kind throws.
public protocol ValueDecodable: SendableMetatype {
	init(decoding value: Value) throws
}

public typealias ValueCodable = ValueEncodable & ValueDecodable

/// A value a typed adapter's result type cannot hold.
public struct ValueTypeMismatch: Error, CustomStringConvertible {
	public let value: Value
	public let expected: String

	public var description: String { "\(value) (\(value.typeName)) is not a \(expected)" }
}

/// A typed adapter refused at creation: not a fn, or not of that arity. Checked here and never again (§5).
public struct ClosureSignatureMismatch: Error, CustomStringConvertible {
	public let fn: Value
	public let arity: Int

	public var description: String {
		fn.isFn ? "\(fn) does not take \(arity) argument\(arity == 1 ? "" : "s")" : "\(fn) is not a fn"
	}
}

extension Value: ValueCodable {
	public var asValue: Value { self }
	public init(decoding value: Value) { self = value }
}

extension Int: ValueCodable {
	public var asValue: Value { Value(self) }
	public init(decoding value: Value) throws {
		guard let n = value.int else { throw ValueTypeMismatch(value: value, expected: "Int") }
		self = n
	}
}

extension Double: ValueCodable {
	public var asValue: Value { Value(self) }
	public init(decoding value: Value) throws {
		guard let d = value.double else { throw ValueTypeMismatch(value: value, expected: "Double") }
		self = d
	}
}

extension String: ValueCodable {
	public var asValue: Value { Value(self) }
	public init(decoding value: Value) throws {
		guard let s = value.string else { throw ValueTypeMismatch(value: value, expected: "String") }
		self = s
	}
}

extension Unicode.Scalar: ValueCodable {
	public var asValue: Value { Value(self) }
	public init(decoding value: Value) throws {
		guard let c = value.scalar else { throw ValueTypeMismatch(value: value, expected: "Unicode.Scalar") }
		self = c
	}
}

extension Bool: ValueCodable {
	public var asValue: Value { Value(self) }
	/// Clojure truthiness: everything but nil and false is true, so a Bool result never fails to decode.
	public init(decoding value: Value) { self = value.isTruthy }
}

// A fixed-width integer is a long in range: a value past the type's ends is an error, never a wrap or a trap.
public protocol ValueFixedWidthInteger: FixedWidthInteger, ValueCodable {}

extension ValueFixedWidthInteger {
	public var asValue: Value {
		if let n = Int64(exactly: self) { return Value(owning: clj_long_new(n)) }
		let text = String(self)
		return Value(owning: text.withCString { clj_bigint_parse($0, text.utf8.count, 10) })
	}

	public init(decoding value: Value) throws {
		if let n = value.int, let v = Self(exactly: n) {
			self = v
			return
		}
		// Only a UInt64 or UInt holds a value past a long, and such a value arrives as a bigint.
		if clj_is_bigint(value.raw), let text = (withExtendedLifetime(value) { Value(owning: clj_bigint_to_string(value.raw)) }).string,
		   let v = Self(text) {
			self = v
			return
		}
		throw ValueTypeMismatch(value: value, expected: "\(Self.self) (\(Self.min)...\(Self.max))")
	}
}

extension Int8: ValueFixedWidthInteger {}
extension Int16: ValueFixedWidthInteger {}
extension Int32: ValueFixedWidthInteger {}
extension Int64: ValueFixedWidthInteger {}
extension UInt8: ValueFixedWidthInteger {}
extension UInt16: ValueFixedWidthInteger {}
extension UInt32: ValueFixedWidthInteger {}
extension UInt64: ValueFixedWidthInteger {}
extension UInt: ValueFixedWidthInteger {}

extension Float: ValueCodable {
	public var asValue: Value { Value(Double(self)) }
	/// A double that rounds to a Float; one past its range is an error, not an infinity.
	public init(decoding value: Value) throws {
		guard let d = value.double, d.isNaN || d.isInfinite || Float(d).isFinite else {
			throw ValueTypeMismatch(value: value, expected: "Float")
		}
		self = Float(d)
	}
}

extension CGFloat: ValueCodable {
	public var asValue: Value { Value(Double(self)) }
	public init(decoding value: Value) throws {
		guard let d = value.double else { throw ValueTypeMismatch(value: value, expected: "CGFloat") }
		self = CGFloat(d)
	}
}

// Foundation's value types cross as the core's own values (design §5 «Значения ядра на границе»).

extension Date: ValueCodable {
	/// An inst holds whole milliseconds: the sub-millisecond rest is dropped, toward the past.
	public var asValue: Value {
		Value(owning: clj_inst_new(Int64((timeIntervalSince1970 * 1000).rounded(.down))))
	}

	public init(decoding value: Value) throws {
		guard clj_is_inst(value.raw) else { throw ValueTypeMismatch(value: value, expected: "inst") }
		self = Date(timeIntervalSince1970: Double(withExtendedLifetime(value) { clj_inst_ms(value.raw) }) / 1000)
	}
}

extension UUID: ValueCodable {
	public var asValue: Value {
		let b = uuid
		let hi = [b.0, b.1, b.2, b.3, b.4, b.5, b.6, b.7].reduce(UInt64(0)) { $0 << 8 | UInt64($1) }
		let lo = [b.8, b.9, b.10, b.11, b.12, b.13, b.14, b.15].reduce(UInt64(0)) { $0 << 8 | UInt64($1) }
		return Value(owning: clj_uuid_new(Int64(bitPattern: hi), Int64(bitPattern: lo)))
	}

	public init(decoding value: Value) throws {
		guard clj_is_uuid(value.raw) else { throw ValueTypeMismatch(value: value, expected: "uuid") }
		let (hi, lo) = withExtendedLifetime(value) {
			(UInt64(bitPattern: clj_uuid_of(value.raw).pointee.hi), UInt64(bitPattern: clj_uuid_of(value.raw).pointee.lo))
		}
		func byte(_ half: UInt64, _ i: Int) -> UInt8 { UInt8(truncatingIfNeeded: half >> (56 - 8 * i)) }
		self = UUID(uuid: (byte(hi, 0), byte(hi, 1), byte(hi, 2), byte(hi, 3), byte(hi, 4), byte(hi, 5), byte(hi, 6), byte(hi, 7),
		                   byte(lo, 0), byte(lo, 1), byte(lo, 2), byte(lo, 3), byte(lo, 4), byte(lo, 5), byte(lo, 6), byte(lo, 7)))
	}
}

extension URL: ValueCodable {
	/// The absolute text as a URI; one the RFC 3986 parse refuses stays a box, as level 1 keeps such an NSURL a handle.
	public var asValue: Value {
		let text = Value(absoluteString)
		let uri = Value(owning: withExtendedLifetime(text) { clj_uri_parse(text.raw) })
		return uri.isNil ? SwiftStubs.box(self) : uri
	}

	public init(decoding value: Value) throws {
		if !clj_is_uri(value.raw) {
			self = try SwiftStubs.unbox(value, as: URL.self)
			return
		}
		let text = withExtendedLifetime(value) { Value(borrowing: clj_uri_text(value.raw)).string ?? "" }
		guard let url = URL(string: text) else { throw ValueTypeMismatch(value: value, expected: "URL") }
		self = url
	}
}

extension Optional: ValueEncodable where Wrapped: ValueEncodable {
	public var asValue: Value { self?.asValue ?? .nil_ }
}

extension Optional: ValueDecodable where Wrapped: ValueDecodable {
	/// Clojure nil is Swift's nil — for `Bool?` too, which is therefore three-valued where `Bool` is not.
	public init(decoding value: Value) throws { self = value.isNil ? nil : try Wrapped(decoding: value) }
}

extension Array: ValueEncodable where Element: ValueEncodable {
	public var asValue: Value { Value(map(\.asValue)) }
}

extension Array: ValueDecodable where Element: ValueDecodable {
	/// A vector or any seq; realizing a lazy seq is the caller's O(n), as `ns-array` is in the other direction.
	public init(decoding value: Value) throws {
		guard let items = value.array ?? value.list else {
			throw ValueTypeMismatch(value: value, expected: "Array")
		}
		self = try items.map { try Element(decoding: $0) }
	}
}

/// Typed adapters over the canonical `Value` form, for a caller that knows the signature (design §5).
///
/// The arity is checked against the fn once, at creation, and the conversions are the generic parameters'
/// own: a call is the conversions plus the invoke, with no check of its own. The adapter holds the `Value`
/// (+1 → SHARED, atomic count), so it may be called from any thread and outlive what handed it over.
///
/// A `throws` adapter delivers `ClojureError`, the Swift error a host fn failed with, or `ValueTypeMismatch`
/// for a result of the wrong kind; a non-`throws` one takes `onFailure` (`Value.trap`, `Value.report`).
extension Value {
	public func closure<R: ValueDecodable>() throws -> @Sendable () throws -> R {
		let fn = try checkedFn(0)
		return { try R(decoding: fn.apply([])) }
	}

	public func closure<A: ValueEncodable, R: ValueDecodable>() throws -> @Sendable (A) throws -> R {
		let fn = try checkedFn(1)
		return { try R(decoding: fn.apply([$0.asValue])) }
	}

	public func closure<A: ValueEncodable, B: ValueEncodable, R: ValueDecodable>() throws -> @Sendable (A, B) throws -> R {
		let fn = try checkedFn(2)
		return { try R(decoding: fn.apply([$0.asValue, $1.asValue])) }
	}

	public func closure<A: ValueEncodable, B: ValueEncodable, C: ValueEncodable, R: ValueDecodable>() throws -> @Sendable (A, B, C) throws -> R {
		let fn = try checkedFn(3)
		return { try R(decoding: fn.apply([$0.asValue, $1.asValue, $2.asValue])) }
	}

	// The result is dropped, so no result type is inferred and `Void` needs no conformance (it can have none).
	public func closure() throws -> @Sendable () throws -> Void {
		let fn = try checkedFn(0)
		return { _ = try fn.apply([]) }
	}

	public func closure<A: ValueEncodable>() throws -> @Sendable (A) throws -> Void {
		let fn = try checkedFn(1)
		return { _ = try fn.apply([$0.asValue]) }
	}

	public func closure<A: ValueEncodable, B: ValueEncodable>() throws -> @Sendable (A, B) throws -> Void {
		let fn = try checkedFn(2)
		return { _ = try fn.apply([$0.asValue, $1.asValue]) }
	}

	public func closure<A: ValueEncodable, B: ValueEncodable, C: ValueEncodable>() throws -> @Sendable (A, B, C) throws -> Void {
		let fn = try checkedFn(3)
		return { _ = try fn.apply([$0.asValue, $1.asValue, $2.asValue]) }
	}
}

// A UIKit signature has nowhere to put an error, so the stub says here what a failure does. `onFailure` has
// no default: one would make `closure()` ambiguous, a non-throwing function type converting to a throwing one.
extension Value {
	public func closure<R: ValueDecodable>(onFailure: @escaping @Sendable (any Error) -> R) throws -> @Sendable () -> R {
		let call: @Sendable () throws -> R = try closure()
		return { do { return try call() } catch { return onFailure(error) } }
	}

	public func closure<A: ValueEncodable, R: ValueDecodable>(onFailure: @escaping @Sendable (any Error) -> R) throws -> @Sendable (A) -> R {
		let call: @Sendable (A) throws -> R = try closure()
		return { do { return try call($0) } catch { return onFailure(error) } }
	}

	public func closure<A: ValueEncodable, B: ValueEncodable, R: ValueDecodable>(onFailure: @escaping @Sendable (any Error) -> R) throws -> @Sendable (A, B) -> R {
		let call: @Sendable (A, B) throws -> R = try closure()
		return { do { return try call($0, $1) } catch { return onFailure(error) } }
	}

	public func closure<A: ValueEncodable, B: ValueEncodable, C: ValueEncodable, R: ValueDecodable>(onFailure: @escaping @Sendable (any Error) -> R) throws -> @Sendable (A, B, C) -> R {
		let call: @Sendable (A, B, C) throws -> R = try closure()
		return { do { return try call($0, $1, $2) } catch { return onFailure(error) } }
	}

	public func closure(onFailure: @escaping @Sendable (any Error) -> Void) throws -> @Sendable () -> Void {
		let call: @Sendable () throws -> Void = try closure()
		return { do { try call() } catch { onFailure(error) } }
	}

	public func closure<A: ValueEncodable>(onFailure: @escaping @Sendable (any Error) -> Void) throws -> @Sendable (A) -> Void {
		let call: @Sendable (A) throws -> Void = try closure()
		return { do { try call($0) } catch { onFailure(error) } }
	}

	public func closure<A: ValueEncodable, B: ValueEncodable>(onFailure: @escaping @Sendable (any Error) -> Void) throws -> @Sendable (A, B) -> Void {
		let call: @Sendable (A, B) throws -> Void = try closure()
		return { do { try call($0, $1) } catch { onFailure(error) } }
	}

	public func closure<A: ValueEncodable, B: ValueEncodable, C: ValueEncodable>(onFailure: @escaping @Sendable (any Error) -> Void) throws -> @Sendable (A, B, C) -> Void {
		let call: @Sendable (A, B, C) throws -> Void = try closure()
		return { do { try call($0, $1, $2) } catch { onFailure(error) } }
	}

	/// The dev policy of design §5: a failed host callback is a crash with the Clojure trace in the message,
	/// because the host signature cannot carry the error and a swallowed one is a wrong value later.
	@Sendable
	public static func trap<R>(_ error: any Error) -> R {
		fatalError("Clojure closure failed: \(error)")
	}

	/// The release policy of design §5: the failure is logged (stderr, as an uncaught coroutine error is) and
	/// the callback answers `value`. In a debug build it still traps, so a stub written once behaves as the
	/// design says in both.
	public static func report<R: Sendable>(default value: R) -> @Sendable (any Error) -> R {
		{ error in
			#if DEBUG
				return trap(error)
			#else
				FileHandle.standardError.write(Data("Clojure closure failed: \(error)\n".utf8))
				return value
			#endif
		}
	}
}

/// The `async` typed adapters: the body runs on a fresh coroutine (`callAsync`), so Clojure code inside may
/// park freely, and the awaiting Task's cancellation cancels it. `affinity` picks the carrier, as on
/// `callAsync` itself.
extension Value {
	public func closureAsync<R: ValueDecodable>(affinity: Affinity = .pool) throws -> @Sendable () async throws -> R {
		let fn = try checkedFn(0)
		return { try R(decoding: await fn.applyAsync([], affinity: affinity)) }
	}

	public func closureAsync<A: ValueEncodable, R: ValueDecodable>(affinity: Affinity = .pool) throws -> @Sendable (A) async throws -> R {
		let fn = try checkedFn(1)
		return { try R(decoding: await fn.applyAsync([$0.asValue], affinity: affinity)) }
	}

	public func closureAsync<A: ValueEncodable, B: ValueEncodable, R: ValueDecodable>(affinity: Affinity = .pool) throws -> @Sendable (A, B) async throws -> R {
		let fn = try checkedFn(2)
		return { try R(decoding: await fn.applyAsync([$0.asValue, $1.asValue], affinity: affinity)) }
	}

	public func closureAsync<A: ValueEncodable, B: ValueEncodable, C: ValueEncodable, R: ValueDecodable>(affinity: Affinity = .pool) throws -> @Sendable (A, B, C) async throws -> R {
		let fn = try checkedFn(3)
		return { try R(decoding: await fn.applyAsync([$0.asValue, $1.asValue, $2.asValue], affinity: affinity)) }
	}

	public func closureAsync(affinity: Affinity = .pool) throws -> @Sendable () async throws -> Void {
		let fn = try checkedFn(0)
		return { _ = try await fn.applyAsync([], affinity: affinity) }
	}

	public func closureAsync<A: ValueEncodable>(affinity: Affinity = .pool) throws -> @Sendable (A) async throws -> Void {
		let fn = try checkedFn(1)
		return { _ = try await fn.applyAsync([$0.asValue], affinity: affinity) }
	}

	public func closureAsync<A: ValueEncodable, B: ValueEncodable>(affinity: Affinity = .pool) throws -> @Sendable (A, B) async throws -> Void {
		let fn = try checkedFn(2)
		return { _ = try await fn.applyAsync([$0.asValue, $1.asValue], affinity: affinity) }
	}

	public func closureAsync<A: ValueEncodable, B: ValueEncodable, C: ValueEncodable>(affinity: Affinity = .pool) throws -> @Sendable (A, B, C) async throws -> Void {
		let fn = try checkedFn(3)
		return { _ = try await fn.applyAsync([$0.asValue, $1.asValue, $2.asValue], affinity: affinity) }
	}
}

extension Value {
	// The one check of the feature. A keyword, a map or a vector is invokable but has no arity to check, so
	// it is refused here rather than silently checked on every call.
	private func checkedFn(_ arity: Int) throws -> Value {
		guard isFn, withExtendedLifetime(self, { clj_fn_accepts(raw, arity) }) else {
			throw ClosureSignatureMismatch(fn: self, arity: arity)
		}
		return self
	}
}
