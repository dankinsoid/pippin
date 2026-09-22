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

nonisolated(unsafe) private var uncaughtTraces: [(String, Int)] = []
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

		// The guard page lands "Stack overflow" at the host boundary, past any try in between (guard.c); the loop tick
		// stops a loop without calls.
		@Test func endlessRecursionAndLoopAreStopped() throws {
			clj_init()
			_ = try cljEval("(ns cp.stop)")
			defer { clj_ns_set_current(clj_ns_user()) }
			for closed in [false, true] {
				try compiledEval(closed: closed) {
					_ = try cljEval("(declare cp-b) (let [] (defn cp-a [n] (cp-b (inc n))) (defn cp-b [n] (cp-a n)) (defn cp-spin [] (loop [i 0] (recur (inc i)))))")
				}
				#expect(cljEvalError("(cp-a 0)")?.hasPrefix("#error {:message \"Stack overflow\"") == true, "closed: \(closed)")
				#expect(clj_shadow_stack_depth() == 0 && clj_debug_retired_roots() == 0)
				#expect(cljEvalError("(try (cp-a 0) (catch :default e :caught))")?.hasPrefix("#error {:message \"Stack overflow\"") == true)
				// the same from Swift: Value.apply is a recovery point of its own
				let f = try cljEval("cp-a")
				do {
					_ = try f(0)
					Issue.record("no overflow through Value.apply")
				} catch let e as ClojureError {
					#expect(e.message == "Stack overflow" && e.trace.count == 256 && e.trace.allSatisfy { $0.fn == "cp.stop/cp-a" || $0.fn == "cp.stop/cp-b" })
				}
				#expect(clj_shadow_stack_depth() == 0 && clj_debug_retired_roots() == 0)
				clj_deadline_set_ms(100)
				#expect(cljEvalError("(cp-spin)")?.contains("Execution timed out") == true, "closed: \(closed)")
				clj_deadline_set_ms(0)
				#expect(try cljEval("(+ 1 2)") == 3)
			}
		}

		// Compiled recursion inside a coroutine hits the coroutine's own guard page and lands at its entry: that
		// coroutine ends with "Stack overflow" (reported, its channel closes), the carrier and everything else live on.
		// Compiled frames inside a coroutine trace like anywhere else.
		@Test func overflowInsideACoroutineThrowsOnlyThere() throws {
			clj_init()
			_ = try cljEval("(ns cp.coro (:require [clojure.core.async :refer [go <!!]]))")
			defer { clj_ns_set_current(clj_ns_user()) }
			for closed in [false, true] {
				try compiledEval(closed: closed) {
					_ = try cljEval("(declare cp-cb) (let [] (defn cp-ca [n] (cp-cb (inc n))) (defn cp-cb [n] (cp-ca n)) (defn cp-throws [] (throw (ex-info \"t\" {}))) (defn cp-mid [] (cp-throws)))")
				}
				let coros = clj_debug_live_coros()
				uncaughtTraces = []
				clj_coro_set_uncaught_handler { ex, trace in
					let t = Value(borrowing: trace)
					uncaughtTraces.append((Value(borrowing: ex).description, (t.array ?? []).count))
				}
				defer { clj_coro_set_uncaught_handler(nil) }
				#expect(try cljEval("(<!! (go (cp-ca 0)))") == nil, "closed: \(closed)")
				#expect(clj_debug_coro_settle(coros, 5000))
				#expect(uncaughtTraces.count == 1 && uncaughtTraces[0].0.hasPrefix("#error {:message \"Stack overflow\"") && uncaughtTraces[0].1 == 256, "\(uncaughtTraces)")
				#expect(clj_shadow_stack_depth() == 0 && clj_debug_retired_roots() == 0)
				#expect(try cljEval("(<!! (go (+ 1 2)))") == 3)
				#expect(try cljEval("(<!! (go (try (cp-mid) (catch :default e (mapv :fn (ex-trace e))))))") == [Value(symbol: "cp.coro/cp-throws"), Value(symbol: "cp.coro/cp-mid"), nil], "closed: \(closed)")
			}
		}

		// A keyword or ExceptionInfo selector must exclude :cancelled and, for the keyword, nothing else.
		@Test func compiledCatchSelectivityMatchesTheInterpreter() throws {
			clj_init()
			_ = try cljEval("(ns cp.catchkw)")
			defer { clj_ns_set_current(clj_ns_user()); clj_deadline_set_ms(0) }
			for closed in [false, true] {
				try compiledEval(closed: closed) {
					_ = try cljEval("""
						(defn cp-catch-kw [f] (try (f) (catch :cancelled e :wrong) (catch :default e :right)))
						(defn cp-catch-exinfo [f] (try (f) (catch ExceptionInfo e :wrong)))
						(defn cp-spin [] (loop [i 0] (recur (inc i))))
						""")
				}
				// ex-type selection: an ordinary ex-info skips :cancelled; one tagged :type :cancelled hits it.
				#expect(try cljEval("(cp-catch-kw (fn [] (throw (ex-info \"boom\" {}))))") == Value(keyword: "right"), "closed: \(closed)")
				#expect(try cljEval("(cp-catch-kw (fn [] (throw (ex-info \"boom\" {:type :cancelled}))))") == Value(keyword: "wrong"), "closed: \(closed)")
				// ExceptionInfo still catches an ordinary ex-info...
				#expect(try cljEval("(cp-catch-exinfo (fn [] (throw (ex-info \"boom\" {}))))") == Value(keyword: "wrong"), "closed: \(closed)")
				// ...but a real cancellation (the deadline) is not an ex-info at all, so ExceptionInfo misses it.
				clj_deadline_set_ms(50)
				#expect(cljEvalError("(cp-catch-exinfo cp-spin)")?.contains("Execution timed out") == true, "closed: \(closed)")
				clj_deadline_set_ms(0)
				// The rethrow out of the compiled try captures no frames either (design §4).
				let rt = Runtime()
				clj_deadline_set_ms(50)
				do {
					_ = try rt.eval("(cp-catch-exinfo cp-spin)")
					Issue.record("the deadline did not fire, closed: \(closed)")
				} catch let e as ClojureError {
					#expect(e.message == "Execution timed out" && e.trace.isEmpty, "closed: \(closed), \(e.trace)")
				}
				clj_deadline_set_ms(0)
			}
		}

		// The compiler emits its own loop tick, so the shared predicate has to answer both ways through it: a
		// cancellation unwinds the emitted loop, a suspension parks inside it and the loop goes on afterwards.
		@Test func compiledLoopTickParksOnASuspend() throws {
			clj_init()
			_ = try cljEval("(ns cp.suspend (:require [clojure.core.async :refer [go timeout <!! alts!! cancel! suspend! resume!]]))")
			defer { clj_ns_set_current(clj_ns_user()) }
			// A timer still holding its guard channel would fail the next suite's live-object baseline.
			_ = try cljEval("(defn cp-joined [ch] (let [t (timeout 200) v (first (alts!! [ch t]))] (<!! t) v))")
			let coros = clj_debug_live_coros()
			for closed in [false, true] {
				try compiledEval(closed: closed) {
					_ = try cljEval("(defn cp-spin-bump [r] (loop [] (swap! r inc) (recur)))")
				}
				let got = try cljEval("""
					(let [r (atom 0)
					      g (go (try (cp-spin-bump r) (catch :cancelled e :done)))]
					  (<!! (timeout 10))
					  (suspend! g)
					  (<!! (timeout 10))
					  (let [a @r]
					    (<!! (timeout 10))
					    (let [b @r]
					      (resume! g)
					      (<!! (timeout 10))
					      (let [c @r]
					        (cancel! g)
					        [(= a b) (> c b) (cp-joined g)]))))
					""")
				#expect(got == [true, true, Value(keyword: "done")], "closed: \(closed), \(got)")
			}
			#expect(clj_debug_coro_settle(coros, 5000))
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
