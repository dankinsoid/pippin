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
private func compileAsUnit(_ source: String, file: String, name: String, closed: Bool = false) throws {
	var opts = cljc_options()
	opts.line = true
	opts.skip_embedded = true
	opts.closed = closed
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
			// closed: no guards, direct calls, int64 loop variables; a fixture that rebinds vars or evals stays dev-only
			guard !source.contains("with-redefs") && !source.contains("(eval ") && !source.contains("load-string") else { return }
			try compileAsUnit(source, file: file, name: name + "_closed", closed: true)
			#expect(try runUnit(file) == expected, "closed compiled output of \(name)")
			let live2 = clj_debug_live_objects()
			#expect(try runUnit(file) == expected, "second closed compiled output of \(name)")
			#expect(clj_debug_live_objects() - live2 <= interpretedGrowth, "closed compiled run of \(name) leaks")
		}

		// A closed site whose receiver the join names takes the direct arm; a receiver the join never saw and an
		// extend after the site was compiled (through the interpreter, the dev store) both fall back, right.
		@Test func closedDirectArmGuardsAndFallsBack() throws {
			clj_init()
			_ = try cljEval("(ns cp.direct)")
			defer { clj_ns_set_current(clj_ns_user()) }
			let arms0 = clj_debug_proto_arm_hits(), cache0 = clj_debug_proto_cache_hits()
			try compiledEval(closed: true) {
				_ = try cljEval("(defprotocol CpP (cp-m [x])) (deftype CpT [] CpP (cp-m [x] :t)) (extend-type Long CpP (cp-m [x] :long))")
				_ = try cljEval("(let [] (defn cp-mono [x] (cp-m x)) (defn cp-run [] [(cp-mono (->CpT)) (cp-mono (->CpT))]))")
				#expect(try cljEval("(cp-run)") == [Value(keyword: "t"), Value(keyword: "t")])
			}
			let arms1 = clj_debug_proto_arm_hits(), cache1 = clj_debug_proto_cache_hits()
			#expect(arms1 - arms0 >= 1, "the second call hits the arm: \(arms1 - arms0)")
			#expect(cache1 == cache0, "no cache hit on the arm's receiver")
			// the protocol's other implementor is an arm too (the requirement names both); a kind outside the arms
			// throws through the tables, and one extended after the compile is served by the cache
			#expect(try cljEval("[(cp-mono 5) (cp-mono 5)]") == [Value(keyword: "long"), Value(keyword: "long")])
			#expect(clj_debug_proto_arm_hits() - arms1 >= 1 && clj_debug_proto_cache_hits() == cache1)
			#expect(cljEvalError("(cp-mono \"s\")")?.contains("No implementation of method: :cp-m") == true)
			_ = try cljEval("(extend-type String CpP (cp-m [x] :str))")
			#expect(try cljEval("[(cp-mono \"s\") (cp-mono \"s\")]") == [Value(keyword: "str"), Value(keyword: "str")])
			#expect(clj_debug_proto_cache_hits() - cache1 == 1)
			// the arm's impl replaced after the site was compiled: the epoch moves, the fill fails, the cache answers
			_ = try cljEval("(extend-type CpT CpP (cp-m [x] :t2))")
			let arms2 = clj_debug_proto_arm_hits()
			#expect(try cljEval("(cp-run)") == [Value(keyword: "t2"), Value(keyword: "t2")])
			#expect(clj_debug_proto_arm_hits() == arms2, "no arm hit after the extend")
			// satisfies? folded to a verified constant, and re-verified after the extend
			try compiledEval(closed: true) {
				_ = try cljEval("(let [] (defn cp-is [x] (satisfies? CpP x)) (defn cp-is-run [] (cp-is (->CpT))))")
				#expect(try cljEval("[(cp-is-run) (cp-is 5) (cp-is \"s\") (cp-is :k)]") == [true, true, true, false])
			}
			_ = try cljEval("(defprotocol CpP (cp-m [x]))")
			#expect(try cljEval("[(cp-is-run) (cp-is 5)]") == [false, false])
		}

		// A dev unit's site keeps a per-thread cache: it hits on a repeated receiver, misses on another, and an
		// extend-type after the compile refills it with the new impl.
		@Test func devInlineCacheRefillsAfterExtend() throws {
			clj_init()
			_ = try cljEval("(ns cp.dev)")
			defer { clj_ns_set_current(clj_ns_user()) }
			_ = try cljEval("(defprotocol CdP (cd-m [x])) (deftype CdT [] CdP (cd-m [x] :t)) (extend-type Long CdP (cd-m [x] :long))")
			let cache0 = clj_debug_proto_cache_hits()
			try compiledEval {
				_ = try cljEval("(defn cd-any [x] (cd-m x))")
				#expect(try cljEval("[(cd-any (->CdT)) (cd-any (->CdT)) (cd-any 5) (cd-any 5) (cd-any (->CdT))]")
					== [Value(keyword: "t"), Value(keyword: "t"), Value(keyword: "long"), Value(keyword: "long"), Value(keyword: "t")])
			}
			let cache1 = clj_debug_proto_cache_hits()
			#expect(cache1 - cache0 == 2, "one hit per repeated receiver: \(cache1 - cache0)")
			_ = try cljEval("(extend-type Long CdP (cd-m [x] :long2))")
			#expect(try cljEval("[(cd-any 5) (cd-any 5) (cd-any (->CdT))]") == [Value(keyword: "long2"), Value(keyword: "long2"), Value(keyword: "t")])
			#expect(clj_debug_proto_cache_hits() - cache1 == 1)
			#expect(cljEvalError("(cd-any \"s\")")?.contains("No implementation of method: :cd-m") == true)
			#expect(cljEvalError("(cd-m)")?.contains("Wrong number of args (0)") == true)
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
