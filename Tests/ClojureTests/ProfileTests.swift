// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

private func kw(_ s: String) -> Value { Value(keyword: s) }

// An analyzed tree with its exec table; releases both.
private final class Tree {
	let node: UnsafeMutablePointer<clj_node>
	let exec: clj_value

	init(_ source: String) throws {
		let form = try Value(reading: source)
		guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, nil) }) else { throw ClojureError.takePending() }
		self.node = node
		exec = clj_exec_new(node)
	}

	func run() throws -> Value {
		let raw = clj_exec_run(exec)
		if raw == CLJ_THROWN { throw ClojureError.takePending() }
		return Value(owning: raw)
	}

	func hits(_ n: UnsafePointer<clj_node>) -> UInt64 { clj_exec_hits(exec, n.pointee.id) }

	deinit {
		clj_release(exec)
		clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
	}
}

private func field(_ m: Value, _ key: String) -> Value {
	withExtendedLifetime(m) { Value(borrowing: clj_map_get(m.raw, kw(key).raw, CLJ_NIL)) }
}

extension CoreTests {
	@Suite struct ProfileTests {
		let rt = Runtime()

		init() {
			for k in ["fns", "name", "line", "column", "calls", "ns", "result", "profile", "a", "b", "default"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		// Calls are exact and inclusive time is positive; the caller sorts above what it calls.
		@Test func fnProfile() throws {
			try declare("pf-leaf", "pf-mid", "pf-top")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("""
				(defn pf-leaf [x] (+ x 1))
				(defn pf-mid [x] (pf-leaf (pf-leaf x)))
				(defn pf-top [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc (pf-mid i))) acc)))
				""")
				#expect(clj_profile_running() == false)
				clj_profile_start()
				#expect(clj_profile_running())
				#expect(try rt.eval("(pf-top 10)") == 65)
				let data = Value(owning: clj_profile_stop())
				#expect(clj_profile_running() == false)
				let fns = try #require(field(data, "fns").array)
				let rows = fns.map { (field($0, "name").description, field($0, "calls").int ?? -1, field($0, "ns").int ?? -1, field($0, "line").int ?? -1) }
				#expect(rows.map(\.0) == ["user/pf-top", "user/pf-mid", "user/pf-leaf"])
				#expect(rows.map(\.1) == [1, 10, 20])
				#expect(rows.allSatisfy { $0.2 > 0 })
				#expect(rows.map(\.3) == [3, 2, 1])
				#expect(rows[0].2 >= rows[1].2 && rows[1].2 >= rows[2].2)
				// Off again: nothing is recorded, and a stop without a start yields an empty report.
				#expect(try rt.eval("(pf-top 3)") == 9)
				#expect(Value(owning: clj_profile_stop()) == [kw("fns"): []])
				// The macro returns the result with the report; the profiler is off afterwards, thrown or not.
				let profiled = try rt.eval("(profile (pf-mid 1) (pf-mid 2))")
				#expect(field(profiled, "result") == 4)
				let names = try #require(field(field(profiled, "profile"), "fns").array).map { field($0, "name").description }
				#expect(names == ["user/pf-mid", "user/pf-leaf"])
				#expect(try rt.eval("(try (profile (pf-mid 1) (throw (ex-info \"in body\" nil))) (catch :default e (ex-message e)))") == "in body")
				#expect(clj_profile_running() == false)
				try unbind("pf-leaf", "pf-mid", "pf-top")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A stop while calls are in flight drops them: only a call timed from its entry is recorded. Macro
		// expansion runs closures too, so a form analyzed with the profiler on shows core.clj's macros.
		@Test func profileStopMidCall() throws {
			try declare("pf-stopper", "pf-run")
			let before = clj_debug_live_objects()
			do {
				let stop = Value(function: "pf-stopper") { _ in Value(owning: clj_profile_stop()) }
				let sym = Value(symbol: "pf-stopper")
				withExtendedLifetime((sym, stop)) { clj_var_bind_root(clj_ns_resolve(clj_ns_user(), sym.raw), stop.raw) }
				let run = try rt.eval("(def pf-run (fn pf-outer [] ((fn pf-inner [] 1)) (pf-stopper)))")
				clj_profile_start()
				let data = try run()
				#expect(try #require(field(data, "fns").array).map { field($0, "name").description } == ["pf-inner"])
				#expect(clj_profile_running() == false)
				clj_profile_start()
				#expect(try rt.eval("(let [x 1] x)") == 1)
				let expanded = Value(owning: clj_profile_stop())
				#expect(try #require(field(expanded, "fns").array).map { field($0, "name").description }.contains("clojure.core/let"))
				try unbind("pf-stopper", "pf-run")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func signpostsToggle() throws {
			try declare("sp-qualified")
			let before = clj_debug_live_objects()
			do {
				#expect(Runtime.signposts == false)
				Runtime.signposts = true
				#expect(Runtime.signposts)
				#expect(try rt.eval("((fn sp-named [x] ((fn [] x))) 7)") == 7)
				#expect(try rt.eval("(defn sp-qualified [x] x) (sp-qualified 8)") == 8)
				Runtime.signposts = false
				#expect(Runtime.signposts == false)
				#expect(try rt.eval("(def sp-qualified nil) 9") == 9)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Counting is a rewrite of the exec table: every node's eval swaps for a wrapper and back.
		@Test func execHitCounters() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("(let [f (fn [x] (if (< x 3) :a :b))] [(f 0) (f 1) (f 2) (f 5)])")
				let fn = tree.node.pointee.u.let.inits[0]!
				#expect(fn.pointee.kind == CLJ_NODE_DIRECT_FN)
				let body = fn.pointee.u.fn.fixed.1!.pointee.body!
				#expect(body.pointee.kind == CLJ_NODE_IF)
				let then = body.pointee.u.if_.then!, else_ = body.pointee.u.if_.else_!
				#expect(try tree.run() == [kw("a"), kw("a"), kw("a"), kw("b")])
				#expect(tree.hits(tree.node) == 0 && tree.hits(body) == 0)
				clj_exec_count(tree.exec, true)
				#expect(try tree.run() == [kw("a"), kw("a"), kw("a"), kw("b")])
				#expect(tree.hits(tree.node) == 1)
				#expect(tree.hits(body) == 4)
				#expect(tree.hits(then) == 3)
				#expect(tree.hits(else_) == 1)
				#expect(tree.hits(body.pointee.u.if_.test!) == 4)
				clj_exec_count(tree.exec, false)
				#expect(try tree.run() == [kw("a"), kw("a"), kw("a"), kw("b")])
				#expect(tree.hits(body) == 4 && tree.hits(then) == 3)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func liveObjectsPerType() throws {
			let probe: Value = [Value("probe")]
			let vectorType = clj_type_of(probe.raw), stringType = withExtendedLifetime(probe) { clj_type_of(clj_vector_nth(probe.raw, 0)) }
			let before = clj_debug_live_objects()
			let vectors = clj_debug_live_objects_of(vectorType), strings = clj_debug_live_objects_of(stringType)
			do {
				let v: Value = [1, 2, 3]
				let s = Value("held")
				#expect(clj_debug_live_objects_of(vectorType) == vectors + 1)
				#expect(clj_debug_live_objects_of(stringType) == strings + 1)
				// A vector is two objects: the wrapper and its tail node.
				#expect(clj_debug_live_objects() == before + 3)
				withExtendedLifetime((v, s)) {}
			}
			#expect(clj_debug_live_objects_of(vectorType) == vectors)
			#expect(clj_debug_live_objects_of(stringType) == strings)
			#expect(clj_debug_live_objects() == before)
			clj_debug_live_report()
		}
	}
}
