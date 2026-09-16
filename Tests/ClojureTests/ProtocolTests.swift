// @ai-generated(guided)
import CljCore
import Dispatch
import Testing
@testable import Clojure

private func kw(_ s: String) -> Value { Value(keyword: s) }

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

private func owned(_ raw: clj_value) throws -> Value {
	if raw == CLJ_THROWN { throw ClojureError.takePending() }
	return Value(owning: raw)
}

extension CoreTests {
	@Suite struct ProtocolTests {
		let rt = Runtime()

		init() {
			// Method names become interned keywords in the impl maps; values used in expectations too.
			for k in ["area", "perimeter", "name-of", "greet", "count-up", "describe", "total", "kind", "tag", "hit",
			          "self", "first", "second", "third", "long", "long2", "a", "b", "k", "nope"] { _ = kw(k) }
		}

		// Vars, protocols and deftype descriptors are immortal or var-held: declare them before a baseline.
		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func defprotocolAndDeftype() throws {
			try declare("Shape", "area", "perimeter", "Rect", "->Rect", "Circle", "->Circle", "Pt", "->Pt", "pt-sum")
			_ = try rt.eval("""
			(defprotocol Shape "a shape" (area [this]) (perimeter [this]))
			(deftype Rect [w h] Shape (area [this] (* w h)) (perimeter [this] (* 2 (+ w h))))
			(deftype Circle [r] Shape (area [this] (* 3 r r)))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(area (->Rect 3 4))") == 12)
				#expect(try rt.eval("(perimeter (->Rect 3 4))") == 14)
				#expect(try rt.eval("(area (->Circle 2))") == 12)
				#expect(try rt.eval("(map area [(->Rect 1 2) (->Circle 1)])") == Value(list: [2, 3]))
				#expect(try rt.eval("(apply area [(->Rect 2 2)])") == 4)
				// A method is a fn with the protocol's name; the protocol and type print by name.
				#expect(try rt.eval("(fn? area)") == true)
				#expect(try rt.eval("(pr-str area)") == "#object[fn user/area]")
				#expect(try rt.eval("(pr-str Shape)") == "#object[protocol user/Shape]")
				#expect(try rt.eval("(pr-str Rect)") == "user.Rect")
				#expect(try rt.eval("(pr-str (->Rect 1 2))") == "#object[user.Rect]")
				#expect(try rt.eval("(pr-str (type (->Rect 1 2)))") == "user.Rect")
				#expect(try rt.eval("(identical? (type (->Rect 1 2)) Rect)") == true)
				#expect(message(rt, "(perimeter (->Circle 1))") == "No implementation of method: :perimeter of protocol: #'user/Shape found for type: user.Circle")
				#expect(message(rt, "(area 42)") == "No implementation of method: :area of protocol: #'user/Shape found for type: long")
				#expect(message(rt, "(area nil)") == "No implementation of method: :area of protocol: #'user/Shape found for type: nil")
				#expect(message(rt, "(area)") == "Wrong number of args (0) passed to: user/area")
				#expect(message(rt, "(area (->Rect 1 1) 2)") == "Wrong number of args (2) passed to: user/area")
				#expect(message(rt, "(->Rect 1)") == "Wrong number of args (1) passed to: user/->Rect")
				// Fields are locals of the method body; a param of the same name shadows the field.
				_ = try rt.eval("(deftype Pt [x y] Shape (area [this] (+ x y)) (perimeter [x] (if (instance? Pt x) :self x)))")
				#expect(try rt.eval("[(area (->Pt 1 2)) (perimeter (->Pt 1 2))]") == [3, kw("self")])
				_ = try rt.eval("(def pt-sum (fn [p] (area p)))")
				#expect(try rt.eval("(pt-sum (->Pt 5 6))") == 11)
				try unbind("Pt", "->Pt", "pt-sum")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func multipleProtocolsMultiArityAndDefaults() throws {
			try declare("Named", "name-of", "Greeter", "greet", "Person", "->Person", "Counter", "count-up")
			_ = try rt.eval("""
			(defprotocol Named (name-of [this]))
			(defprotocol Greeter (greet [this] [this other] [this other & more]))
			(deftype Person [n]
			  Named (name-of [this] n)
			  Greeter
			  (greet [this] (str "hi from " n))
			  (greet [this other] (str n " greets " (name-of other)))
			  (greet [this other & more] (str n " greets " (count (cons other more)))))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [a (->Person \"ann\") b (->Person \"bob\")] [(greet a) (greet a b) (greet a b b b) (name-of b)])")
					== ["hi from ann", "ann greets bob", "ann greets 3", "bob"])
				#expect(try rt.eval("[(satisfies? Named (->Person \"x\")) (satisfies? Greeter (->Person \"x\")) (satisfies? Named 1)]") == [true, true, false])
				#expect(message(rt, "(greet)") == "Wrong number of args (0) passed to: user/greet")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A table entry keeps its impls for the life of the process, like a var: extend before the baseline.
		@Test func objectIsTheDefault() throws {
			try declare("Named", "name-of", "Person", "->Person", "Counter", "count-up")
			_ = try rt.eval("""
			(defprotocol Named (name-of [this]))
			(deftype Person [n] Named (name-of [this] n))
			(extend-type Object Named (name-of [this] (str "anon:" (pr-str this))))
			(defprotocol Counter (count-up [this] [this n]))
			(extend-type Object Counter (count-up ([this] (count-up this 1)) ([this n] n)))
			(extend-type Long Counter (count-up ([this] (+ this 1)) ([this n] (+ this n))))
			""")
			let before = clj_debug_live_objects()
			do {
				// Object covers every type, nil and deftypes without their own impl included.
				#expect(try rt.eval("[(name-of 1) (name-of nil) (name-of \"s\") (name-of (->Person \"p\"))]") == ["anon:1", "anon:nil", "anon:\"s\"", "p"])
				#expect(try rt.eval("[(satisfies? Named 1) (satisfies? Named nil) (extends? Named Long) (extends? Named nil)]") == [true, true, true, true])
				// A method missing from the type's own impl falls through to the default.
				#expect(try rt.eval("[(count-up 5) (count-up 5 10) (count-up \"x\") (count-up \"x\" 7)]") == [6, 15, 1, 7])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A reify site's type is made on its first evaluation and lives for the process: every site runs once before the baseline.
		@Test func reifyCapturesLocals() throws {
			try declare("Named", "name-of", "Greeter", "greet", "make-named", "other-site", "named-x", "self-site")
			_ = try rt.eval("""
			(defprotocol Named (name-of [this]))
			(defprotocol Greeter (greet [this] [this other]))
			(defn make-named [n] (reify Named (name-of [this] n) Greeter (greet [this] (str "hi " n)) (greet [this o] (str n "+" (name-of o)))))
			(defn other-site [] (reify Named (name-of [_] 0)))
			(defn named-x [x] (reify Named (name-of [this] (inc x))))
			(defn self-site [] (reify Named (name-of [this] (identical? this this))))
			[(make-named 0) (other-site) (named-x 0) (self-site)]
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [a (make-named \"a\") b (make-named \"b\")] [(name-of a) (name-of b) (greet a) (greet a b) (greet b a)])")
					== ["a", "b", "hi a", "a+b", "b+a"])
				// One anonymous type per site: two instances share it, two sites do not.
				#expect(try rt.eval("(identical? (type (make-named 1)) (type (make-named 2)))") == true)
				#expect(try rt.eval("(identical? (type (make-named 1)) (type (other-site)))") == false)
				#expect(try rt.eval("(let [r (named-x 41)] [(name-of r) (satisfies? Named r) (satisfies? Greeter r) (instance? (type r) r)])") == [42, true, false, true])
				#expect(try rt.eval("(str (name-of (named-x 40)))") == "41")
				// this is the instance itself.
				#expect(try rt.eval("(name-of (self-site))") == true)
				#expect(try rt.eval("(pr-str (make-named 1))").string?.hasPrefix("#object[user.reify__") == true)
				// Expansion-time errors: no site, no type.
				#expect(message(rt, "(reify Nope (x [this] 1))") == "Unable to resolve protocol: Nope")
				#expect(message(rt, "(reify (x [this] 1))") == "Method x given before any protocol")
				#expect(try rt.eval("(macroexpand '(reify Named (name-of [_] 0)))").description.hasPrefix("(clojure.core/new* (clojure.core/reify-type* (quote reify__") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// One type per site however often it runs, and a tree that went through data reaches the same type.
		@Test func reifySiteMakesOneType() throws {
			try declare("Named", "name-of", "boxed")
			_ = try rt.eval("(defprotocol Named (name-of [this])) (defn boxed [n] (reify Named (name-of [this] n)))")
			_ = try rt.eval("(boxed 0) (boxed 1)")
			let after2 = clj_debug_live_objects()
			for i in 2..<100 { #expect(try rt.eval("(name-of (boxed \(i)))") == Value(i)) }
			#expect(clj_debug_live_objects() == after2)
			#expect(try rt.eval("(identical? (type (boxed 1)) (type (boxed 2)))") == true)
			// The site's name is a constant symbol in the tree, so the read-back copy finds the same registry entry.
			let source = try Value(reading: "(fn [n] (reify Named (name-of [this] (+ n 1))))")
			let tree = try #require(withExtendedLifetime(source) { clj_analyze(source.raw, nil) })
			let data = try owned(clj_node_to_data(tree))
			let text = try #require(Value(owning: clj_pr_str(data.raw)).string)
			let read = try Value(reading: text)
			let copy = try #require(withExtendedLifetime(read) { clj_node_from_data(read.raw) })
			_ = try rt.eval("(def boxed-a) (def boxed-b)")
			for (name, node) in [("boxed-a", tree), ("boxed-b", copy)] {
				let exec = clj_exec_new(node)
				let fn = try owned(clj_exec_run(exec)), holder = try rt.eval("#'\(name)")
				withExtendedLifetime((fn, holder)) { clj_var_bind_root(holder.raw, fn.raw) }
				clj_release(exec)
				clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
			}
			#expect(try rt.eval("[(name-of (boxed-a 1)) (name-of (boxed-b 2)) (identical? (type (boxed-a 0)) (type (boxed-b 0)))]") == [2, 3, true])
			_ = try rt.eval("(def boxed-a nil) (def boxed-b nil)")
		}

		@Test func extendBuiltinTypes() throws {
			try declare("Describe", "describe", "Sum", "total", "Dup")
			_ = try rt.eval("""
			(defprotocol Describe (describe [this])) (defprotocol Sum (total [this]))
			(extend-protocol Describe
			  String (describe [s] (str "string " s))
			  Long (describe [n] (str "long " n))
			  nil (describe [_] "nil!")
			  PersistentVector (describe [v] (str "vec of " (count v)))
			  Keyword (describe [k] (str "kw " (name k)))
			  Double (describe [d] "double")
			  Boolean (describe [b] (if b "yes" "no"))
			  Character (describe [c] (str "char " c)))
			""")
			let swiftImpl = Value(function: "swift-total", arity: 1...1) { args in Value(args[0].array!.count * 100) }
			_ = try rt.eval("(fn [f] (extend PersistentVector Sum {:total f}))")(swiftImpl)
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(describe \"s\") (describe 3) (describe nil) (describe [1 2]) (describe :k) (describe 1.5) (describe false) (describe \\a)]")
					== ["string s", "long 3", "nil!", "vec of 2", "kw k", "double", "no", "char a"])
				#expect(try rt.eval("[(satisfies? Describe \"s\") (satisfies? Describe {}) (extends? Describe String) (extends? Describe PersistentHashMap)]") == [true, false, true, false])
				#expect(message(rt, "(describe {})") == "No implementation of method: :describe of protocol: #'user/Describe found for type: map")
				#expect(try rt.eval("(total [1 2 3])") == 300)
			}
			#expect(clj_debug_live_objects() == before)
			// extend with a method map; a core interface as the type covers every type with those bits, the concrete type winning.
			_ = try rt.eval("""
			(extend PersistentHashMap Describe {:describe (fn [m] (str "map of " (count m)))})
			(extend-type ISeq Describe (describe [s] (str "seq " (count s))))
			(extend-type IPersistentCollection Sum (total [c] (count c)))
			""")
			let before2 = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(describe {:a 1})") == "map of 1")
				#expect(try rt.eval("[(describe '(1 2 3)) (describe (range 2)) (describe (map inc [1])) (describe [9])]") == ["seq 3", "seq 2", "seq 1", "vec of 1"])
				#expect(try rt.eval("[(total '(1 2 3)) (total [1 2 3]) (total {:a 1}) (satisfies? Sum (range 3))]") == [3, 300, 1, true])
				#expect(try rt.eval("[(instance? String \"s\") (instance? String 1) (instance? Long 1) (instance? nil nil) (instance? nil 1) (instance? Object nil) (instance? ISeq '(1)) (instance? ISeq [1]) (instance? IFn :k)]")
					== [true, false, true, true, false, true, true, false, true])
				#expect(try rt.eval("[(type nil) (type 1) (type \"s\") (type :k) (type [1]) (type {}) (type true) (type \\a) (type inc) (type '(1)) (type ())]")
					== Value(try ["(type nil)", "Long", "String", "Keyword", "PersistentVector", "PersistentHashMap", "Boolean", "Character", "Fn", "PersistentList", "EmptyList"].map { try rt.eval($0) }))
				// Any type × a user protocol is allowed; a builtin type × a core interface is not.
				#expect(message(rt, "(extend-type String ISeq (first [s] nil))") == "ISeq is a core interface, not a protocol: the core-interface slots of a type are write-once")
				#expect(message(rt, "(extend String IFn {})") == "IFn is a core interface, not a protocol: the core-interface slots of a type are write-once")
				#expect(message(rt, "(extend-type String Describe (nope [s] 1))") == "No method :nope in protocol Describe")
				#expect(message(rt, "(extend-type 42 Describe (describe [s] 1))") == "42 is not a type")
				#expect(message(rt, "(extend-type String inc (describe [s] 1))") == "fn is not a protocol")
				#expect(message(rt, "(extend String Describe {:describe 1})") == "Method implementation must be a fn, got: 1")
				#expect(message(rt, "(defprotocol Dup (a [x]) (a [x y]))") == "Duplicate method a in protocol Dup")
			}
			#expect(clj_debug_live_objects() == before2)
		}

		@Test func redefinitionTakesEffect() throws {
			try declare("Kind", "kind", "Box", "->Box")
			_ = try rt.eval("(defprotocol Kind (kind [this])) (deftype Box [v] Kind (kind [this] :first)) (extend-type Long Kind (kind [this] :zero))")
			let before = clj_debug_live_objects()
			do {
				// Each redefinition below has the shape of what it replaces, so the live count is unchanged.
				let epoch0 = try rt.eval("(protocol-epoch*)").int!
				let b = try rt.eval("(->Box 1)")
				let kindOf = try rt.eval("kind")
				#expect(try kindOf(b) == kw("first"))
				_ = try rt.eval("(extend-type Box Kind (kind [this] :second))")
				#expect(try kindOf(b) == kw("second"))
				_ = try rt.eval("(extend-type Long Kind (kind [this] :long))")
				#expect(try rt.eval("(kind 1)") == kw("long"))
				_ = try rt.eval("(extend Long Kind {:kind (fn [_] :long2)})")
				#expect(try rt.eval("(kind 1)") == kw("long2"))
				#expect(try rt.eval("(protocol-epoch*)").int! == epoch0 + 3)
				// Redefining the type makes a new descriptor; old instances keep the old one and its impls.
				_ = try rt.eval("(deftype Box [v] Kind (kind [this] :third))")
				#expect(try kindOf(b) == kw("second"))
				#expect(try rt.eval("(kind (->Box 1))") == kw("third"))
				#expect(try rt.eval("(instance? Box (->Box 1))") == true)
				#expect(try rt.eval("(fn [x] (instance? Box x))")(b) == false)
				#expect(try rt.eval("(fn [x] (= (type x) Box))")(b) == false)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func identityEqualityAndHash() throws {
			try declare("Tag", "tag", "T", "->T")
			_ = try rt.eval("(defprotocol Tag (tag [this])) (deftype T [a] Tag (tag [this] a))")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(let [x (->T 1) y (->T 1)] [(= x x) (= x y) (= (hash x) (hash x)) (identical? x y)])") == [true, false, true, false])
				#expect(try rt.eval("(let [x (->T 1) y (->T 1) m {x :a y :b}] [(get m x) (get m y) (count m) (contains? m (->T 1))])") == [kw("a"), kw("b"), 2, false])
				#expect(try rt.eval("(let [x (->T 1)] (= [x] [x]))") == true)
				// A deftype value crosses to Swift and back as the same object.
				let t = try rt.eval("(->T 7)")
				#expect(try rt.eval("tag")(t) == 7)
				#expect(t.typeName == "user.T")
				#expect(t == t)
				#expect(t.kind.isObject)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func concurrentDispatchWhileExtending() throws {
			try declare("Hit", "hit", "H", "->H")
			// A closure keeps its whole top-level tree alive, so the restore below repeats these exact forms.
			_ = try rt.eval("(defprotocol Hit (hit [this])) (deftype H [n]) (extend-type Long Hit (hit [n] n)) (extend-type H Hit (hit [this] (field* this 0)))")
			let before = clj_debug_live_objects()
			do {
				let call = try rt.eval("(fn [x] (hit x))")
				let h = try rt.eval("(->H 3)")
				let threads = 8, rounds = 2000
				var sums = [Int](repeating: 0, count: threads)
				sums.withUnsafeMutableBufferPointer { sp in
					nonisolated(unsafe) let sp = sp
					DispatchQueue.concurrentPerform(iterations: threads) { i in
						if i == 0 {
							// The writer: every extend rebuilds the tables the readers dispatch through.
							var done = 0
							for round in 0..<200 {
								if (try? rt.eval("(extend-type Long Hit (hit [n] (+ n \(round % 2))))")) != nil { done += 1 }
								if (try? rt.eval("(extend-type H Hit (hit [this] (+ (field* this 0) \(round % 2))))")) != nil { done += 1 }
							}
							sp[0] = done
							return
						}
						var sum = 0
						for _ in 0..<rounds {
							if let a = try? call(h).int, let b = try? call(Value(5)).int { sum += a + b }
						}
						sp[i] = sum
					}
				}
				// Every read saw one of the two published impls, never garbage.
				#expect(sums[0] == 400)
				for i in 1..<threads {
					#expect(sums[i] >= 8 * rounds && sums[i] <= 10 * rounds, "\(sums[i])")
				}
				// Back to the original impls, in their original shape, for the live count.
				_ = try rt.eval("(extend-type Long Hit (hit [n] n)) (extend-type H Hit (hit [this] (field* this 0)))")
				#expect(try rt.eval("[(hit 5) (hit (->H 3))]") == [5, 3])
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}

private extension Value.Kind {
	var isObject: Bool {
		if case .object = self { return true }
		return false
	}
}
