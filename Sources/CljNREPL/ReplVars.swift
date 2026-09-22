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

	private static func resolve(_ name: String) -> Value {
		withExtendedLifetime(Value(symbol: name)) { Value(borrowing: clj_ns_resolve(clj_ns_core(), $0.raw)) }
	}

	/// A fresh session's starting frame: `*ns*` at `namespace`, no REPL history yet.
	static func defaultFrame(namespace: String) -> Value {
		let nsValue = withExtendedLifetime(Value(symbol: namespace)) { Value(owning: clj_ns_find_or_create($0.raw)) }
		return withExtendedLifetime(nsValue) {
			var m = clj_map_empty()
			m = clj_map_assoc(m, ns.raw, nsValue.raw)
			for v in [v1, v2, v3, e] { m = clj_map_assoc(m, v.raw, CLJ_NIL) }
			return Value(owning: m)
		}
	}

	/// `*ns*` from a frame, "user" if the frame somehow lacks one (never true for a frame this module built).
	static func namespace(in frame: Value) -> String {
		let v = withExtendedLifetime(frame) { Value(borrowing: clj_map_get(frame.raw, ns.raw, CLJ_NIL)) }
		return v.isNil ? "user" : Value(borrowing: clj_ns_name(v.raw)).description
	}

	// Only the coroutine that pushed a binding may set! it (NOTES "Coroutines"); drains any failure's exception.
	private static func set(_ v: Value, _ value: Value) {
		let r = withExtendedLifetime((v, value)) { clj_var_set(v.raw, value.raw) }
		if r == CLJ_THROWN {
			_ = Value(owning: clj_take_pending_trace())
			_ = Value(owning: clj_take_pending())
		} else {
			clj_release(r)
		}
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
