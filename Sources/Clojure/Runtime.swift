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
			(message, data, cause) = withExtendedLifetime(thrown) {
				(Value(owning: clj_ex_message(thrown.raw)).string ?? "",
				 Value(owning: clj_ex_data(thrown.raw)),
				 Value(owning: clj_ex_cause(thrown.raw)))
			}
		} else {
			message = "Thrown value: \(thrown)"
			data = thrown
			cause = nil
		}
	}

	public var causeError: ClojureError? { cause.isNil ? nil : ClojureError(thrown: cause) }

	/// The value pending in the calling thread as a Swift error; the caller has just seen CLJ_THROWN.
	static func takePending() -> any Error {
		let ex = clj_take_pending()
		precondition(ex != CLJ_NIL, "CLJ_THROWN without a pending exception")
		return ClojureError(thrown: Value(owning: ex))
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
