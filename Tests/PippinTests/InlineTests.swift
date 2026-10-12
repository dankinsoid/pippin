// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

// The kinds of a tree analyzed in `user`, in pre-order, and its codec data.
private func analyzed(_ source: String) throws -> ([clj_node_kind], String) {
	let form = try Value(reading: source)
	var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
	guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
	defer { clj_release(clj_from_ptr(node)) }
	var kinds: [clj_node_kind] = []
	func walk(_ n: UnsafePointer<clj_node>) {
		kinds.append(n.pointee.kind)
		var children: [UnsafePointer<clj_node>] = []
		withUnsafeMutablePointer(to: &children) { ctx in
			clj_node_children(n, { child, ctx in ctx!.assumingMemoryBound(to: [UnsafePointer<clj_node>].self).pointee.append(child!) }, ctx)
		}
		for c in children { walk(c) }
	}
	walk(node)
	let data = Value(owning: clj_node_to_data(node))
	return (kinds, data.description)
}

// The kinds of the INLINE nodes and the guard counts of the ROOT_IS nodes of a tree analyzed in `user`, in pre-order.
private func inlines(_ source: String) throws -> (hows: [clj_inline_how], guards: [UInt32]) {
	let form = try Value(reading: source)
	var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
	guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, &env) }) else { throw ClojureError.takePending() }
	defer { clj_release(clj_from_ptr(node)) }
	var hows: [clj_inline_how] = []
	var guards: [UInt32] = []
	func walk(_ n: UnsafePointer<clj_node>) {
		if n.pointee.kind == CLJ_NODE_INLINE { hows.append(clj_inline_how(UInt32(n.pointee.u.inline_.how))) }
		if n.pointee.kind == CLJ_NODE_ROOT_IS { guards.append(n.pointee.u.root_is.nguards) }
		var children: [UnsafePointer<clj_node>] = []
		withUnsafeMutablePointer(to: &children) { ctx in
			clj_node_children(n, { child, ctx in ctx!.assumingMemoryBound(to: [UnsafePointer<clj_node>].self).pointee.append(child!) }, ctx)
		}
		for c in children { walk(c) }
	}
	walk(node)
	return (hows, guards)
}

// A compiled core's fns are no closures: nothing of theirs is copied or stood for.
func coreRunsInterpreted() -> Bool {
	let sym = Value(symbol: "assoc-in")
	let root = withExtendedLifetime(sym) { clj_var_root(clj_ns_resolve(clj_ns_core(), sym.raw)) }
	return clj_is_fn(root) && clj_fn_of(root).pointee.kind == CLJ_FN_CLOSURE
}

extension CoreTests {
	// Design §10 step 7a, optimizer.c: a small pure closure's arity body copied into its caller behind a guard on the root.
	@Suite struct InlineTests {
		let rt = Runtime()

		@Test func aSmallPureCalleeIsInlinedAndItsCallKept() throws {
			_ = try rt.eval("(defn il-sq [x] (* x x)) (defn il-use [y] (il-sq (inc y)))")
			#expect(try rt.eval("(il-use 2)") == 9)
			let (kinds, data) = try analyzed("(fn [y] (il-sq (inc y)))")
			#expect(kinds.contains(CLJ_NODE_ROOT_IS) && kinds.contains(CLJ_NODE_INLINE))
			// the codec writes the call the guard falls back to, never the copy
			#expect(!data.contains("root-is") && data.contains("[:invoke [:var user/il-sq"))
			#expect(try rt.eval("(with-redefs [il-sq (fn [x] (- x))] (il-use 2))") == -3)
			_ = try rt.eval("(defn il-sq [x] (+ x x))")
			#expect(try rt.eval("(il-use 2)") == 6)
		}

		// Effects past alloc and throw, recursion, a closure in the body, a dynamic var, a top-level call: the call stays.
		@Test func whatIsNotInlined() throws {
			_ = try rt.eval("""
			(defn il-loud [x] (println x) x)
			(defn il-rec [n] (if (pos? n) (il-rec (dec n)) n))
			(defn il-maker [x] (fn [] x))
			(defn ^:dynamic il-dyn [x] x)
			(defn il-small [x] (inc x))
			""")
			for call in ["(il-loud 1)", "(il-rec 3)", "(il-maker 1)", "(il-dyn 1)"] {
				#expect(!(try analyzed("(fn [] \(call))").0.contains(CLJ_NODE_INLINE)), Comment(rawValue: call))
			}
			#expect(!(try analyzed("(il-small 1)").0.contains(CLJ_NODE_INLINE)))
			#expect(try analyzed("(fn [] (il-small 1))").0.contains(CLJ_NODE_INLINE))
			let was = clj_inline_set_enabled(false)
			defer { _ = clj_inline_set_enabled(was) }
			#expect(!(try analyzed("(fn [] (il-small 1))").0.contains(CLJ_NODE_INLINE)))
		}

		// The callee's frame in a trace, at the call's position, and the arguments once each, in order.
		@Test func traceAndArguments() throws {
			_ = try rt.eval("""
			(defn il-boom [x] (if (pos? x) (throw (ex-info "boom" {})) x))
			(defn il-call [x] (il-boom x))
			(defn il-pair [a b] [a b])
			""")
			#expect(try rt.eval("(try (il-call 1) (catch :default e (subvec (mapv :fn (ex-trace e)) 0 2)))") == Value(reading: "[user/il-boom user/il-call]"))
			#expect(try rt.eval("((fn [] (let [log (atom [])] [(il-pair (do (swap! log conj 1) 1) (do (swap! log conj 2) 2)) @log])))") == Value(reading: "[[1 2] [1 2]]"))
		}

		// A callee calling its parameter: a literal fn's body where the call was, an unknown value still called, a var of
		// an intrinsic read at its one call; an impure literal is called as a direct fn.
		@Test func theCallsOfAParameter() throws {
			_ = try rt.eval("(defn il-app [f x] (f x)) (defn il-loud [f x] (println x) (f x))")
			let lit = try inlines("(fn [y] (il-app (fn [x] (+ x y)) 1))")
			#expect(lit.hows.contains(CLJ_INLINE_COPY) && lit.hows.contains(CLJ_INLINE_LITERAL))
			#expect(try rt.eval("((fn [y] (il-app (fn [x] (+ x y)) 1)) 2)") == 3)
			let unknown = try inlines("(fn [g y] (il-app g y))")
			#expect(unknown.hows == [CLJ_INLINE_COPY])
			#expect(try rt.eval("((fn [g y] (il-app g y)) inc 2)") == 3)
			#expect(try inlines("(fn [y] (il-app inc y))").hows == [CLJ_INLINE_COPY])
			let impure = try inlines("(fn [y] (il-app (fn [x] (println x) x) y))")
			#expect(impure.hows == [CLJ_INLINE_COPY])
			// the callee's own code is impure: no copy
			#expect(try inlines("(fn [y] (il-loud (fn [x] x) y))").hows.isEmpty)
			// a literal of another arity than the call passes keeps the call
			#expect(try inlines("(fn [y] (il-app (fn [a b] a) y))").hows == [CLJ_INLINE_COPY])
		}

		// get-in, assoc-in and update-in over a literal path: one guard over the var and the core vars it stands for,
		// a level per frame of core's recursion; a path past four keys or not a vector literal stays a call.
		@Test func aKnownPathUnrolls() throws {
			#expect(try rt.eval("((fn [m k] (update-in m [:a k] (fn [x] (inc x)))) {:a {:b 1}} :b)") == Value(reading: "{:a {:b 2}}"))
			#expect(try rt.eval("((fn [m] (get-in m [:r :s])) {:r (reduced {:s 9})})") == Value(reading: "{:s 9}"))
			guard coreRunsInterpreted() else {
				#expect(try inlines("(fn [m k] (assoc-in m [:a k :c] 1))").hows.isEmpty)
				return
			}
			let assoc = try inlines("(fn [m k] (assoc-in m [:a k :c] 1))")
			#expect(assoc.guards == [4] && assoc.hows == [CLJ_INLINE_PATH, CLJ_INLINE_LEVEL, CLJ_INLINE_LEVEL])
			let update = try inlines("(fn [m k] (update-in m [:a k] (fn [x] (inc x))))")
			#expect(update.guards == [5] && update.hows == [CLJ_INLINE_PATH, CLJ_INLINE_LEVEL, CLJ_INLINE_LEVEL, CLJ_INLINE_LITERAL])
			#expect(try inlines("(fn [m] (get-in m [:a :b] 0))").hows == [CLJ_INLINE_PATH])
			// get-in's own body is small and pure: copied as any callee is
			#expect(try inlines("(fn [m] (get-in m [:a :b :c :d :e]))").hows == [CLJ_INLINE_COPY])
			#expect(try inlines("(fn [m ks] (get-in m ks))").hows == [CLJ_INLINE_COPY])
			#expect(try inlines("(fn [m] (assoc-in m [:a :b :c :d :e] 1))").hows.isEmpty)
		}
	}
}
