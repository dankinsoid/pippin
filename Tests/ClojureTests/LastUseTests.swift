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
		var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
		guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
		self.node = node
		exec = clj_exec_new(node)
	}

	init(data: Value) throws {
		guard let node = withExtendedLifetime(data, { clj_node_from_data(data.raw) }) else { throw ClojureError.takePending() }
		self.node = node
		exec = clj_exec_new(node)
	}

	func data() throws -> Value {
		let raw = clj_node_to_data(node)
		if raw == CLJ_THROWN { throw ClojureError.takePending() }
		return Value(owning: raw)
	}

	func run() throws -> Value {
		let raw = clj_exec_run(exec)
		if raw == CLJ_THROWN { throw ClojureError.takePending() }
		return Value(owning: raw)
	}

	// The data with every node's trailing `line column` pair stripped.
	func shape() throws -> String { try Self.strip(data()).description }

	private static func strip(_ v: Value) -> Value {
		guard var items = v.array else { return v }
		let node = items.first.map { clj_is_keyword($0.raw) } ?? false
		if node, items.count >= 3, let line = items[items.count - 2].int, line > 0, items[items.count - 1].int != nil {
			items.removeLast(2)
		}
		if node && items[0] == kw("const") { return Value(items) }
		return Value(items.map(strip))
	}

	deinit {
		clj_release(exec)
		clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
	}
}

private func message<T>(_ body: () throws -> T) -> String? {
	do {
		_ = try body()
		return nil
	} catch {
		return (error as? ClojureError)?.message ?? "\(error)"
	}
}

extension CoreTests {
	// Last-use reads (optimizer.c liveness, eval.c eval_local_last).
	@Suite struct LastUseTests {
		let rt = Runtime()

		init() {
			for k in ["a", "b", "x", "y", "xf", "first", "caught", "const", "local", "last", "captured", "var", "the-var", "if", "do", "let", "loop", "recur", "fn", "invoke",
			          "intrinsic", "def", "vector", "map", "try", "throw", "all", "error", "fused", "outer", "direct-fn", "direct-call", "keys"] { _ = kw(k) }
		}

		private func define(_ name: String, _ value: Value) {
			let sym = Value(symbol: name)
			withExtendedLifetime((sym, value)) { clj_var_bind_root(clj_ns_resolve(clj_ns_user(), sym.raw), value.raw) }
		}

		// Consuming calls the analyzed source makes when run (macro expansion runs core.clj, which consumes too), and its value.
		private func consumed(_ source: String) throws -> (Int64, Value) {
			let tree = try Tree(source)
			let before = clj_debug_consuming_calls()
			let v = try tree.run()
			return (clj_debug_consuming_calls() - before, v)
		}

		// The address the value was at: a host fn sees the same object the frame holds.
		@Test func lastUseUpdatesInPlace() throws {
			_ = try rt.eval("(def lu-ptr) (def lu-v)")
			define("lu-ptr", Value(function: "lu-ptr", arity: 1...1) { args in Value(Int(bitPattern: UInt(args[0].raw))) })
			let before = clj_debug_live_objects()
			// The address is kept only where reuse is on; -DCLJ_NO_REUSE copies and the values stay the same.
			let inPlace: Value = clj_reuse_enabled() ? true : false
			do {
				// v is unique: the last read hands it over and conj grows it where it is.
				#expect(try rt.eval("(let [v (vector 1) p (lu-ptr v) w (conj v 2)] [(= p (lu-ptr w)) w])") == [inPlace, [1, 2]])
				#expect(try rt.eval("(let [m (hash-map :a 1) p (lu-ptr m) n (assoc m :b 2)] [(= p (lu-ptr n)) n])") == [inPlace, Value(reading: "{:a 1 :b 2}")])
				#expect(try rt.eval("(let [m (hash-map :a 1 :b 2) p (lu-ptr m) n (dissoc m :b)] [(= p (lu-ptr n)) n])") == [inPlace, Value(reading: "{:a 1}")])
				#expect(try rt.eval("(let [v (vector 1) p (lu-ptr v) w (with-meta v {:a 1})] [(= p (lu-ptr w)) (meta w)])") == [inPlace, Value(reading: "{:a 1}")])
				// A read that is not the last stays a borrow: v is unchanged and the result is a copy.
				#expect(try rt.eval("(let [v (vector 1) p (lu-ptr v) w (conj v 2)] [(= p (lu-ptr w)) v w])") == [false, [1], [1, 2]])
				#expect(try rt.eval("(let [v (vector 1)] (conj v 2) v)") == [1])
				#expect(try Tree("(let [v (vector 1)] (conj v 2) v)").shape()
					== "[:let [[0 [:invoke [:var clojure.core/vector] [:const 1]]]] [:do [:intrinsic clojure.core/conj [:local 0] [:const 2]] [:local 0 :last]]]")
				// Nested: the inner result is site-owned, so the outer call consumes too.
				let (n, v) = try consumed("(let [v (vector 0)] (conj (conj v 1) 2))")
				#expect(n == 2 && v == [0, 1, 2])
				let (m, mv) = try consumed("(let [m (hash-map)] (assoc (assoc m :a 1) :b 2))")
				#expect(try m == 2 && mv == Value(reading: "{:a 1 :b 2}"))
				// Something else holds the value: the hand-over is a plain +1 and the core copies.
				#expect(try rt.eval("(def lu-v (vector 1)) (let [v lu-v] [(conj v 2) lu-v])") == [[1, 2], [1]])
				#expect(try rt.eval("(let [v (vector 1) f (fn [] v)] [(conj v 2) (f)])") == [[1, 2], [1]])
				#expect(try rt.eval("(let [v (vector 1) s (lazy-seq (cons 0 v))] [(conj v 2) s])") == [[1, 2], Value(list: [0, 1])])
				_ = try rt.eval("(def lu-v nil)")
			}
			#expect(clj_debug_live_objects() == before)
			_ = try rt.eval("(def lu-ptr nil)")
		}

		// Loop vars die at the recur; outer locals live across it.
		@Test func loops() throws {
			let before = clj_debug_live_objects()
			do {
				let loop = try Tree("(loop [v [] i 0] (if (< i 3) (recur (conj v i) (inc i)) v))")
				#expect(try loop.shape() == "[:loop [[0 [:const []]] [1 [:const 0]]] [:if [:intrinsic clojure.core/< [:local 1] [:const 3]] [:recur [0 1] [[:intrinsic clojure.core/conj [:local 0 :last] [:local 1]] [:intrinsic clojure.core/inc [:local 1]]]] [:local 0 :last]]]")
				let (n, v) = try consumed("(loop [v [] i 0] (if (< i 3) (recur (conj v i) (inc i)) v))")
				#expect(n == 3 && v == [0, 1, 2])
				#expect(try rt.eval("(loop [m {} i 0] (if (< i 3) (recur (assoc m i (* i i)) (inc i)) m))") == Value(reading: "{0 0 1 1 2 4}"))
				// The outer local: read in every iteration, so never last inside the recur path.
				let outer = try Tree("(let [x [1]] (loop [i 0 acc []] (if (< i 3) (recur (inc i) (conj acc (conj x i))) [acc x])))")
				#expect(try outer.shape().contains("[:intrinsic clojure.core/conj [:local 0] [:local 1]]"))
				#expect(try outer.run() == [[[1, 0], [1, 1], [1, 2]], [1]])
				// On the exit path it is a last use: the loop is over.
				let exit = try Tree("(let [x [1]] (loop [i 0] (if (< i 3) (recur (inc i)) (conj x i))))")
				#expect(try exit.shape().contains("[:intrinsic clojure.core/conj [:local 0 :last] [:local 1]]"))
				#expect(try exit.run() == [1, 3])
				// Not on the exit path of an inner loop the outer one re-enters.
				let nested = try Tree("(let [x [1]] (loop [j 0 acc []] (if (< j 2) (recur (inc j) (conj acc (loop [i 0] (if (< i 2) (recur (inc i)) (conj x i))))) acc)))")
				#expect(try nested.shape().contains("[:intrinsic clojure.core/conj [:local 0] [:local 3]]"))
				#expect(try nested.run() == [[1, 2], [1, 2]])
				// A read in an earlier recur arg is not last when a later one reads the slot.
				#expect(try rt.eval("(loop [v [0] n 0] (if (< n 2) (recur (conj v n) (count v)) [v n]))") == [[0, 0, 1], 2])
				#expect(try rt.eval("(loop [a (list 1) b (list 2) i 0] (if (< i 3) (recur b a (inc i)) [a b]))") == [Value(list: [2]), Value(list: [1])])
				// recur in a fn body: the params are the target.
				let fn = try Tree("(fn [v i] (if (< i 3) (recur (conj v i) (inc i)) v))")
				#expect(try fn.shape().contains("[:intrinsic clojure.core/conj [:local 0 :last] [:local 1]]"))
				#expect(try rt.eval("((fn [v i] (if (< i 3) (recur (conj v i) (inc i)) v)) [] 0)") == [0, 1, 2])
				#expect(try rt.eval("((fn [v & r] (if r (recur (conj v (first r)) (next r)) v)) [] 1 2 3)") == [1, 2, 3])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func branchesAreSeparatePaths() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("(fn [t v] (if t (conj v 1) v))")
				#expect(try tree.shape() == "[:fn nil [[2 false nil 2 [:if [:local 0] [:intrinsic clojure.core/conj [:local 1 :last] [:const 1]] [:local 1 :last]]]] []]")
				#expect(try rt.eval("[((fn [t v] (if t (conj v 1) v)) true []) ((fn [t v] (if t (conj v 1) v)) false [])]") == [[1], []])
				let cond = try Tree("(let [v [] k :a] (cond (= k :a) (conj v 1) (= k :b) (conj v 2) :else v))")
				#expect(try cond.shape().contains("[:intrinsic clojure.core/conj [:local 0 :last] [:const 1]]"))
				#expect(try cond.shape().contains("[:intrinsic clojure.core/conj [:local 0 :last] [:const 2]]"))
				#expect(try cond.run() == [1])
				// The test position reads the slot before the branches: not last when a branch reads it.
				#expect(try Tree("(let [v []] (if v (conj v 1) 2))").shape().contains("[:if [:local 0] [:intrinsic clojure.core/conj [:local 0 :last] [:const 1]] [:const 2]]"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A borrowed operand lives until its call completes: a later operand cannot hand its slot over.
		@Test func borrowedOperandsStayLiveAcrossLaterOperands() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("(let [acc [[1]] i 0] (assoc acc i (conj (nth acc i) 2)))")
				#expect(try tree.shape() == "[:let [[0 [:const [[1]]]] [1 [:const 0]]] [:intrinsic clojure.core/assoc [:local 0] [:local 1] [:intrinsic clojure.core/conj [:intrinsic clojure.core/nth [:local 0] [:local 1]] [:const 2]]]]")
				#expect(try tree.run() == [[1, 2]])
				#expect(try rt.eval("(let [v (vector 1)] (vector v (conj v 2)))") == [[1], [1, 2]])
				#expect(try rt.eval("(let [v (vector 1)] {v (conj v 2)})") == Value(reading: "{[1] [1 2]}"))
				#expect(try rt.eval("(let [f (fn [& r] r)] (let [v (vector 1)] (f v (count v) (conj v 2))))") == Value(list: [[1], 1, [1, 2]]))
				#expect(try rt.eval("(let [v (vector 1)] (conj v (count v)))") == [1, 1])
				// The fn position is an operand too.
				#expect(try rt.eval("(let [f (fn [x] x)] (f (f f)))").isFn)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A capture reads the slot when the closure is made; the closure body is a frame of its own.
		@Test func capturesAndThunks() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("(let [v [1] f (fn [] (conj v 2))] [(f) (f) (fn? f)])")
				#expect(try tree.shape() == "[:let [[0 [:const [1]]] [1 [:fn nil [[0 false nil 0 [:intrinsic clojure.core/conj [:captured 0] [:const 2]]]] [[:local 0]]]]] [:vector [:invoke [:local 1]] [:invoke [:local 1]] [:intrinsic clojure.core/fn? [:local 1]]]]")
				#expect(try tree.run() == [[1, 2], [1, 2], true])
				#expect(try rt.eval("(let [v (vector 1)] (let [f (fn [] v)] (conj v 2)) v)") == [1])
				#expect(try rt.eval("(let [v (vector 1) s (lazy-seq (list (conj v 2)))] [(conj v 3) (first s) v])") == [[1, 3], [1, 2], [1]])
				// A param of the closure is borrowed from the caller: its last use is an ordinary owned read.
				#expect(try rt.eval("(let [v (vector 1) g (fn [x] (conj x 2))] [(g v) v])") == [[1, 2], [1]])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A slot a direct fn body reads through the static link is live for the whole defining frame.
		@Test func directFnReadsPinTheSlot() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("(let [v [1] f (fn [] (count v))] (let [w (conj v 2)] [(f) w]))")
				#expect(try tree.data().description.contains(":direct-fn"))
				#expect(try tree.shape().contains("[:intrinsic clojure.core/conj [:local 0] [:const 2]]"))
				#expect(try tree.run() == [1, [1, 2]])
				let after = try Tree("(let [v [1] f (fn [] (count v))] [(f) (conj v 2)])")
				#expect(try after.shape().contains("[:intrinsic clojure.core/conj [:local 0] [:const 2]]"))
				#expect(try after.run() == [1, [1, 2]])
				// Two links up, and a closure made inside the direct fn body capturing the definer's slot.
				let deep = try Tree("(let [v [1] f (fn [] (let [g (fn [] (count v))] (g)))] (let [w (conj v 2)] [(f) w]))")
				#expect(try deep.shape().contains("[:intrinsic clojure.core/conj [:local 0] [:const 2]]"))
				#expect(try deep.run() == [1, [1, 2]])
				let capture = try Tree("(let [v [1] f (fn [] (fn [] v))] (let [w (conj v 2)] [((f)) w]))")
				#expect(try capture.shape().contains("[:intrinsic clojure.core/conj [:local 0] [:const 2]]"))
				#expect(try capture.run() == [[1], [1, 2]])
				// The direct fn's own params are its frame: their last use hands over as in any frame.
				let own = try Tree("(let [f (fn [x] (conj x 1))] (f (vector 0)))")
				#expect(try own.shape().contains("[:intrinsic clojure.core/conj [:local 0 :last] [:const 1]]"))
				#expect(try own.run() == [0, 1])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Anything a handler or finally reads is live at every point of the try body.
		@Test func tryKeepsHandlerReadsLive() throws {
			let before = clj_debug_live_objects()
			do {
				let caught = try Tree("(let [v [1]] (try (conj v 2) (catch :default e v)))")
				#expect(try caught.shape() == "[:let [[0 [:const [1]]]] [:try [:intrinsic clojure.core/conj [:local 0] [:const 2]] [[:all 1 [:local 0 :last]]] nil]]")
				#expect(try caught.run() == [1, 2])
				let fin = try Tree("(let [v [1]] (try (conj v 2) (finally (count v))))")
				#expect(try fin.shape() == "[:let [[0 [:const [1]]]] [:try [:intrinsic clojure.core/conj [:local 0] [:const 2]] [] [:intrinsic clojure.core/count [:local 0]]]]")
				#expect(try fin.run() == [1, 2])
				#expect(try rt.eval("(let [v (vector 1)] (try (do (conj v 2) (throw (ex-info \"x\" {}))) (catch :default e v)))") == [1])
				// A handler's own reads are last where nothing follows; the catch slot is a frame slot like any other.
				let handler = try Tree("(let [v [1]] (try (throw (ex-info \"x\" {})) (catch :default e [(conj v 2) (ex-message e)])))")
				#expect(try handler.shape().contains("[:vector [:intrinsic clojure.core/conj [:local 0 :last] [:const 2]] [:invoke [:var clojure.core/ex-message] [:local 1]]]"))
				#expect(try handler.run() == [[1, 2], "x"])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A consumer that throws: the handed-over value is released once, by the consumer or the site.
		@Test func throwingConsumers() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [v (vector 1)] (try (conj v (throw (ex-info \"x\" {}))) (catch :default e :caught)))") == kw("caught"))
				#expect(try rt.eval("(let [v (vector 1)] (try (with-meta v 1) (catch :default e :caught)))") == kw("caught"))
				#expect(try rt.eval("(let [v (vector 1)] (try (dissoc v :a) (catch :default e :caught)))") == kw("caught"))
				#expect(try rt.eval("(let [v (vector 1)] (try (assoc v 5 :x) (catch :default e :caught)))") == kw("caught"))
				#expect(try rt.eval("(let [v (list 1)] (try (assoc v 0 :x) (catch :default e :caught)))") == kw("caught"))
				#expect(try rt.eval("(let [b (ex-info \"b\" {})] (try (conj b 1) (catch :default e (ex-message e))))") == "conj not supported on this type: exception")
				#expect(try rt.eval("(let [v (vector 1)] (try (loop [v v i 0] (if (< i 3) (recur (conj v (if (= i 2) (throw (ex-info \"x\" {})) i)) (inc i)) v)) (catch :default e :caught)))") == kw("caught"))
				#expect(message { try rt.eval("(let [v (vector 1)] (conj v (throw (ex-info \"y\" {}))))") } == "y")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A shadowing let binds a fresh slot; destructuring expands to lets and behaves the same.
		@Test func shadowingAndDestructuring() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("(let [v [1]] (let [v (conj v 2)] v))")
				#expect(try tree.shape() == "[:let [[0 [:const [1]]]] [:let [[1 [:intrinsic clojure.core/conj [:local 0 :last] [:const 2]]]] [:local 1 :last]]]")
				#expect(try tree.run() == [1, 2])
				#expect(try rt.eval("((fn [x] (let [x (conj x 2) x (conj x 3)] x)) [1])") == [1, 2, 3])
				#expect(try rt.eval("(let [[a b] [1 2] v (vector a)] (conj v b))") == [1, 2])
				#expect(try rt.eval("(let [{:keys [a b]} {:a [1] :b 2}] (conj a b))") == [1, 2])
				#expect(try rt.eval("(let [[a & r] [[1] 2 3]] [(conj a 0) r])") == [[1, 0], Value(list: [2, 3])])
				#expect(try rt.eval("((fn [[a b :as all]] [(conj a b) all]) [[1] 2])") == [[1, 2], [[1], 2]])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The drivers: a consuming native as the reducing fn takes the accumulator the driver owns.
		@Test func driversHandTheAccumulatorOver() throws {
			let before = clj_debug_live_objects()
			do {
				let (n, v) = try consumed("(reduce conj [] (range 5))")
				#expect(n == 5 && v == [0, 1, 2, 3, 4])
				let (n2, v2) = try consumed("(reduce conj [0] (map inc (range 3)))")
				#expect(n2 == 3 && v2 == [0, 1, 2, 3])
				let (n3, v3) = try consumed("(reduce-kv assoc {} {:a 1 :b 2})")
				#expect(try n3 == 2 && v3 == Value(reading: "{:a 1 :b 2}"))
				// The 2-arity seeds from the first element: the seed is shared with the source, the steps consume.
				let (n4, v4) = try consumed("(reduce conj [[] 1 2 3])")
				#expect(n4 == 3 && v4 == [1, 2, 3])
				// A user fn as the reducing fn sees the accumulator at +0: its param is borrowed, so nothing is handed over.
				let (n5, v5) = try consumed("(reduce (fn [a x] (conj a x)) [] (range 3))")
				#expect(n5 == 0 && v5 == [0, 1, 2])
				#expect(try rt.eval("(let [v (vector 0)] [(reduce (fn [a x] (conj a x)) v [1 2]) v])") == [[0, 1, 2], [0]])
				#expect(try rt.eval("(let [v (vector 0)] [(reduce conj v [1 2]) v])") == [[0, 1, 2], [0]])
				#expect(try rt.eval("(let [v (vector 0)] [(into [] (map inc) [1 2]) v])") == [[2, 3], [0]])
				// into with an xform goes through the driver: the same results as transduce, completion included.
				#expect(try rt.eval("(into [] (map inc) (range 5))") == [1, 2, 3, 4, 5])
				#expect(try rt.eval("(into () (map inc) [1 2])") == Value(list: [3, 2]))
				#expect(try rt.eval("(into {} (map identity) {:a 1})") == Value(reading: "{:a 1}"))
				#expect(try rt.eval("(into [] (take 2) (range))") == [0, 1])
				#expect(try rt.eval("(into [] (partition-all 2) (range 5))") == [[0, 1], [2, 3], [4]])
				#expect(try rt.eval("(into [] (comp (filter even?) (map inc)) (range 6))") == [1, 3, 5])
				#expect(try rt.eval("(let [v (vector 0)] (into v (map inc) (range 3)))") == [0, 1, 2, 3])
				#expect(try rt.eval("[(into [] (map inc) nil) (into nil (map inc) [1])]") == [[], Value(list: [2])])
				#expect(message { try rt.eval("(into [] (map (fn [x] (throw (ex-info \"xf\" {})))) [1])") } == "xf")
				#expect(message { try rt.eval("(into [] (map inc) 1)") } == "Don't know how to create ISeq from: long")
				#expect(try rt.eval("(= (into [] (map inc) (range 5)) (transduce (map inc) conj [] (range 5)))") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// [:local slot :last] reads back as the mark; it is bounds-checked like a local.
		@Test func serialization() throws {
			let before = clj_debug_live_objects()
			do {
				let tree = try Tree("(let [v (vector 1)] (conj v 2))")
				let data = try tree.data()
				#expect(try data == Value(reading: "[:let [[0 [:invoke [:var clojure.core/vector 1 9] [:const 1 1 9] 1 9]]] [:intrinsic clojure.core/conj [:local 0 :last 1 21] [:const 2 1 21] 1 21] 1 1]"))
				let text = try #require(Value(owning: clj_pr_str(data.raw)).string)
				let read = try Tree(data: Value(reading: text))
				#expect(try read.data() == data)
				#expect(read.kinds == tree.kinds)
				#expect(try read.run() == [1, 2])
				#expect(try Tree(data: Value(reading: "[:let [[0 [:const 1]]] [:local 0 :last]]")).run() == 1)
				#expect(message { try Tree(data: Value(reading: "[:fn nil [[1 false nil 1 [:local 1 :last]]] []]")) } == "malformed node data, slot 1 outside a frame of 1")
				#expect(message { try Tree(data: Value(reading: "[:local 0 :first]")) } == "malformed node data, expected a slot number: [:local 0 :first]")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}

private extension Tree {
	var kinds: [clj_node_kind] {
		var out: [clj_node_kind] = []
		collect(node, &out)
		return out
	}

	private func collect(_ n: UnsafePointer<clj_node>, _ out: inout [clj_node_kind]) {
		out.append(n.pointee.kind)
		var children: [UnsafePointer<clj_node>] = []
		withUnsafeMutablePointer(to: &children) { ctx in
			clj_node_children(n, { child, ctx in
				ctx!.assumingMemoryBound(to: [UnsafePointer<clj_node>].self).pointee.append(child!)
			}, ctx)
		}
		for c in children { collect(c, &out) }
	}
}
