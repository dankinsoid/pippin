// @ai-generated(guided)
import CljCore

/// A value thrown by Clojure code and not caught: an `ex-info`, or any other value (`throw` takes anything).
public struct ClojureError: Error, CustomStringConvertible {
	/// The thrown value itself.
	public let thrown: Value
	/// `ex-message`, or "Thrown value: <pr-str>" when the value is not an error.
	public let message: String
	/// `ex-data` (a map, or nil); the value itself when it is not an error.
	public let data: Value
	/// `ex-cause`, or nil.
	public let cause: Value

	/// Wraps a thrown value, sharing it with the core.
	public init(thrown: Value) {
		self.thrown = thrown
		if thrown.isException {
			// A deftype error's slots run Clojure code and may throw; that exception's text stands in for the field.
			func field(_ slot: (clj_value) -> clj_value) -> Value {
				let raw = slot(thrown.raw)
				return raw == CLJ_THROWN ? Value(ClojureError(thrown: Value(owning: clj_take_pending())).message) : Value(owning: raw)
			}
			(message, data, cause) = withExtendedLifetime(thrown) {
				(field(clj_ex_message).string ?? "", field(clj_ex_data), field(clj_ex_cause))
			}
		} else {
			message = "Thrown value: \(thrown)"
			data = thrown
			cause = nil
		}
	}

	public var causeError: ClojureError? { cause.isNil ? nil : ClojureError(thrown: cause) }

	/// The value pending in the calling thread as a Swift error; the caller has just seen CLJ_THROWN.
	/// A host error made from a Swift error yields that error itself, so it round-trips through Clojure code.
	static func takePending() -> any Error {
		let ex = Value(owning: clj_take_pending())
		return ex.hostError ?? ClojureError(thrown: ex)
	}

	public var description: String { message }
}

/// Reads and evaluates Clojure source in the `user` namespace.
///
/// The core is bootstrapped once per process; every instance shares it, so definitions made through one
/// are visible through all. Concurrent evaluation on several threads is not supported yet (NOTES.md).
public final class Runtime: Sendable {
	public init() { clj_init() }

	/// Evaluates every form in order and returns the last value; nil for empty input.
	/// Throws `ReaderError` for syntax errors and `ClojureError` for analysis and runtime errors.
	public func eval(_ source: String) throws -> Value {
		var bytes = Array(source.utf8)
		return try bytes.withUnsafeMutableBufferPointer { buf in
			try buf.withMemoryRebound(to: CChar.self) { chars in
				var reader = clj_reader()
				clj_reader_init(&reader, chars.baseAddress, chars.count)
				reader.resolve = clj_syntax_quote_resolve
				var last: Value = nil
				while true {
					var raw: clj_value = CLJ_NIL
					switch clj_read(&reader, &raw) {
					case CLJ_READ_EOF:
						return last
					case CLJ_READ_ERROR:
						throw ReaderError(
							message: String(cString: clj_reader_message(&reader)),
							line: Int(reader.error_line), column: Int(reader.error_col))
					default:
						let form = Value(owning: raw)
						var env = clj_env(ns: clj_ns_user(), line: reader.form_line, col: reader.form_col)
						let result = withExtendedLifetime(form) { clj_eval(form.raw, &env) }
						if result == CLJ_THROWN { throw ClojureError.takePending() }
						last = Value(owning: result)
					}
				}
			}
		}
	}
}

extension Value {
	/// Invokes a fn, keyword, map or vector as Clojure does. Throws what the call threw: the Swift error a
	/// host fn failed with, or `ClojureError` for anything thrown by Clojure code.
	public func callAsFunction(_ args: Value...) throws -> Value {
		try withExtendedLifetime((self, args)) {
			let result = args.map(\.raw).withUnsafeBufferPointer { clj_invoke(raw, $0.baseAddress, $0.count) }
			if result == CLJ_THROWN { throw ClojureError.takePending() }
			return Value(owning: result)
		}
	}

	public var isFn: Bool { clj_is_fn(raw) }
	/// Any error value: an `ex-info` or a host error.
	public var isException: Bool { clj_is_exception(raw) }
}

// Payload of a host error made from a Swift error.
private final class HostErrorBox {
	let error: any Error
	init(_ error: any Error) { self.error = error }
}

private final class NativeBody {
	let body: ([Value]) throws -> Value
	init(_ body: @escaping ([Value]) throws -> Value) { self.body = body }
}

extension Value {
	/// A host error carrying `error`: `ex-message` is `String(describing: error)`, `ex-data` is
	/// `{:host/error <this value>}`. `hostError` gives the Swift error back.
	public init(hostError error: any Error) {
		let message = Value(String(describing: error))
		let payload = Unmanaged.passRetained(HostErrorBox(error)).toOpaque()
		self.init(owning: withExtendedLifetime(message) {
			clj_host_error_new(message.raw, payload) { Unmanaged<HostErrorBox>.fromOpaque($0!).release() }
		})
	}

	/// The Swift error inside a host error; nil for every other value.
	public var hostError: (any Error)? {
		guard clj_is_host_error(raw) else { return nil }
		return withExtendedLifetime(self) { Unmanaged<HostErrorBox>.fromOpaque(clj_host_error_payload(raw)).takeUnretainedValue().error }
	}

	/// A fn backed by a Swift closure, callable from Clojure code. `arity` bounds the argument count
	/// (nil accepts any); the name appears in arity errors. The body runs on whichever thread invokes the
	/// fn. A thrown `ClojureError` rethrows its original value; any other Swift error becomes a host error,
	/// which `try`/`catch` sees as an `ExceptionInfo` and which comes back as the same Swift error when it
	/// reaches `Runtime.eval` or `callAsFunction` uncaught.
	public init(function name: String? = nil, arity: ClosedRange<Int>? = nil, _ body: @escaping ([Value]) throws -> Value) {
		let symbol = name.map { Value(symbol: $0) } ?? nil
		let ctx = Unmanaged.passRetained(NativeBody(body)).toOpaque()
		let min = UInt32(arity?.lowerBound ?? 0)
		let max = arity.map { UInt32($0.upperBound) } ?? CLJ_ARITY_ANY
		self.init(owning: withExtendedLifetime(symbol) {
			clj_fn_native_ctx(symbol.raw, Self.invokeNative, ctx, { Unmanaged<NativeBody>.fromOpaque($0!).release() }, min, max)
		})
	}

	// Swift errors cannot unwind C frames: the boundary catches everything and leaves it pending.
	private static let invokeNative: clj_native_ctx_fn = { ctx, args, n in
		let body = Unmanaged<NativeBody>.fromOpaque(ctx!).takeUnretainedValue().body
		let values = (0..<n).map { Value(borrowing: args![$0]) }
		do {
			let result = try body(values)
			return withExtendedLifetime(result) { clj_retain(result.raw) }
		} catch let e as ClojureError {
			return withExtendedLifetime(e.thrown) { clj_throw(clj_retain(e.thrown.raw)) }
		} catch {
			let wrapped = Value(hostError: error)
			return withExtendedLifetime(wrapped) { clj_throw(clj_retain(wrapped.raw)) }
		}
	}
}
