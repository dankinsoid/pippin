// @ai-generated(solo)
import CljCore
import Pippin

/// Reads and evaluates `code` form by form, streaming `:out`/`:value`/`:err`; must run on a pool coroutine.
enum Evaluator {
	static func run(code: String, overrideNamespace: String?, session: Session, id: String, conn: Connection,
	                onlyLastValue: Bool = false, bindings: ReplVars.MessageBindings = .init()) {
		func reply(_ extra: [String: BValue]) {
			conn.send(Ops.reply(["id": .string(id), "session": .string(session.id)], extra))
		}
		func currentNS() -> String { Value(borrowing: clj_ns_name(clj_ns_current())).description }

		let sessionFrame = session.currentFrame
		var frame = sessionFrame
		if let overrideNamespace {
			let nsValue = withExtendedLifetime(Value(symbol: overrideNamespace)) { Value(owning: clj_ns_find_or_create($0.raw)) }
			// The session still holds the frame this assoc would consume (map.h).
			frame = Value(owning: withExtendedLifetime((frame, nsValue)) { clj_map_assoc(clj_retain(frame.raw), ReplVars.ns.raw, nsValue.raw) })
		}
		var bindings = bindings
		bindings.bind(ReplVars.input, session.inputBinding(id: id, conn: conn))
		frame = bindings.applied(to: frame)

		var pushed = false
		var cancelled = false
		// Skipped for a cancelled eval (must not poison the session) and for a push that never took (nothing of ours to capture).
		defer {
			if pushed && !cancelled {
				session.updateFrame(bindings.restored(in: Value(owning: clj_var_get_thread_bindings()), from: sessionFrame))
			}
		}

		guard withExtendedLifetime(frame, { clj_var_push_bindings(frame.raw) }) != CLJ_THROWN else {
			reportError(takePendingError(), reply)
			return
		}
		pushed = true

		var lastValue: Value?
		var bytes = Array(code.utf8)
		bytes.withUnsafeMutableBufferPointer { buf in
			buf.withMemoryRebound(to: CChar.self) { chars in
				var reader = clj_reader()
				clj_reader_init(&reader, chars.baseAddress, chars.count)
				clj_reader_use_namespaces(&reader)
				// One :out per write, and a write is one print call (builtins.c print_line).
				pushOutputStream { text in reply(["out": .string(text)]) }
				// A stream keeps nothing; the pop releases it, and every exit from the loop must reach it.
				defer { _ = Value(owning: clj_output_pop_capture()) }
				while true {
					var raw: clj_value = CLJ_NIL
					switch clj_read(&reader, &raw) {
					case CLJ_READ_EOF:
						if onlyLastValue, let value = lastValue { reply(["value": .string(printed(value)), "ns": .string(currentNS())]) }
						reply(["status": .list([.string("done")])])
						return
					case CLJ_READ_ERROR:
						let message = String(cString: clj_reader_message(&reader))
						reportReaderError(message: message, line: reader.error_line, column: reader.error_col, reply)
						return
					default:
						let form = Value(owning: raw)
						var env = clj_env(ns: CLJ_NIL, line: reader.form_line, col: reader.form_col)
						let result = withExtendedLifetime(form) { clj_eval(form.raw, &env) }
						if result == CLJ_THROWN {
							let error = takePendingError()
							if clj_coro_current_cancelled() {
								cancelled = true
								reply(["status": .list([.string("interrupted"), .string("done")])])
							} else {
								ReplVars.recordError(error.thrown)
								reportError(error, reply)
							}
							return
						}
						let value = Value(owning: result)
						ReplVars.recordValue(value)
						if onlyLastValue {
							lastValue = value
						} else {
							reply(["value": .string(printed(value)), "ns": .string(currentNS())])
						}
					}
				}
			}
		}
	}

	// pr under the frame's own *print-length*/*print-level*, which is what a client asking for them expects.
	private static func printed(_ value: Value) -> String {
		let text = withExtendedLifetime(value) { clj_pr_str_dynamic(value.raw, true) }
		guard text != CLJ_THROWN else { return "#<printing threw: \(takePendingError().description)>" }
		return Value(owning: text).string ?? ""
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
