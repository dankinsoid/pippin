// @ai-generated(guided)
import CljCore
import Testing
@testable import Clojure

private func kw(_ s: String) -> Value { Value(keyword: s) }

// A map with keyword keys: m(["a": 1]) is {:a 1}.
private func m(_ entries: [String: Value]) -> Value {
	Value(Dictionary(uniqueKeysWithValues: entries.map { (kw($0.key), $0.value) }))
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

private func message(_ rt: Runtime, _ source: String) -> String? { clojureError(rt, source)?.message }

extension CoreTests {
	@Suite struct MetaTests {
		let rt = Runtime()

		init() {
			for k in ["a", "b", "c", "tag", "line", "column", "doc", "arglists", "private", "dynamic", "macro", "ns", "name",
			          "meta", "withMeta", "m", "x", "k", "v", "count", "seq", "first", "next", "home"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func metaOnEverySupportedType() throws {
			let before = clj_debug_live_objects()
			do {
				for form in ["'x", "'ns/x", "[1 2]", "{:a 1}", "(list 1 2)", "()", "(fn [x] x)", "+"] {
					#expect(try rt.eval("(meta \(form))") == nil, "\(form)")
					#expect(try rt.eval("(meta (with-meta \(form) {:a 1}))") == m(["a": 1]), "\(form)")
					#expect(try rt.eval("(meta (with-meta (with-meta \(form) {:a 1}) nil))") == nil, "\(form)")
					// A fn compares by identity, and a with-meta copy of a shared fn is a new object, as in Clojure.
					let identity = form.hasPrefix("(fn") || form == "+"
					#expect(try rt.eval("(let [x \(form)] (= x (with-meta x {:a 1})))") == Value(!identity), "\(form)")
					// x is still read after the with-meta, so the copy is real; a last-use x would be re-labelled in place.
					#expect(try rt.eval("(let [x \(form) y (with-meta x {:a 1})] (= (hash x) (hash y)))") == Value(!identity), "\(form)")
					#expect(try rt.eval("(let [x \(form)] (= (pr-str (with-meta x {:a 1})) (pr-str x)))") == true, "\(form)")
				}
				// Collection operations keep the meta of the root they rebuild.
				#expect(try rt.eval("(meta (conj (with-meta [1] {:a 1}) 2))") == m(["a": 1]))
				#expect(try rt.eval("(meta (assoc (with-meta {} {:a 1}) :b 2))") == m(["a": 1]))
				#expect(try rt.eval("(meta (dissoc (with-meta {:b 2} {:a 1}) :b))") == m(["a": 1]))
				#expect(try rt.eval("(meta (assoc (with-meta [1] {:a 1}) 0 2))") == m(["a": 1]))
				#expect(try rt.eval("(let [v (with-meta [1 2] {:a 1}) w (conj v 3)] [(meta v) (meta w) v w])") == [m(["a": 1]), m(["a": 1]), [1, 2], [1, 2, 3]])
				// A fn copy still runs, with the same arities, and is a different object.
				#expect(try rt.eval("(let [f (fn [x] (* 2 x)) g (with-meta f {:a 1})] [(g 4) (meta g) (= f g)])") == [8, m(["a": 1]), false])
				#expect(try rt.eval("(let [g (with-meta + {:a 1})] [(g 1 2 3) (meta g) (meta +)])") == [6, m(["a": 1]), nil])
				#expect(try rt.eval("(let [g (with-meta first {:a 1})] (g [7]))") == 7)
				// A protocol method fn borrows its dispatch context; the copy dispatches like the original.
				#expect(try rt.eval("(let [g (with-meta count {:a 1})] (g [1 2 3]))") == 3)
				// Meta on immediates, strings and keywords reads nil.
				for form in ["nil", "1", "1.5", "\\a", "true", "\"s\"", ":k"] {
					#expect(try rt.eval("(meta \(form))") == nil, "\(form)")
				}
				#expect(message(rt, "(with-meta \"s\" {})") == "with-meta: string does not support metadata")
				#expect(message(rt, "(with-meta :k {})") == "with-meta: keyword does not support metadata")
				#expect(message(rt, "(with-meta 1 {})") == "with-meta: long does not support metadata")
				#expect(message(rt, "(with-meta nil {})") == "with-meta: nil does not support metadata")
				#expect(message(rt, "(with-meta (seq [1]) {})") == "with-meta: vector-seq does not support metadata")
				#expect(message(rt, "(with-meta (range 3) {})") == "with-meta: range does not support metadata")
				#expect(message(rt, "(with-meta (lazy-seq [1]) {})") == "with-meta: lazy-seq does not support metadata")
				#expect(message(rt, "(with-meta #'inc {})") == "with-meta: var does not support metadata")
				#expect(message(rt, "(with-meta [] 1)") == "with-meta: metadata must be a map, got: long")
				#expect(message(rt, "(with-meta [] [])") == "with-meta: metadata must be a map, got: vector")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func withMetaReusesUniqueRootsAndSharesChildren() throws {
			let before = clj_debug_live_objects()
			do {
				// Unique: the same object, meta set in place. Shared (held by v): a copy, the original untouched.
				#expect(try rt.eval("(let [v [1 2] w (with-meta v {:a 1})] [(meta v) (meta w) (identical? v w)])") == [nil, m(["a": 1]), false])
				#expect(try rt.eval("(let [v (with-meta [1 2] {:a 1}) w (with-meta v {:b 2})] [(meta v) (meta w)])") == [m(["a": 1]), m(["b": 2])])
				// A plain cons stays 32 bytes; only the with-meta'd cell carries the slot.
				#expect(clj_debug_cell_size(MemoryLayout<clj_cons>.size) == 32 || !clj_debug_pool_enabled())
				let plain = Value(list: [1, 2])
				#expect(clj_header_of(plain.raw).pointee.flags & UInt32(CLJ_FLAG_META) == 0)
				let tagged = try rt.eval("(with-meta '(1 2) {:a 1})")
				#expect(clj_header_of(tagged.raw).pointee.flags & UInt32(CLJ_FLAG_META) != 0)
				#expect(tagged == plain)
				#expect(tagged.list == [1, 2])
				#expect(try rt.eval("(meta (rest (with-meta '(1 2 3) {:a 1})))") == nil)
				let empty = try rt.eval("(with-meta () {:a 1})")
				#expect(empty.description == "()")
				#expect(empty.meta == m(["a": 1]))
				#expect(clj_is_empty_list(empty.raw))
				#expect(try rt.eval("(with-meta () nil)").raw == clj_list_empty())
				// Sharing a with-meta'd value shares its meta; the shared root copies on with-meta.
				let shared = try rt.eval("(with-meta [1] {:a [1]})")
				#expect(clj_debug_all_shared(shared.raw))
				#expect(try shared.withMeta(m(["b": 2])).meta == m(["b": 2]))
				#expect(shared.meta == m(["a": [1]]))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func varMetaAltersAtomically() throws {
			try declare("mt-v")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(reset-meta! #'mt-v nil)")
				#expect(try rt.eval("(meta #'mt-v)") == nil)
				#expect(try rt.eval("(reset-meta! #'mt-v {:a 1})") == m(["a": 1]))
				#expect(try rt.eval("(meta #'mt-v)") == m(["a": 1]))
				#expect(try rt.eval("(alter-meta! #'mt-v assoc :b 2)") == m(["a": 1, "b": 2]))
				#expect(try rt.eval("(alter-meta! #'mt-v (fn [m] (dissoc m :a)))") == m(["b": 2]))
				#expect(try rt.eval("(alter-meta! #'mt-v (fn [m] nil))") == nil)
				#expect(try rt.eval("(meta #'mt-v)") == nil)
				#expect(message(rt, "(alter-meta! #'mt-v (fn [m] 1))") == "alter-meta! fn must return a map, got: long")
				#expect(message(rt, "(alter-meta! #'mt-v (fn [m] (throw (ex-info \"boom\" {}))))") == "boom")
				#expect(message(rt, "(alter-meta! [] assoc :a 1)") == "alter-meta! expects a var or an atom, got: vector")
				#expect(message(rt, "(reset-meta! [] {})") == "reset-meta! expects a var or an atom, got: vector")
				#expect(message(rt, "(reset-meta! #'mt-v 1)") == "reset-meta! expects a map, got: long")
				#expect(try rt.eval("(meta #'mt-v)") == nil)
				let vector = try rt.eval("(reset-meta! #'mt-v {:a [1 2]})")
				#expect(clj_debug_all_shared(vector.raw))
				try declare("mt-v")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func deftypeImplementsIMetaAndIObj() throws {
			try declare("MetaBox", "->MetaBox", "OnlyMeta", "->OnlyMeta", "BadMeta", "->BadMeta", "meta-site")
			// A reify site's type is made on its first evaluation and lives for the process, like a var.
			_ = try rt.eval("(defn meta-site [m] (reify IMeta (meta [_] m))) (meta-site nil)")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("""
				(deftype MetaBox [x m]
				  IObj
				  (meta [_] m)
				  (withMeta [_ m2] (->MetaBox x m2)))
				(deftype OnlyMeta [m] IMeta (meta [_] m))
				(deftype BadMeta [] IMeta (meta [_] 1))
				""")
				#expect(try rt.eval("(meta (->MetaBox 1 {:a 1}))") == m(["a": 1]))
				#expect(try rt.eval("(meta (->MetaBox 1 nil))") == nil)
				#expect(try rt.eval("(let [b (with-meta (->MetaBox 1 nil) {:b 2})] [(field* b 0) (meta b)])") == [1, m(["b": 2])])
				#expect(try rt.eval("(meta (vary-meta (->MetaBox 1 {:a 1}) assoc :b 2))") == m(["a": 1, "b": 2]))
				#expect(try rt.eval("[(satisfies? IObj (->MetaBox 1 nil)) (satisfies? IMeta (->MetaBox 1 nil)) (satisfies? IObj (->OnlyMeta nil)) (satisfies? IMeta (->OnlyMeta {}))]") == [true, true, false, true])
				#expect(try rt.eval("[(satisfies? IMeta [1]) (satisfies? IObj 'x) (satisfies? IObj #'inc) (satisfies? IMeta #'inc) (satisfies? IMeta :k)]") == [true, true, false, true, false])
				#expect(try rt.eval("(meta (->OnlyMeta {:a 1}))") == m(["a": 1]))
				#expect(message(rt, "(with-meta (->OnlyMeta {}) {})") == "with-meta: user.OnlyMeta does not support metadata")
				#expect(message(rt, "(meta (->BadMeta))") == "meta of user.BadMeta must return a map or nil, got: long")
				#expect(message(rt, "(deftype BadMeta [] IMeta (withMeta [_ m] m))") == "No method :withMeta in interface IMeta")
				#expect(try rt.eval("(meta (meta-site {:a 1}))") == m(["a": 1]))
				try unbind("MetaBox", "->MetaBox", "OnlyMeta", "->OnlyMeta", "BadMeta", "->BadMeta")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The def form's position and the symbol's meta land on the var; :ns is the namespace's symbol here.
		@Test func defPutsMetaOnTheVar() throws {
			try declare("mt-plain", "mt-tagged", "mt-dyn", "mt-eval", "mt-line")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(def mt-plain 1) (meta #'mt-plain)") == Value(reading: "{:ns user :name mt-plain :line 1 :column 1}"))
				#expect(try rt.eval("(def ^{:doc \"d\" :tag String} mt-tagged 1) (meta #'mt-tagged)") ==
					rt.eval("{:ns 'user :name 'mt-tagged :line 1 :column 1 :doc \"d\" :tag String}"))
				#expect(try rt.eval("(:tag (meta #'mt-tagged))") == rt.eval("String"))
				#expect(try rt.eval("(def ^:dynamic mt-dyn 1) (:dynamic (meta #'mt-dyn))") == true)
				let dyn = try rt.eval("#'mt-dyn")
				#expect(clj_var_is_dynamic(dyn.raw))
				#expect(try rt.eval("(def mt-dyn 2) (:dynamic (meta #'mt-dyn))") == nil)
				let plainAgain = try rt.eval("#'mt-dyn")
				#expect(!clj_var_is_dynamic(plainAgain.raw))
				// Meta values are expressions, as in Clojure; :ns, :name, :line and :column always win.
				#expect(try rt.eval("(def ^{:k (+ 1 2) :ns 'other :line 99} mt-eval 1) (let [m (meta #'mt-eval)] [(:k m) (:ns m) (:line m)])") == [3, Value(symbol: "user"), 1])
				#expect(try rt.eval("\n\n   (def mt-line 1) (meta #'mt-line)") == Value(reading: "{:ns user :name mt-line :line 3 :column 4}"))
				#expect(try rt.eval("(def mt-line) (meta #'mt-line)") == Value(reading: "{:ns user :name mt-line :line 1 :column 1}"))
				#expect(message(rt, "(def ^{:k (nope)} mt-eval 1)") == "Unable to resolve symbol: nope in this context")
				// A redefinition replaces the meta, as Clojure's setMeta does.
				#expect(try rt.eval("(def mt-tagged 2) (:doc (meta #'mt-tagged))") == nil)
				#expect(try rt.eval("(alter-meta! #'mt-tagged assoc :x 1) (def mt-tagged 3) (:x (meta #'mt-tagged))") == nil)
				#expect(try rt.eval("(meta (var mt-plain))") == Value(reading: "{:ns user :name mt-plain :line 1 :column 1}"))
				try unbind("mt-plain", "mt-tagged", "mt-dyn", "mt-eval", "mt-line")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func defnAndDefmacroVarMeta() throws {
			try declare("mt-f", "mt-g", "mt-h", "mt-m", "mt-n", "mt-p", "mt-attr")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defn mt-f \"Adds.\" [a b] (+ a b))")
				let m = try rt.eval("(meta #'mt-f)")
				#expect(m.dictionary?[kw("doc")] == "Adds.")
				#expect(try m.dictionary?[kw("arglists")] == Value(reading: "([a b])"))
				#expect(m.dictionary?[kw("name")] == Value(symbol: "mt-f"))
				#expect(m.dictionary?[kw("ns")] == Value(symbol: "user"))
				#expect(m.dictionary?[kw("line")] == 1)
				#expect(m.dictionary?[kw("column")] == 1)
				#expect(m.dictionary?[kw("private")] == nil)
				#expect(try rt.eval("(mt-f 1 2)") == 3)
				#expect(try rt.eval("(defn mt-g ([] 0) ([x] x) ([x & r] r)) (:arglists (meta #'mt-g))") == Value(reading: "([] [x] [x & r])"))
				#expect(try rt.eval("(:doc (meta #'mt-g))") == nil)
				// Attr-maps before the params and after the arities merge into the meta; the later ones win.
				// In the single-arity form a trailing map is a body form, as in Clojure.
				#expect(try rt.eval("(defn mt-attr \"d\" {:a 1 :doc \"d2\"} ([x] x) {:b 2 :a 3}) (let [m (meta #'mt-attr)] [(:doc m) (:a m) (:b m)])") == ["d2", 3, 2])
				#expect(try rt.eval("(mt-attr 4)") == 4)
				#expect(try rt.eval("(defn mt-attr [x] x {:b 2}) [(mt-attr 4) (:b (meta #'mt-attr))]") == Value(reading: "[{:b 2} nil]"))
				#expect(try rt.eval("(defn ^:private mt-h [] 1) (:private (meta #'mt-h))") == true)
				#expect(try rt.eval("(defn- mt-p \"priv\" [] 2) [(:private (meta #'mt-p)) (:doc (meta #'mt-p)) (mt-p)]") == [true, "priv", 2])
				#expect(try rt.eval("(str mt-f)") == "#object[fn user/mt-f]")
				// defmacro: :macro true, the docstring, an attr-map and the arglists as written (no &form/&env).
				_ = try rt.eval("(defmacro mt-m \"Twice.\" {:a 1} [x] `(+ ~x ~x))")
				let mm = try rt.eval("(meta #'mt-m)")
				#expect(mm.dictionary?[kw("macro")] == true)
				#expect(mm.dictionary?[kw("doc")] == "Twice.")
				#expect(mm.dictionary?[kw("a")] == 1)
				#expect(try mm.dictionary?[kw("arglists")] == Value(reading: "([x])"))
				#expect(try rt.eval("(mt-m 3)") == 6)
				#expect(try rt.eval("(defmacro mt-n ([] 1) ([x] x)) (:arglists (meta #'mt-n))") == Value(reading: "([] [x])"))
				#expect(try rt.eval("[(mt-n) (mt-n 2) (:macro (meta #'mt-n))]") == [1, 2, true])
				let macroVar = try rt.eval("#'mt-n")
				#expect(clj_var_is_macro(macroVar.raw))
				#expect(try rt.eval("(:macro (meta #'when))") == true)
				#expect(try rt.eval("(:arglists (meta #'when))") == Value(reading: "([test & body])"))
				#expect(try rt.eval("(:doc (meta #'map))") == "Returns a lazy seq of f applied to the items of the colls in parallel, ending\n  with the shortest; with f alone, the transducer of the same.")
				#expect(try rt.eval("(:arglists (meta #'map))") == Value(reading: "([f] [f coll] [f c1 c2] [f c1 c2 c3] [f c1 c2 c3 & colls])"))
				#expect(try rt.eval("(:private (meta #'clojure.core/maybe-destructured))") == true)
				#expect(try rt.eval("(:line (meta #'clojure.core/defn))").int ?? 0 > 0)
				#expect(message(rt, "(defn 1 [] 1)") == "First argument to defn must be a symbol")
				#expect(message(rt, "(defmacro mt-n)") == "Parameter declaration missing")
				#expect(message(rt, "(defmacro mt-n \"doc\" {:a 1})") == "Parameter declaration missing")
				try unbind("mt-f", "mt-g", "mt-h", "mt-m", "mt-n", "mt-p", "mt-attr")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func privateVarsResolveOnlyAtHome() throws {
			try declare("mt-priv", "mt-use")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defn- mt-priv [] :home)")
				#expect(try rt.eval("(mt-priv)") == kw("home"))
				#expect(try rt.eval("(user/mt-priv)") == kw("home"))
				// clojure.core's private helpers do not fall through into user; a qualified reference is refused; var still works.
				#expect(message(rt, "(maybe-destructured [] [])") == "Unable to resolve symbol: maybe-destructured in this context")
				#expect(message(rt, "(clojure.core/maybe-destructured [] [])") == "var: clojure.core/maybe-destructured is not public")
				#expect(message(rt, "clojure.core/check-bindings") == "var: clojure.core/check-bindings is not public")
				#expect(try rt.eval("#'clojure.core/maybe-destructured").description == "#'clojure.core/maybe-destructured")
				#expect(try rt.eval("((var clojure.core/maybe-destructured) '[a] '(a))") == Value(reading: "([a] a)"))
				#expect(try rt.eval("(resolve 'maybe-destructured)") == nil)
				#expect(try rt.eval("(resolve 'clojure.core/maybe-destructured)").description == "#'clojure.core/maybe-destructured")
				// A public fn of a private var's namespace may still call it, and the doc macro reaches its private printer.
				#expect(try rt.eval("(defn mt-use [] (mt-priv)) (mt-use)") == kw("home"))
				#expect(try rt.eval("(let [[a b] [1 2]] (+ a b))") == 3)
				try unbind("mt-priv", "mt-use")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func docPrintsLikeClojure() throws {
			try declare("mt-doc", "mt-nodoc")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defn mt-doc \"Adds one.\" ([x] (inc x)) ([x y] (+ x y)))")
				let out = try capturingOutput { _ = try rt.eval("(doc mt-doc)") }
				#expect(out == "-------------------------\nuser/mt-doc\n([x] [x y])\n  Adds one.\n")
				let macro = try capturingOutput { _ = try rt.eval("(doc when)") }
				#expect(macro == "-------------------------\nclojure.core/when\n([test & body])\nMacro\n  Evaluates body in an implicit do when test is logical true, else nil.\n")
				let plain = try capturingOutput { _ = try rt.eval("(def mt-nodoc 1) (doc mt-nodoc)") }
				#expect(plain == "-------------------------\nuser/mt-nodoc\n")
				#expect(message(rt, "(doc mt-never-defined)") == "Unable to resolve var: mt-never-defined in this context")
				try unbind("mt-doc", "mt-nodoc")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func defprotocolDocstrings() throws {
			try declare("MtProto", "mt-pm", "mt-pn")
			let before = clj_debug_live_objects()
			do {
				_ = try rt.eval("(defprotocol MtProto \"A protocol.\" (mt-pm [this] [this a] \"Method doc.\") (mt-pn [this]))")
				#expect(try rt.eval("(:doc (meta #'MtProto))") == "A protocol.")
				#expect(try rt.eval("(:doc (meta #'mt-pm))") == "Method doc.")
				#expect(try rt.eval("(:arglists (meta #'mt-pm))") == Value(reading: "([this] [this a])"))
				#expect(try rt.eval("(:doc (meta #'mt-pn))") == nil)
				#expect(try rt.eval("(:arglists (meta #'mt-pn))") == Value(reading: "([this])"))
				let out = try capturingOutput { _ = try rt.eval("(doc mt-pm)") }
				#expect(out == "-------------------------\nuser/mt-pm\n([this] [this a])\n  Method doc.\n")
				try unbind("MtProto", "mt-pm", "mt-pn")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Analysis errors report the innermost enclosing list with a position; a macro's throw the same.
		@Test func errorsReportTheInnermostPositionedList() throws {
			try declare("mt-e")
			let before = clj_debug_live_objects()
			do {
				let source = """
				(defn mt-e [x]
				  (let [y 1]
				    (let [z] z)))
				"""
				let e = try #require(clojureError(rt, source))
				#expect(e.message == "let requires an even number of forms in binding vector")
				#expect(try e.data == Value(reading: "{:line 3 :column 5}"))
				let f = try #require(clojureError(rt, "(defn mt-e [x]\n  (if x\n      (nope)\n      x))"))
				#expect(f.message == "Unable to resolve symbol: nope in this context")
				#expect(try f.data == Value(reading: "{:line 3 :column 7}"))
				let g = try #require(clojureError(rt, "(defn mt-e [x]\n  [1 2 (recur)])"))
				#expect(g.message == "Can only recur from tail position")
				#expect(try g.data == Value(reading: "{:line 2 :column 8}"))
				// A symbol outside any list: the top-level position from the host.
				let h = try #require(clojureError(rt, "\n  nope"))
				#expect(try h.data == Value(reading: "{:line 2 :column 3}"))
				// The same through the C API, which passes no position: the form's own line/column.
				#expect(cljEvalError("(defn mt-e [x]\n  (let [z] z))") == "#error {:message \"let requires an even number of forms in binding vector\", :data {:line 2, :column 3}, :cause #error {:message \"let requires an even number of forms in binding vector\", :data nil}}")
				#expect(cljEvalError("(do\n (nope))") == "#error {:message \"Unable to resolve symbol: nope in this context\", :data {:line 2, :column 2}}")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func swiftBridgeReadsAndWritesMeta() throws {
			let before = clj_debug_live_objects()
			do {
				let v: Value = [1, 2]
				#expect(v.meta == nil)
				let tagged = try v.withMeta(m(["a": 1]))
				#expect(tagged.meta == m(["a": 1]))
				#expect(tagged == v)
				#expect(v.meta == nil)
				#expect(try tagged.withMeta(nil).meta == nil)
				#expect(Value(symbol: "x").meta == nil)
				#expect(try Value(symbol: "x").withMeta(m(["tag": Value(symbol: "String")])).meta == m(["tag": Value(symbol: "String")]))
				#expect(Value(1).meta == nil)
				#expect(throws: ClojureError.self) { try Value("s").withMeta([:]) }
				#expect(throws: ClojureError.self) { try v.withMeta(1) }
				do {
					_ = try Value(keyword: "k").withMeta([:])
				} catch let e as ClojureError {
					#expect(e.message == "with-meta: keyword does not support metadata")
				}
				#expect(try Value(reading: "^:a x").meta == m(["a": true]))
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
