// @ai-generated(solo)
import CljCore
import Pippin

/// Reads and evaluates `code` form by form, streaming `:out`/`:value`/`:err`; must run on a pool coroutine.
enum Evaluator {
	// CLJ_CANCELLED_MESSAGE (eval.h): how a coroutine's own cancellation throw is told apart from a real error.
	private static let cancelledMessage = "Coroutine cancelled"

	static func run(code: String, startNamespace: String, session: Session, id: String, conn: Connection, onlyLastValue: Bool = false) {
		func reply(_ extra: [String: BValue]) {
			conn.send(Ops.reply(["id": .string(id), "session": .string(session.id)], extra))
		}
		func currentNS() -> String { Value(borrowing: clj_ns_name(clj_ns_current())).description }

		// Never popped: only its owner may set! it (NOTES "Coroutines"), and the frame dies with the coroutine.
		let nsVar = Value(borrowing: clj_ns_var())
		let startValue = Value(owning: withExtendedLifetime(Value(symbol: startNamespace)) { clj_ns_find_or_create($0.raw) })
		let bindings = Value(owning: withExtendedLifetime((nsVar, startValue)) { clj_map_assoc(clj_map_empty(), nsVar.raw, startValue.raw) })
		defer { session.currentNamespace = currentNS() }
		if withExtendedLifetime(bindings, { clj_var_push_bindings(bindings.raw) }) == CLJ_THROWN {
			reportError(takePendingError(), reply)
			return
		}

		var lastValue: Value?
		var bytes = Array(code.utf8)
		bytes.withUnsafeMutableBufferPointer { buf in
			buf.withMemoryRebound(to: CChar.self) { chars in
				var reader = clj_reader()
				clj_reader_init(&reader, chars.baseAddress, chars.count)
				clj_reader_use_namespaces(&reader)
				while true {
					var raw: clj_value = CLJ_NIL
					switch clj_read(&reader, &raw) {
					case CLJ_READ_EOF:
						if onlyLastValue, let value = lastValue { reply(["value": .string(value.description), "ns": .string(currentNS())]) }
						reply(["status": .list([.string("done")])])
						return
					case CLJ_READ_ERROR:
						let message = String(cString: clj_reader_message(&reader))
						reportReaderError(message: message, line: reader.error_line, column: reader.error_col, reply)
						return
					default:
						let form = Value(owning: raw)
						clj_output_push_capture()
						var env = clj_env(ns: CLJ_NIL, line: reader.form_line, col: reader.form_col)
						let result = withExtendedLifetime(form) { clj_eval(form.raw, &env) }
						let captured = Value(owning: clj_output_pop_capture())
						if let text = captured.string, !text.isEmpty { reply(["out": .string(text)]) }
						if result == CLJ_THROWN {
							let error = takePendingError()
							if error.message == cancelledMessage {
								reply(["status": .list([.string("interrupted"), .string("done")])])
							} else {
								reportError(error, reply)
							}
							return
						}
						let value = Value(owning: result)
						if onlyLastValue {
							lastValue = value
						} else {
							reply(["value": .string(value.description), "ns": .string(currentNS())])
						}
					}
				}
			}
		}
	}

	private static func reportReaderError(message: String, line: UInt32, column: UInt32, _ reply: ([String: BValue]) -> Void) {
		reply(["err": .string("\(message) (\(line):\(column))\n"), "ex": .string("ReaderError"), "root-ex": .string("ReaderError")])
		reply(["status": .list([.string("eval-error"), .string("done")])])
	}

	private static func reportError(_ error: ClojureError, _ reply: ([String: BValue]) -> Void) {
		var root = error
		while let deeper = root.causeError { root = deeper }
		reply(["err": .string(error.description + "\n"), "ex": .string(error.thrown.typeName), "root-ex": .string(root.thrown.typeName)])
		reply(["status": .list([.string("eval-error"), .string("done")])])
	}

	// ClojureError.takePending() is internal to Pippin; drains the same two pending slots through public API.
	private static func takePendingError() -> ClojureError {
		_ = Value(owning: clj_take_pending_trace())
		return ClojureError(thrown: Value(owning: clj_take_pending()))
	}
}
