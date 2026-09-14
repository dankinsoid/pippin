// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

private struct MyError: Error, Equatable {
	let code: Int
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

// Counts live instances, to see a fn's Swift context released with the fn.
private final class Token {
	nonisolated(unsafe) static var live = 0
	init() { Token.live += 1 }
	deinit { Token.live -= 1 }
}

extension CoreTests {
	@Suite struct HostErrorTests {
		let rt = Runtime()

		init() {
			for k in ["host/error", "k", "caught", "default", "info", "other"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		// Binds a declared user var to a Swift-made value, as a def would.
		private func define(_ name: String, _ value: Value) {
			let sym = Value(symbol: name)
			withExtendedLifetime((sym, value)) { clj_var_bind_root(clj_ns_resolve(clj_ns_user(), sym.raw), value.raw) }
		}

		@Test func swiftClosuresAreFns() throws {
			let before = clj_debug_live_objects()
			do {
				let add = Value(function: "he-add", arity: 2...2) { args in Value(args[0].int! + args[1].int!) }
				#expect(add.isFn)
				#expect(try add(1, 2) == 3)
				let apply = try rt.eval("(fn [f] [(f 1 2) (apply f [3 4]) (map f [1 2] [10 20])])")
				#expect(try apply(add) == [3, 7, Value(list: [11, 22])])
				do {
					_ = try add(1)
					Issue.record("expected an arity error")
				} catch let e as ClojureError {
					#expect(e.message == "Wrong number of args (1) passed to: he-add")
				}
				let count = Value(function: nil) { args in Value(args.count) }
				#expect(try count() == 0)
				#expect(try count(1, 2, 3) == 3)
				let atLeastOne = Value(function: "he-one", arity: 1...3) { _ in nil }
				#expect(throws: ClojureError.self) { try atLeastOne() }
				#expect(throws: ClojureError.self) { try atLeastOne(1, 2, 3, 4) }
				#expect(try atLeastOne(1, 2, 3) == nil)
				#expect(add.description == "#object[fn he-add]")
				#expect(count.description == "#object[fn]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func fnReleasesItsContext() throws {
			let before = clj_debug_live_objects()
			do {
				let token = Token()
				let f = Value(function: "he-token") { _ in
					_ = token
					return 1
				}
				#expect(Token.live == 1)
				#expect(try f() == 1)
				_ = consume f
			}
			#expect(Token.live == 0)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func swiftErrorIsCaughtInClojure() throws {
			let before = clj_debug_live_objects()
			do {
				let boom = Value(function: "he-boom") { _ in throw MyError(code: 7) }
				let byDefault = try rt.eval("(fn [f] (try (f) (catch :default e [(ex-message e) (ex-cause e)])))")
				#expect(try byDefault(boom) == ["MyError(code: 7)", nil])
				let byInfo = try rt.eval("(fn [f] (try (f) (catch ExceptionInfo e :caught)))")
				#expect(try byInfo(boom) == kw("caught"))
				// ex-data carries the error itself, so Clojure code can hand it back to Swift.
				let data = try rt.eval("(fn [f] (try (f) (catch :default e (ex-data e))))")
				let map = try #require(try data(boom).dictionary)
				let e = try #require(map[kw("host/error")])
				#expect(e.isException)
				#expect(e.hostError as? MyError == MyError(code: 7))
				#expect(e.description == "#error {:message \"MyError(code: 7)\", :data {:host/error #object[host-error]}}")
				#expect(e.typeName == "host-error")
				// A host error is a legal ex-info cause.
				let wrapped = try rt.eval("(fn [f] (try (f) (catch :default e (throw (ex-info \"wrapped\" {:k 1} e)))))")
				do {
					_ = try wrapped(boom)
					Issue.record("expected a ClojureError")
				} catch let err as ClojureError {
					#expect(err.message == "wrapped")
					#expect(err.data == [kw("k"): 1])
					#expect(err.cause.hostError as? MyError == MyError(code: 7))
					#expect(err.causeError?.message == "MyError(code: 7)")
				}
				// finally runs while the Swift error unwinds.
				let finallyOrder = try rt.eval("(fn [f] (try (f) (finally (println \"finally\"))))")
				var thrown: (any Error)?
				let out = capturingOutput {
					do { _ = try finallyOrder(boom) } catch { thrown = error }
				}
				#expect(out == "finally\n")
				#expect(thrown as? MyError == MyError(code: 7))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func uncaughtSwiftErrorComesBackAsItself() throws {
			try declare("he-fail", "he-call")
			let before = clj_debug_live_objects()
			do {
				define("he-fail", Value(function: "he-fail") { args in throw MyError(code: args.first?.int ?? 0) })
				do {
					_ = try rt.eval("(+ 1 (he-fail 3))")
					Issue.record("expected MyError")
				} catch let e as MyError {
					#expect(e == MyError(code: 3))
				}
				do {
					_ = try rt.eval("(try (he-fail 4) (catch :default e (throw e)))")
					Issue.record("expected MyError")
				} catch let e as MyError {
					#expect(e == MyError(code: 4))
				}
				let call = try rt.eval("(fn [f] (f 5))")
				do {
					_ = try call(try rt.eval("he-fail"))
					Issue.record("expected MyError")
				} catch let e as MyError {
					#expect(e == MyError(code: 5))
				}
				// Through a second Swift frame the same error keeps its identity: the box is not re-wrapped.
				define("he-call", Value(function: "he-call") { args in try args[0]() })
				do {
					_ = try rt.eval("(he-call he-fail)")
					Issue.record("expected MyError")
				} catch let e as MyError {
					#expect(e == MyError(code: 0))
				}
				#expect(try rt.eval("(try (he-call he-fail) (catch :default e (ex-message e)))") == "MyError(code: 0)")
				// Errors from the core reach Swift as ClojureError, and only when uncaught.
				#expect(throws: ClojureError.self) { try rt.eval("(he-call (fn [] (+ 1 nil)))") }
				#expect(try rt.eval("(try (he-call (fn [] (+ 1 nil))) (catch ExceptionInfo e (ex-message e)))") == "nil cannot be cast to a number")
				try unbind("he-fail", "he-call")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func clojureValuesRoundTripThroughSwift() throws {
			let before = clj_debug_live_objects()
			do {
				let passthrough = Value(function: "he-pass") { args in try args[0]() }
				let viaSwift = try rt.eval("(fn [pass] (try (pass (fn [] (throw (ex-info \"m\" {:k 1})))) (catch ExceptionInfo e [(ex-message e) (ex-data e)])))")
				#expect(try viaSwift(passthrough) == ["m", [kw("k"): 1]])
				let identity = try rt.eval("(fn [pass] (let [ex (ex-info \"m\" {})] (try (pass (fn [] (throw ex))) (catch :default e (= e ex)))))")
				#expect(try identity(passthrough) == true)
				let plain = try rt.eval("(fn [pass] (try (pass (fn [] (throw 42))) (catch ExceptionInfo e :info) (catch :default e e)))")
				#expect(try plain(passthrough) == 42)
				let str = try rt.eval("(fn [pass] (try (pass (fn [] (throw \"s\"))) (catch :default e (ex-message e))))")
				#expect(try str(passthrough) == "s")
				// A ClojureError thrown by Swift code on its own also rethrows the value it wraps.
				let rethrow = Value(function: "he-rethrow") { _ in throw ClojureError(thrown: [1, 2]) }
				let caught = try rt.eval("(fn [f] (try (f) (catch :default e e)))")
				#expect(try caught(rethrow) == [1, 2])
				do {
					_ = try rethrow()
					Issue.record("expected a ClojureError")
				} catch let e as ClojureError {
					#expect(e.thrown == [1, 2])
					#expect(e.message == "Thrown value: [1 2]")
				}
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
