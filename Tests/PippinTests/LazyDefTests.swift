// @ai-generated(solo)
import CljCompiler
import CljCore
import Foundation
import Testing
@testable import Pippin

// Design §4 «Var и ленивые def»; Fixtures/compiler/lazy.clj prints the same through both backends.

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func userVar(_ ns: String, _ name: String) -> clj_value {
	let n = Value(symbol: ns), s = Value(symbol: name)
	return withExtendedLifetime((n, s)) {
		let found = clj_ns_find(n.raw)
		let v = clj_ns_resolve(found, s.raw)
		clj_release(v) // vars are immortal
		return v
	}
}

private func pending(_ name: String, in ns: String = "user") -> Bool { clj_var_is_pending(userVar(ns, name)) }

// The thunk a compiled unit binds holds no tree: its init is a C function of the unit.
private func compiledThunk(_ name: String, in ns: String) -> Bool {
	let root = clj_var_root(userVar(ns, name))
	return clj_is_lazy_def(root) && clj_lazy_def_of(root).pointee.env.v == CLJ_NIL
}

extension CoreTests {
	@Suite struct LazyDefTests {
		init() throws {
			clj_init()
			_ = try eval("(defn ld-sum [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc i)) acc))) (defn ld-div [a b] (quot a b))")
		}

		// The init's cost is paid at the first deref and once: the def is an analysis and a thunk.
		@Test func aPureInitRunsAtTheFirstDeref() throws {
			let t0 = DispatchTime.now().uptimeNanoseconds
			_ = try eval("(def ld-total (ld-sum 1000000))")
			let t1 = DispatchTime.now().uptimeNanoseconds
			#expect(pending("ld-total"))
			#expect(try eval("(bound? #'ld-total)") == true)
			#expect(try eval("ld-total") == 499999500000)
			let t2 = DispatchTime.now().uptimeNanoseconds
			#expect(!pending("ld-total"))
			#expect((t1 - t0) * 4 < t2 - t1, "the def took \(t1 - t0) ns, its first deref \(t2 - t1) ns")
			#expect(try eval("[ld-total ld-total]") == [499999500000, 499999500000])
			#expect(DispatchTime.now().uptimeNanoseconds - t2 < (t2 - t1) / 4)
		}

		@Test func effectsReadsOfStateAndRegistrationsStayEager() throws {
			_ = try eval("(def ld-a (atom 0)) (defn ld-bump [] (swap! ld-a inc)) (def ld-bumped (ld-bump))")
			#expect(!pending("ld-a") && !pending("ld-bumped"))
			#expect(try eval("@ld-a") == 1)
			_ = try eval("(def ^:dynamic *ld-dyn* 5) (defn ld-read-dyn [] *ld-dyn*) (def ld-dyn-read (ld-read-dyn))")
			#expect(!pending("ld-dyn-read"))
			_ = try eval("(def ld-arr (int-array 3)) (def ld-arr-count (count ld-arr)) (def ld-deref (inc @ld-a))")
			#expect(!pending("ld-arr-count") && !pending("ld-deref"))
			_ = try eval("(def ld-printed (with-out-str (print (ld-sum 3))))")
			#expect(!pending("ld-printed"))
			// The load keeps its order: each registration is in place for the form after it.
			_ = try eval("""
			(def ld-log (atom []))
			(load-string "(defmulti ld-mm identity) (defmethod ld-mm 1 [_] :one) (def ld-reg (do (swap! ld-log conj (ld-mm 1)) 2)) (derive ::ld-child ::ld-parent) (swap! ld-log conj (isa? ::ld-child ::ld-parent))")
			""")
			#expect(try eval("@ld-log") == [Value(keyword: "one"), true])
			#expect(!pending("ld-reg"))
		}

		@Test func lazyAndEagerAreAskedFor() throws {
			_ = try eval("(def ld-c (atom 0)) (def ^:lazy ld-l (do (swap! ld-c inc) :l)) (def ^:eager ld-e (ld-sum 10))")
			#expect(pending("ld-l") && !pending("ld-e"))
			#expect(try eval("@ld-c") == 0)
			#expect(try eval("[ld-l ld-l @ld-c ld-e]") == [Value(keyword: "l"), Value(keyword: "l"), 1, 45])
		}

		@Test func afterLoadForcesWhatTheLoadDeferredAndEagerDefersNothing() throws {
			_ = try eval("(binding [*lazy-defs* :after-load] (load-string \"(def ld-al (ld-sum 10))\"))")
			#expect(!pending("ld-al"))
			let failed = message("(binding [*lazy-defs* :after-load] (load-string \"(def ld-ok (ld-sum 2))\\n(def ld-bad (quot 1 (count [])))\"))")
			#expect(failed == "Syntax error compiling at (NO_SOURCE_PATH:2:1). Divide by zero")
			#expect(!pending("ld-ok"))
			_ = try eval("(binding [*lazy-defs* :eager] (load-string \"(def ld-eg (ld-sum 10))\") (eval '(def ld-eg2 (ld-sum 3))))")
			#expect(!pending("ld-eg") && !pending("ld-eg2"))
			#expect(try eval("*lazy-defs*") == Value(keyword: "lazy"))
		}

		@Test func aRecursiveDefinitionNamesItsVar() throws {
			_ = try eval("(declare ld-self) (defn ld-read-self [] (count ld-self)) (def ld-self (assoc {} :n (ld-read-self)))")
			#expect(message("ld-self") == "Recursive definition of #'user/ld-self: its value is read while it is being computed")
			#expect(message("ld-self") == "Recursive definition of #'user/ld-self: its value is read while it is being computed")
		}

		// One force however many coroutines ask at once: the rest park on the claim and read the one value.
		@Test func concurrentFirstDerefsForceOnce() throws {
			_ = try eval("(def ld-forces (atom 0)) (def ^:lazy ld-once (do (swap! ld-forces inc) [(ld-sum 200000)])) (def ld-shared (vec (range 2000)))")
			#expect(try eval("""
			(let [a (mapv (fn [_] (future ld-once)) (range 64)) b (mapv (fn [_] (future ld-shared)) (range 64)) va (mapv deref a) vb (mapv deref b)]
			  [(every? #(identical? (first va) %) va) (every? #(identical? (first vb) %) vb) @ld-forces (first va)])
			""") == [true, true, 1, [19999900000]])
		}

		// As a failed eager def leaves its var unusable until the next def, and as a delay rethrows: the same exception.
		@Test func aThrowingInitIsThrownByEveryDeref() throws {
			_ = try eval("(def ld-broken (ld-div 1 0))")
			#expect(message("ld-broken") == "Divide by zero")
			#expect(try eval("(let [a (try ld-broken (catch :default e e)) b (try ld-broken (catch :default e e))] (identical? a b))") == true)
			#expect(!pending("ld-broken"))
			#expect(try eval("(bound? #'ld-broken)") == true)
			_ = try eval("(def ld-broken (ld-div 4 2))")
			#expect(try eval("ld-broken") == 2)
		}

		@Test func redefinitionBeforeAndAfterTheForce() throws {
			_ = try eval("(def ld-r1 (ld-sum 3)) (def ld-r1 (+ ld-r1 (ld-sum 4)))")
			#expect(pending("ld-r1"))
			#expect(try eval("ld-r1") == 9)
			_ = try eval("(def ld-r2 (ld-sum 3))")
			#expect(try eval("ld-r2") == 3)
			_ = try eval("(def ld-r2 (+ ld-r2 1))")
			#expect(try eval("ld-r2") == 4)
			// A pending init that read a var through a fn sees the root the load had when its def ran.
			_ = try eval("(def ld-base 1) (defn ld-get-base [] ld-base) (def ld-derived (+ 10 (ld-get-base)))")
			#expect(pending("ld-derived"))
			_ = try eval("(def ld-base 2)")
			#expect(!pending("ld-derived"))
			#expect(try eval("[ld-derived ld-base]") == [11, 2])
			_ = try eval("(def ld-w (+ 1 (ld-get-base)))")
			#expect(try eval("(with-redefs [ld-base 100] (ld-get-base))") == 100)
			#expect(try eval("[ld-w ld-base]") == [3, 2])
			_ = try eval("(def ld-av (ld-sum 4)) (alter-var-root #'ld-av inc)")
			#expect(try eval("ld-av") == 7)
		}

		// The analysis of the load that compiles a unit decides, and the unit binds the same thunk with no tree in it.
		@Test func aCompiledUnitDefersAsTheInterpreterDoes() throws {
			let file = "Tests/PippinTests/Fixtures/lazy-unit.clj"
			let source = """
			(ns ld.unit)
			(defn costly [n] (loop [i 0 acc 0] (if (< i n) (recur (inc i) (+ acc i)) acc)))
			(def total (costly 100))
			(def ^:eager now (costly 10))
			(def broken (quot 1 (count [])))
			"""
			defer { clj_ns_set_current(clj_ns_user()) }
			try compileFixtureAsUnit(source, file: file, name: "lazy_unit")
			_ = try runFixtureUnit(file)
			#expect(compiledThunk("total", in: "ld.unit") && compiledThunk("broken", in: "ld.unit"))
			#expect(!pending("now", in: "ld.unit"))
			#expect(try eval("[ld.unit/total ld.unit/now]") == [4950, 45])
			#expect(message("ld.unit/broken") == "Divide by zero")
			_ = try eval("(binding [*lazy-defs* :after-load] (try (load-file \"\(file)\") (catch :default e (ex-message e))))")
			#expect(!pending("total", in: "ld.unit"))
		}
	}
}
