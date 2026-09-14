// @ai-generated(guided)
import CljCore

/// A Clojure exception (`ex-info`): message, data map and an optional cause.
public struct ClojureError: Error, CustomStringConvertible {
	public let message: String
	/// A map, or nil.
	public let data: Value
	/// The causing exception value, or nil.
	public let cause: Value

	/// Wraps an exception value, sharing it with the core.
	public init(exception: Value) {
		precondition(clj_is_exception(exception.raw), "not an exception")
		let (message, data, cause) = withExtendedLifetime(exception) {
			(Value(borrowing: clj_exception_message(exception.raw)).string ?? "",
			 Value(borrowing: clj_exception_data(exception.raw)),
			 Value(borrowing: clj_exception_cause(exception.raw)))
		}
		self.message = message
		self.data = data
		self.cause = cause
	}

	public var causeError: ClojureError? { cause.isNil ? nil : ClojureError(exception: cause) }

	/// The exception pending in the calling thread; the caller has just seen CLJ_THROWN.
	static func takePending() -> ClojureError {
		let ex = clj_take_pending()
		precondition(ex != CLJ_NIL, "CLJ_THROWN without a pending exception")
		return ClojureError(exception: Value(owning: ex))
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
	/// Invokes a fn, keyword, map or vector as Clojure does; throws `ClojureError` otherwise.
	public func callAsFunction(_ args: Value...) throws -> Value {
		try withExtendedLifetime((self, args)) {
			let result = args.map(\.raw).withUnsafeBufferPointer { clj_invoke(raw, $0.baseAddress, $0.count) }
			if result == CLJ_THROWN { throw ClojureError.takePending() }
			return Value(owning: result)
		}
	}

	public var isFn: Bool { clj_is_fn(raw) }
	public var isException: Bool { clj_is_exception(raw) }
}
