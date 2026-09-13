// @ai-generated(guided)
import CljCore
@testable import Clojure

struct CljEvalFailure: Error {
	let message: String
}

// Evaluates every form in the current namespace through the C API and returns the last value.
func cljEval(_ source: String) throws -> Value {
	clj_init()
	var last: Value = nil
	for form in try Value.readAll(source) {
		let raw = withExtendedLifetime(form) { clj_eval(form.raw, nil) }
		if raw == CLJ_THROWN {
			let ex = Value(owning: clj_take_pending())
			throw CljEvalFailure(message: ex.description)
		}
		last = Value(owning: raw)
	}
	return last
}

// The printed exception, or nil when the source evaluates.
func cljEvalError(_ source: String) -> String? {
	do {
		_ = try cljEval(source)
		return nil
	} catch let e as CljEvalFailure {
		return e.message
	} catch {
		return nil
	}
}

// Captures println/prn output for the duration of body; the hook is process-wide, so callers run serially.
nonisolated(unsafe) private var captured = ""

func capturingOutput(_ body: () throws -> Void) rethrows -> String {
	captured = ""
	clj_set_output({ bytes, len, _ in
		captured += String(decoding: UnsafeRawBufferPointer(start: bytes, count: len), as: UTF8.self)
	}, nil)
	defer { clj_set_output(nil, nil) }
	try body()
	return captured
}
