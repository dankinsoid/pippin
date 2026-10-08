// @ai-generated(solo)
import CljCompiler
import CljCore
import Foundation
import Testing
@testable import Pippin

// An <embedded>/ lib stands in for core.clj, which a test process past clj_init cannot put through the hook again.

private let libPath = "<embedded>/fixture/shake_lib.clj"
private let appPath = "Tests/PippinTests/Fixtures/shake/unit-app.clj"

private let libSource = """
(ns fixture.shake.lib)
(defn chain-live [x] (+ x 1))
(defn reached-from-app [x] (chain-live x))
(defn chain-dead [x] (* x 2))
(defn dead-caller [x] (chain-dead x))
(defn dead-leaf [x] x)
(def not-a-fn 41)
(def ^:dynamic *dyn-fn* (fn [] :dyn))
(defn named-by-symbol [] :named)
(def quoted-name 'named-by-symbol)
(defn twice-defined [] :first)
(defn twice-defined [] :second)
(defn called-at-load [] :load)
(def ^:eager load-time-result (called-at-load))
(defn called-by-lazy [] :lazy)
(def lazy-result (called-by-lazy))
(defmacro only-a-macro [x] x)
"""

private let appSource = """
(ns fixture.shake.app)
(defn app-own-def [] :kept)
(def app-result [(fixture.shake.lib/reached-from-app 1) (app-own-def)])
"""

private struct ShakeRun {
	var report: cljc_shake_report
	var dropped: Set<String>
	var kept: Set<String>
	var text: String
}

private func shakeRun(closed: Bool = true, app: Bool = true, noShake: Bool = false) throws -> ShakeRun {
	clj_init()
	var opts = cljc_options()
	opts.line = false
	opts.closed = closed
	opts.no_shake = noShake
	let c = cljc_new(&opts)!
	defer { cljc_free(c) }
	cljc_begin(c)
	_ = try capturingOutput {
		try loadFixtureSource(libSource, file: libPath)
		if app { try loadFixtureSource(appSource, file: appPath) }
	}
	cljc_end(c)
	#expect(cljc_refusal_count(c) == 0)
	var report = cljc_shake_report()
	cljc_shake_report_of(c, &report)
	var dropped: Set<String> = [], kept: Set<String> = []
	var libText = ""
	for i in 0..<cljc_unit_count(c) {
		for k in 0..<cljc_unit_def_count(c, i) {
			var d = cljc_def_size()
			cljc_unit_def_at(c, i, k, &d)
			let name = String(cString: d.name)
			if d.dropped { dropped.insert(name) } else { kept.insert(name) }
		}
		if String(cString: cljc_unit_file(c, i)) == libPath {
			let text = cljc_unit_text(c, i, nil)!
			libText = String(cString: text)
			free(text)
		}
	}
	return ShakeRun(report: report, dropped: dropped, kept: kept, text: libText)
}

// Under CoreTests, whose .serialized is what keeps these allocations out of another suite's live-object window.
extension CoreTests {
	@Suite(.serialized) struct ShakeTests {
		@Test func dropsOnlyWhatNothingReaches() throws {
			let r = try shakeRun()
			#expect(r.report.ran)
			// Nothing names these, and nothing names what the first one calls; a lazy def's init never ran at load.
			#expect(r.dropped == ["fixture.shake.lib/dead-caller", "fixture.shake.lib/chain-dead", "fixture.shake.lib/dead-leaf",
			                      "fixture.shake.lib/only-a-macro", "fixture.shake.lib/lazy-result", "fixture.shake.lib/called-by-lazy"])
			// Row 7 through the app, and one hop further.
			#expect(r.kept.contains("fixture.shake.lib/reached-from-app"))
			#expect(r.kept.contains("fixture.shake.lib/chain-live"))
			// Row 8: only a quoted symbol in a live def's init names it.
			#expect(r.kept.contains("fixture.shake.lib/named-by-symbol"))
			// Rows 1, 3, 9, and a def a load-time form calls.
			#expect(r.kept.contains("fixture.shake.lib/not-a-fn"))
			#expect(r.kept.contains("fixture.shake.lib/*dyn-fn*"))
			#expect(r.kept.contains("fixture.shake.lib/twice-defined"))
			#expect(r.kept.contains("fixture.shake.lib/called-at-load"))
			// Row 2: the program has no declared entry point, so its own defs all stay.
			#expect(r.kept.contains("fixture.shake.app/app-own-def"))
			#expect(r.text.contains("clj_c_shaken"))
		}

		@Test func devDropsNothing() throws {
			let r = try shakeRun(closed: false)
			#expect(!r.report.ran)
			#expect(r.dropped.isEmpty)
			#expect(!r.text.contains("clj_c_shaken"))
			#expect(String(cString: r.report.skipped!).contains("dev mode"))
		}

		@Test func noEntryUnitDropsNothing() throws {
			let r = try shakeRun(app: false)
			#expect(!r.report.ran)
			#expect(r.dropped.isEmpty)
			#expect(String(cString: r.report.skipped!).contains("no entry unit"))
		}

		@Test func noShakeDropsNothing() throws {
			let r = try shakeRun(noShake: true)
			#expect(!r.report.ran)
			#expect(r.dropped.isEmpty)
			#expect(!r.text.contains("clj_c_shaken"))
		}

		// The fatal itself is scripts/shake.sh's to provoke; here the var, its meta and its tripwire root.
		@Test func aDroppedVarKeepsItsVarAndTripwire() throws {
			try runShakenUnit("run")
			let dead = try #require(resolveVar("fixture.shake.run", "dead-one"))
			#expect(clj_c_is_shaken(dead.raw))
			#expect(Value(borrowing: clj_var_meta(dead.raw)).description.contains(":pippin/shaken"))
			let live = try #require(resolveVar("fixture.shake.run", "live-one"))
			#expect(!clj_c_is_shaken(live.raw))
			// The flag is what routes a call through expand_once to the invoke slot (shake.sh provokes the abort).
			let macroVar = try #require(resolveVar("fixture.shake.run", "dead-macro"))
			#expect(clj_c_is_shaken(macroVar.raw))
			#expect(clj_var_is_macro(macroVar.raw))
			#expect(Value(borrowing: clj_var_meta(macroVar.raw)).description.contains(":macro true"))
		}

		// A macro var's root is only ever invoked (expand_once), so no reference reaches the tripwire.
		@Test func aDroppedMacroIsRefusedAsAValueLikeAnyMacro() throws {
			try runShakenUnit("ref")
			let refused = "Can't take value of a macro: #'fixture.shake.ref/dead-macro"
			#expect(cljEvalErrorScoped("fixture.shake.ref/dead-macro")?.contains(refused) == true)
			#expect(cljEvalErrorScoped("(map fixture.shake.ref/dead-macro [1 2])")?.contains(refused) == true)
			// Inspection still answers: the var resolves and nothing reached its root to abort over.
			#expect(try cljEvalScoped("(var fixture.shake.ref/dead-macro)").description == "#'fixture.shake.ref/dead-macro")
			#expect(try cljEvalScoped("(:macro (meta (var fixture.shake.ref/dead-macro)))").description == "true")
		}

		// The slot policy of the tripwire type (NOTES.md, "Compiler"): what answers, and that fn? is not a lie.
		@Test func aDroppedRootAnswersOnlyWhatInspectsIt() throws {
			try runShakenUnit("slots")
			let dead = try #require(resolveVar("fixture.shake.slots", "dead-one"))
			let root = Value(borrowing: clj_var_root(dead.raw))
			#expect(root.description == "#shaken[fixture.shake.slots/dead-one]")
			// fn? is a type-identity test: (fn? @(resolve 'x)) must not claim a dropped def is a function.
			#expect(clj_fn_p(root.raw) == CLJ_FALSE)
			#expect(clj_ifn_p(root.raw) == CLJ_TRUE)
			#expect(clj_equals(root.raw, root.raw))
			#expect(!clj_equals(root.raw, CLJ_NIL))
			#expect(clj_hash(root.raw) == clj_hash(root.raw))
			// Every other predicate reads a bit the type does not claim, so none of them aborts either.
			#expect(clj_map_p(root.raw) == CLJ_FALSE)
			#expect(clj_vector_p(root.raw) == CLJ_FALSE)
			#expect(clj_seqable_p(root.raw) == CLJ_FALSE)
			#expect(clj_coll_p(root.raw) == CLJ_FALSE)
		}

		// A namespace of its own, since a shaken unit's init is written to run once (NOTES.md, "Compiler").
		private func runShakenUnit(_ tag: String) throws {
			clj_init()
			var opts = cljc_options()
			opts.line = false
			opts.closed = true
			let c = cljc_new(&opts)!
			defer { cljc_free(c) }
			cljc_begin(c)
			let file = "<embedded>/fixture/shake_\(tag).clj"
			let lib = """
			(ns fixture.shake.\(tag))
			(defn live-one [x] x)
			(defn dead-one [x] x)
			(defmacro dead-macro [x] x)
			"""
			let app = "(ns fixture.shake.\(tag)app)\n(def r (fixture.shake.\(tag)/live-one 1))\n"
			_ = try capturingOutput {
				try loadFixtureSource(lib, file: file)
				try loadFixtureSource(app, file: "Tests/PippinTests/Fixtures/shake/\(tag)-app.clj")
			}
			cljc_end(c)
			var index = -1
			for i in 0..<cljc_unit_count(c) where String(cString: cljc_unit_file(c, i)) == file { index = Int(i) }
			try #require(index >= 0)
			let text = cljc_unit_text(c, index, nil)!
			defer { free(text) }
			#expect(String(cString: text).contains("clj_c_shaken"))
			var o = cljc_eval_options()
			o.root = UnsafePointer(jitRootPath)
			o.dir = UnsafePointer(jitDirPath)
			o.closed = true
			guard let unit = cljc_load_dylib(&o, "fixture_shake_\(tag)", String(cString: text)) else { throw ClojureError.takePending() }
			clj_compiled_register(unit.pointee.path, unit.pointee.`init`)
			_ = try capturingOutput {
				let path = Value(file)
				let r = withExtendedLifetime(path) { clj_load_file(path.raw) }
				if r == CLJ_THROWN { throw ClojureError.takePending() }
				clj_release(r)
			}
		}
	}
}

nonisolated(unsafe) private let packageRootURL = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
nonisolated(unsafe) private let jitRootPath = strdup(packageRootURL.path)
nonisolated(unsafe) private let jitDirPath = strdup(packageRootURL.appendingPathComponent(".build/compiled-fixtures").path)

private func resolveVar(_ ns: String, _ name: String) -> Value? {
	let nsValue = withExtendedLifetime(Value(symbol: ns)) { Value(borrowing: clj_ns_find($0.raw)) }
	if nsValue.isNil { return nil }
	let sym = Value(symbol: name)
	let v = withExtendedLifetime((nsValue, sym)) { Value(borrowing: clj_ns_resolve(nsValue.raw, sym.raw)) }
	return v.isNil ? nil : v
}
