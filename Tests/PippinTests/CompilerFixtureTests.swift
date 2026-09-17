// @ai-generated(solo)
import CljCompiler
import CljCore
import Foundation
import Testing
@testable import Pippin

// Fixtures/compiler/<name>.clj prints what both backends must print as <name>.out (CLJ_FIXTURE_UPDATE=1 rewrites it).

private let fixtureDir = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/compiler")
private let packageRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()

private func fixtureNames() -> [String] {
	let files = (try? FileManager.default.contentsOfDirectory(atPath: fixtureDir.path)) ?? []
	return files.filter { $0.hasSuffix(".clj") }.map { String($0.dropLast(4)) }.sorted()
}

private func load(_ source: String, file: String) throws {
	var bytes = Array(source.utf8)
	let path = Value(file)
	try bytes.withUnsafeMutableBufferPointer { buf in
		try buf.withMemoryRebound(to: CChar.self) { chars in
			let r = withExtendedLifetime(path) { clj_load_source(chars.baseAddress, chars.count, path.raw) }
			if r == CLJ_THROWN { throw ClojureError.takePending() }
			clj_release(r)
		}
	}
}

// The compiled eval for the body, restoring whatever the process had (CLJ_EVAL=compiled keeps it on).
private func compiledEval<T>(closed: Bool = false, _ body: () throws -> T) throws -> T {
	var o = evalOptions()
	o.closed = closed
	#expect(cljc_eval_enable(&o))
	defer {
		cljc_eval_disable()
		clj_compiled_eval_boot()
	}
	return try body()
}

nonisolated(unsafe) private let jitDir = strdup(packageRoot.appendingPathComponent(".build/compiled-fixtures").path)
nonisolated(unsafe) private let jitRoot = strdup(packageRoot.path)

private func evalOptions() -> cljc_eval_options {
	var o = cljc_eval_options()
	o.root = UnsafePointer(jitRoot)
	o.dir = UnsafePointer(jitDir)
	return o
}

// The file as one unit, the way clj-compile emits it: the load evaluates every form (its output is dropped) and the
// unit built from what the hook saw is registered for the path, so clj_load_file runs it in place of the source.
private func compileAsUnit(_ source: String, file: String, name: String) throws {
	var opts = cljc_options()
	opts.line = true
	opts.skip_embedded = true
	let c = cljc_new(&opts)!
	defer { cljc_free(c) }
	cljc_begin(c)
	defer { cljc_end(c) }
	_ = try capturingOutput { try load(source, file: file) }
	cljc_end(c)
	try #require(cljc_unit_count(c) == 1)
	#expect(cljc_refusal_count(c) == 0)
	let text = cljc_unit_text(c, 0, nil)!
	defer { free(text) }
	var o = evalOptions()
	guard let unit = cljc_load_dylib(&o, "fixture_\(name)", text) else { throw ClojureError.takePending() }
	clj_compiled_register(unit.pointee.path, unit.pointee.`init`)
}

private func runUnit(_ file: String) throws -> String {
	try capturingOutput {
		let path = Value(file)
		let r = withExtendedLifetime(path) { clj_load_file(path.raw) }
		if r == CLJ_THROWN { throw ClojureError.takePending() }
		clj_release(r)
	}
}

extension CoreTests {
	@Suite(.serialized) struct CompilerFixtureTests {
		@Test(arguments: fixtureNames())
		func fixture(_ name: String) throws {
			clj_init()
			let source = try String(contentsOf: fixtureDir.appendingPathComponent("\(name).clj"), encoding: .utf8)
			let file = fixtureDir.appendingPathComponent("\(name).clj").path
			defer { clj_ns_set_current(clj_ns_user()) }
			let interpreted = try capturingOutput { try load(source, file: file) }
			if ProcessInfo.processInfo.environment["CLJ_FIXTURE_UPDATE"] != nil {
				try interpreted.write(to: fixtureDir.appendingPathComponent("\(name).out"), atomically: true, encoding: .utf8)
			}
			let expected = try String(contentsOf: fixtureDir.appendingPathComponent("\(name).out"), encoding: .utf8)
			#expect(interpreted == expected, "interpreter output of \(name)")
			// A second run replaces the first run's definitions one for one, so what it adds is what the source itself
			// keeps per run (a deftype's descriptor, a parked root); the compiled backend must not add more.
			let live0 = clj_debug_live_objects()
			_ = try capturingOutput { try load(source, file: file) }
			let interpretedGrowth = clj_debug_live_objects() - live0
			try compileAsUnit(source, file: file, name: name)
			let compiled = try runUnit(file)
			#expect(compiled == expected, "compiled output of \(name)")
			let live1 = clj_debug_live_objects()
			let again = try runUnit(file)
			#expect(again == expected, "second compiled output of \(name)")
			#expect(clj_debug_live_objects() - live1 <= interpretedGrowth, "compiled run of \(name) leaks")
		}

		// Under --closed a form the generator refuses is an error naming the node and its position, not a fallback.
		@Test func closedRefusesEval() throws {
			clj_init()
			let message = try compiledEval(closed: true) { cljEvalError("(defn evaluates [] (eval '(+ 1 2)))") }
			#expect(message?.contains("compiler refused a invoke node at 1:") == true, "\(message ?? "nil")")
			#expect(try compiledEval(closed: true) { try cljEval("((fn [x] (+ x 1)) 41)") }.int == 42)
		}
	}
}
