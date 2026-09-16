// @ai-generated(guided)
import CljCore
import Foundation
import Testing
@testable import Clojure

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

extension CoreTests {
	@Suite struct DynamicVarTests {
		@Test func bindingNestsAndRestores() throws {
			clj_init()
			_ = try eval("(def ^:dynamic dv-x 1) (def ^:dynamic dv-y :root) (def dv-plain 1) (defn dv-read [] dv-x) (def dv-unbound) (def ^:dynamic dv-f (fn [] :root)) [:inner :bound]")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[dv-x (binding [dv-x 2] [dv-x (dv-read)]) dv-x]") == [1, [2, 2], 1])
				#expect(try eval("(binding [dv-x 2] (binding [dv-x 3 dv-y :inner] [dv-x dv-y]))") == [3, kw("inner")])
				#expect(try eval("(binding [dv-x 2] (binding [dv-y :inner] dv-x))") == 2)
				#expect(try eval("(binding [dv-x 2 dv-y dv-x] dv-y)") == 1)
				#expect(try eval("(try (binding [dv-x 2] (throw (ex-info \"boom\" {}))) (catch :default e dv-x))") == 1)
				#expect(try eval("(binding [dv-x 2] (set! dv-x 5) [dv-x (dv-read)])") == [5, 5])
				#expect(try eval("dv-x") == 1)
				#expect(message("(set! dv-x 2)") == "Can't change/establish root binding of: user/dv-x with set")
				#expect(message("(binding [dv-plain 2] dv-plain)") == "Can't dynamically bind non-dynamic var: user/dv-plain")
				#expect(message("(binding [dv-x] 1)") == "binding requires an even number of forms in binding vector")
				#expect(try eval("[(bound? #'dv-x) (thread-bound? #'dv-x) (binding [dv-x 2] (thread-bound? #'dv-x)) (bound? #'dv-x #'dv-plain)]") == [true, false, true, true])
				#expect(try eval("[(bound? #'dv-unbound) (binding [dv-x 1] (bound? #'dv-unbound #'dv-x))]") == [false, false])
				#expect(try eval("(binding [dv-x 7] (get (get-thread-bindings) #'dv-x))") == 7)
				#expect(try eval("(get-thread-bindings)") == Value([:]))
				#expect(try eval("(binding [dv-x 2] (var-get #'dv-x))") == 2)
				#expect(try eval("(binding [dv-x 2] (var-set #'dv-x 9) dv-x)") == 9)
				#expect(try eval("(with-bindings {#'dv-x 4} (dv-read))") == 4)
				#expect(try eval("(with-bindings* {#'dv-x 4} (fn [a] (+ dv-x a)) 1)") == 5)
				// A var's own value is +0 in a call position when its root is a fn; a bound dynamic var reads owned.
				#expect(try eval("(binding [dv-f (fn [] :bound)] (dv-f))") == kw("bound"))
				#expect(try eval("(dv-f)") == kw("root"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func bindingsArePerThread() throws {
			clj_init()
			_ = try eval("(def ^:dynamic dv-t 1)")
			let before = clj_debug_live_objects()
			do {
				let f = try eval("(fn [] (binding [dv-t 2] dv-t))")
				let g = try eval("(fn [] dv-t)")
				let boundFn = try eval("(binding [dv-t 3] (bound-fn [] dv-t))")
				nonisolated(unsafe) var seen: [Value] = []
				let thread = Thread {
					// A fresh thread has no frames: it sees the root even while this thread is inside a binding.
					seen.append(try! g())
					seen.append(try! f())
					seen.append(try! boundFn())
				}
				_ = try eval("(push-thread-bindings {#'dv-t 10})")
				thread.start()
				while !thread.isFinished { usleep(1000) }
				_ = try eval("(pop-thread-bindings)")
				#expect(seen == [1, 2, 3])
				#expect(try eval("dv-t") == 1)
				#expect(try eval("(binding [dv-t 5] ((bound-fn* (fn [] dv-t))))") == 5)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func withRedefsSwapsRootsAndRestores() throws {
			clj_init()
			_ = try eval("(defn dv-target [] :orig) (def dv-value 1) (def ^:dynamic dv-dyn 1) [:temp :orig]")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(with-redefs [dv-target (fn [] :temp)] (dv-target)) (dv-target)]") == [kw("temp"), kw("orig")])
				#expect(try eval("(with-redefs [dv-value 2 dv-dyn 3] [dv-value dv-dyn])") == [2, 3])
				#expect(try eval("[dv-value dv-dyn]") == [1, 1])
				#expect(try eval("(try (with-redefs [dv-value 2] (throw (ex-info \"x\" {}))) (catch :default e dv-value))") == 1)
				#expect(try eval("(with-redefs-fn {#'dv-value 5} (fn [] dv-value))") == 5)
				#expect(try eval("(alter-var-root #'dv-value + 10)") == 11)
				#expect(try eval("(alter-var-root #'dv-value (constantly 1))") == 1)
				#expect(message("(with-redefs [dv-value] 1)") == "with-redefs requires an even number of forms in binding vector")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A binding of *ns* is what a load runs under: in-ns inside it does not leak out.
		@Test func currentNamespaceIsABinding() throws {
			clj_init()
			_ = try eval("(in-ns 'dv-ns.other) (in-ns 'user)")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(binding [*ns* *ns*] (in-ns 'dv-ns.other) (ns-name *ns*))") == Value(symbol: "dv-ns.other"))
				#expect(try eval("(ns-name *ns*)") == Value(symbol: "user"))
				#expect(try eval("(binding [*ns* (the-ns 'dv-ns.other)] (ns-name *ns*))") == Value(symbol: "dv-ns.other"))
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
