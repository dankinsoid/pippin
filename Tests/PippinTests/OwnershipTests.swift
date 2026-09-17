// @ai-generated(guided)
import CljCore
import Testing
@testable import Pippin

private func kw(_ s: String) -> Value { Value(keyword: s) }

// A missing retain shows as a leak against the baseline or, under ASan, as a use-after-free.
extension CoreTests {
	@Suite struct OwnershipTests {
		let rt = Runtime()

		init() {
			for k in ["caught", "a", "b", "x", "self"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		private func define(_ name: String, _ value: Value) {
			let sym = Value(symbol: name)
			withExtendedLifetime((sym, value)) { clj_var_bind_root(clj_ns_resolve(clj_ns_user(), sym.raw), value.raw) }
		}

		private func caught(_ source: String) -> String? {
			do {
				_ = try rt.eval(source)
				return nil
			} catch let e as ClojureError {
				return e.message
			} catch {
				return nil
			}
		}

		@Test func paramsReturnedAndCaptured() throws {
			try declare("ow-mk")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("((fn [x] x) (list 1 2))") == Value(list: [1, 2]))
				#expect(try rt.eval("(let [v (list 1 2)] ((fn [x] x) v))") == Value(list: [1, 2]))
				#expect(try rt.eval("(let [v (list 1 2) f (fn [x] x)] [(f v) (f v)])") == [Value(list: [1, 2]), Value(list: [1, 2])])
				// The inner closure outlives the call whose param it captured.
				#expect(try rt.eval("(let [g ((fn [x] (fn [] x)) (list 1 2))] (g))") == Value(list: [1, 2]))
				_ = try rt.eval("(def ow-mk (fn [x] (fn [] x)))")
				#expect(try rt.eval("(let [v (vector 1) g (ow-mk v)] [(g) (g)])") == [[1], [1]])
				#expect(try rt.eval("((fn me [x] (if (empty? x) me (me (rest x)))) (list 1 2))").isFn)
				try unbind("ow-mk")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func recurSwapsSlots() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(loop [a (list 1) b (list 2) i 0] (if (< i 3) (recur b a (inc i)) [a b]))") == [Value(list: [2]), Value(list: [1])])
				#expect(try rt.eval("((fn [a b i] (if (< i 3) (recur b a (inc i)) [a b])) (list 1) (list 2) 0)") == [Value(list: [2]), Value(list: [1])])
				#expect(try rt.eval("(let [x (list 1) y (list 2)] ((fn [a b i] (if (< i 4) (recur b a (inc i)) [a b])) x y 0))") == [Value(list: [1]), Value(list: [2])])
				// A loop in a fn body seeded from the param, rebound on every iteration.
				#expect(try rt.eval("((fn [x] (loop [i 0 acc x] (if (< i 3) (recur (inc i) (conj acc i)) acc))) [])") == [0, 1, 2])
				#expect(try rt.eval("(let [v (list 9)] ((fn [x] (loop [s x n 0] (if s (recur (next s) (+ n (first s))) [n x]))) v))") == [9, Value(list: [9])])
				// recur through the rest param of a variadic fn.
				#expect(try rt.eval("((fn [a & r] (if r (recur (first r) (next r)) a)) (list 1) (list 2) (list 3))") == Value(list: [3]))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func letShadowsAndRebinds() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("((fn [x] (let [x (conj x 3)] x)) [1 2])") == [1, 2, 3])
				#expect(try rt.eval("((fn [x] (let [x (conj x 2) x (conj x 3)] x)) [1])") == [1, 2, 3])
				#expect(try rt.eval("((fn [x] (let [y x] (let [y (list y)] [x y]))) (list 1))") == [Value(list: [1]), Value(list: [Value(list: [1])])])
				#expect(try rt.eval("(loop [i 0 v (list)] (if (< i 3) (let [w (cons i v)] (recur (inc i) w)) v))") == Value(list: [2, 1, 0]))
				#expect(try rt.eval("((fn [x] (loop [i 0] (let [y (list x i)] (if (< i 2) (recur (inc i)) y)))) (vector 7))") == Value(list: [[7], 2]))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func catchBindsAndRethrows() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(try ((fn [e] (try (throw e) (catch :default c (throw c)))) (ex-info \"m\" {:a 1})) (catch :default e (ex-data e)))") == Value(reading: "{:a 1}"))
				#expect(try rt.eval("(let [e (ex-info \"m\" {:a 2})] (try (throw e) (catch :default c [(ex-data c) (identical? c e)])))") == [Value(reading: "{:a 2}"), true])
				#expect(try rt.eval("(loop [i 0 last nil] (if (< i 3) (recur (inc i) (try (throw (list i)) (catch :default c c))) last))") == Value(list: [2]))
				#expect(try rt.eval("((fn [x] (try (throw x) (catch :default x (list x)))) [1])") == Value(list: [[1]]))
				#expect(try rt.eval("((fn [x] (try (throw (list 1)) (catch :default c x) (finally (list x)))) [2])") == [2])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func applyAndNativesCallBack() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(apply (fn [a b c] [c b a]) (list [1] [2] [3]))") == [[3], [2], [1]])
				#expect(try rt.eval("(let [s (list 1 2 3) z (list 0)] (apply (fn [a & r] [a r]) z s))") == [Value(list: [0]), Value(list: [1, 2, 3])])
				#expect(try rt.eval("(let [k (list 1)] (map (fn [x] (list x k)) [1 2]))") == Value(list: [Value(list: [1, Value(list: [1])]), Value(list: [2, Value(list: [1])])]))
				#expect(try rt.eval("(let [k [0]] (reduce (fn [acc x] (conj acc x k)) [] (list 1 2)))") == [1, [0], 2, [0]])
				#expect(try rt.eval("(let [k (list :x)] (some (fn [x] (when (= x 2) (list x k))) [1 2 3]))") == Value(list: [2, Value(list: [kw("x")])]))
				#expect(try rt.eval("(let [f (fn [x] (list x))] (f 1))") == Value(list: [1]))
				#expect(try rt.eval("(let [f (fn [x y] [x y]) a (list 1) b [2]] (f a b))") == [Value(list: [1]), [2]])
				#expect(try rt.eval("(let [f (fn [x] x) g (fn [h v] (h v)) v (list 3)] (g f v))") == Value(list: [3]))
				#expect(try rt.eval("(let [v (list 1 2 3)] (apply + 0 v))") == 6)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func throwMidwayThroughArguments() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [a (list 1)] (try (vector a [1 2 (throw (ex-info \"x\" {}))] 3) (catch :default e :caught)))") == kw("caught"))
				#expect(try rt.eval("(let [a (list 1)] (try (vector a (vector 1 2) (throw (ex-info \"x\" {}))) (catch :default e [:caught a])))") == [kw("caught"), Value(list: [1])])
				#expect(try rt.eval("(let [a (list 1)] (try ((fn [x y z] x) a (list 2) (throw (ex-info \"x\" {}))) (catch :default e a)))") == Value(list: [1]))
				#expect(try rt.eval("(let [a (list 1)] (try {:a a :b (throw (ex-info \"x\" {}))} (catch :default e a)))") == Value(list: [1]))
				#expect(try rt.eval("(let [a (list 1)] (try (loop [i 0 j 0] (recur (list a) (throw (ex-info \"x\" {})))) (catch :default e a)))") == Value(list: [1]))
				#expect(caught("(let [a (list 1)] (vector a (vector 1 2) (throw (ex-info \"y\" {}))))") == "y")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Frames past 64 slots retain every param at entry and treat every slot as owned.
		@Test func framesBeyondTheMask() throws {
			let bindings = (0..<70).map { "b\($0) (list \($0))" }.joined(separator: " ")
			let params = (0..<20).map { "p\($0)" }.joined(separator: " ")
			let args = (0..<20).map { "(list \($0))" }.joined(separator: " ")
			let seventy = (0..<70).map { "(list \($0))" }.joined(separator: " ")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [\(bindings)] [b0 b69])") == [Value(list: [0]), Value(list: [69])])
				#expect(try rt.eval("((fn [x] (let [\(bindings)] [x b69])) (list :x))") == [Value(list: [kw("x")]), Value(list: [69])])
				#expect(try rt.eval("((fn [\(params)] (let [\(bindings)] [p0 p19 b69])) \(args))") == [Value(list: [0]), Value(list: [19]), Value(list: [69])])
				#expect(try rt.eval("((fn me [\(params)] (let [\(bindings)] (if (empty? p0) [(count p19) b69] (me \((0..<20).map { _ in "()" }.joined(separator: " ")))))) \(args))") == [0, Value(list: [69])])
				// A big frame is rebound by recur and by a shadowing let like a small one.
				#expect(try rt.eval("((fn [a b i] (let [\(bindings)] (if (< i 3) (recur b a (inc i)) [a b b69]))) (list 1) (list 2) 0)") == [Value(list: [2]), Value(list: [1]), Value(list: [69])])
				#expect(try rt.eval("(loop [i 0 acc []] (let [\(bindings)] (if (< i 2) (recur (inc i) (conj acc b69)) acc)))") == [Value(list: [69]), Value(list: [69])])
				#expect(try rt.eval("((fn [x] (let [\(bindings) x (conj x b0)] x)) [])") == [Value(list: [0])])
				#expect(try rt.eval("(let [\(bindings)] (try (throw b1) (catch :default e [e b69])))") == [Value(list: [1]), Value(list: [69])])
				#expect(caught("(fn [\((0..<21).map { "q\($0)" }.joined(separator: " "))] 1)") == "Can't specify more than 20 params")
				#expect(try rt.eval("((fn [& r] [(count r) (first r) (last r)]) \(seventy))") == [70, Value(list: [0]), Value(list: [69])])
				#expect(try rt.eval("(apply (fn [& r] [(count r) (last r)]) (list \(seventy)))") == [70, Value(list: [69])])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func deftypeMethodsTakeBorrowedArgs() throws {
			try declare("OwBox", "->OwBox", "OwProto", "ow-pm")
			_ = try rt.eval("""
			(defprotocol OwProto (ow-pm [this x]))
			(deftype OwBox [v]
			  IFn
			  (invoke [_ x] (list v x))
			  OwProto
			  (ow-pm [this x] [x v this]))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [b (->OwBox (list 1)) a [2]] (b a))") == Value(list: [Value(list: [1]), [2]]))
				#expect(try rt.eval("(let [b (->OwBox (list 1)) a [2]] (first (ow-pm b a)))") == [2])
				#expect(try rt.eval("(let [b (->OwBox (list 1)) a [2]] (identical? (last (ow-pm b a)) b))") == true)
				#expect(try rt.eval("(let [b (->OwBox (list 1))] (map b [[1] [2]]))") == Value(list: [Value(list: [Value(list: [1]), [1]]), Value(list: [Value(list: [1]), [2]])]))
				#expect(try rt.eval("(let [b (->OwBox (list 1)) a (list 3)] (apply b [a]))") == Value(list: [Value(list: [1]), Value(list: [3])]))
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("OwBox", "->OwBox", "OwProto", "ow-pm")
		}

		@Test func hostClosureTakesLocals() throws {
			try declare("ow-host", "ow-host-calls")
			let before = clj_debug_live_objects()
			do {
				let echo = Value(function: "ow-host", arity: 1...2) { args in args.count == 1 ? args[0] : [args[0], args[1]] }
				define("ow-host", echo)
				#expect(try rt.eval("(let [a (list 1 2)] (ow-host a))") == Value(list: [1, 2]))
				#expect(try rt.eval("(let [a (list 1 2) b [3]] [(ow-host a b) a])") == [[Value(list: [1, 2]), [3]], Value(list: [1, 2])])
				#expect(try rt.eval("((fn [x] (ow-host x)) (list 4))") == Value(list: [4]))
				let calls = Value(function: "ow-host-calls", arity: 2...2) { args in try args[0](args[1]) }
				define("ow-host-calls", calls)
				#expect(try rt.eval("(let [f (fn [x] (list x)) v [5]] (ow-host-calls f v))") == Value(list: [[5]]))
				#expect(try rt.eval("(let [v (list 6)] (ow-host-calls (fn [x] (ow-host x)) v))") == Value(list: [6]))
				try unbind("ow-host", "ow-host-calls")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
