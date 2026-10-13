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
	let names = files.filter { $0.hasSuffix(".clj") }.map { String($0.dropLast(4)) }.sorted()
	#if arch(arm64)
	return names
	#else
	// What x86_64 refuses instead (docs/portability.md, objc.c) is ObjCTests' to check.
	return names.filter { !$0.hasSuffix("-arm64") }
	#endif
}

func loadFixtureSource(_ source: String, file: String) throws {
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
func compileFixtureAsUnit(_ source: String, file: String, name: String, closed: Bool = false) throws {
	var opts = cljc_options()
	opts.line = true
	opts.skip_embedded = true
	opts.closed = closed
	let c = cljc_new(&opts)!
	defer { cljc_free(c) }
	cljc_begin(c)
	defer { cljc_end(c) }
	_ = try capturingOutput { try loadFixtureSource(source, file: file) }
	cljc_end(c)
	try #require(cljc_unit_count(c) == 1)
	#expect(cljc_refusal_count(c) == 0)
	let text = cljc_unit_text(c, 0, nil)!
	defer { free(text) }
	var o = evalOptions()
	guard let unit = cljc_load_dylib(&o, "fixture_\(name)", text) else { throw ClojureError.takePending() }
	clj_compiled_register(unit.pointee.path, unit.pointee.`init`)
}

func runFixtureUnit(_ file: String) throws -> String {
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
			let interpreted = try capturingOutput { try loadFixtureSource(source, file: file) }
			if ProcessInfo.processInfo.environment["CLJ_FIXTURE_UPDATE"] != nil {
				try interpreted.write(to: fixtureDir.appendingPathComponent("\(name).out"), atomically: true, encoding: .utf8)
			}
			let expected = try String(contentsOf: fixtureDir.appendingPathComponent("\(name).out"), encoding: .utf8)
			#expect(interpreted == expected, "interpreter output of \(name)")
			// A second run replaces the first run's definitions one for one, so what it adds is what the source itself
			// keeps per run (a deftype's descriptor, a parked root); the compiled backend must not add more.
			// A run can return with work still on a carrier (repoint's await wakes before the agent's action ends).
			func settledLive(_ when: String) -> Int64 {
				runtimeSettled("\(name): \(when)")
				return clj_debug_live_objects()
			}
			let live0 = settledLive("before the second interpreted run")
			let census0 = LiveCensus()
			_ = try capturingOutput { try loadFixtureSource(source, file: file) }
			let interpretedGrowth = settledLive("after the second interpreted run") - live0
			let interpretedMoves = LiveCensus().moves(since: census0).joined(separator: ", ")
			try compileFixtureAsUnit(source, file: file, name: name)
			let compiled = try runFixtureUnit(file)
			#expect(compiled == expected, "compiled output of \(name)")
			let live1 = settledLive("before the second compiled run")
			let census1 = LiveCensus()
			let again = try runFixtureUnit(file)
			#expect(again == expected, "second compiled output of \(name)")
			#expect(settledLive("after the second compiled run") - live1 <= interpretedGrowth,
			        "compiled run of \(name) leaks: \(LiveCensus().moves(since: census1).joined(separator: ", ")); interpreted \(interpretedMoves)")
			// closed: no guards, direct calls, int64 loop variables; a fixture that rebinds vars or evals stays dev-only
			guard !source.contains("with-redefs") && !source.contains("(eval ") && !source.contains("load-string") else { return }
			try compileFixtureAsUnit(source, file: file, name: name + "_closed", closed: true)
			#expect(try runFixtureUnit(file) == expected, "closed compiled output of \(name)")
			let live2 = settledLive("before the second closed run")
			let census2 = LiveCensus()
			#expect(try runFixtureUnit(file) == expected, "second closed compiled output of \(name)")
			#expect(settledLive("after the second closed run") - live2 <= interpretedGrowth,
			        "closed compiled run of \(name) leaks: \(LiveCensus().moves(since: census2).joined(separator: ", ")); interpreted \(interpretedMoves)")
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

		// Compiled closures capture through the interpreter's fn constructors, so the reach bits and the collector are one.
		@Test func cyclesMadeByCompiledCodeAreCollected() throws {
			clj_init()
			_ = try cljEval("(ns cp.cycles)")
			defer { clj_ns_set_current(clj_ns_user()) }
			for k in ["n", "f"] { _ = Value(keyword: k) }
			try compiledEval {
				_ = try cljEval("(defn cp-ring [n] (let [a (atom nil)] (reset! a {:n n :f (fn [] @a)}) nil))")
				_ = try cljEval("(defn cp-cell [n] (let [v (volatile! nil)] (vreset! v [v n]) nil))")
				_ = try cljEval("(defn cp-cells [k] (dotimes [i k] (letfn [(f [x] (if (pos? x) (g (dec x)) x)) (g [x] (f x))] (f 2))))")
			}
			let base = CoroBaseline()
			_ = try cljEval("(dotimes [i 300] (cp-ring i) (cp-cell i)) (cp-cells 300)")
			base.check()
		}

		// A freed cell comes back at the same address anyway (the free list is LIFO): only the counters tell reuse apart.
		@Test func reuseTokensTakeDyingCells() throws {
			clj_init()
			_ = try cljEval("(ns cp.reuse)")
			defer { clj_ns_set_current(clj_ns_user()) }
			try compiledEval {
				_ = try cljEval("(defn cr-bump [n l] (loop [i 0 l l] (if (< i n) (let [x (first l) r (rest l)] (recur (inc i) (cons (inc x) r))) l)))")
				_ = try cljEval("(defn cr-sum [v] (loop [n 0 s (seq v)] (if s (recur (+ n (first s)) (next s)) n)))")
			}
			_ = try cljEval("(def cr-kept (list 1 2 3))")
			var c0 = [Int64](repeating: 0, count: 3), c1 = c0, c2 = c0
			clj_debug_reuse_counts(&c0)
			#expect(try cljEval("(cr-bump 3 (list 1 2 3))") == Value(list: [4, 2, 3]))
			#expect(try cljEval("(cr-sum [1 2 3 4])") == 10)
			clj_debug_reuse_counts(&c1)
			// shared by the var: the first step copies, and every later cell is the loop's own
			#expect(try cljEval("[(cr-bump 1 cr-kept) cr-kept]") == [Value(list: [2, 2, 3]), Value(list: [1, 2, 3])])
			clj_debug_reuse_counts(&c2)
			if clj_reuse_enabled() {
				// cr-bump: steps 2 and 3 in place, each keeping the tail; cr-sum: four cells taken, the last one ends the walk
				#expect(c1[0] - c0[0] == 6 && c1[1] - c0[1] == 5, "taken \(c1[0] - c0[0]), used \(c1[1] - c0[1])")
				#expect(c1[2] - c0[2] == 5, "kept fields \(c1[2] - c0[2])")
				#expect(c2[0] == c1[0], "a shared value lends its cell")
			} else {
				#expect(c2 == c0)
			}
		}

		// The first step's list is also the host's, so only the later steps reach co-head at rc 1.
		@Test func ownedParametersTakeTheCallersDyingValue() throws {
			clj_init()
			_ = try cljEval("(ns cp.owned)")
			defer { clj_ns_set_current(clj_ns_user()) }
			try compiledEval(closed: true) {
				// the recursion keeps co-head a call rather than a body inlined into the loop
				_ = try cljEval("(defn co-head [s] (if (empty? s) (co-head (list 0)) (cons (inc (first s)) (rest s))))")
				_ = try cljEval("(defn co-steps [n s] (loop [i 0 s s] (if (< i n) (recur (inc i) (co-head s)) s)))")
			}
			_ = try cljEval("(def co-kept (list 1 2 3))")
			var c0 = [Int64](repeating: 0, count: 3), c1 = c0, c2 = c0
			clj_debug_reuse_counts(&c0)
			#expect(try cljEval("(co-steps 3 (list 1 2 3))") == Value(list: [4, 2, 3]))
			clj_debug_reuse_counts(&c1)
			#expect(try cljEval("[(co-head co-kept) co-kept]") == [Value(list: [2, 2, 3]), Value(list: [1, 2, 3])])
			clj_debug_reuse_counts(&c2)
			if clj_reuse_enabled() && clj_no_opt() & clj_no_opt_parse("owned") == 0 {
				#expect(c1[0] - c0[0] == 2 && c1[1] - c0[1] == 2, "taken \(c1[0] - c0[0]), used \(c1[1] - c0[1])")
				#expect(c2[0] == c1[0], "a value the var keeps lends its cell")
			} else {
				#expect(c1[1] == c0[1] && c2[1] == c0[1])
			}
		}

		// The levels are written out: a unit over the compiled core gets no known path.
		@Test func takeOutGetMovesTheChildOutOfAUniqueParent() throws {
			clj_init()
			_ = try cljEval("(ns cp.takeout)")
			defer { clj_ns_set_current(clj_ns_user()) }
			try compiledEval(closed: true) {
				_ = try cljEval("(defn to-bump [m] (assoc m :a (let [c (get m :a)] (assoc c :b (conj (get c :b) 1)))))")
				_ = try cljEval("(defn to-fresh [] (to-bump (hash-map :a (hash-map :b (vector 0)))))")
			}
			_ = try cljEval("(def to-kept {:a {:b [0]}})")
			let t0 = clj_debug_taken_out()
			#expect(try cljEval("[(to-bump to-kept) to-kept]") == cljEval("[{:a {:b [0 1]}} {:a {:b [0]}}]"))
			let t1 = clj_debug_taken_out()
			#expect(try cljEval("(to-fresh)") == cljEval("{:a {:b [0 1]}}"))
			let t2 = clj_debug_taken_out()
			if t0 >= 0 && clj_reuse_enabled() && clj_no_opt() & clj_no_opt_parse("owned") == 0 {
				#expect(t1 == t0, "taken out of the var's map: \(t1 - t0)")
				#expect(t2 - t1 == 2, "taken out \(t2 - t1)")
			}
		}

		// u held to its rebinding would leave the child at rc 2 for the update, which then copies both levels.
		@Test func deadSlotIsDroppedBeforeTheUpdate() throws {
			clj_init()
			_ = try cljEval("(ns cp.drop)")
			defer { clj_ns_set_current(clj_ns_user()) }
			try compiledEval(closed: true) {
				_ = try cljEval("""
				(defn dr-steps [n]
				  (loop [i 0 st (hash-map :u (hash-map :a (vector 0))) acc 0]
				    (if (< i n)
				      (let [c (let [u (get st :u)] (if (nil? u) 0 (count (get u :a))))]
				        (recur (inc i) (assoc st :u (let [m (get st :u)] (assoc m :a (conj (get m :a) i)))) (+ acc c)))
				      [st acc])))
				""")
			}
			let t0 = clj_debug_taken_out()
			#expect(try cljEval("(dr-steps 3)") == cljEval("[{:u {:a [0 0 1 2]}} 6]"))
			let t1 = clj_debug_taken_out()
			if t0 >= 0 && clj_reuse_enabled() && clj_no_opt() & clj_no_opt_parse("owned,drop") == 0 {
				#expect(t1 - t0 == 6, "taken out \(t1 - t0)")
			}
		}

		// Step 1's accumulator is the var's, so only the later steps reach the cons at rc 1.
		@Test func loweredReduceHandsTheAccumulatorOver() throws {
			clj_init()
			_ = try cljEval("(ns cp.lowacc)")
			defer { clj_ns_set_current(clj_ns_user()) }
			try compiledEval(closed: true) {
				_ = try cljEval("(defn la-steps [l xs] (reduce (fn [s x] (cons (+ x (first s)) (rest s))) l xs))")
			}
			_ = try cljEval("(def la-kept (list 0 1 2))")
			_ = try cljEval("(defn li-steps [m xs] (reduce (fn [a x] (assoc a x x)) m xs))")
			let on = clj_no_opt() == 0
			var c0 = [Int64](repeating: 0, count: 3), c1 = c0
			clj_debug_reuse_counts(&c0)
			#expect(try cljEval("[(la-steps la-kept [1 2 3]) la-kept]") == [Value(list: [6, 1, 2]), Value(list: [0, 1, 2])])
			clj_debug_reuse_counts(&c1)
			if clj_reuse_enabled() && on {
				#expect(c1[0] - c0[0] == 2 && c1[1] - c0[1] == 2, "taken \(c1[0] - c0[0]), used \(c1[1] - c0[1])")
			} else if !on {
				#expect(c1[1] - c0[1] <= 2)
			}
			let consuming = clj_debug_consuming_calls()
			#expect(try cljEval("(li-steps {} [1 2 3])") == cljEval("{1 1 2 2 3 3}"))
			// -1 outside a debug build; under CLJ_EVAL=compiled li-steps is compiled, whose assoc calls its owned form directly
			if consuming >= 0 && on && getenv("CLJ_EVAL") == nil { #expect(clj_debug_consuming_calls() - consuming == 3) }
		}

		// Without the let-go only the inner stage's first cell, a fresh vector-seq, reaches its token at rc 1.
		@Test func lazyThunksLetTheirCapturesGoAtTheTailCons() throws {
			clj_init()
			_ = try cljEval("(ns cp.letgo)")
			defer { clj_ns_set_current(clj_ns_user()) }
			try compiledEval {
				_ = try cljEval("(defn lg-map [f coll] (lazy-seq (when-let [s (seq coll)] (cons (f (first s)) (lg-map f (rest s))))))")
			}
			var c0 = [Int64](repeating: 0, count: 3), c1 = c0
			clj_debug_reuse_counts(&c0)
			#expect(try cljEval("(doall (lg-map inc (lg-map inc [1 2 3 4])))") == Value(list: [3, 4, 5, 6]))
			clj_debug_reuse_counts(&c1)
			let owned = clj_no_opt() & clj_no_opt_parse("owned") == 0
			if clj_reuse_enabled() {
				let want: Int64 = owned ? 8 : 1
				// the system allocator records no size, so a vector-seq's cell takes no cons: only the outer stage's
				let system = getenv("CLJ_SYSTEM_ALLOC").map { $0.pointee != 0 && $0.pointee != 0x30 } ?? false
				let used: Int64 = system ? (owned ? 4 : 0) : want
				#expect(c1[0] - c0[0] == want && c1[1] - c0[1] == used, "taken \(c1[0] - c0[0]), used \(c1[1] - c0[1])")
			} else {
				#expect(c1[1] == c0[1])
			}
			// Shared: every thread parks on a cell another one is forcing, and each sees the whole seq.
			let s = try cljEval("(def lg-shared (lg-map inc (lg-map inc (vec (range 2000))))) lg-shared")
			#expect(clj_is_shared(s.raw))
			let threads = 8
			var sums = [Int](repeating: 0, count: threads)
			sums.withUnsafeMutableBufferPointer { sp in
				// Each iteration writes its own index; the closure outlives nothing.
				nonisolated(unsafe) let sp = sp
				DispatchQueue.concurrentPerform(iterations: threads) { i in
					var it = clj_seq_iter_start(s.raw)
					var item: clj_value = CLJ_NIL
					var sum = 0
					while clj_seq_iter_next(&it, &item) { sum += clj_fixnum_val(item) }
					sp[i] = sum
				}
			}
			#expect(sums.allSatisfy { $0 == 2_003_000 })
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

		// A fault in a prologue still names the frame's caller; each Swift depth moves the fault to another instruction.
		@Test func anOverflowTraceKeepsTheFaultingFramesCaller() throws {
			clj_init()
			_ = try cljEval("(ns cp.prologue)")
			defer { clj_ns_set_current(clj_ns_user()) }
			@inline(never) func nested(_ depth: Int, _ f: Value) throws -> Value {
				if depth == 0 { return try f(0) }
				let r = try nested(depth - 1, f)
				return withExtendedLifetime(depth) { r }
			}
			for closed in [false, true] {
				try compiledEval(closed: closed) {
					_ = try cljEval("(declare pr-b) (let [] (defn pr-a [n] (pr-b (inc n))) (defn pr-b [n] (pr-a (inc n))))")
				}
				let f = try cljEval("pr-a")
				for depth in 0..<48 {
					do {
						_ = try nested(depth, f)
						Issue.record("no overflow")
					} catch let e as ClojureError {
						let names = e.trace.map(\.fn)
						#expect(e.message == "Stack overflow" && zip(names, names.dropFirst()).allSatisfy { $0 != $1 }, "closed: \(closed), depth \(depth): \(names.prefix(4))")
					}
				}
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

		// The loading execution fills a unit's constant pool; every execution running its code retains from it.
		@Test func aUnitsConstantsAreReadByOtherExecutions() throws {
			clj_init()
			_ = try cljEval("(ns cp.consts (:require [clojure.core.async :refer [go thread <!!]]))")
			defer { clj_ns_set_current(clj_ns_user()) }
			// Polled, not a timed deref: its timeout channel outlives the test on the timer thread, under a later baseline.
			let define = """
				(defn cp-lit [] [1 2 {:a "s" :v ["t"]}])
				(defn cp-on-a-new-host-thread []
				  (let [done (atom nil)
				        o (objc-reify {} (["run:" "v@:@"] [self arg] (reset! done (cp-lit))))]
				    (.detach-new-thread-selector (objc-class "NSThread") "run:" :to-target o :with-object nil)
				    (loop [i 0] (when (and (nil? @done) (< i 500)) (Thread/sleep 10) (recur (inc i))))
				    @done))
				"""
			let run = "[(<!! (go (cp-lit))) @(future (cp-lit)) (<!! (thread (cp-lit))) (cp-on-a-new-host-thread)]"
			let lit = "[1 2 {:a \"s\", :v [\"t\"]}]"
			let expected = "[\(lit) \(lit) \(lit) \(lit)]"
			_ = try cljEval(define)
			#expect(try cljEval(run).description == expected, "interpreted")
			for closed in [false, true] {
				let out = try compiledEval(closed: closed) {
					_ = try cljEval(define)
					return try cljEval(run).description
				}
				#expect(out == expected, "closed: \(closed)")
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

		// An emitted `=` site checks clj_eq's result like any intrinsic's: an expiry in a thunk it forced is no answer.
		@Test func compiledEqualsRethrowsAnExpiryInsideIt() throws {
			clj_init()
			_ = try cljEval("(ns cp.eqexpiry)")
			defer { clj_ns_set_current(clj_ns_user()); clj_deadline_set_ms(0) }
			for closed in [false, true] {
				try compiledEval(closed: closed) {
					_ = try cljEval("""
						(defn cp-eq-spin [] (= (lazy-seq (loop [] (recur))) [1]))
						(defn cp-not-eq-spin [] (not= [1] (lazy-seq (loop [] (recur)))))
						""")
				}
				for call in ["(cp-eq-spin)", "(cp-not-eq-spin)"] {
					clj_deadline_set_ms(100)
					#expect(cljEvalError(call)?.contains("Execution timed out") == true, "\(call), closed: \(closed)")
					clj_deadline_set_ms(0)
				}
			}
		}

		// The compiler emits its own loop tick, so the shared predicate has to answer both ways through it: a
		// cancellation unwinds the emitted loop, a suspension parks inside it and the loop goes on afterwards.
		@Test func compiledLoopTickParksOnASuspend() throws {
			clj_init()
			try cljTimingSupport()
			_ = try cljEval("(ns cp.suspend (:require [clojure.core.async :refer [go timeout <!! cancel! suspend! resume!]]))")
			defer { clj_ns_set_current(clj_ns_user()) }
			_ = try cljEval("(refer 'test-support)")
			let coros = clj_debug_live_coros()
			for closed in [false, true] {
				try compiledEval(closed: closed) {
					_ = try cljEval("(defn cp-spin-bump [r] (loop [] (swap! r inc) (recur)))")
				}
				let got = try cljEval("""
					(let [r (atom 0)
					      g (go (try (cp-spin-bump r) (catch :cancelled e :done)))]
					  (await-true 10000 #(pos? @r))
					  (suspend! g)
					  (let [gated (await-true 10000 #(gated? g))
					        a @r]
					    (<!! (timeout 10))
					    (let [b @r]
					      (resume! g)
					      (let [moved (await-true 10000 #(> @r b))]
					        (cancel! g)
					        [gated (= a b) moved (join g 10000)]))))
					""")
				#expect(got == [true, true, true, Value(keyword: "done")], "closed: \(closed), \(got)")
			}
			#expect(clj_debug_coro_settle(coros, 5000))
		}

		// One compile of the sources as files, each unit loaded and registered for its path.
		private func loadUnits(_ sources: [(source: String, file: String)], tag: String) throws {
			var opts = cljc_options()
			opts.line = true
			opts.skip_embedded = true
			let c = cljc_new(&opts)!
			defer { cljc_free(c) }
			cljc_begin(c)
			_ = try capturingOutput { for s in sources { try loadFixtureSource(s.source, file: s.file) } }
			cljc_end(c)
			try #require(cljc_unit_count(c) == sources.count)
			var o = evalOptions()
			for i in 0..<sources.count {
				let text = cljc_unit_text(c, i, nil)!
				defer { free(text) }
				guard let unit = cljc_load_dylib(&o, "xu_\(tag)_\(i)", text) else { throw ClojureError.takePending() }
				clj_compiled_register(unit.pointee.path, unit.pointee.`init`)
			}
		}

		// A copy of another unit's fn runs only under a root compiled from the same tree, a reloaded one included; poly's
		// sites call the unit's one outlined copy of it under the same guard.
		@Test func crossUnitInlineFollowsTheCalleesTree() throws {
			clj_init()
			defer { clj_ns_set_current(clj_ns_user()) }
			let dir = String(cString: jitDir!)
			let lib = "(ns cp.xu-lib)\n(defn sq [x] (* x x))\n(defn poly [x] (+ (* x x) (* 3 x) 1))\n"
			let app = "(ns cp.xu-app)\n(defn f [y] (cp.xu-lib/sq (inc y)))\n(defn g [y] [(cp.xu-lib/poly (inc y)) (cp.xu-lib/poly y)])\n"
			try loadUnits([(lib, "\(dir)/xu/lib.clj"), (app, "\(dir)/xu/app.clj")], tag: "a")
			_ = try runFixtureUnit("\(dir)/xu/lib.clj")
			_ = try runFixtureUnit("\(dir)/xu/app.clj")
			let inlining = clj_no_opt() & (clj_no_opt_parse("inline") | clj_no_opt_parse("passes")) == 0
			let hit0 = clj_debug_tree_matches(true), miss0 = clj_debug_tree_matches(false)
			#expect(try cljEval("(cp.xu-app/f 2)") == 9)
			#expect(try cljEval("(cp.xu-app/g 2)") == cljEval("[19 11]"))
			if inlining { #expect(clj_debug_tree_matches(true) > hit0, "the copy never ran") }
			#expect(try cljEval("(with-redefs [cp.xu-lib/sq (fn [x] (- x))] (cp.xu-app/f 2))") == -3)
			#expect(try cljEval("(with-redefs [cp.xu-lib/poly (fn [x] (- x))] (cp.xu-app/g 2))") == cljEval("[-3 -2]"))
			#expect(try cljEval("(cp.xu-app/f 2)") == 9)
			#expect(try cljEval("(cp.xu-app/g 2)") == cljEval("[19 11]"))
			_ = try cljEval("(alter-var-root #'cp.xu-lib/sq (constantly (fn [x] (+ x 100))))")
			_ = try cljEval("(alter-var-root #'cp.xu-lib/poly (constantly (fn [x] (+ x 100))))")
			#expect(try cljEval("(cp.xu-app/f 2)") == 103)
			#expect(try cljEval("(cp.xu-app/g 2)") == cljEval("[103 102]"))
			_ = try cljEval("(in-ns 'cp.xu-lib) (defn sq [x] (+ x x)) (defn poly [x] (* x 10))")
			#expect(try cljEval("(cp.xu-app/f 2)") == 6)
			#expect(try cljEval("(cp.xu-app/g 2)") == cljEval("[30 20]"))
			// another body compiled: another digest, so the copy of the first one must not run
			try loadUnits([("(ns cp.xu-lib)\n(defn sq [x] (- x x))\n(defn poly [x] (- (* x x) (* 3 x) 1))\n", "\(dir)/xu/lib2.clj")], tag: "b")
			_ = try runFixtureUnit("\(dir)/xu/lib2.clj")
			#expect(try cljEval("(cp.xu-app/f 2)") == 0)
			#expect(try cljEval("(cp.xu-app/g 2)") == cljEval("[-1 -3]"))
			if inlining { #expect(clj_debug_tree_matches(false) > miss0, "the other body was never looked up") }
			// the same body in a new unit: a new dispatcher of the copied tree
			let hit1 = clj_debug_tree_matches(true)
			try loadUnits([(lib, "\(dir)/xu/lib3.clj")], tag: "c")
			_ = try runFixtureUnit("\(dir)/xu/lib3.clj")
			#expect(try cljEval("(cp.xu-app/f 2)") == 9)
			#expect(try cljEval("(cp.xu-app/g 2)") == cljEval("[19 11]"))
			if inlining { #expect(clj_debug_tree_matches(true) > hit1, "the reloaded tree was not recognized") }
		}

		// clj-compile's core is interpreted, so its unit carries copies of core fns that this compiled core must vouch for.
		@Test func devUnitCopiesOfCoreFollowTheirVars() throws {
			clj_init()
			defer { clj_ns_set_current(clj_ns_user()) }
			guard !coreRunsInterpreted() else { return }
			let tool = ProcessInfo.processInfo.environment["CLJ_COMPILE"] ?? packageRoot.appendingPathComponent(".build/plain/debug/clj-compile").path
			try #require(FileManager.default.isExecutableFile(atPath: tool), "\(tool): make test-compiled builds the plain clj-compile first")
			let file = packageRoot.appendingPathComponent("Tests/PippinTests/Fixtures/dev-inline/core.clj").path
			let expected = try String(contentsOfFile: packageRoot.appendingPathComponent("Tests/PippinTests/Fixtures/dev-inline/core.out").path, encoding: .utf8)
			let out = URL(fileURLWithPath: String(cString: jitDir!)).appendingPathComponent("dev-inline")
			try? FileManager.default.removeItem(at: out)
			try FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
			let proc = Process()
			proc.executableURL = URL(fileURLWithPath: tool)
			proc.arguments = ["--stats", "--file", file, "--out", out.path]
			let stderr = Pipe()
			proc.standardError = stderr
			proc.standardOutput = FileHandle.nullDevice
			try proc.run()
			let stats = String(decoding: stderr.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
			proc.waitUntilExit()
			try #require(proc.terminationStatus == 0, "clj-compile: \(stats)")
			let inlining = clj_no_opt() & (clj_no_opt_parse("inline") | clj_no_opt_parse("passes")) == 0
			// mod in m7 and in m7's copies, each a call of the unit's one outlined copy of mod; max-key calls k twice: a call
			let cross = stats.components(separatedBy: "of which from another unit ").dropFirst().first.flatMap { Int($0.prefix { $0.isNumber }) } ?? 0
			let outlined = stats.components(separatedBy: "(outlined ").dropFirst().first.flatMap { Int($0.prefix { $0.isNumber }) } ?? 0
			if inlining { #expect(cross >= 2 && outlined >= 1, "\(stats)") }
			var o = evalOptions()
			for line in try String(contentsOf: out.appendingPathComponent("units.txt"), encoding: .utf8).split(separator: "\n") {
				let cells = line.split(separator: "\t", maxSplits: 1).map(String.init)
				let text = try String(contentsOf: out.appendingPathComponent("\(cells[0]).c"), encoding: .utf8)
				guard let unit = cljc_load_dylib(&o, "devinline_\(cells[0])", text) else { throw ClojureError.takePending() }
				#expect(String(cString: unit.pointee.path) == cells[1])
				clj_compiled_register(unit.pointee.path, unit.pointee.`init`)
			}
			let hit0 = clj_debug_tree_matches(true)
			#expect(try runFixtureUnit(file) == expected)
			if inlining { #expect(clj_debug_tree_matches(true) > hit0, "no copy of a core fn ran") }
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
