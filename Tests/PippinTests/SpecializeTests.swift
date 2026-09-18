// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// The exec of the closure at a var's root.
private func execOf(_ rt: Runtime, _ name: String) throws -> clj_value {
	let f = try rt.eval(name)
	return withExtendedLifetime(f) { clj_fn_of(f.raw).pointee.code }
}

private func specialized(_ rt: Runtime, _ name: String, _ op: String) throws -> Bool {
	let exec = try execOf(rt, name)
	let id = clj_debug_exec_intrinsic_id(exec, op)
	return id != UInt32.max && clj_debug_exec_node_specialized(exec, id)
}

extension CoreTests {
	// The specialized arithmetic node and its guards (specialize.c, eval.c; NOTES.md "Analyzer and evaluator").
	@Suite(.serialized) struct SpecializeTests {
		let rt = Runtime()

		init() {
			_ = try? rt.eval("""
			(do (defn sp-count [n] (loop [i 0] (if (< i n) (recur (inc i)) i)))
			    (defn sp-inc [n] (inc n))
			    (defn sp-add [a b] (+ a b))
			    (defn sp-mul [a b] (* a b))
			    (defn ^:dynamic sp-dyn [n] (inc n))
			    (defn sp-mixed [] (loop [i 0] (if (< i 3) (recur (+ i 0.5)) i)))
			    (defn sp-preds [n] [(zero? n) (pos? n) (neg? n) (= n 1) (<= n 1) (>= n 1) (> n 1) (dec n) (- n 1)])
			    (defn sp-dbl [n] (loop [i 0 x 0.0] (if (< i n) (recur (inc i) (+ x 0.5)) x)))
			    (defn sp-dbl-ops [a b] [(+ a b) (- a b) (* a b) (/ a b) (< a b) (<= a b) (> a b) (>= a b) (= a b) (zero? a) (pos? a) (neg? a) (inc a) (dec a)])
			    (defn sp-fd [i d] [(+ i d) (- i d) (* i d) (/ i d) (< i d) (<= i d) (> i d) (>= i d) (= i d)])
			    (defn sp-df [d i] [(+ d i) (- d i) (* d i) (/ d i) (< d i) (<= d i) (> d i) (>= d i)]))
			""")
		}

		// A loop variable is int64 by pass 1 alone; the parameter it is compared with needs the callers.
		@Test func loopVariableThenCallerJoin() throws {
			#expect(try specialized(rt, "sp-count", "inc"))
			#expect(!(try specialized(rt, "sp-count", "<")))
			let exec = try execOf(rt, "sp-count")
			let rounds = clj_exec_derivations(exec)
			#expect(clj_exec_derivation_valid(exec))
			_ = try rt.eval("(defn sp-run [] (sp-count 100))")
			#expect(try specialized(rt, "sp-count", "<"))
			#expect(clj_exec_derivations(exec) == rounds + 1)
			#expect(clj_exec_derivation_valid(exec))
			#expect(try rt.eval("(sp-run)").int == 100)
			// a later def whose fn passes a double de-specializes the comparison; the result is right before and after
			_ = try rt.eval("(defn sp-run-double [] (sp-count 1.5))")
			#expect(!(try specialized(rt, "sp-count", "<")))
			#expect(try specialized(rt, "sp-count", "inc"))
			#expect(clj_exec_derivation_valid(exec))
			#expect(try rt.eval("(sp-run-double)").int == 2)
			#expect(try rt.eval("(sp-run)").int == 100)
			// the callers gone, the join is back to what remains
			_ = try rt.eval("(def sp-run-double nil)")
			_ = try rt.eval("(defn sp-run [] (sp-count 7))")
			#expect(try specialized(rt, "sp-count", "<"))
			#expect(try rt.eval("(sp-run)").int == 7)
		}

		// The fast path throws where the generic one does, boxes what the tag cannot hold, and yields to a boxed long.
		@Test func overflowAndBoxedLong() throws {
			_ = try rt.eval("(defn sp-use-inc [] (sp-inc 1)) (defn sp-use-mul [] (sp-mul 2 3))")
			#expect(try specialized(rt, "sp-inc", "inc"))
			#expect(try specialized(rt, "sp-mul", "*"))
			#expect(try rt.eval("(sp-inc 4611686018427387903)").description == "4611686018427387904")
			#expect(cljEvalError("(sp-inc 9223372036854775807)")?.contains("integer overflow") == true)
			#expect(cljEvalError("(sp-mul 4611686018427387903 4)")?.contains("integer overflow") == true)
			#expect(try rt.eval("(sp-mul 4611686018427387903 2)").description == "9223372036854775806")
			#expect(try rt.eval("(sp-inc 1.5)").double == 2.5)
			#expect(cljEvalError("(sp-inc \"a\")")?.contains("string cannot be cast to a number") == true)
			// a call from a top-level form or through apply is not a recorded site: the tag check is what holds
			#expect(try rt.eval("(apply sp-inc [1.5])").double == 2.5)
			#expect(try specialized(rt, "sp-inc", "inc"))
		}

		// with-redefs of the callee runs the new root; with-redefs of the operator falls to the generic path.
		@Test func redefinitions() throws {
			_ = try rt.eval("(defn sp-use-add [] (sp-add 5 3))")
			#expect(try specialized(rt, "sp-add", "+"))
			#expect(try rt.eval("(with-redefs [sp-add (fn [a b] (str a b))] (sp-use-add))").description == "\"53\"")
			#expect(try rt.eval("(with-redefs [+ -] (sp-use-add))").int == 2)
			#expect(try rt.eval("(sp-use-add)").int == 8)
			#expect(try specialized(rt, "sp-add", "+"))
		}

		// A redefined operator loses its entries at the def, not at the next join change, and gets them back the same way.
		@Test func rootRebindPushes() throws {
			_ = try rt.eval("(defn sp-use-mul-2 [] (sp-mul 6 7))")
			let exec = try execOf(rt, "sp-mul")
			#expect(try specialized(rt, "sp-mul", "*"))
			let rounds = clj_exec_derivations(exec)
			_ = try rt.eval("(in-ns 'clojure.core) (def sp-boot-mul *) (def * (fn [a b] (+ a b))) (in-ns 'user)")
			#expect(!(try specialized(rt, "sp-mul", "*")))
			#expect(clj_exec_derivations(exec) == rounds + 1)
			#expect(clj_exec_derivation_valid(exec))
			#expect(try rt.eval("(sp-mul 6 7)").int == 13)
			// the specialized entry of a fn whose operator is rebound mid-flight still sees the rebinding
			#expect(try rt.eval("(with-redefs [* clojure.core/sp-boot-mul] (sp-mul 6 7))").int == 42)
			#expect(try rt.eval("(sp-mul 6 7)").int == 13)
			// each rebind of with-redefs pushed one more
			#expect(clj_exec_derivations(exec) == rounds + 3)
			_ = try rt.eval("(in-ns 'clojure.core) (def * sp-boot-mul) (in-ns 'user)")
			#expect(try specialized(rt, "sp-mul", "*"))
			#expect(clj_exec_derivations(exec) == rounds + 4)
			#expect(clj_exec_derivation_valid(exec))
			#expect(try rt.eval("(sp-mul 6 7)").int == 42)
			#expect(try rt.eval("(with-redefs [* -] (sp-mul 6 7))").int == -1)
			#expect(try specialized(rt, "sp-mul", "*"))
			#expect(try rt.eval("(sp-mul 6 7)").int == 42)
		}

		// A dynamic var and a loop whose variable turns double are never specialized; the predicates are.
		@Test func neverAndAlways() throws {
			_ = try rt.eval("(defn sp-use-dyn [] (sp-dyn 1)) (defn sp-use-preds [] (sp-preds 1))")
			#expect(!(try specialized(rt, "sp-dyn", "inc")))
			#expect(try rt.eval("(binding [sp-dyn (fn [n] (str n))] (sp-use-dyn))").description == "\"1\"")
			#expect(!(try specialized(rt, "sp-mixed", "+")))
			#expect(!(try specialized(rt, "sp-mixed", "<")))
			#expect(try rt.eval("(sp-mixed)").double == 3.0)
			for op in ["zero?", "pos?", "neg?", "=", "<=", ">=", ">", "dec", "-"] { #expect(try specialized(rt, "sp-preds", op), Comment(rawValue: op)) }
			#expect(try rt.eval("(sp-preds 1)").description == "[false true false true true true false 0 0]")
			#expect(try rt.eval("(sp-preds 0)").description == "[true false false false true false false -1 -1]")
			#expect(try rt.eval("(sp-preds -1.5)").description == "[false false true false true false false -2.5 -2.5]")
		}

		// Every double entry, the mixed ones and the IEEE edges: ##Inf from (/ 1.0 0.0), NaN unordered and unequal.
		@Test func doublesAndMixed() throws {
			_ = try rt.eval("(defn sp-use-dbl [] [(sp-dbl 4) (sp-dbl-ops 1.5 0.5) (sp-fd 3 0.5) (sp-df 0.5 3)])")
			#expect(try specialized(rt, "sp-dbl", "+"))
			#expect(try specialized(rt, "sp-dbl", "inc"))
			#expect(try specialized(rt, "sp-dbl", "<"))
			for op in ["+", "-", "*", "/", "<", "<=", ">", ">=", "=", "zero?", "pos?", "neg?", "inc", "dec"] {
				#expect(try specialized(rt, "sp-dbl-ops", op), Comment(rawValue: op))
			}
			for op in ["+", "-", "*", "/", "<", "<=", ">", ">="] {
				#expect(try specialized(rt, "sp-fd", op), Comment(rawValue: op))
				#expect(try specialized(rt, "sp-df", op), Comment(rawValue: op))
			}
			#expect(!(try specialized(rt, "sp-fd", "=")))
			#expect(try rt.eval("(sp-dbl 4)").double == 2.0)
			#expect(try rt.eval("(sp-dbl-ops 1.5 0.5)").description == "[2.0 1.0 0.75 3.0 false false true true false false true false 2.5 0.5]")
			#expect(try rt.eval("(sp-fd 3 0.5)").description == "[3.5 2.5 1.5 6.0 false false true true false]")
			#expect(try rt.eval("(sp-df 0.5 3)").description == "[3.5 -2.5 1.5 0.16666666666666666 true true false false]")
			#expect(try rt.eval("(sp-dbl-ops 1.0 0.0)").description == "[1.0 1.0 0.0 ##Inf false false true true false false true false 2.0 0.0]")
			#expect(try rt.eval("(sp-dbl-ops ##NaN ##NaN)").description == "[##NaN ##NaN ##NaN ##NaN false false false false false false false false ##NaN ##NaN]")
			#expect(try rt.eval("(sp-dbl-ops -0.0 ##Inf)").description == "[##Inf ##-Inf ##NaN -0.0 true true false false false true false false 1.0 -1.0]")
			// a wrong fact at the host boundary: every entry falls to the generic path
			#expect(try rt.eval("(sp-dbl-ops 3 2)").description == "[5 1 6 3/2 false false true true false false true false 4 2]")
			#expect(try rt.eval("(sp-fd 1.5 2)").description == "[3.5 -0.5 3.0 0.75 true true false false false]")
			#expect(try rt.eval("(sp-fd 9223372036854775807 0.5)").description == "[9.223372036854776E18 9.223372036854776E18 4.611686018427388E18 1.8446744073709552E19 false false true true false]")
			#expect(cljEvalError("(sp-dbl-ops \"a\" 1.0)")?.contains("string cannot be cast to a number") == true)
		}

		// The hit counter's wrapper and the specialization coexist: counting off puts the fast path back.
		@Test func countingKeepsTheEntries() throws {
			_ = try rt.eval("(defn sp-use-preds [] (sp-preds 1))")
			let exec = try execOf(rt, "sp-preds")
			let id = clj_debug_exec_intrinsic_id(exec, "dec")
			clj_exec_count(exec, true)
			_ = try rt.eval("(sp-preds 2)")
			#expect(clj_exec_hits(exec, id) == 1)
			clj_exec_count(exec, false)
			#expect(clj_debug_exec_node_specialized(exec, id))
			#expect(try rt.eval("(sp-preds 2)").description == "[false true false false false true true 1 1]")
		}
	}
}
