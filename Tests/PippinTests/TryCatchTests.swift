// @ai-generated(guided)
import CljCore
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

private func message(_ rt: Runtime, _ source: String) -> String? { clojureError(rt, source)?.message }

extension CoreTests {
	@Suite struct TryCatchTests {
		let rt = Runtime()

		init() {
			for k in ["k", "default", "failed", "done", "caught", "inner", "outer", "never", "no", "yes", "other", "line", "column",
			          "et/boom", "tck/db", "tck/other", "tck/pg", "hit", "miss", "rec", "type",
			          "Foundation/CocoaError", "NSPOSIXErrorDomain/2"] { _ = kw(k) }
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
				// Any keyword but :default is a selector now (ex-type/isa?), not a classname error.
				#expect(try rt.eval("(try 1 (catch :other e 2))") == 1)
				#expect(message(rt, "(try 1 (catch java.lang.Exception e 2))") == "Unable to resolve classname: java.lang.Exception")
				// A qualified symbol is a host type, resolved where the clause runs and not where it is read,
				// so a body that never throws never asks the host anything (design §4).
				#expect(try rt.eval("(try 1 (catch Foundation/CocoaError e 2))") == 1)
				#expect(try rt.eval("(try 1 (catch Nowhere/AtAll e 2))") == 1)
				#expect(message(rt, "(try (throw (ex-info \"m\" {})) (catch Nowhere/AtAll e 2))") ==
				        "Unable to resolve host type: Nowhere/AtAll")
				// A keyword is still a selector of its own, whatever it looks like.
				#expect(try rt.eval("(try 1 (catch :NSPOSIXErrorDomain/2 e 2))") == 1)
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

		// Our own type in catch position: an unqualified symbol, the var's descriptor, instance? (design §4).
		@Test func ourTypeIsACatchSelector() throws {
			_ = try rt.eval("(defrecord TcRec [x]) (defrecord TcOther [y]) (deftype TcType [a]) (def tc-not-a-type 1)")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(try (throw (->TcRec 1)) (catch TcRec e [:rec (:x e)]) (catch :default e :no))") ==
				        [kw("rec"), 1])
				#expect(try rt.eval("(try (throw (->TcRec 1)) (catch TcOther e :other) (catch :default e :no))") == kw("no"))
				#expect(try rt.eval("(try (throw (->TcType 2)) (catch TcRec e :rec) (catch TcType e :type))") == kw("type"))
				// A thrown record is an ordinary value: ex-type has nothing to say about it, the clause does.
				#expect(try rt.eval("(try (throw (->TcRec 1)) (catch TcRec e (ex-type e)))") == nil)
				#expect(try rt.eval("(try (throw (ex-info \"m\" {})) (catch TcRec e :rec) (catch :default e :no))") == kw("no"))
				// A var that holds no type cannot decide, and says so where the clause runs.
				#expect(message(rt, "(try (throw (ex-info \"m\" {})) (catch tc-not-a-type e 1))") == "1 is not a type")
				#expect(try rt.eval("(try 1 (catch tc-not-a-type e 2))") == 1)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// ex-type is total: keyword -> itself, ex-info -> its lifted :type, everything else -> nil.
		@Test func exTypeIsTotal() throws {
			_ = try rt.eval("(defrecord TcExTypeRec [a])")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(ex-type 1) (ex-type nil) (ex-type \"s\") (ex-type [1 2]) (ex-type {})]") == [nil, nil, nil, nil, nil])
				#expect(try rt.eval("(ex-type :k)") == kw("k"))
				#expect(try rt.eval("(ex-type (ex-info \"m\" nil))") == nil)
				#expect(try rt.eval("(ex-type (ex-info \"m\" {:type :et/boom}))") == kw("et/boom"))
				// :type stays a plain data key; ex-type only lifts it when the value is a keyword.
				#expect(try rt.eval("(:type (ex-data (ex-info \"m\" {:type :et/boom})))") == kw("et/boom"))
				#expect(try rt.eval("(ex-type (ex-info \"m\" {:type \"not-a-keyword\"}))") == nil)
				#expect(try rt.eval("(ex-type (ex-info \"m\" {:type 42}))") == nil)
				#expect(try rt.eval("(ex-type (->TcExTypeRec 1))") == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// catch by keyword: (isa? (ex-type thrown) K), widened by derive (CoroTests proves :default vs :cancelled).
		@Test func catchByKeywordAndHierarchy() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(try (throw (ex-info \"m\" {:type :tck/db})) (catch :tck/db e :hit))") == kw("hit"))
				#expect(try rt.eval("(try (throw (ex-info \"m\" {:type :tck/other})) (catch :tck/db e :hit) (catch :default e :miss))") == kw("miss"))
				// A bare thrown keyword is its own ex-type: no ex-info needed to select on it.
				#expect(try rt.eval("(try (throw :tck/db) (catch :tck/db e :hit))") == kw("hit"))
				// derive widens the match transitively; :default still catches an ordinary ex-info.
				_ = try rt.eval("(derive :tck/pg :tck/db)")
				#expect(try rt.eval("(try (throw (ex-info \"m\" {:type :tck/pg})) (catch :tck/db e :hit))") == kw("hit"))
				#expect(try rt.eval("(try (throw (ex-info \"m\" nil)) (catch :tck/db e :hit) (catch :default e :miss))") == kw("miss"))
				_ = try rt.eval("(underive :tck/pg :tck/db)")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A real cancellation is not an ex-info (design.md §4), so ExceptionInfo misses it by construction.
		@Test func exceptionInfoDoesNotCatchCancellation() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(try (throw (ex-info \"m\" nil)) (catch ExceptionInfo e :hit))") == kw("hit"))
				// An ex-info merely tagged :type :cancelled is still a real ex-info: ExceptionInfo catches it.
				#expect(try rt.eval("(try (throw (ex-info \"m\" {:type :cancelled})) (catch ExceptionInfo e :hit))") == kw("hit"))
				clj_deadline_set_ms(50)
				defer { clj_deadline_set_ms(0) }
				#expect(message(rt, "(try (loop [i 0] (recur (inc i))) (catch ExceptionInfo e :never))")?.contains("Execution timed out") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Naming the Swift type means :cancelled, because what arrives is our own cancellation and a cast
		// against CancellationError would be false always (design §4).
		@Test func cancellationErrorNamesOurCancellation() throws {
			let before = clj_debug_live_objects()
			do {
				clj_deadline_set_ms(50)
				defer { clj_deadline_set_ms(0) }
				#expect(try rt.eval("(try (loop [i 0] (recur (inc i))) (catch Swift/CancellationError e (ex-message e)))") ==
				        "Execution timed out")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
