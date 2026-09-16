// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

// The value or the thrown message of one evaluation; values agree when `=` or printed alike.
private enum Outcome: Equatable {
	case value(Value)
	case thrown(String)

	init(_ source: String) {
		do {
			self = .value(try cljEval(source))
		} catch let e as CljEvalFailure {
			self = .thrown(e.message)
		} catch {
			self = .thrown("\(error)")
		}
	}

	var isThrown: Bool {
		if case .thrown = self { return true }
		return false
	}

	static func == (a: Outcome, b: Outcome) -> Bool {
		switch (a, b) {
		case let (.value(x), .value(y)): return x == y || x.description == y.description
		case let (.thrown(x), .thrown(y)): return x == y
		default: return false
		}
	}
}

// An analyzed tree with its exec table; releases both.
private final class Tree {
	let node: UnsafeMutablePointer<clj_node>
	let exec: clj_value

	let source: String

	convenience init(source: String) throws { try self.init(source) }

	init(_ source: String, ns: clj_value = CLJ_NIL) throws {
		self.source = source
		let form = try Value(reading: source)
		var env = clj_env(ns: ns, line: 0, col: 0)
		guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
		self.node = node
		exec = clj_exec_new(node)
	}

	init(data: Value) throws {
		source = data.description
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

	var kinds: [clj_node_kind] { nodes.map { $0.pointee.kind } }

	// Every node in id order.
	var nodes: [UnsafePointer<clj_node>] {
		var out: [UnsafePointer<clj_node>] = []
		collect(node, &out)
		return out
	}

	private func collect(_ n: UnsafePointer<clj_node>, _ out: inout [UnsafePointer<clj_node>]) {
		out.append(n)
		var children: [UnsafePointer<clj_node>] = []
		withUnsafeMutablePointer(to: &children) { ctx in
			clj_node_children(n, { child, ctx in
				ctx!.assumingMemoryBound(to: [UnsafePointer<clj_node>].self).pointee.append(child!)
			}, ctx)
		}
		for c in children { collect(c, &out) }
	}

	var fusedNodes: [UnsafePointer<clj_node>] { nodes.filter { $0.pointee.kind == CLJ_NODE_FUSED } }

	// Hits of the fused and the original program of the first FUSED node, counted while clj_exec_count is on.
	var pathHits: (fused: UInt64, original: UInt64) {
		let f = fusedNodes[0].pointee.u.fused
		return (clj_exec_hits(exec, f.fused.pointee.id), clj_exec_hits(exec, f.original.pointee.id))
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

private func evalMessage(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func coreVar(_ name: String) -> clj_value {
	let sym = Value(symbol: name)
	return withExtendedLifetime(sym) { clj_ns_resolve(clj_ns_core(), sym.raw) }
}

private func clojureError(_ source: String) -> ClojureError? {
	do {
		_ = try Runtime().eval(source)
		return nil
	} catch let e as ClojureError {
		return e
	} catch {
		return nil
	}
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

extension CoreTests {
	// The fusion pass: a consumer over lazy stages runs through the transducer drivers, guarded by the core roots.
	@Suite struct FusionTests {
		init() {
			clj_init()
			for k in ["k", "const", "local", "captured", "var", "the-var", "if", "do", "let", "loop", "recur", "fn", "invoke", "intrinsic", "fused", "outer", "direct-fn", "direct-call",
			          "def", "vector", "map", "try", "throw", "all", "error", "f", "init", "g", "p", "coll", "redefined", "local", "mine", "e", "done", "a", "b", "two", "three"] {
				_ = Value(keyword: k)
			}
		}

		@Test func rewriteShape() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try Tree("(reduce + (map inc (range 4)))").data() == Value(reading: """
				[:fused [clojure.core/reduce clojure.core/map clojure.core/fused-reduce*]
				 [[:var clojure.core/+ 1 1] [:var clojure.core/inc 1 11] [:invoke [:var clojure.core/range 1 20] [:const 4 1 20] 1 20]]
				 [:invoke [:var clojure.core/fused-reduce* 1 1] [:local 0 1 1] [:local 2 1 1] [:vector [:invoke [:var clojure.core/map 1 1] [:local 1 1 1] 1 1] 1 1] 1 1]
				 [:invoke [:var clojure.core/reduce 1 1] [:local 0 1 1] [:invoke [:var clojure.core/map 1 1] [:local 1 1 1] [:local 2 1 1] 1 1] 1 1] 1 1]
				"""))
				#expect(try Tree("(vec (dedupe [1 2]))").data() == Value(reading: """
				[:fused [clojure.core/vec clojure.core/dedupe clojure.core/fused-into*] [[:const [1 2] 1 6]]
				 [:invoke [:var clojure.core/fused-into* 1 1] [:const [] 1 1] [:local 0 1 1] [:vector [:invoke [:var clojure.core/dedupe 1 1] 1 1] 1 1] 1 1]
				 [:invoke [:var clojure.core/vec 1 1] [:invoke [:var clojure.core/dedupe 1 1] [:local 0 1 1] 1 1] 1 1] 1 1]
				"""))
				// Stages nest consumer-first in the args and in the vector; the original count is the intrinsic.
				#expect(try Tree("(count (take 2 (filter even? [1 2])))").data() == Value(reading: """
				[:fused [clojure.core/count clojure.core/take clojure.core/filter clojure.core/fused-count*]
				 [[:const 2 1 8] [:var clojure.core/even? 1 16] [:const [1 2] 1 16]]
				 [:invoke [:var clojure.core/fused-count* 1 1] [:local 2 1 1] [:vector [:invoke [:var clojure.core/take 1 1] [:local 0 1 1] 1 1] [:invoke [:var clojure.core/filter 1 1] [:local 1 1 1] 1 1] 1 1] 1 1]
				 [:intrinsic clojure.core/count [:invoke [:var clojure.core/take 1 1] [:local 0 1 1] [:invoke [:var clojure.core/filter 1 1] [:local 1 1 1] [:local 2 1 1] 1 1] 1 1] 1 1] 1 1]
				"""))
				#expect(try Tree("(into () (map inc (filter even? [1 2])))").data() == Value(reading: """
				[:fused [clojure.core/into clojure.core/map clojure.core/filter clojure.core/fused-into*]
				 [[:const () 1 1] [:var clojure.core/inc 1 10] [:var clojure.core/even? 1 19] [:const [1 2] 1 19]]
				 [:invoke [:var clojure.core/fused-into* 1 1] [:local 0 1 1] [:local 3 1 1] [:vector [:invoke [:var clojure.core/map 1 1] [:local 1 1 1] 1 1] [:invoke [:var clojure.core/filter 1 1] [:local 2 1 1] 1 1] 1 1] 1 1]
				 [:invoke [:var clojure.core/into 1 1] [:local 0 1 1] [:invoke [:var clojure.core/map 1 1] [:local 1 1 1] [:invoke [:var clojure.core/filter 1 1] [:local 2 1 1] [:local 3 1 1] 1 1] 1 1] 1 1] 1 1]
				"""))
				#expect(try Tree("(reduce + 0 (map inc [1]))").fusedNodes.count == 1)
				#expect(try Tree("(reduce + 0 (map inc [1]))").run() == 2)
				// A pipeline as the source of another is fused on its own.
				let nested = try Tree("(reduce + (map inc (vec (map inc [1 2]))))")
				#expect(nested.fusedNodes.count == 2)
				#expect(try nested.run() == 7)
				// Every stage kind, chained.
				let all = try Tree(source: """
				(vec (dedupe (interpose 0 (keep-indexed (fn [i x] (when (even? i) x)) (map-indexed + (mapcat (fn [x] [x x])
				  (drop-while neg? (take-while (fn [x] (< x 100)) (drop 1 (take 8 (remove odd? (filter even? (keep identity (map inc (range -3 20)))))))))))))))
				""")
				#expect(all.fusedNodes.count == 1)
				#expect(all.fusedNodes[0].pointee.u.fused.nargs == 13 && all.fusedNodes[0].pointee.u.fused.nguards == 15)
				let was = clj_fusion_set_enabled(false)
				let lazy = try Tree(all.source)
				clj_fusion_set_enabled(was)
				#expect(lazy.fusedNodes.isEmpty)
				#expect(try all.run() == lazy.run())
				#expect(try all.run() == [0, 4, 0, 8, 0, 12, 0, 16, 0, 20, 0, 24])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Multi-coll map, a local or foreign head, another consumer, a value position, a stage arity without a transducer.
		@Test func notFused() throws {
			let nsName = Value(symbol: "fusion-ns"), mapSym = Value(symbol: "map")
			let ns = withExtendedLifetime(nsName) { clj_ns_find_or_create(nsName.raw) }
			let own = withExtendedLifetime(mapSym) { clj_ns_intern(ns, mapSym.raw) }
			do {
				let mine = try cljEval("(fn [f c] [:mine])")
				withExtendedLifetime(mine) { clj_var_bind_root(own, mine.raw) }
			}
			_ = try cljEval("(def fu-map (fn [f c] [:mine]))")
			let before = clj_debug_live_objects()
			do {
				func unfused(_ source: String, ns: clj_value = CLJ_NIL) throws -> Bool { try Tree(source, ns: ns).fusedNodes.isEmpty }
				#expect(try unfused("(reduce + (map + [1 2] [10 20]))"))
				#expect(try cljEval("(reduce + (map + [1 2] [10 20]))") == 33)
				#expect(try unfused("(reduce + (mapcat (fn [a b] [a b]) [1 2] [3 4]))"))
				#expect(try cljEval("(reduce + (mapcat (fn [a b] [a b]) [1 2] [3 4]))") == 10)
				#expect(try unfused("(let [map (fn [f c] [:local])] (reduce conj [] (map inc [1])))"))
				#expect(try cljEval("(let [map (fn [f c] [:local])] (reduce conj [] (map inc [1])))") == [kw("local")])
				#expect(try unfused("(reduce conj [] (fu-map inc [1]))"))
				#expect(try cljEval("(reduce conj [] (fu-map inc [1]))") == [kw("mine")])
				#expect(try unfused("(reduce conj [] (map inc [1]))", ns: ns))
				#expect(try Tree("(reduce conj [] (map inc [1]))", ns: ns).run() == [kw("mine")])
				#expect(try Tree("(reduce conj [] (clojure.core/map inc [1]))", ns: ns).fusedNodes.count == 1)
				#expect(try Tree("(reduce conj [] (clojure.core/map inc [1]))", ns: ns).run() == [2])
				for source in ["(first (map inc [1 2]))", "(seq (map inc []))", "(doall (map inc [1]))", "(map inc [1])", "(reduce + [1 2])", "(reduce + 0 (range 3))",
				               "(let [p (map inc [1 2])] (reduce + p))", "(count (partition-all 2 [1 2 3]))", "(into [] (map inc) [1])", "(reduce + (eduction (map inc) [1]))",
				               "(vec (map inc [1] [2]))", "(count (interpose 0))", "(reduce + (map inc))"] {
					#expect(try unfused(source), Comment(rawValue: source))
				}
				#expect(try cljEval("[(first (map inc [1 2])) (seq (map inc [])) (doall (map inc [1])) (let [p (map inc [1 2])] (reduce + p)) (count (partition-all 2 [1 2 3]))]") == [2, nil, Value(list: [2]), 5, 2])
				// partition-all's transducer emits vectors where the lazy arity emits seqs; conj on a seed tells them apart.
				#expect(try cljEval("(reduce conj (partition-all 2 [1 2 3]))").description == "((3) 1 2)")
				#expect(try Tree("(reduce conj (partition-all 2 (map inc [1 2 3])))").fusedNodes.isEmpty)
				// A lazy partition-all is still a source for the stages above it.
				#expect(try Tree("(vec (map first (partition-all 2 [1 2 3])))").fusedNodes.count == 1)
				#expect(try cljEval("(vec (map first (partition-all 2 [1 2 3])))") == [1, 3])
				#expect(try unfused("(fn [x] (reduce x (map inc [1])))") == false)
			}
			#expect(clj_debug_live_objects() == before)
			_ = try cljEval("(def fu-map nil)")
			clj_var_bind_root(own, CLJ_NIL)
		}

		// The same forms with the pass off: every consumer, every stage, every source kind; values and messages agree.
		// One documented deviation (NOTES.md): the fused walk seqs the source even when `(take 0 ...)` would never pull
		// from it, so a non-seqable source throws where the lazy pipeline was empty.
		@Test func differentialAgainstTheLazyPath() throws {
			let sources = ["[1 2 2 3 4 5]", "'(1 2 2 3 4 5)", "(range 6)", "(lazy-seq [1 2 2 3 4 5])", "(seq [1 2 2 3 4 5])", "(next [1 2 2 3 4 5])", "(list* 1 2 [2 3 4 5])",
		                   "\"abbcde\"", "(seq \"ab\")", "[]", "()", "nil", "(range 0)", "\"\"", "{:a 1 :b 2}", "(lazy-seq nil)", "5", ":k",
		                   "(take 6 (iterate inc 1))", "(concat [1 2] (lazy-seq [2 3]))"]
			let stages = ["(map inc %)", "(keep (fn [x] (when (odd? x) (* x x))) %)", "(filter odd? %)", "(remove odd? %)", "(take 3 %)", "(take 0 %)", "(drop 2 %)",
		                  "(take-while (fn [x] (< x 3)) %)", "(drop-while (fn [x] (< x 3)) %)", "(mapcat (fn [x] [x x]) %)", "(map-indexed vector %)",
		                  "(keep-indexed (fn [i x] (when (odd? i) x)) %)", "(interpose 0 %)", "(dedupe %)", "(distinct %)", "(map str %)", "(map identity %)",
		                  "(take 2 (map inc (filter odd? %)))", "(interpose :k (dedupe (drop 1 %)))", "(mapcat identity (partition-all 2 %))", "(distinct (mapcat (fn [x] [x x]) %))"]
			let consumers = ["(reduce + %)", "(reduce + 10 %)", "(reduce conj [] %)", "(reduce conj %)", "(reduce (fn [a x] (conj a x)) %)", "(reduce (fn [a x] [a x]) %)",
		                     "(reduce (fn [a x] (if (= x 3) (reduced a) [a x])) [] %)", "(reduce (fn [] :e) %)", "(reduce (fn [a b] (if (> a b) a b)) %)", "(vec %)", "(count %)", "(into () %)",
		                     "(into [0] %)", "(into nil %)", "(into {} %)", "(reduce str %)", "(reduce str \"\" %)"]
			var fused = 0, agreed = 0, thrown = 0
			let before = clj_debug_live_objects()
			do {
				for consumer in consumers {
					for stage in stages {
						for source in sources {
							let form = consumer.replacingOccurrences(of: "%", with: stage.replacingOccurrences(of: "%", with: source))
							let on = Outcome(form)
							let was = clj_fusion_set_enabled(false)
							let off = Outcome(form)
							clj_fusion_set_enabled(was)
							let nonSeqable = source == "5" || source == ":k"
							if nonSeqable && stage == "(take 0 %)" {
								#expect(on == .thrown("#error {:message \"Don't know how to create ISeq from: \(source == "5" ? "long" : "keyword")\", :data nil}"), Comment(rawValue: form))
							} else {
								#expect(on == off, Comment(rawValue: form))
							}
							if on == off { agreed += 1 }
							if on.isThrown { thrown += 1 }
						}
					}
					fused += try Tree(consumer.replacingOccurrences(of: "%", with: "(map inc [1])")).fusedNodes.count
				}
			}
			#expect(clj_debug_live_objects() == before)
			#expect(fused == consumers.count)
			#expect(agreed > consumers.count * stages.count * sources.count * 95 / 100)
			#expect(thrown > 100 && thrown < agreed / 2)
			// The pass is back on for everyone else.
			#expect(clj_fusion_enabled())
		}

		// Argument expressions run once each, left to right, before any stage is built; the guard changes nothing.
		@Test func argumentsEvaluateOnceInOrder() throws {
			let before = clj_debug_live_objects()
			do {
				let form = """
				(let [log (volatile! [])
				      note (fn [k v] (vswap! log conj k) v)]
				  [(reduce (note :f +) (note :init 0) (map (note :g inc) (filter (note :p even?) (note :coll (range 10))))) @log])
				"""
				#expect(try cljEval(form) == [25, [kw("f"), kw("init"), kw("g"), kw("p"), kw("coll")]])
				let was = clj_fusion_set_enabled(false)
				defer { clj_fusion_set_enabled(was) }
				#expect(try cljEval(form) == [25, [kw("f"), kw("init"), kw("g"), kw("p"), kw("coll")]])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func infiniteSourcesTerminate() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("(vec (take 5 (map inc (range))))") == [1, 2, 3, 4, 5])
				#expect(try cljEval("(reduce + (take 3 (iterate inc 0)))") == 3)
				#expect(try cljEval("(count (take-while (fn [x] (< x 5)) (map inc (range))))") == 4)
				#expect(try cljEval("(into [] (take 2 (filter even? (iterate inc 0))))") == [0, 2])
				#expect(try cljEval("(reduce + 0 (take 4 (mapcat (fn [x] [x x]) (range))))") == 2)
				#expect(try cljEval("(vec (take 3 (partition-all 2 (range))))") == [[0, 1], [2, 3], [4, 5]])
				#expect(try cljEval("(vec (take 3 (dedupe (mapcat (fn [x] [x x]) (range)))))") == [0, 1, 2])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// f's reduced stops the walk and realizes nothing past it; a reduced box among the values or as the seed is data.
		@Test func earlyReduced() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("(reduce (fn [a x] (if (= x 3) (reduced a) (+ a x))) 0 (map inc (range)))") == 3)
				#expect(try cljEval("(reduce (fn [a x] (if (> a 5) (reduced :done) (+ a x))) (map inc (range)))") == kw("done"))
				#expect(try cljEval("(let [n (volatile! 0)] [(reduce (fn [a x] (if (= x 2) (reduced a) (+ a x))) 0 (map (fn [x] (vswap! n inc) x) (range 100))) @n])") == [1, 3])
				#expect(try cljEval("(let [n (volatile! 0)] [(vec (take 2 (map (fn [x] (vswap! n inc) x) (range 100)))) @n])") == [[0, 1], 2])
				#expect(try cljEval("(into [] (map (fn [x] (reduced x)) [1 2]))").description == "[#object[reduced] #object[reduced]]")
				#expect(try cljEval("(count (map reduced [1 2 3]))") == 3)
				#expect(try cljEval("(reduce (fn [a x] [a x]) (map identity [(reduced 5) 1]))").description == "[#object[reduced] 1]")
				#expect(try cljEval("[(reduced? (reduce + (map identity [(reduced 7)]))) (reduced? (reduce + (reduced 7) (map inc []))) (reduced? (reduce + (reduced 7) (take 0 (range))))]") == [true, true, true])
				#expect(try cljEval("(reduce (fn [a x] (if (reduced? a) (+ @a x) (+ a x))) (reduced 7) (map inc [1 2]))") == 12)
				#expect(evalMessage("(reduce + (reduced 7) (map inc [1 2]))") == "reduced cannot be cast to a number")
				#expect(try cljEval("(vec (mapcat (fn [x] [(reduced x)]) [1]))").description == "[#object[reduced]]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A throw in a stage fn or in the source propagates as from the lazy pipeline, the enclosing fn on the trace.
		@Test func throwsPropagateWithTheTrace() throws {
			_ = try cljEval("""
			(defn fu-stage [] (reduce + (map (fn [x] (throw (ex-info "stage" {:x x}))) [1 2])))
			(defn fu-source [] (count (map inc (lazy-seq (throw (ex-info "source" {}))))))
			(defn fu-f [] (reduce (fn [a x] (throw (ex-info "f" {}))) (filter odd? [1 2 3])))
			(defn fu-into [] (into 1 (map inc [1])))
			""")
			let before = clj_debug_live_objects()
			do {
				let stage = try #require(clojureError("(fu-stage)"))
				#expect(stage.message == "stage" && stage.data == [kw("x"): 1])
				#expect(stage.trace.first?.fn == nil && stage.trace.last?.fn == "user/fu-stage" && stage.trace.last?.line == 1)
				let source = try #require(clojureError("(fu-source)"))
				#expect(source.message == "source" && source.trace.last?.fn == "user/fu-source" && source.trace.last?.line == 1)
				let f = try #require(clojureError("(fu-f)"))
				#expect(f.message == "f" && f.trace.first?.fn == nil && f.trace.last?.fn == "user/fu-f")
				#expect(clojureError("(fu-into)")?.message == "conj not supported on this type: long")
				#expect(evalMessage("(reduce + (map inc 5))") == "Don't know how to create ISeq from: long")
				#expect(evalMessage("(reduce + (map inc [1 :k]))") == "keyword cannot be cast to a number")
				#expect(evalMessage("(vec (take :k [1]))") == "keyword cannot be cast to a number")
				#expect(evalMessage("(count (map 1 [1]))") == "1 cannot be invoked")
			}
			#expect(clj_debug_live_objects() == before)
			_ = try cljEval("(def fu-stage nil) (def fu-source nil) (def fu-f nil) (def fu-into nil)")
		}

		// A rebound stage or consumer var sends the site down the original program until the boot root is back.
		@Test func guardFallsBackWhenACoreVarIsRebound() throws {
			let mapVar = coreVar("map"), reduceVar = coreVar("reduce")
			let bootMap = clj_var_root(mapVar), bootReduce = clj_var_root(reduceVar)
			let meta = Value(borrowing: clj_var_meta(mapVar))
			let tree = try Tree("(fn [xs] (reduce conj [] (map inc xs)))")
			let f = try tree.run()
			clj_exec_count(tree.exec, true)
			let before = clj_debug_live_objects()
			do {
				#expect(try f([1, 2]) == [2, 3])
				#expect(tree.pathHits == (1, 0))
				do {
					let wrapper = try cljEval("(let [m map] (fn [& args] (apply m args)))")
					withExtendedLifetime(wrapper) { clj_var_bind_root(mapVar, wrapper.raw) }
				}
				#expect(try f([1, 2]) == [2, 3])
				#expect(tree.pathHits == (1, 1))
				clj_var_bind_root(mapVar, bootMap)
				#expect(try f([1, 2]) == [2, 3])
				#expect(tree.pathHits == (2, 1))

				clj_ns_set_current(clj_ns_core())
				_ = try cljEval("(def map (fn [f coll] [:redefined]))")
				clj_ns_set_current(clj_ns_user())
				#expect(try f([1, 2]) == [kw("redefined")])
				#expect(try cljEval("(reduce conj [] (map inc [1]))") == [kw("redefined")])
				#expect(tree.pathHits == (2, 2))
				clj_var_bind_root(mapVar, bootMap)
				withExtendedLifetime(meta) { clj_var_set_meta(mapVar, meta.raw) }
				#expect(try f([1, 2]) == [2, 3])
				#expect(tree.pathHits == (3, 2))

				do {
					let counting = try cljEval("(fn ([f coll] :two) ([f init coll] :three))")
					withExtendedLifetime(counting) { clj_var_bind_root(reduceVar, counting.raw) }
				}
				#expect(try f([1, 2]) == kw("three"))
				#expect(tree.pathHits == (3, 3))
				clj_var_bind_root(reduceVar, bootReduce)
				#expect(try f([1, 2]) == [2, 3])
				#expect(tree.pathHits == (4, 3))
				#expect(clj_var_root(mapVar) == bootMap && clj_var_root(reduceVar) == bootReduce)
			}
			clj_exec_count(tree.exec, false)
			#expect(clj_debug_live_objects() == before)
		}

		// [:fused ...] reads back as the same data and evaluates through the fused program; malformed shapes are refused.
		@Test func serializationRoundTrip() throws {
			let before = clj_debug_live_objects()
			do {
				for source in ["(reduce + (map inc (filter even? (range 10))))", "(fn [xs] (vec (take 2 (map inc xs))))",
				               "(let [n 3] (count (take n (interpose 0 (dedupe [1 1 2 3])))))", "(into () (mapcat (fn [x] [x x]) [1 2]))"] {
					let tree = try Tree(source)
					let data = try tree.data()
					#expect(data.description.contains(":fused"))
					let text = try #require(Value(owning: clj_pr_str(data.raw)).string)
					let read = try Tree(data: Value(reading: text))
					#expect(try read.data() == data, Comment(rawValue: source))
					#expect(read.node.pointee.nnodes == tree.node.pointee.nnodes && read.fusedNodes.count == 1)
					clj_exec_count(read.exec, true)
					let value = try read.run()
					if clj_is_fn(value.raw) {
						#expect(try value([1, 2, 3]) == tree.run()([1, 2, 3]), Comment(rawValue: source))
					} else {
						#expect(try value == tree.run(), Comment(rawValue: source))
					}
					#expect(read.pathHits == (1, 0), Comment(rawValue: source))
				}
				#expect(try Tree(data: Value(reading: "[:fused [clojure.core/map] [[:const 3]] [:local 0] [:local 0]]")).run() == 3)
				#expect(message { try Tree(data: Value(reading: "[:fused [clojure.core/nope] [[:const 3]] [:local 0] [:local 0]]")) } == "malformed node data, unknown fusion var: [clojure.core/nope]")
				#expect(message { try Tree(data: Value(reading: "[:fused [clojure.core/first] [] [:const 3] [:const 3]]")) } == "malformed node data, unknown fusion var: [clojure.core/first]")
				#expect(message { try Tree(data: Value(reading: "[:fused [clojure.core/map] [[:const 3]] [:local 1] [:local 0]]")) } == "malformed node data, slot 1 outside a frame of 1")
				#expect(message { try Tree(data: Value(reading: "[:fused [] [] [:const 3] [:const 3]]")) } == "malformed node data, expected [guards args fused original]: [:fused [] [] [:const 3] [:const 3]]")
				#expect(message { try Tree(data: Value(reading: "[:fused [clojure.core/map] [:const 3]]")) } == "malformed node data, expected [guards args fused original]: [:fused [clojure.core/map] [:const 3]]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The drivers by hand, and their contract: (f) seeds only an empty walk, the transducers see nil.
		@Test func driversByHand() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("[(fused-reduce* + [1 2 3] [(map inc)]) (fused-reduce* + 10 [1 2] []) (fused-reduce* + [] []) (fused-reduce* (fn [] :e) nil [(map inc)])]") == [9, 13, 0, kw("e")])
				#expect(try cljEval("[(fused-into* [] (range 3) [(filter odd?)]) (fused-into* () [1 2] []) (fused-count* (range 5) [(take 2)]) (fused-count* nil [])]") == [[1], Value(list: [2, 1]), 2, 0])
				#expect(try cljEval("(fused-reduce* (fn [a x] (reduced [a x])) [1 2 3] [(map inc)])") == [2, 3])
				#expect(try cljEval("(let [seen (volatile! [])] (fused-reduce* + 0 [1 2] [(fn [rf] (fn ([r] (rf r)) ([r x] (vswap! seen conj r) (rf r x))))]) @seen)") == [nil, nil])
				#expect(evalMessage("(fused-count* [1] 5)") == "fused driver expects a vector of transducers, got: long")
				#expect(evalMessage("(fused-reduce* + [1] [inc])") == "fn cannot be cast to a number")
				#expect(evalMessage("(fused-reduce* + [1 :k] [(map inc)])") == "keyword cannot be cast to a number")
				#expect(evalMessage("(fused-into* [] [1] [(map (fn [x] (throw (ex-info \"xf\" {}))))])") == "xf")
				// A transducer that keeps the reducing fn past the walk gets a refusal, never a dead frame.
				#expect(evalMessage("(let [keep (volatile! nil)] (fused-count* [1] [(fn [rf] (vreset! keep rf) rf)]) (@keep nil 1))") == "reducing fn called after its reduce finished")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
