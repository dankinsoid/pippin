// @ai-generated(solo)
import CljCompiler
import CljCore
import Foundation
import Testing
@testable import Pippin

// The §5b reconciler is a plain library on the load path, so its tests are clojure.test in lib/test.

private let packageRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
private let srcDir = packageRoot.appendingPathComponent("lib/src")
private let testDir = packageRoot.appendingPathComponent("lib/test")

// Required in this order: a compiled unit replaces its source only once its dependencies' vars are there.
private let libFiles = [
	srcDir.appendingPathComponent("pippin/ui/reconciler.clj").path,
	srcDir.appendingPathComponent("pippin/ui/mock.clj").path,
	testDir.appendingPathComponent("pippin/ui/reconciler_test.clj").path,
]

private let suiteNs = "pippin.ui.reconciler-test"

private nonisolated(unsafe) let jitDir = strdup(packageRoot.appendingPathComponent(".build/compiled-ui").path)
private nonisolated(unsafe) let jitRoot = strdup(packageRoot.path)

private func evalOptions(closed: Bool) -> cljc_eval_options {
	var o = cljc_eval_options()
	o.root = UnsafePointer(jitRoot)
	o.dir = UnsafePointer(jitDir)
	o.closed = closed
	return o
}

private func loadFile(_ path: String) throws {
	let p = Value(path)
	let r = withExtendedLifetime(p) { clj_load_file(p.raw) }
	if r == CLJ_THROWN { throw ClojureError.takePending() }
	clj_release(r)
}

// [:test :pass :fail :error] of one clojure.test run; the printed report is kept for a failure's message.
private func runSuite(_ what: String) throws -> [Int] {
	var summary: Value = nil
	let report = try capturingOutput {
		summary = try cljEvalScoped("(in-ns 'user) (require '\(suiteNs)) (clojure.test/run-tests '\(suiteNs))")
	}
	let m = summary.dictionary ?? [:]
	let counts = ["test", "pass", "fail", "error"].map { m[Value(keyword: $0)]?.int ?? -1 }
	if counts[2] != 0 || counts[3] != 0 || counts[1] <= 0 {
		Issue.record("\(what): \(counts)\n\(report)")
	}
	return counts
}

// The three files as C translation units, built and loaded, then run in place of their sources.
private func compileAndRun(closed: Bool) throws -> [Int] {
	var opts = cljc_options()
	opts.line = true
	opts.skip_embedded = true
	opts.closed = closed
	let c = cljc_new(&opts)!
	defer { cljc_free(c) }
	cljc_begin(c)
	// The source text, not clj_load_file: a path whose unit is already registered would run that unit instead.
	_ = try capturingOutput {
		for path in libFiles {
			try loadFixtureSource(try String(contentsOf: URL(fileURLWithPath: path), encoding: .utf8), file: path)
		}
	}
	cljc_end(c)
	for i in 0..<cljc_refusal_count(c) {
		let r = cljc_refusal_at(c, i)!.pointee
		Issue.record("refusal: \(String(cString: r.file)):\(r.line) \(String(cString: r.kind)): \(String(cString: r.reason))")
	}
	try #require(cljc_unit_count(c) == libFiles.count)
	var o = evalOptions(closed: closed)
	for i in 0..<cljc_unit_count(c) {
		let text = cljc_unit_text(c, i, nil)!
		defer { free(text) }
		let cname = cljc_unit_cname(c, i)!
		defer { free(cname) }
		let name = (closed ? "closed_" : "dev_") + String(cString: cname)
		guard let unit = cljc_load_dylib(&o, name, String(cString: text)) else { throw ClojureError.takePending() }
		clj_compiled_register(unit.pointee.path, unit.pointee.`init`)
	}
	_ = try capturingOutput {
		for path in libFiles { try loadFile(path) }
	}
	return try runSuite(closed ? "closed" : "compiled")
}

extension CoreTests {
	@Suite(.serialized) struct ReconcilerTests {
		@Test func theLibraryPassesInterpretedAndCompiled() throws {
			clj_init()
			let previous = Runtime.loadPath
			Runtime.loadPath = [srcDir.path, testDir.path]
			defer {
				Runtime.loadPath = previous
				clj_ns_set_current(clj_ns_user())
			}
			let interpreted = try runSuite("interpreted")
			#expect(interpreted[0] > 0)
			// The same assertions through the C backend: a pure-Clojure library is where the two must agree.
			#expect(try compileAndRun(closed: false) == interpreted)
			#expect(try compileAndRun(closed: true) == interpreted)
		}
	}
}
