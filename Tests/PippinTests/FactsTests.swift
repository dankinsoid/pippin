// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// The tree, its fact table and a pre-order index of the nodes; releases everything.
private final class Facts {
	let node: UnsafeMutablePointer<clj_node>
	let table: OpaquePointer
	let nodes: [UnsafePointer<clj_node>]

	init(_ source: String) throws {
		let form = try Value(reading: source)
		var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
		guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
		self.node = node
		table = clj_facts_of(node)!
		var all: [UnsafePointer<clj_node>] = []
		Facts.collect(node, &all)
		nodes = all
	}

	static func collect(_ n: UnsafePointer<clj_node>, _ out: inout [UnsafePointer<clj_node>]) {
		out.append(n)
		var children: [UnsafePointer<clj_node>] = []
		withUnsafeMutablePointer(to: &children) { ctx in
			clj_node_children(n, { child, ctx in
				ctx!.assumingMemoryBound(to: [UnsafePointer<clj_node>].self).pointee.append(child!)
			}, ctx)
		}
		for c in children { collect(c, &out) }
	}

	// The n-th node of a kind in pre-order; the root is `nth(node.pointee.kind, 0)` for any tree.
	func id(_ kind: clj_node_kind, _ occurrence: Int) -> UInt32? {
		let matching = nodes.filter { $0.pointee.kind == kind }
		return occurrence < matching.count ? matching[occurrence].pointee.id : nil
	}

	func fact(_ kind: clj_node_kind, _ occurrence: Int = 0) -> String {
		guard let id = id(kind, occurrence), let f = clj_facts_node(table, id) else { return "<missing>" }
		return describe(f.pointee)
	}

	var root: String { describe(clj_facts_node(table, 0)!.pointee) }

	// Escape of a slot of the frame the n-th node of a kind sits in.
	func escape(_ kind: clj_node_kind, _ occurrence: Int, slot: UInt32) -> clj_escape {
		guard let id = id(kind, occurrence) else { return CLJ_ESCAPE_ESCAPES }
		return clj_facts_escape(table, clj_facts_frame_of(table, id), slot)
	}

	var loopVars: [String] {
		var out: [String] = []
		for i in 0..<clj_facts_nloops(table) {
			let l = clj_facts_loop_at(table, i)!.pointee
			for k in 0..<l.n { out.append(describe(clj_facts_loop_var(table, i, k)!.pointee)) }
		}
		return out
	}

	var conflicts: UInt32 { clj_facts_conflicts(table) }
	var widenings: UInt32 { clj_facts_widenings(table) }

	deinit {
		clj_facts_free(table)
		clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
	}
}

// "<kinds>/<nullability>" plus "=<value>" for a singleton and "<elem>" for an array of a known kind.
private func describe(_ f: clj_fact) -> String {
	var text: String
	if f.types == 0 { text = "⊥" } else if f.types == UInt32(CLJ_T_TOP) { text = "⊤" } else {
		var names: [String] = []
		for bit in 0..<27 where f.types & (1 << bit) != 0 { names.append(String(cString: clj_fact_kind_name(1 << bit))) }
		text = names.joined(separator: "|")
	}
	switch clj_null(rawValue: UInt32(f.null)) {
	case CLJ_NULL_NEVER: text += "/never"
	case CLJ_NULL_ALWAYS: text += "/always"
	case CLJ_NULL_MAYBE: text += "/maybe"
	default: text += "/⊥"
	}
	if f.elem != 0 { text += "<" + String(cString: clj_array_kind_name(clj_array_kind(UInt32(f.elem) - 1))) + ">" }
	if f.singleton != CLJ_UNBOUND { text += "=" + Value(borrowing: f.singleton).description }
	return text
}

extension CoreTests {
	// Pass 1 of the facts lattice (facts.c, NOTES.md "Facts").
	@Suite struct FactsTests {
		let rt = Runtime()

		init() {
			for k in ["a", "b", "c", "k", "x", "else", "default", "id", "const", "local", "last", "captured", "var", "the-var", "if",
			          "do", "let", "loop", "recur", "fn", "invoke", "def", "vector", "map", "set", "try", "throw", "all", "error",
			          "intrinsic", "fused", "outer", "direct-fn", "direct-call", "line", "column", "name"] { _ = Value(keyword: k) }
			// a var and its name live for the process, so the def row must not run inside a live-object window
			_ = try? rt.eval("(def facts-def-x 1)")
		}

		// Constants are singletons of their kind; a collection or fn literal is its kind without a value.
		@Test func constantsAndLiterals() throws {
			let before = clj_debug_live_objects()
			do {
				let rows: [(String, String)] = [
					("1", "fixnum/never=1"),
					("nil", "nil/always=nil"),
					("true", "bool/never=true"),
					("\\a", "char/never=\\a"),
					("1.5", "double/never=1.5"),
					("1/2", "ratio/never=1/2"),
					("1N", "bigint/never=1N"),
					("1M", "decimal/never=1M"),
					("\"s\"", "string/never=\"s\""),
					(":k", "keyword/never=:k"),
					("'y", "symbol/never=y"),
					("[1 2]", "vector/never=[1 2]"),
					("{:a 1}", "map/never={:a 1}"),
					("#{1}", "set/never=#{1}"),
					("'(1)", "seq/never=(1)"),
					("#\"a\"", "regex/never=#\"a\""),
					("(fn [] 1)", "fn/never"),
					("(def facts-def-x 1)", "var/never"),
				]
				for (source, expected) in rows { #expect(try Facts(source).root == expected, Comment(rawValue: source)) }
				// a literal with a non-constant element stays a node of its own
				#expect(try Facts("(fn [a] [a])").fact(CLJ_NODE_VECTOR) == "vector/never")
				#expect(try Facts("(fn [a] {:a a})").fact(CLJ_NODE_MAP) == "map/never")
				#expect(try Facts("(fn [a] #{a})").fact(CLJ_NODE_SET) == "set/never")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Intrinsics and annotated C builtins are the only calls that answer anything but ⊤.
		@Test func callSignatures() throws {
			let before = clj_debug_live_objects()
			do {
				let rows: [(String, String)] = [
					("(fn [a] (+ a 1))", "fixnum|long|bigint|ratio|decimal|double/never"),
					("(fn [a b] (+ a b))", "fixnum|long|bigint|ratio|decimal|double/never"),
					("(fn [^long a] (inc a))", "fixnum|long|bigint|ratio|decimal|double/never"),
					("(fn [a] (< a 1))", "bool/never"),
					("(fn [a] (nil? a))", "bool/never"),
					("(fn [a] (count a))", "fixnum/never"),
					("(fn [a] (seq a))", "nil|seq/maybe"),
					("(fn [a] (rest a))", "seq/never"),
					("(fn [a] (str a))", "string/never"),
					("(fn [a] (keys a))", "nil|seq/maybe"),
					("(fn [a] (name a))", "string/never"),
					("(fn [a] (namespace a))", "nil|string/maybe"),
					("(fn [a] (vec a))", "vector/never"),
					("(fn [a] (set a))", "set/never"),
					("(fn [a] (sorted-map a 1))", "sorted-map/never"),
					("(fn [a] (atom a))", "atom/never"),
					("(fn [a] (re-pattern a))", "regex/never"),
					("(fn [n] (int-array n))", "array/never<int>"),
					("(fn [n] (double-array n))", "array/never<double>"),
					("(fn [a] (first a))", "⊤/maybe"),
					("(fn [f a] (f a))", "⊤/maybe"),
					("(fn [a] (map inc a))", "seq/never"),
				]
				for (source, expected) in rows {
					let f = try Facts(source)
					let body = f.nodes.first { $0.pointee.kind != CLJ_NODE_FN }!.pointee.id
					#expect(describe(clj_facts_node(f.table, body)!.pointee) == expected, Comment(rawValue: source))
				}
				// (conj coll x) and (assoc coll k v) answer the collection's own kind, nil included
				#expect(try Facts("(fn [x] (conj [] x))").fact(CLJ_NODE_INTRINSIC) == "vector/never")
				#expect(try Facts("(fn [x] (conj nil x))").fact(CLJ_NODE_INTRINSIC) == "seq/never")
				#expect(try Facts("(fn [x] (assoc nil :a x))").fact(CLJ_NODE_INTRINSIC) == "map/never")
				#expect(try Facts("(fn [m x] (assoc m :a x))").fact(CLJ_NODE_INTRINSIC) == "vector|map|sorted-map|record/never")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// cond/and/or/when/if-let expand to if plus let, so refining those two covers every branching form.
		@Test func refinements() throws {
			let before = clj_debug_live_objects()
			do {
				// the node asserted on is the second read of the parameter, inside the refined branch
				let rows: [(String, String)] = [
					("(fn [x] (if (nil? x) x 1))", "nil/always"),
					("(fn [x] (if (nil? x) 1 x))", "⊤/never"),
					("(fn [x] (if (some? x) x 1))", "⊤/never"),
					("(fn [x] (if (string? x) x 1))", "string/never"),
					("(fn [x] (if (keyword? x) x 1))", "keyword/never"),
					("(fn [x] (if (map? x) x 1))", "map|sorted-map|record/never"),
					("(fn [x] (if (vector? x) x 1))", "vector/never"),
					("(fn [x] (if (number? x) x 1))", "fixnum|long|bigint|ratio|decimal|double/never"),
					("(fn [x] (if (integer? x) x 1))", "fixnum|long|bigint/never"),
					("(fn [x] (if (fn? x) x 1))", "fn/never"),
					("(fn [x] (if (instance? String x) x 1))", "string/never"),
					("(fn [x] (if (= x :id) x 1))", "keyword/never=:id"),
					("(fn [x] (if (not (nil? x)) x 1))", "⊤/never"),
					("(fn [x] (if x x 1))", "⊤/never"),
					("(fn [x] (when (string? x) x))", "string/never"),
					
					("(fn [x] (cond (keyword? x) x :else 1))", "keyword/never"),
					("(fn [x] (if-let [y x] y 1))", "⊤/never"),
					("(fn [x] (when-let [y x] y))", "⊤/never"),
					("(fn [x] (or x 1))", "⊤/never"),
				]
				for (source, expected) in rows {
					let f = try Facts(source)
					let reads = f.nodes.filter { $0.pointee.kind == CLJ_NODE_LOCAL }
					let last = try #require(reads.last)
					#expect(describe(clj_facts_node(f.table, last.pointee.id)!.pointee) == expected,
					        Comment(rawValue: source))
				}
				// nullability is refined where the type cannot be: ⊤ minus nil is 26 kinds, which the cap widens back
				#expect(try Facts("(fn [x] (if (some? x) x 1))").fact(CLJ_NODE_LOCAL, 1) == "⊤/never")
				// a let binding takes the refined fact of its init, which is how if-let and when-let work
				#expect(try Facts("(fn [x] (if (string? x) (let [y x] y) 1))").fact(CLJ_NODE_LOCAL, 2) == "string/never")
				// (and a b) is (let [t a] (if t b t)): the refined read is the third local, the last one is the temp
				#expect(try Facts("(fn [x] (and (string? x) x))").fact(CLJ_NODE_LOCAL, 2) == "string/never")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Four members is the cap, the numeric kinds counting as one; a fifth widens to ⊤.
		@Test func unionCap() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try Facts("(fn [a b c] (if a \"s\" (if b :k 'y)))").root(of: CLJ_NODE_IF) == "string|keyword|symbol/never")
				#expect(try Facts("(fn [a b c] (if a \"s\" (if b :k (if c 'y \\c))))").root(of: CLJ_NODE_IF)
					== "char|string|keyword|symbol/never")
				#expect(try Facts("(fn [a b c d] (if a \"s\" (if b :k (if c 'y (if d \\c [])))))").root(of: CLJ_NODE_IF) == "⊤/never")
				// a number joined with three other kinds is still four members
				#expect(try Facts("(fn [a b c] (if a 1 (if b \"s\" (if c :k 1.5))))").root(of: CLJ_NODE_IF)
					== "fixnum|double|string|keyword/never")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A loop variable is the join of its init and every recur argument, iterated; three rounds, then ⊤.
		@Test func loopsAndWidening() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try Facts("(loop [i 0] (if (< i 10) (recur (inc i)) i))").loopVars == ["fixnum|long/never"])
				#expect(try Facts("(fn [n] (loop [i 0 acc []] (if (< i n) (recur (inc i) (conj acc i)) acc)))").loopVars
					== ["fixnum|long/never", "vector/never"])
				// the init alone is not the answer: the recur argument joins into it
				#expect(try Facts("(fn [xs] (loop [s (seq xs)] (if s (recur (next s)) s)))").loopVars == ["nil|seq/maybe"])
				let rotating = try Facts("(fn [] (loop [a 0 b \"s\" c :k d \\c] (recur d a b c)))")
				#expect(rotating.loopVars == ["⊤/maybe", "⊤/maybe", "⊤/maybe", "⊤/maybe"])
				#expect(rotating.widenings == 4)
				#expect(try Facts("(loop [i 0] (if (< i 10) (recur (inc i)) i))").widenings == 0)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A slot is local until something makes it leave; unknown is escaping.
		@Test func escaping() throws {
			let before = clj_debug_live_objects()
			do {
				let rows: [(String, clj_escape)] = [
					("(fn [] (let [x 1] (+ x 1)))", CLJ_ESCAPE_LOCAL),
					("(fn [] (let [x 1] (count x)))", CLJ_ESCAPE_LOCAL),
					("(fn [] (let [x 1] (if x 1 2)))", CLJ_ESCAPE_LOCAL),
					("(fn [] (let [x 1] x))", CLJ_ESCAPE_ESCAPES),

					("(fn [] (let [x 1] [x] 2))", CLJ_ESCAPE_ESCAPES),
					("(fn [] (let [x 1] (conj [] x) 2))", CLJ_ESCAPE_ESCAPES),
					("(fn [] (let [x 1] (throw x)))", CLJ_ESCAPE_ESCAPES),
					("(fn [] (let [x 1] (fn [] x)))", CLJ_ESCAPE_CAPTURED),
				]
				for (source, expected) in rows {
					let f = try Facts(source)
					#expect(f.escape(CLJ_NODE_LET, 0, slot: 0) == expected, Comment(rawValue: source))
				}
				// slot 1: the parameter takes slot 0, so the let's binding is the next one
				#expect(try Facts("(fn [f] (let [x 1] (f x) 2))").escape(CLJ_NODE_LET, 0, slot: 1) == CLJ_ESCAPE_ESCAPES)
				// an alias carries the escape back: y leaves, so x does too
				let aliased = try Facts("(fn [] (let [x 1 y x] y))")
				#expect(aliased.escape(CLJ_NODE_LET, 0, slot: 0) == CLJ_ESCAPE_ESCAPES)
				// a direct fn reads the defining frame's slot through the static link: read by address, so captured
				let outer = try Facts("(fn [] (let [x 1 f (fn [] (+ x 1))] (f)))")
				#expect(outer.id(CLJ_NODE_DIRECT_FN, 0) != nil && outer.id(CLJ_NODE_OUTER, 0) != nil)
				#expect(outer.escape(CLJ_NODE_LET, 0, slot: 0) == CLJ_ESCAPE_CAPTURED)
				// two links up, and a closure made inside the direct fn body capturing the slot
				let deep = try Facts("(fn [] (let [x 1 f (fn [] (let [g (fn [] (+ x 1))] (g)))] (f)))")
				#expect(deep.escape(CLJ_NODE_LET, 0, slot: 0) == CLJ_ESCAPE_CAPTURED)
				let closed = try Facts("(fn [] (let [x 1 f (fn [] (fn [] x))] (f)))")
				#expect(closed.escape(CLJ_NODE_LET, 0, slot: 0) == CLJ_ESCAPE_CAPTURED)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// try joins its body with every handler, and a handler sees the environment the try was entered with.
		@Test func tryAndThrow() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try Facts("(try 1 (catch :default e :k))").root == "fixnum|keyword/never")
				#expect(try Facts("(try 1 (finally 2))").root == "fixnum/never=1")
				// the caught value is any value, and a throw produces none
				#expect(try Facts("(try 1 (catch :default e e))").fact(CLJ_NODE_LOCAL) == "⊤/maybe")
				#expect(try Facts("(fn [] (throw 1))").fact(CLJ_NODE_THROW) == "⊥/⊥")
				#expect(try Facts("(fn [x] (if x 1 (throw x)))").fact(CLJ_NODE_IF) == "fixnum/never=1")
				// a contradicting refinement marks the branch dead, by the pinned literal or by the predicates alone
				let dead = try Facts("(let [x [1]] (if (nil? x) (count x) 1))")
				#expect(dead.conflicts == 1)
				#expect(dead.fact(CLJ_NODE_INTRINSIC, 1) == "fixnum/never")
				#expect(clj_facts_node(dead.table, dead.id(CLJ_NODE_INTRINSIC, 1)!)!.pointee.unreachable == UInt8(CLJ_DEAD_LITERAL.rawValue))
				let refined = try Facts("(fn [x] (if (string? x) (if (number? x) (inc x) 1) 2))")
				#expect(refined.conflicts == 1)
				#expect(clj_facts_node(refined.table, refined.id(CLJ_NODE_INTRINSIC, 2)!)!.pointee.unreachable == UInt8(CLJ_DEAD_REFINED.rawValue))
				let pinned = try Facts("(fn [x] (if (= x 1) (if (string? x) (count x) 1) 2))")
				#expect(pinned.conflicts == 1)
				#expect(clj_facts_node(pinned.table, pinned.id(CLJ_NODE_INTRINSIC, 2)!)!.pointee.unreachable == UInt8(CLJ_DEAD_LITERAL.rawValue))
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A closure captures the fact of the slot at the moment it is made; a parameter and a var are ⊤.
		@Test func capturesAndVars() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try Facts("(let [x \"s\"] (fn [] x))").fact(CLJ_NODE_CAPTURED) == "string/never=\"s\"")
				#expect(try Facts("(fn [a] (fn [] a))").fact(CLJ_NODE_CAPTURED) == "⊤/maybe")
				#expect(try Facts("(fn [] map)").fact(CLJ_NODE_VAR) == "⊤/maybe")
				// a variadic rest parameter is the seq of the extra arguments, or nil
				#expect(try Facts("(fn [& more] more)").fact(CLJ_NODE_LOCAL) == "nil|seq/maybe")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The table is a side table: the tree is untouched and two runs over the same tree agree.
		@Test func pureAndSideTableOnly() throws {
			let before = clj_debug_live_objects()
			do {
				let source = "(fn [x] (let [y (if (string? x) x \"\")] (count y)))"
				let form = try Value(reading: source)
				var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
				let node = try #require(withExtendedLifetime(form) { clj_analyze(form.raw, &env) })
				let firstData = Value(owning: clj_node_to_data(node))
				let a = clj_facts_of(node)!
				let secondData = Value(owning: clj_node_to_data(node))
				let b = clj_facts_of(node)!
				#expect(firstData == secondData)
				#expect(clj_facts_nnodes(a) == clj_facts_nnodes(b))
				for id in 0..<clj_facts_nnodes(a) {
					#expect(clj_fact_eq(clj_facts_node(a, id)!.pointee, clj_facts_node(b, id)!.pointee), Comment(rawValue: "node \(id)"))
				}
				clj_facts_free(a)
				clj_facts_free(b)
				clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A refinement never contradicts, and a value node is ⊥ only where a throw or recur is the only way out.
		@Test func noContradictionOverCore() throws {
			let root = URL(fileURLWithPath: #filePath)
				.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
				.appendingPathComponent("Sources/CljCore/boot")
			var sources: [(URL, clj_value)] = [(root.appendingPathComponent("core.clj"), clj_ns_core())]
			for lib in ["set", "string", "walk", "template", "test"] {
				let sym = Value(symbol: "clojure.\(lib)")
				_ = try rt.eval("(require 'clojure.\(lib))")
				sources.append((root.appendingPathComponent("clojure/\(lib).clj"), withExtendedLifetime(sym) { clj_ns_find(sym.raw) }))
			}
			var forms = 0, conflicts = 0, bottoms = 0
			for (url, ns) in sources {
				let previous = clj_ns_current()
				clj_ns_set_current(ns)
				defer { clj_ns_set_current(previous) }
				var env = clj_env(ns: ns, line: 0, col: 0)
				for form in try Value.readAll(try String(contentsOf: url, encoding: .utf8)) {
					guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else {
						clj_release(clj_take_pending())
						continue
					}
					forms += 1
					let table = clj_facts_of(node)!
					if clj_facts_conflicts(table) > 0 {
						conflicts += Int(clj_facts_conflicts(table))
						Issue.record(Comment(rawValue: "\(url.lastPathComponent): refinement conflict at node \(clj_facts_conflict_node(table))"))
					}
					var all: [UnsafePointer<clj_node>] = []
					Facts.collect(node, &all)
					for n in all where clj_facts_value_node(n.pointee.kind) {
						let fact = clj_facts_node(table, n.pointee.id)!.pointee
						guard fact.types == 0, fact.unreachable == 0 else { continue }
						var sub: [UnsafePointer<clj_node>] = []
						Facts.collect(n, &sub)
						if !sub.contains(where: { $0.pointee.kind == CLJ_NODE_THROW || $0.pointee.kind == CLJ_NODE_RECUR }) {
							bottoms += 1
							Issue.record(Comment(rawValue: "\(url.lastPathComponent):\(n.pointee.line) ⊥ without a throw"))
						}
					}
					clj_facts_free(table)
					clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
				}
			}
			#expect(forms > 300)
			#expect(conflicts == 0)
			#expect(bottoms == 0)
		}
	}
}

private extension Facts {
	// The outermost node of a kind: what a form's value is when the root is a wrapper.
	func root(of kind: clj_node_kind) -> String { fact(kind, 0) }
}
