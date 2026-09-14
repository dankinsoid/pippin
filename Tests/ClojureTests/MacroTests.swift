// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

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

extension CoreTests {
	@Suite struct MacroTests {
		// Vars are immortal: declare them before the baseline and unbind (which also clears the macro flag) after.
		private func declare(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func defmacroAndExpansion() throws {
			clj_init()
			for k in ["a", "b", "k", "zero", "line", "column"] { _ = Value(keyword: k) }
			try declare("mt-unless", "mt-unless2", "mt-echo", "mt-env", "mt-forever", "mt-id", "mt-arities", "mt-doc", "mt-x")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(defmacro mt-unless [c & body] `(if ~c nil (do ~@body)))").description == "#'user/mt-unless")
				#expect(try eval("(mt-unless false 1 2)") == 2)
				#expect(try eval("(mt-unless true 1 2)") == nil)
				#expect(try eval("(mt-unless nil)") == nil)
				#expect(try eval("(macroexpand-1 '(mt-unless a b))") == Value(reading: "(if a nil (do b))"))
				// A macro built on another macro; macroexpand-1 stops after one step, macroexpand goes on.
				_ = try eval("(defmacro mt-unless2 [c & body] `(mt-unless (not ~c) ~@body))")
				#expect(try eval("(mt-unless2 true :a :b)") == Value(keyword: "b"))
				#expect(try eval("(macroexpand-1 '(mt-unless2 x y))") == Value(reading: "(user/mt-unless (clojure.core/not x) y)"))
				#expect(try eval("(macroexpand '(mt-unless2 x y))") == Value(reading: "(if (clojure.core/not x) nil (do y))"))
				#expect(try eval("(macroexpand '(inc 1))") == Value(reading: "(inc 1)"))
				#expect(try eval("(macroexpand-1 'x)") == Value(symbol: "x"))
				#expect(try eval("(macroexpand-1 '())") == Value(list: []))
				#expect(try eval("(macroexpand-1 '(let [x 1] x))") == Value(reading: "(let [x 1] x)"))
				// &form is the whole call, &env nil.
				_ = try eval("(defmacro mt-echo [& xs] (list 'quote &form))")
				#expect(try eval("(mt-echo 1 (2) [3])") == Value(reading: "(mt-echo 1 (2) [3])"))
				_ = try eval("(defmacro mt-env [] (list 'quote &env))")
				#expect(try eval("(mt-env)") == nil)
				// Expansion must terminate.
				_ = try eval("(defmacro mt-forever [] (list 'mt-forever))")
				#expect(message("(mt-forever)") == "Macro expansion exceeded 1000 steps at: (mt-forever)")
				// A macro returning the identical form stops expanding and the form becomes a call, as in Clojure;
				// a quoted constant in the body is such a form from the second step on.
				_ = try eval("(defmacro mt-id [& body] &form)")
				#expect(message("(mt-id 1)") == "Can't take value of a macro: #'user/mt-id")
				_ = try eval("(defmacro mt-id [] '(mt-id))")
				#expect(message("(mt-id)") == "Can't take value of a macro: #'user/mt-id")
				#expect(message("mt-unless") == "Can't take value of a macro: #'user/mt-unless")
				#expect(message("(mt-unless)") == "Wrong number of args (2) passed to: user/mt-unless")
				// Locals shadow macros.
				#expect(try eval("(let [mt-unless (fn [c a b] (if c a b))] (mt-unless true 1 2))") == 1)
				#expect(try eval("((fn [mt-unless] (mt-unless 5)) inc)") == 6)
				// Multiple arities and a docstring.
				_ = try eval("(defmacro mt-arities \"doc\" ([] :zero) ([x] x) ([x & more] `(+ ~x ~@more)))")
				#expect(try eval("[(mt-arities) (mt-arities 1) (mt-arities 1 2 3)]") == [Value(keyword: "zero"), 1, 6])
				_ = try eval("(defmacro mt-doc \"doc only\" [] 7)")
				#expect(try eval("(mt-doc)") == 7)
				// A qualified macro name expands too.
				#expect(try eval("(user/mt-unless false 3)") == 3)
				// The var quote and the macro var.
				#expect(try eval("#'mt-unless").description == "#'user/mt-unless")
				#expect(try eval("(var mt-unless)") == eval("#'user/mt-unless"))
				#expect(message("(var mt-nope)") == "Unable to resolve var: mt-nope in this context")
				#expect(message("(defmacro)") == "First argument to defmacro must be a Symbol")
				#expect(message("(defmacro mt-x)") == "Parameter declaration missing")
				#expect(message("(defmacro mt-x 1)") == "Parameter declaration 1 should be a vector")
				#expect(message("(defmacro mt-x (1))") == "Parameter declaration 1 should be a vector")
				// A macro used before its definition is an unresolved symbol (design: defmacro strictly before use).
				#expect(message("(mt-later 1) (defmacro mt-later [x] x)") == "Unable to resolve symbol: mt-later in this context")
				try unbind("mt-unless", "mt-unless2", "mt-echo", "mt-env", "mt-forever", "mt-id", "mt-arities", "mt-doc")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func topLevelDoSplitsForms() throws {
			clj_init()
			try declare("mt-do-m", "mt-do-wrap")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(do (defmacro mt-do-m [x] `(+ ~x 1)) (mt-do-m 1))") == 2)
				#expect(try eval("(do (do (defmacro mt-do-m [x] `(+ ~x 10))) (do (mt-do-m 1)))") == 11)
				// The top-level form is expanded before the do check, so a macro producing a do splits too.
				_ = try eval("(defmacro mt-do-wrap [& forms] `(do ~@forms))")
				#expect(try eval("(mt-do-wrap (defmacro mt-do-m [x] `(* ~x 3)) (mt-do-m 2))") == 6)
				#expect(try eval("(do)") == nil)
				#expect(try eval("(let [x 1] (do (defmacro mt-do-m [] 1) x))") == 1)
				try unbind("mt-do-m", "mt-do-wrap")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func errorsInsideMacrosCarryTheUseSitePosition() throws {
			let rt = Runtime()
			for k in ["k", "line", "column"] { _ = Value(keyword: k) }
			_ = try rt.eval("(def mt-boom) (def mt-bad)")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defmacro mt-boom [] (throw (ex-info \"boom\" {:k 1})))")
				let e = try #require(clojureError(rt, "1\n\n  (mt-boom)"))
				#expect(e.message == "boom")
				#expect(try e.data == Value(reading: "{:k 1 :line 3 :column 3}"))
				#expect(e.causeError?.message == "boom")
				#expect(try e.causeError?.data == Value(reading: "{:k 1}"))
				// Runtime failures in the macro body (not just throw) are positioned the same way.
				_ = try rt.eval("(defmacro mt-bad [] (+ 1 nil))")
				let f = try #require(clojureError(rt, "\n(mt-bad)"))
				#expect(f.message == "nil cannot be cast to a number")
				#expect(try f.data == Value(reading: "{:line 2 :column 1}"))
				// Through the C API without a position the exception passes unchanged.
				#expect(cljEvalError("(mt-boom)") == "#error {:message \"boom\", :data {:k 1}}")
				#expect(cljEvalError("(macroexpand-1 '(mt-boom))") == "#error {:message \"boom\", :data {:k 1}}")
				// Analysis errors inside the expansion keep the position of the top-level form.
				let g = try #require(clojureError(rt, "\n\n(mt-unless-nope (nope))"))
				#expect(g.message == "Unable to resolve symbol: mt-unless-nope in this context")
				#expect(try g.data == Value(reading: "{:line 3 :column 1}"))
				_ = try rt.eval("(def mt-boom nil) (def mt-bad nil)")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func syntaxQuoteEvaluates() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [b 2 c [3 4]] `(a ~b ~@c))") == Value(reading: "(user/a 2 3 4)"))
				#expect(try eval("(let [x 1] `[~x ~@[2 3] {~x ~x}])") == Value(reading: "[1 2 3 {1 1}]"))
				#expect(try eval("`()") == Value(list: []))
				#expect(try eval("`(~@nil)") == nil)
				#expect(try eval("(let [x 1] `(~x ~@[] ~@'(2) ~@{3 4}))") == Value(reading: "(1 2 [3 4])"))
				// Auto-gensyms are fixed at read time: one read, one symbol, however often the form runs.
				#expect(try eval("(let [f (fn [] `(let [v# 1] v#))] (= (f) (f)))") == true)
				#expect(try eval("(let [f (fn [] (gensym))] (= (f) (f)))") == false)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func seqBuiltins() throws {
			clj_init()
			for k in ["a", "b", "k"] { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(seq nil)") == nil)
				#expect(try eval("(seq ())") == nil)
				#expect(try eval("(seq [])") == nil)
				#expect(try eval("(seq {})") == nil)
				#expect(try eval("(seq '(1 2))") == Value(list: [1, 2]))
				#expect(try eval("(seq? (seq '(1 2)))") == true)
				#expect(try eval("(let [l '(1 2)] (= l (seq l)))") == true)
				#expect(try eval("(seq [1 2])") == Value(list: [1, 2]))
				#expect(try eval("(seq? (seq [1 2]))") == true)
				#expect(try eval("(seq {:a 1})") == Value(reading: "([:a 1])"))
				#expect(try eval("(count (seq {:a 1 :b 2}))") == 2)
				#expect(message("(seq 1)") == "Don't know how to create ISeq from: fixnum")
				#expect(try eval("[(seq? ()) (seq? '(1)) (seq? [1]) (seq? nil) (seq? {})]") == [true, true, false, false, false])
				#expect(try eval("(concat)") == Value(list: []))
				#expect(try eval("(concat [1] '(2 3) nil [] {:a 1})") == Value(reading: "(1 2 3 [:a 1])"))
				#expect(try eval("(seq (concat))") == nil)
				#expect(message("(concat [1] 2)") == "Don't know how to create ISeq from: fixnum")
				#expect(try eval("(list* nil)") == nil)
				#expect(try eval("(list* [])") == nil)
				#expect(try eval("(list* 1 nil)") == Value(list: [1]))
				#expect(try eval("(list* 1 2 [3 4])") == Value(list: [1, 2, 3, 4]))
				#expect(try eval("(list* '(1 2))") == Value(list: [1, 2]))
				#expect(try eval("[(empty? nil) (empty? ()) (empty? []) (empty? {}) (empty? \"\") (empty? [1]) (empty? \"a\") (empty? '(1))]") == [true, true, true, true, true, false, false, false])
				#expect(try eval("[(second [1 2 3]) (second '(1)) (second nil) (last [1 2 3]) (last '(1)) (last nil) (last [])]") == [2, nil, nil, 3, 1, nil, nil])
				#expect(try eval("[(butlast [1 2 3]) (butlast [1]) (butlast nil)]") == [Value(list: [1, 2]), nil, nil])
				#expect(try eval("[(reverse [1 2 3]) (reverse nil) (reverse '(1))]") == [Value(list: [3, 2, 1]), Value(list: []), Value(list: [1])])
				#expect(try eval("(into [1] '(2 3))") == [1, 2, 3])
				#expect(try eval("(into '(1) [2 3])") == Value(list: [3, 2, 1]))
				#expect(try eval("(into nil [1 2])") == Value(list: [2, 1]))
				#expect(try eval("(into {:a 1} [[:b 2]])") == Value(reading: "{:a 1 :b 2}"))
				#expect(try eval("(into {} {:a 1})") == Value(reading: "{:a 1}"))
				#expect(try eval("(into [] nil)") == [])
				#expect(message("(into 1 [2])") == "conj not supported on this type: fixnum")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nameBuiltins() throws {
			clj_init()
			for k in ["a", "ns/a", "b"] { _ = Value(keyword: k) }
			_ = try eval("(def mt-var)")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(symbol \"a\")") == Value(symbol: "a"))
				#expect(try eval("(symbol \"ns/a\")") == Value(symbol: "ns/a"))
				#expect(try eval("(symbol \"/\")") == Value(symbol: "/"))
				#expect(try eval("(symbol \"ns\" \"a\")") == Value(symbol: "ns/a"))
				#expect(try eval("(symbol nil \"a\")") == Value(symbol: "a"))
				#expect(try eval("(symbol 'a)") == Value(symbol: "a"))
				#expect(try eval("(symbol :ns/a)") == Value(symbol: "ns/a"))
				#expect(try eval("(symbol #'mt-var)") == Value(symbol: "user/mt-var"))
				#expect(message("(symbol 1)") == "no conversion to symbol from: fixnum")
				#expect(message("(symbol 1 \"a\")") == "namespace must be a string or nil, got: fixnum")
				#expect(message("(symbol \"a\" 'b)") == "name must be a string, got: symbol")
				#expect(try eval("(keyword \"a\")") == Value(keyword: "a"))
				#expect(try eval("(keyword \"ns/a\")") == Value(keyword: "ns/a"))
				#expect(try eval("(keyword 'ns/a)") == Value(keyword: "ns/a"))
				#expect(try eval("(keyword :a)") == Value(keyword: "a"))
				#expect(try eval("(keyword \"ns\" \"a\")") == Value(keyword: "ns/a"))
				#expect(try eval("[(name 'a) (name 'ns/a) (name :ns/a) (name \"s\")]") == ["a", "a", "a", "s"])
				#expect(try eval("[(namespace 'a) (namespace 'ns/a) (namespace :ns/a)]") == [nil, "ns", "ns"])
				#expect(message("(name 1)") == "fixnum cannot be cast to a named value")
				#expect(message("(namespace \"s\")") == "string cannot be cast to a named value")
				let g1 = try eval("(gensym)"), g2 = try eval("(gensym)"), g3 = try eval("(gensym \"p\")"), g4 = try eval("(gensym 'q)")
				#expect(g1 != g2)
				#expect(g1.description.hasPrefix("G__"))
				#expect(g3.description.hasPrefix("p") && !g3.description.hasPrefix("p_"))
				#expect(g4.description.hasPrefix("q"))
				#expect(try eval("(symbol? (gensym))") == true)
				#expect(message("(gensym 1)") == "gensym prefix must be a string or symbol, got: fixnum")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func exInfoAndThrow() throws {
			clj_init()
			for k in ["k", "ok", "message", "data", "cause"] { _ = Value(keyword: k) }
			let before = clj_debug_live_objects()
			do {
				#expect(cljEvalError("(throw (ex-info \"boom\" {:k 1}))") == "#error {:message \"boom\", :data {:k 1}}")
				#expect(cljEvalError("(throw (ex-info \"outer\" nil (ex-info \"inner\" {})))") == "#error {:message \"outer\", :data nil, :cause #error {:message \"inner\", :data {}}}")
				#expect(message("(throw 1)") == "Can only throw an exception, got: fixnum")
				#expect(message("(ex-info 1 {})") == "ex-info message must be a string, got: fixnum")
				#expect(message("(ex-info \"m\" 1)") == "ex-info data must be a map, got: fixnum")
				#expect(message("(ex-info \"m\" {} 1)") == "ex-info cause must be an exception, got: fixnum")
				#expect(try eval("(let [e (ex-info \"m\" {:k 1})] (if e :ok))") == Value(keyword: "ok"))
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
