// @ai-generated(guided)
import CljCore

/// A value thrown by Clojure code and not caught: an `ex-info`, or any other value (`throw` takes anything).
public struct ClojureError: Error, CustomStringConvertible {
	/// One Clojure fn on the stack at the throw: its name and where it was called (its own position when
	/// the call came from the host or a native).
	public struct Frame: Equatable, Sendable {
		public let fn: String?
		public let line: Int
		public let column: Int
	}

	/// The thrown value itself.
	public let thrown: Value
	/// `ex-message`, or "Thrown value: <pr-str>" when the value is not an error.
	public let message: String
	/// `ex-data` (a map, or nil); the value itself when it is not an error.
	public let data: Value
	/// `ex-cause`, or nil.
	public let cause: Value
	/// The Clojure frames at the throw, innermost first; empty for a throw at top level.
	public let trace: [Frame]
	// The trace as Clojure data, so a rethrow from a host fn keeps the frames of a non-error value.
	let traceValue: Value

	/// Wraps a thrown value, sharing it with the core; the trace is the one on an `ex-info`, else none.
	public init(thrown: Value) {
		self.init(thrown: thrown, trace: thrown.isException ? Value(owning: withExtendedLifetime(thrown) { clj_ex_trace(thrown.raw) }) : nil)
	}

	init(thrown: Value, trace: Value) {
		self.thrown = thrown
		traceValue = trace
		self.trace = (trace.array ?? []).map { m in
			withExtendedLifetime(m) {
				let fn = Value(borrowing: clj_map_get(m.raw, clj_keyword_from_cstr("fn"), CLJ_NIL))
				let line = clj_map_get(m.raw, clj_keyword_from_cstr("line"), CLJ_NIL)
				let column = clj_map_get(m.raw, clj_keyword_from_cstr("column"), CLJ_NIL)
				return Frame(fn: fn.isNil ? nil : fn.description, line: clj_is_fixnum(line) ? clj_fixnum_val(line) : 0,
				             column: clj_is_fixnum(column) ? clj_fixnum_val(column) : 0)
			}
		}
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
		let trace = Value(owning: clj_take_pending_trace())
		let ex = Value(owning: clj_take_pending())
		return ex.hostError ?? ClojureError(thrown: ex, trace: trace)
	}

	/// The message, then one line per frame: `at fn (line:column)`, innermost first.
	public var description: String {
		trace.reduce(message) { "\($0)\n\tat \($1.fn ?? "fn") (\($1.line):\($1.column))" }
	}
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

	/// Clojure `meta`: the metadata map, or nil for a value without one (or without a meta slot).
	public var meta: Value {
		withExtendedLifetime(self) { Value(owning: clj_meta(raw)) }
	}

	/// Clojure `with-meta`: the same value carrying `m` (a map or nil) as its metadata. Throws `ClojureError`
	/// for a value that does not support metadata (a string, a keyword, a number, a seq view, a var).
	public func withMeta(_ m: Value) throws -> Value {
		try withExtendedLifetime((self, m)) {
			let result = clj_with_meta(clj_retain(raw), m.raw)
			if result == CLJ_THROWN { throw ClojureError.takePending() }
			return Value(owning: result)
		}
	}
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
			return withExtendedLifetime((e.thrown, e.traceValue)) { clj_throw_traced(clj_retain(e.thrown.raw), clj_retain(e.traceValue.raw)) }
		} catch {
			let wrapped = Value(hostError: error)
			return withExtendedLifetime(wrapped) { clj_throw(clj_retain(wrapped.raw)) }
		}
	}
}
