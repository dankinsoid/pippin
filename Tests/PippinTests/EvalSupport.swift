// @ai-generated(guided)
import CljCore
import Foundation
import Testing
@testable import Pippin

// Boots the runtime before every test of the suite it is applied to (recursively), so a live-object baseline
// never lands before clj_init's one-time allocations, whichever suite runs first, nor before the runtime settled.
struct BootedTrait: SuiteTrait, TestTrait, TestScoping {
	var isRecursive: Bool { true }

	func provideScope(for test: Test, testCase: Test.Case?, performing function: @Sendable () async throws -> Void) async throws {
		clj_init()
		if test.isSuite { return try await function() }
		runtimeSettled("before \(test.name)")
		let watch = DispatchWorkItem { reportHang(test.name) }
		DispatchQueue.global().asyncAfter(deadline: .now() + hangSeconds, execute: watch)
		defer { watch.cancel() }
		try await function()
	}
}

// The run's time bound kills a hung test without a trace (docs/notes/gates.md, "CI").
private let hangSeconds = Double(ProcessInfo.processInfo.environment["CLJ_TEST_HANG_S"] ?? "") ?? 300

// Ends the process: a test past the bound has failed, and waiting on for the run's bound gains nothing.
// @ai-generated(solo)
private func reportHang(_ name: String) {
	FileHandle.standardError.write(Data("hang: \(name) still running after \(hangSeconds) s\n".utf8))
	clj_debug_sched_dump()
	let sample = Process()
	sample.executableURL = URL(fileURLWithPath: "/usr/bin/sample")
	sample.arguments = [String(getpid()), "1", "-file", "/dev/stderr"]
	if (try? sample.run()) != nil { sample.waitUntilExit() }
	fflush(nil)
	_exit(3)
}

nonisolated(unsafe) private var neverSettled = false

// After one failure each call only looks: a leak would otherwise stall every later test into the run's bound.
// @ai-generated(solo)
@discardableResult
func runtimeSettled(_ when: String, coros: Int = 0, sourceLocation: SourceLocation = #_sourceLocation) -> Bool {
	if clj_debug_runtime_settle(coros, neverSettled ? 0 : 10_000) { return true }
	if !neverSettled {
		neverSettled = true
		Issue.record("the runtime never settled \(when): \(clj_debug_live_coros()) coroutines (want \(coros)), \(clj_debug_timers_held()) timers, \(clj_debug_blocking_held()) blocking jobs", sourceLocation: sourceLocation)
		clj_debug_sched_dump()
	}
	return false
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
