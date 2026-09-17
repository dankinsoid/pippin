// @ai-generated(guided)
import CljCore
import Darwin
import Testing
@testable import Pippin

private func kw(_ s: String) -> Value { Value(keyword: s) }

private func clojureError(_ rt: Runtime, _ source: String) -> ClojureError? {
	do {
		_ = try rt.eval(source)
		return nil
	} catch let e as ClojureError {
		return e
	} catch {
		return nil
	}
}

private typealias Frame = ClojureError.Frame

private final class ThreadBox: @unchecked Sendable {
	var result: Result<String, any Error>?
	let body: () throws -> String
	init(_ body: @escaping () throws -> String) { self.body = body }
}

// Runs body on a fresh pthread and waits for it; the body's error or result comes back.
private func onPthread(_ body: @escaping () throws -> String) throws -> String {
	let box = ThreadBox(body)
	var thread: pthread_t?
	let created = pthread_create(&thread, nil, { arg in
		let box = Unmanaged<ThreadBox>.fromOpaque(arg).takeUnretainedValue()
		box.result = Result { try box.body() }
		return nil
	}, Unmanaged.passUnretained(box).toOpaque())
	precondition(created == 0)
	pthread_join(thread!, nil)
	return try box.result!.get()
}

// Everything written to the pipe's read end so far; the writer must have closed its end.
private func drain(_ fd: Int32) -> String {
	var out: [UInt8] = []
	var buf = [UInt8](repeating: 0, count: 4096)
	while true {
		let n = read(fd, &buf, buf.count)
		if n <= 0 { break }
		out += buf[0..<n]
	}
	close(fd)
	return String(decoding: out, as: UTF8.self)
}

extension CoreTests {
	@Suite struct TraceTests {
		let rt = Runtime()

		init() {
			for k in ["fn", "line", "column", "x", "default", "k", "bottom", "no", "h"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		// Each frame is a fn and the position of the call that entered it; the outermost is the top-level call.
		@Test func threeDeepChain() throws {
			try declare("tr-a", "tr-b", "tr-c")
			let before = clj_debug_live_objects()
			do {
				let e = try #require(clojureError(rt, """
				(defn tr-c [x] (throw (ex-info "deep" {:x x})))
				(defn tr-b [x] (tr-c (inc x)))
				(defn tr-a [x]
				  (tr-b (inc x)))
				(tr-a 1)
				"""))
				#expect(e.message == "deep")
				#expect(e.data == [kw("x"): 3])
				#expect(e.trace == [Frame(fn: "user/tr-c", line: 2, column: 16), Frame(fn: "user/tr-b", line: 4, column: 3),
				                    Frame(fn: "user/tr-a", line: 5, column: 1)])
				#expect(e.description == "deep\n\tat user/tr-c (2:16)\n\tat user/tr-b (4:3)\n\tat user/tr-a (5:1)")
				// The same trace as Clojure data, and the shadow stack is empty again.
				#expect(try rt.eval("(try (tr-a 1) (catch :default e (ex-trace e)))")
					== [[kw("fn"): Value(symbol: "user/tr-c"), kw("line"): 2, kw("column"): 16],
					    [kw("fn"): Value(symbol: "user/tr-b"), kw("line"): 4, kw("column"): 3],
					    [kw("fn"): Value(symbol: "user/tr-a"), kw("line"): 1, kw("column"): 6]])
				#expect(clj_shadow_stack_depth() == 0)
				// An anonymous fn has no name; a call from a native (apply) reports the fn's own position.
				let anon = try #require(clojureError(rt, "(apply (fn [x] (throw x)) [42])"))
				#expect(anon.trace == [Frame(fn: nil, line: 1, column: 8)])
				#expect(anon.description == "Thrown value: 42\n\tat fn (1:8)")
				// A throw at top level has no frames.
				#expect(clojureError(rt, "(throw 1)")?.trace == [])
				try unbind("tr-a", "tr-b", "tr-c")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// An ex-info keeps its first frames; a non-error value is re-traced when a handler rethrows it.
		@Test func keptAcrossRethrow() throws {
			try declare("tr-inner", "tr-outer", "tr-again")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("""
				(defn tr-inner [] (throw (ex-info "once" nil)))
				(defn tr-outer [] (try (tr-inner) (catch :default e (throw e))))
				(defn tr-again [e] (throw e))
				""")
				let e = try #require(clojureError(rt, "(tr-outer)"))
				#expect(e.trace.map(\.fn) == ["user/tr-inner", "user/tr-outer"])
				#expect(try rt.eval("(let [e (try (tr-outer) (catch :default e e))] (= (ex-trace e) (try (tr-again e) (catch :default e2 (ex-trace e2)))))") == true)
				#expect(try rt.eval("(count (ex-trace (try (tr-outer) (catch :default e e))))") == 2)
				// A handler that does not match, and a finally, both pass the frames through.
				let unmatched = try #require(clojureError(rt, "(try (tr-inner) (catch ExceptionInfo e (throw e)) (finally nil))"))
				#expect(unmatched.trace.map(\.fn) == ["user/tr-inner"])
				let nonError = try #require(clojureError(rt, "((fn tr-v [] (try (throw 42) (catch ExceptionInfo e :no) (finally nil))))"))
				#expect(nonError.trace.map(\.fn) == ["tr-v"])
				// Rethrowing a caught non-error value records the frames of the rethrow.
				let rethrown = try #require(clojureError(rt, "(tr-again (try ((fn tr-v [] (throw 42))) (catch :default e e)))"))
				#expect(rethrown.trace.map(\.fn) == ["user/tr-again"])
				#expect(try rt.eval("(ex-trace (ex-info \"never\" nil))") == nil)
				#expect(try rt.eval("[(ex-trace 1) (ex-trace nil) (ex-trace \"s\")]") == [nil, nil, nil])
				try unbind("tr-inner", "tr-outer", "tr-again")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nonErrorValue() throws {
			try declare("tr-nv")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defn tr-nv [x] (if (pos? x) (tr-nv (dec x)) (throw [:bottom x])))")
				let e = try #require(clojureError(rt, "(tr-nv 2)"))
				#expect(e.thrown == [kw("bottom"), 0])
				#expect(e.trace.count == 3)
				#expect(e.trace.map(\.fn) == ["user/tr-nv", "user/tr-nv", "user/tr-nv"])
				#expect(e.trace[0].column == 30 && e.trace[2].column == 1)
				#expect(clj_shadow_stack_depth() == 0)
				try unbind("tr-nv")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The host fn is a leaf: frames on both sides of it survive the Swift rethrow at the boundary.
		@Test func throughHostClosure() throws {
			try declare("tr-host", "tr-callee", "tr-caller")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defn tr-callee [x] (throw (ex-info \"from callee\" {:x x})))")
				let callee = try rt.eval("tr-callee")
				let host = Value(function: "tr-host", arity: 1...1) { args in try callee(args[0]) }
				let sym = Value(symbol: "tr-host")
				withExtendedLifetime((sym, host)) { clj_var_bind_root(clj_ns_resolve(clj_ns_user(), sym.raw), host.raw) }
				_ = try rt.eval("(defn tr-caller [x] (tr-host (inc x)))")
				let e = try #require(clojureError(rt, "(tr-caller 1)"))
				#expect(e.message == "from callee")
				#expect(e.trace == [Frame(fn: "user/tr-callee", line: 1, column: 1), Frame(fn: "user/tr-caller", line: 1, column: 1)])
				// A non-error value thrown through the boundary keeps its frames too.
				let hostNonError = Value(function: "tr-host", arity: 1...1) { args in try rt.eval("((fn tr-h [] (throw :h)))") }
				withExtendedLifetime((sym, hostNonError)) { clj_var_bind_root(clj_ns_resolve(clj_ns_user(), sym.raw), hostNonError.raw) }
				let ne = try #require(clojureError(rt, "(tr-caller 1)"))
				#expect(ne.thrown == kw("h"))
				#expect(ne.trace.map(\.fn) == ["tr-h", "user/tr-caller"])
				#expect(clj_shadow_stack_depth() == 0)
				try unbind("tr-host", "tr-callee", "tr-caller")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func depthReturnsToZero() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(clj_shadow_stack_depth() == 0)
				#expect(try rt.eval("((fn f [n] (if (pos? n) (recur (dec n)) n)) 10)") == 0)
				#expect(clj_shadow_stack_depth() == 0)
				#expect(try rt.eval("((fn f [n] (if (pos? n) (f (dec n)) n)) 10)") == 0)
				#expect(clj_shadow_stack_depth() == 0)
				#expect(try rt.eval("(try ((fn f [n] (if (pos? n) (f (dec n)) (throw n))) 10) (catch :default e e))") == 0)
				#expect(clj_shadow_stack_depth() == 0)
				// A stack overflow unwinds every frame.
				#expect(clojureError(rt, "((fn f [n] (inc (f n))) 0)")?.message == "Stack overflow")
				#expect(clj_shadow_stack_depth() == 0)
				#expect(clj_shadow_stack_dropped() == 0)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Past the capacity the innermost frames are kept and the outermost counted as dropped.
		@Test func droppedFramesNearCapacity() throws {
			try declare("tr-deep", "clj-shadow-depth", "clj-shadow-dropped")
			let before = clj_debug_live_objects()
			do {
				let depth = Value(function: "clj-shadow-depth") { _ in Value(clj_shadow_stack_depth()) }
				let dropped = Value(function: "clj-shadow-dropped") { _ in Value(clj_shadow_stack_dropped()) }
				for (name, fn) in [("clj-shadow-depth", depth), ("clj-shadow-dropped", dropped)] {
					let sym = Value(symbol: name)
					withExtendedLifetime((sym, fn)) { clj_var_bind_root(clj_ns_resolve(clj_ns_user(), sym.raw), fn.raw) }
				}
				_ = try rt.eval("(defn tr-deep [n] (if (pos? n) (tr-deep (dec n)) [(clj-shadow-depth) (clj-shadow-dropped) (ex-trace (try (throw (ex-info \"deep\" nil)) (catch :default e e)))]))")
				clj_debug_shadow_stack_set_capacity(16)
				defer { clj_debug_shadow_stack_set_capacity(Int(CLJ_SHADOW_CAPACITY)) }
				let result = try #require(try rt.eval("(tr-deep 39)").array)
				#expect(result[0] == 40)
				#expect(result[1] == 24)
				let trace = try #require(result[2].array)
				#expect(trace.count == 16)
				let name = Value(symbol: "user/tr-deep")
				#expect(trace.allSatisfy { withExtendedLifetime($0) { Value(borrowing: clj_map_get($0.raw, kw("fn").raw, CLJ_NIL)) } == name })
				#expect(clj_shadow_stack_depth() == 0)
				#expect(clj_shadow_stack_dropped() == 0)
				var frames = [clj_shadow_frame](repeating: clj_shadow_frame(), count: 4)
				#expect(clj_shadow_stack_snapshot(&frames, 4) == 0)
				try unbind("tr-deep", "clj-shadow-depth", "clj-shadow-dropped")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The handler writes with write(2) only; SIGUSR1 stands in for a crash so the process survives. The
		// signal goes to the evaluating thread by pthread_kill, so that thread is a plain pthread, not a pool one.
		@Test func crashHandlerWritesFrames() throws {
			try declare("tr-crash", "tr-raise")
			let before = clj_debug_live_objects()
			do {
				let raiser = Value(function: "tr-raise") { _ in
					#expect(pthread_kill(pthread_self(), SIGUSR1) == 0)
					return nil
				}
				let sym = Value(symbol: "tr-raise")
				withExtendedLifetime((sym, raiser)) { clj_var_bind_root(clj_ns_resolve(clj_ns_user(), sym.raw), raiser.raw) }
				_ = try rt.eval("""
				(defn tr-crash [n]
				  (if (pos? n) (tr-crash (dec n)) (tr-raise)))
				""")
				let text = try onPthread {
					var fds: [Int32] = [0, 0]
					let piped = pipe(&fds)
					#expect(piped == 0)
					clj_crash_handler_install_test(SIGUSR1, fds[1])
					_ = try rt.eval("(tr-crash 2)")
					close(fds[1])
					return drain(fds[0])
				}
				#expect(text == """
				clj: signal \(SIGUSR1), Clojure frames (innermost first):
				  at user/tr-crash (2:16)
				  at user/tr-crash (2:16)
				  at user/tr-crash (1:1)

				""")
				// Off the Clojure stack the report says so.
				let none = try onPthread {
					var fds: [Int32] = [0, 0]
					let piped = pipe(&fds)
					#expect(piped == 0)
					clj_crash_handler_install_test(SIGUSR1, fds[1])
					#expect(pthread_kill(pthread_self(), SIGUSR1) == 0)
					close(fds[1])
					return drain(fds[0])
				}
				#expect(none == "clj: signal \(SIGUSR1), Clojure frames (innermost first):\n  (none)\n")
				try unbind("tr-crash", "tr-raise")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
