// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

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

private func message(_ rt: Runtime, _ source: String) -> String? { clojureError(rt, source)?.message }

extension CoreTests {
	@Suite struct TryCatchTests {
		let rt = Runtime()

		init() {
			for k in ["k", "default", "failed", "done", "caught", "inner", "outer", "never", "no", "yes", "other", "line", "column"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func throwAndCatch() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(try 1)") == 1)
				#expect(try rt.eval("(try)") == nil)
				#expect(try rt.eval("(try 1 2 3)") == 3)
				#expect(try rt.eval("(try (throw (ex-info \"m\" {:k 1})) (catch ExceptionInfo e (ex-data e)))") == [kw("k"): 1])
				#expect(try rt.eval("(try (throw (ex-info \"m\" {:k 1})) (catch :default e (ex-message e)))") == "m")
				#expect(try rt.eval("(try (throw (ex-info \"m\" nil)) (catch Throwable e :caught))") == kw("caught"))
				#expect(try rt.eval("(try (throw (ex-info \"m\" nil)) (catch Exception e :caught))") == kw("caught"))
				#expect(try rt.eval("(try (throw (ex-info \"m\" nil)) (catch Object e :caught))") == kw("caught"))
				#expect(try rt.eval("(try (throw 42) (catch :default e (inc e)))") == 43)
				#expect(try rt.eval("(try (throw nil) (catch :default e [e]))") == [nil])
				#expect(try rt.eval("(try (throw [1 2]) (catch Exception e e))") == [1, 2])
				// Clauses are tried in order; ExceptionInfo skips values that are not errors.
				#expect(try rt.eval("(try (throw 42) (catch ExceptionInfo e :inner) (catch :default e :outer))") == kw("outer"))
				#expect(try rt.eval("(try (throw (ex-info \"m\" nil)) (catch :default e :outer) (catch ExceptionInfo e :inner))") == kw("outer"))
				#expect(try rt.eval("(try (+ 1 nil) (catch ExceptionInfo e (ex-message e)))") == "nil cannot be cast to a number")
				#expect(try rt.eval("(try (nth [] 1) (catch :default e (ex-data e)))") == nil)
				// The binding is a local of the handler: closures and nested lets see it.
				#expect(try rt.eval("(try (throw (ex-info \"m\" nil)) (catch :default e (let [f (fn [] (ex-message e))] (f))))") == "m")
				#expect(try rt.eval("(let [e 1] (try (throw 2) (catch :default e e)))") == 2)
				#expect(try rt.eval("(let [e 1] [(try (throw 2) (catch :default e e)) e])") == [2, 1])
				#expect(try rt.eval("[(ex-message 1) (ex-data \"s\") (ex-cause nil) (ex-cause (ex-info \"m\" nil))]") == [nil, nil, nil, nil])
				#expect(try rt.eval("(ex-message (ex-cause (ex-info \"o\" nil (ex-info \"i\" nil))))") == "i")
				// A thrown string reads like an ex-info in a :default handler (ex-message is the string itself),
				// but it is no error: ExceptionInfo skips it and ex-data stays nil.
				#expect(try rt.eval("(try (throw \"msg\") (catch :default e [(ex-message e) (ex-data e) (ex-cause e)]))") == ["msg", nil, nil])
				#expect(try rt.eval("(try (throw \"msg\") (catch ExceptionInfo e :inner) (catch :default e :outer))") == kw("outer"))
				#expect(try rt.eval("(ex-message \"plain\")") == "plain")
				#expect(message(rt, "(throw \"msg\")") == "Thrown value: \"msg\"")
				// Uncaught: the thrown value reaches the host.
				let e = try #require(clojureError(rt, "(try (throw 42) (catch ExceptionInfo e :never))"))
				#expect(e.message == "Thrown value: 42")
				#expect(e.data == 42)
				#expect(e.thrown == 42)
				#expect(e.cause == nil)
				let info = try #require(clojureError(rt, "(throw (ex-info \"boom\" {:k 1}))"))
				#expect(info.message == "boom")
				#expect(info.data == [kw("k"): 1])
				#expect(info.thrown.isException)
				#expect(try rt.eval("(+ 1 2)") == 3)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func finallyRunsOnBothPaths() throws {
			let before = clj_debug_live_objects()
			do {
				var result: Value = nil
				var out = try capturingOutput {
					result = try rt.eval("(try (println \"body\") 1 (finally (println \"finally\")))")
				}
				#expect(result == 1)
				#expect(out == "body\nfinally\n")
				out = try capturingOutput {
					result = try rt.eval("(try (println \"body\") (throw (ex-info \"m\" nil)) (catch :default e (println \"catch\") 2) (finally (println \"finally\")))")
				}
				#expect(result == 2)
				#expect(out == "body\ncatch\nfinally\n")
				// finally's value is discarded.
				#expect(try rt.eval("(try 1 (finally 2))") == 1)
				#expect(try rt.eval("(try (throw 1) (catch :default e 2) (finally 3))") == 2)
				// finally runs on the way out of an uncaught exception, which then continues.
				out = try capturingOutput {
					result = try rt.eval("(try (try (throw (ex-info \"m\" {:k 1})) (finally (println \"inner\"))) (catch :default e (println \"outer\") (ex-data e)))")
				}
				#expect(result == [kw("k"): 1])
				#expect(out == "inner\nouter\n")
				out = try capturingOutput {
					result = try rt.eval("(try (try (throw 7) (catch ExceptionInfo e :no) (finally (println \"inner\"))) (catch :default e e))")
				}
				#expect(result == 7)
				#expect(out == "inner\n")
				// An exception from finally replaces the in-flight result or exception.
				#expect(try rt.eval("(try (try 1 (finally (throw 2))) (catch :default e e))") == 2)
				#expect(try rt.eval("(try (try (throw 1) (finally (throw 2))) (catch :default e e))") == 2)
				#expect(try rt.eval("(try (try (throw 1) (catch :default e (throw 3)) (finally (throw 2))) (catch :default e e))") == 2)
				#expect(message(rt, "(try 1 (finally (throw (ex-info \"from finally\" nil))))") == "from finally")
				// Locals bound in the body are gone by finally; those outside are visible.
				#expect(try rt.eval("(let [x 1] (try (let [y 2] y) (finally x)))") == 2)
				var order: Value = nil
				out = try capturingOutput {
					order = try rt.eval("""
					(let [f (fn f [n] (try (if (zero? n) (throw (ex-info "bottom" nil)) (f (dec n))) (finally (println n))))]
					  (try (f 2) (catch :default e (ex-message e))))
					""")
				}
				#expect(order == "bottom")
				#expect(out == "0\n1\n2\n")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nestingAndRethrow() throws {
			try declare("tc-safe", "tc-div")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(try (try (throw (ex-info \"m\" {:k 1})) (catch :default e (throw (ex-info \"wrapped\" {:k 2} e)))) (catch :default e [(ex-message e) (ex-data e) (ex-message (ex-cause e))]))")
					== ["wrapped", [kw("k"): 2], "m"])
				#expect(try rt.eval("(try (try (throw 1) (catch :default e (throw e))) (catch :default e (inc e)))") == 2)
				#expect(try rt.eval("(try (try (throw 1) (catch :default e (throw e))) (catch :default e (inc e)))") == 2)
				#expect(try rt.eval("(try (throw (try (throw 1) (catch :default e (inc e)))) (catch :default e (inc e)))") == 3)
				#expect(try rt.eval("(try (try (throw 1) (catch ExceptionInfo e :no)) (catch :default e :yes) (finally 0))") == kw("yes"))
				#expect(try rt.eval("(loop [i 0 acc []] (if (< i 3) (recur (inc i) (conj acc (try (if (odd? i) (throw i) i) (catch :default e (- e))))) acc))") == [0, -1, 2])
				// recur is legal inside a loop or fn nested in the try, not across it.
				#expect(try rt.eval("(try (loop [i 0] (if (< i 3) (recur (inc i)) i)))") == 3)
				#expect(try rt.eval("(try ((fn [n] (if (pos? n) (recur (dec n)) :done)) 3))") == kw("done"))
				#expect(try rt.eval("(try (throw 1) (catch :default e (loop [i e] (if (< i 3) (recur (inc i)) i))))") == 3)
				#expect(message(rt, "(loop [i 0] (try (if (< i 3) (recur (inc i)) i)))") == "Cannot recur across try")
				#expect(message(rt, "(loop [i 0] (try 1 (catch :default e (recur 1))))") == "Cannot recur across try")
				#expect(message(rt, "(loop [i 0] (try 1 (finally (recur 1))))") == "Cannot recur across try")
				#expect(message(rt, "(fn [n] (try (recur n)))") == "Cannot recur across try")
				#expect(message(rt, "(try (recur 1))") == "Can only recur from tail position")
				// Syntax-quote keeps the special names bare, so macros can wrap code in try.
				_ = try rt.eval("(defmacro tc-safe [x] `(try ~x (catch :default e# :failed)))")
				#expect(try rt.eval("[(tc-safe 1) (tc-safe (throw 2)) (tc-safe (+ 1 nil))]") == [1, kw("failed"), kw("failed")])
				#expect(try rt.eval("(pr-str `(throw 1))") == "(throw 1)")
				let expansion = try #require(try rt.eval("(pr-str `(try 1 (catch :default e# 2) (finally 3)))").string)
				#expect(expansion.hasPrefix("(try 1 (catch :default e__"))
				#expect(expansion.hasSuffix(" 2) (finally 3))"))
				_ = try rt.eval("(defn tc-div [a b] (try (/ a b) (catch :default e :failed)))")
				#expect(try rt.eval("[(tc-div 6 3) (tc-div 1 \"x\")]") == [2, kw("failed")])
				try unbind("tc-safe", "tc-div")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func analysisErrors() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(message(rt, "(catch :default e 1)") == "catch outside try")
				#expect(message(rt, "(finally 1)") == "finally outside try")
				#expect(message(rt, "(try 1 (finally 2) 3)") == "finally clause must be last in try expression")
				#expect(message(rt, "(try 1 (finally 2) (catch :default e 3))") == "finally clause must be last in try expression")
				#expect(message(rt, "(try 1 (finally 2) (finally 3))") == "finally clause must be last in try expression")
				#expect(message(rt, "(try 1 (catch :default e 2) 3)") == "Only catch or finally clause can follow catch in try expression")
				#expect(message(rt, "(try 1 (catch Nope e 2))") == "Unable to resolve classname: Nope")
				#expect(message(rt, "(try 1 (catch :other e 2))") == "Unable to resolve classname: :other")
				#expect(message(rt, "(try 1 (catch java.lang.Exception e 2))") == "Unable to resolve classname: java.lang.Exception")
				#expect(message(rt, "(try 1 (catch :default 5 2))") == "Bad binding form, expected symbol, got: 5")
				#expect(message(rt, "(try 1 (catch :default a/e 2))") == "Bad binding form, expected symbol, got: a/e")
				#expect(message(rt, "(try 1 (catch :default))") == "catch clause requires a classname and a binding: (catch Class name body*)")
				#expect(message(rt, "(throw)") == "Too few arguments to throw, throw expects a single Throwable instance")
				#expect(message(rt, "(throw 1 2)") == "Too many arguments to throw, throw expects a single Throwable instance")
				#expect(message(rt, "(try (nope) (catch :default e 1))") == "Unable to resolve symbol: nope in this context")
				#expect(message(rt, "(try 1 (catch :default e (nope)))") == "Unable to resolve symbol: nope in this context")
				#expect(message(rt, "(try 1 (finally (nope)))") == "Unable to resolve symbol: nope in this context")
				#expect(try clojureError(rt, "\n (try 1 (catch :default e (nope)))")?.data == Value(reading: "{:line 2 :column 27}"))
				// Analysis errors are not caught by the try being analyzed.
				#expect(message(rt, "(try (nope) (catch :default e :caught))") == "Unable to resolve symbol: nope in this context")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
