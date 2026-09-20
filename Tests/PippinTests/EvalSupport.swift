// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

// Boots the runtime before every test of the suite it is applied to (recursively), so a live-object baseline
// never lands before clj_init's one-time allocations, whichever suite runs first.
struct BootedTrait: SuiteTrait, TestTrait, TestScoping {
	var isRecursive: Bool { true }

	func provideScope(for test: Test, testCase: Test.Case?, performing function: @Sendable () async throws -> Void) async throws {
		clj_init()
		try await function()
	}
}

extension Trait where Self == BootedTrait {
	static var booted: BootedTrait { BootedTrait() }
}

struct CljEvalFailure: Error {
	let message: String
}

// Evaluates every form in the current namespace through the C API and returns the last value. Forms are
// read one at a time, so an in-ns earlier in the source governs how the reader resolves the later ones.
func cljEval(_ source: String) throws -> Value {
	clj_init()
	var bytes = Array(source.utf8)
	return try bytes.withUnsafeMutableBufferPointer { buf in
		try buf.withMemoryRebound(to: CChar.self) { chars in
			var reader = clj_reader()
			clj_reader_init(&reader, chars.baseAddress, chars.count)
			clj_reader_use_namespaces(&reader)
			var last: Value = nil
			while true {
				var raw: clj_value = CLJ_NIL
				switch clj_read(&reader, &raw) {
				case CLJ_READ_EOF:
					return last
				case CLJ_READ_ERROR:
					throw ReaderError(message: String(cString: clj_reader_message(&reader)), line: Int(reader.error_line), column: Int(reader.error_col))
				default:
					let form = Value(owning: raw)
					var env = clj_env(ns: CLJ_NIL, line: reader.form_line, col: reader.form_col)
					let result = withExtendedLifetime(form) { clj_eval(form.raw, &env) }
					if result == CLJ_THROWN {
						let ex = Value(owning: clj_take_pending())
						throw CljEvalFailure(message: ex.description)
					}
					last = Value(owning: result)
				}
			}
		}
	}
}

// *ns* bound around the call: a suite whose sources in-ns must not move the root, which parallel suites' evals read.
func cljEvalScoped(_ source: String) throws -> Value {
	clj_init()
	return try Runtime.bindingCurrentNamespace { try cljEval(source) }
}

func cljEvalErrorScoped(_ source: String) -> String? {
	do {
		_ = try cljEvalScoped(source)
		return nil
	} catch let e as CljEvalFailure {
		return e.message
	} catch {
		return nil
	}
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
// The hook runs on the writer thread, so the queue is flushed before the text is read.
nonisolated(unsafe) private var captured = ""

func capturingOutput(_ body: () throws -> Void) rethrows -> String {
	captured = ""
	clj_set_output({ bytes, len, _ in
		captured += String(decoding: UnsafeRawBufferPointer(start: bytes, count: len), as: UTF8.self)
	}, nil)
	defer { clj_set_output(nil, nil) }
	try body()
	clj_output_flush()
	return captured
}
