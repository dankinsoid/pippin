// @ai-generated(solo)
import CljCore
import Pippin

/// The vars a session's dynamic frame is built from and updated through.
enum ReplVars {
	static let ns = Value(borrowing: clj_ns_var())
	static let v1 = resolve("*1")
	static let v2 = resolve("*2")
	static let v3 = resolve("*3")
	static let e = resolve("*e")
	static let printLength = resolve("*print-length*")
	static let printLevel = resolve("*print-level*")
	static let input = resolve("*in*")
	static let file = Value(borrowing: clj_load_file_var())

	private static func resolve(_ name: String) -> Value {
		withExtendedLifetime(Value(symbol: name)) { Value(borrowing: clj_ns_resolve(clj_ns_core(), $0.raw)) }
	}

	/// A fresh session's starting frame: `*ns*` at `namespace`, no REPL history yet.
	static func defaultFrame(namespace: String) -> Value {
		let nsValue = withExtendedLifetime(Value(symbol: namespace)) { Value(owning: clj_ns_find_or_create($0.raw)) }
		return withExtendedLifetime(nsValue) {
			var m = clj_map_empty()
			m = clj_map_assoc(m, ns.raw, nsValue.raw)
			// Bound at nil, like the JVM's with-bindings: set! and binding need a frame entry of their own.
			for v in [v1, v2, v3, e, printLength, printLevel, input, file] { m = clj_map_assoc(m, v.raw, CLJ_NIL) }
			return Value(owning: m)
		}
	}

	/// `*ns*` from a frame, "user" if the frame somehow lacks one (never true for a frame this module built).
	static func namespace(in frame: Value) -> String {
		let v = withExtendedLifetime(frame) { Value(borrowing: clj_map_get(frame.raw, ns.raw, CLJ_NIL)) }
		return v.isNil ? "user" : Value(borrowing: clj_ns_name(v.raw)).description
	}

	/// Vars one message binds for its own extent: nREPL scopes print options, `*file*` and `*in*` to the request.
	struct MessageBindings {
		private var pairs: [(variable: Value, value: Value)] = []

		mutating func bind(_ variable: Value, _ value: Value) { pairs.append((variable, value)) }

		// assoc and dissoc consume the map (map.h): a retain keeps the version the caller still holds.
		func applied(to frame: Value) -> Value {
			pairs.reduce(frame) { m, pair in
				Value(owning: withExtendedLifetime((m, pair.variable, pair.value)) {
					clj_map_assoc(clj_retain(m.raw), pair.variable.raw, pair.value.raw)
				})
			}
		}

		func restored(in captured: Value, from frame: Value) -> Value {
			pairs.reduce(captured) { m, pair in
				Value(owning: withExtendedLifetime((m, frame, pair.variable)) {
					clj_map_contains(frame.raw, pair.variable.raw)
						? clj_map_assoc(clj_retain(m.raw), pair.variable.raw, clj_map_get(frame.raw, pair.variable.raw, CLJ_NIL))
						: clj_map_dissoc(clj_retain(m.raw), pair.variable.raw)
				})
			}
		}
	}

	// Only the coroutine that pushed a binding may set! it (NOTES "Coroutines"); drains any failure's exception.
	private static func set(_ v: Value, _ value: Value) {
		discardResult(withExtendedLifetime((v, value)) { clj_var_set(v.raw, value.raw) })
	}

	/// Shifts `*1 *2 *3` after a form evaluates cleanly, JVM `clojure.main/repl`'s own order.
	static func recordValue(_ value: Value) {
		let old2 = Value(owning: clj_var_deref(v2.raw))
		let old1 = Value(owning: clj_var_deref(v1.raw))
		set(v3, old2)
		set(v2, old1)
		set(v1, value)
	}

	static func recordError(_ thrown: Value) { set(e, thrown) }
}
