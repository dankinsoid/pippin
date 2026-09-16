// @ai-generated(solo)
import CljCore
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
	@Suite struct HierarchyTests {
		// The keyword table is process-wide and never shrinks, so every keyword the suite reads is interned
		// before a test takes its live-object baseline.
		init() {
			clj_init()
			_ = try? eval("""
			'[:a :b :c :anything :parents :ancestors :descendants :default :unknown :sound :woof :dog :cat :rect :shape :generic
			  :nil-method :fallback :fb :fb-hit :kind :w :h :nons :multifn :dispatch-val :hierarchy :found
			  :t/mammal :t/dog :t/cat :t/animal :t/collie :t/shape :t/rect :t/square :t/x :t/y :t/z :t/circle :t/nope]
			""")
		}

		private func declare(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func makeHierarchyAndDerive() throws {
			clj_init()
			try declare("h")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(make-hierarchy)") == Value([kw("parents"): Value([:]), kw("descendants"): Value([:]), kw("ancestors"): Value([:])]))
				_ = try eval("(def h (-> (make-hierarchy) (derive :t/dog :t/mammal) (derive :t/cat :t/mammal) (derive :t/mammal :t/animal)))")
				#expect(try eval("[(isa? h :t/dog :t/mammal) (isa? h :t/dog :t/animal) (isa? h :t/mammal :t/dog) (isa? h :t/dog :t/dog) (isa? h :t/cat :t/animal)]") == [true, true, false, true, true])
				#expect(try eval("(= (parents h :t/dog) #{:t/mammal})") == true)
				#expect(try eval("(sort (map name (ancestors h :t/dog)))") == Value(["animal", "mammal"]))
				#expect(try eval("(sort (map name (descendants h :t/animal)))") == Value(["cat", "dog", "mammal"]))
				#expect(try eval("[(parents h :t/animal) (ancestors h :t/animal) (descendants h :t/dog)]") == Value([nil, nil, nil]))
				// A derive already implied by the hierarchy is refused, and so is a cycle.
				#expect(message("(derive (derive (make-hierarchy) :t/x :t/y) :t/x :t/y)") == nil)
				#expect(message("(derive (-> (make-hierarchy) (derive :t/x :t/y) (derive :t/y :t/z)) :t/x :t/z)") == ":t/x already has :t/z as ancestor")
				#expect(message("(derive (derive (make-hierarchy) :t/x :t/y) :t/y :t/x)") == "Cyclic derivation: :t/x has :t/y as ancestor")
				#expect(message("(derive (make-hierarchy) :t/x :t/x)") != nil)
				try unbind("h")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// `derive` takes the tags the JVM takes: a namespaced keyword or symbol, or a type in place of a class.
		@Test func deriveShapesAndInvalidHierarchies() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [h (derive (make-hierarchy) String :t/x)] [(isa? h String :t/x) (isa? h :t/x String)])") == [true, false])
				#expect(try eval("(do (derive String :t/x) (let [r (isa? String :t/x)] (underive String :t/x) r))") == true)
				#expect(try eval("(= {:parents {:a #{:b}} :ancestors {:a #{:b}} :descendants {:b #{:a}}} (derive (make-hierarchy) :a :b))") == true)
				for bad in ["(derive :a :t/x)", "(derive 'a 'n/b)", "(derive :t/x :b)", "(derive nil nil)",
				            "(derive :t/x nil)", "(derive :t/x 42)", "(derive :t/x \"p\")", "(derive :t/x String)",
				            "(derive :t/x :t/x)", "(derive (make-hierarchy) :t/x :t/x)",
				            "(derive nil :t/x :t/y)", "(derive {} :t/x :t/y)", "(derive {:parents {} :descendants {}} :t/x :t/y)",
				            "(derive :t/z :t/x :t/y)", "(derive true :t/x :t/y)", "(derive 42 :t/x :t/y)",
				            "(underive nil :t/x :t/y)", "(underive {} :t/x :t/y)", "(underive :t/z :t/x :t/y)"] {
					#expect(message(bad) != nil, Comment(rawValue: bad))
				}
				// The two-argument underive asserts nothing, as Clojure's does not.
				#expect(try eval("[(underive nil nil) (underive :a :a) (underive 'a 'b) (underive true false)]") == Value([nil, nil, nil, nil]))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func isaOnVectorsAndValues() throws {
			clj_init()
			try declare("h")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("(def h (derive (make-hierarchy) :t/square :t/rect))")
				#expect(try eval("[(isa? h [:t/square :t/square] [:t/rect :t/rect]) (isa? h [:t/square :t/rect] [:t/rect :t/square]) (isa? h [:t/square] [:t/rect :t/rect])]") == [true, false, false])
				#expect(try eval("[(isa? 1 1) (isa? :a :a) (isa? \"x\" \"x\") (isa? nil nil) (isa? 1 2) (isa? [1 2] [1 2])]") == [true, true, true, true, false, true])
				#expect(try eval("(isa? h [[:t/square] :t/square] [[:t/rect] :t/rect])") == true)
				try unbind("h")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func globalHierarchy() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				_ = try eval("(derive :t/collie :t/dog) (derive :t/dog :t/mammal)")
				#expect(try eval("[(isa? :t/collie :t/mammal) (isa? :t/mammal :t/collie)]") == [true, false])
				#expect(try eval("(= (parents :t/collie) #{:t/dog})") == true)
				#expect(try eval("(sort (map name (ancestors :t/collie)))") == Value(["dog", "mammal"]))
				#expect(try eval("(sort (map name (descendants :t/mammal)))") == Value(["collie", "dog"]))
				#expect(try eval("(derive :t/x :t/y)") == nil)
				#expect(try eval("(underive :t/x :t/y)") == nil)
				#expect(try eval("[(isa? :t/x :t/y) (parents :t/x)]") == Value([false, nil]))
				// underive keeps the edges it did not remove.
				#expect(try eval("(do (underive :t/collie :t/dog) [(isa? :t/collie :t/mammal) (isa? :t/dog :t/mammal)])") == [false, true])
				_ = try eval("(underive :t/dog :t/mammal)")
				#expect(try eval("(= (deref (var clojure.core/global-hierarchy)) (make-hierarchy))") == true)
				#expect(message("(derive :nons :t/dog)") != nil)
				#expect(message("(derive :t/dog :nons)") != nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func dispatchByEquality() throws {
			clj_init()
			try declare("area")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("""
				(defmulti area (fn [s] (:kind s)))
				(defmethod area :t/rect [s] (* (:w s) (:h s)))
				(defmethod area :default [_] :unknown)
				""")
				#expect(try eval("[(area {:kind :t/rect :w 2 :h 3}) (area {:kind :t/circle})]") == Value([6, kw("unknown")]))
				#expect(try eval("(count (methods area))") == 2)
				#expect(try eval("(= (get-method area :t/rect) (get (methods area) :t/rect))") == true)
				#expect(try eval("(= (get-method area :t/nope) (get (methods area) :default))") == true)
				_ = try eval("(remove-method area :default)")
				#expect(message("(area {:kind :t/circle})") == "No method in multimethod 'area' for dispatch value: :t/circle")
				_ = try eval("(remove-all-methods area)")
				#expect(try eval("(methods area)") == Value([:]))
				try unbind("area")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func dispatchByHierarchy() throws {
			clj_init()
			try declare("speak")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("""
				(derive :t/collie :t/dog)
				(derive :t/dog :t/mammal)
				(defmulti speak identity)
				(defmethod speak :t/mammal [_] :sound)
				""")
				#expect(try eval("[(speak :t/collie) (speak :t/dog) (speak :t/mammal)]") == Value([kw("sound"), kw("sound"), kw("sound")]))
				// A method added for the nearer tag wins, and the cache does not keep the old answer.
				_ = try eval("(defmethod speak :t/dog [_] :woof)")
				#expect(try eval("[(speak :t/collie) (speak :t/dog) (speak :t/mammal)]") == Value([kw("woof"), kw("woof"), kw("sound")]))
				// A derive that changes the hierarchy value invalidates the cache too.
				_ = try eval("(derive :t/cat :t/mammal)")
				#expect(try eval("(speak :t/cat)") == kw("sound"))
				_ = try eval("(underive :t/collie :t/dog)")
				#expect(message("(speak :t/collie)") == "No method in multimethod 'speak' for dispatch value: :t/collie")
				_ = try eval("(underive :t/dog :t/mammal) (underive :t/cat :t/mammal)")
				try unbind("speak")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func ambiguityAndPreference() throws {
			clj_init()
			try declare("f", "g")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("""
				(derive :t/collie :t/dog)
				(derive :t/collie :t/cat)
				(defmulti f identity)
				(defmethod f :t/dog [_] :dog)
				(defmethod f :t/cat [_] :cat)
				""")
				#expect(message("(f :t/collie)")?.hasPrefix("Multiple methods in multimethod 'f' match dispatch value: :t/collie -> ") == true)
				#expect(try eval("(prefers f)") == Value([:]))
				_ = try eval("(prefer-method f :t/dog :t/cat)")
				#expect(try eval("(f :t/collie)") == kw("dog"))
				#expect(try eval("(= (prefers f) {:t/dog #{:t/cat}})") == true)
				#expect(message("(prefer-method f :t/cat :t/dog)") == "Preference conflict in multimethod 'f': :t/dog is already preferred to :t/cat")
				// A preference along the parents counts, as Clojure's MultiFn.prefers walks them.
				_ = try eval("(derive :t/square :t/rect) (defmulti g identity) (defmethod g :t/rect [_] :rect) (defmethod g :t/shape [_] :shape) (derive :t/square :t/shape) (prefer-method g :t/rect :t/shape)")
				#expect(try eval("(g :t/square)") == kw("rect"))
				_ = try eval("(underive :t/collie :t/dog) (underive :t/collie :t/cat) (underive :t/square :t/rect) (underive :t/square :t/shape)")
				try unbind("f", "g")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func multimethodHierarchyOption() throws {
			clj_init()
			try declare("own", "local")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("""
				(def local (atom (derive (make-hierarchy) :t/collie :t/dog)))
				(defmulti own identity :hierarchy local)
				(defmethod own :t/dog [_] :dog)
				""")
				#expect(try eval("(own :t/collie)") == kw("dog"))
				// The global hierarchy has no such edge, so the default multimethod would not match.
				#expect(try eval("(isa? :t/collie :t/dog)") == false)
				_ = try eval("(reset! local (make-hierarchy))")
				#expect(message("(own :t/collie)") == "No method in multimethod 'own' for dispatch value: :t/collie")
				try unbind("own", "local")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func vectorDispatch() throws {
			clj_init()
			try declare("conv")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("""
				(derive :t/square :t/rect)
				(defmulti conv (fn [from to] [from to]))
				(defmethod conv [:t/rect :t/shape] [_ _] :generic)
				""")
				#expect(try eval("(conv :t/square :t/shape)") == kw("generic"))
				#expect(message("(conv :t/shape :t/rect)") != nil)
				_ = try eval("(underive :t/square :t/rect)")
				try unbind("conv")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nilAndDefaultDispatchValues() throws {
			clj_init()
			try declare("n", "k")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("(defmulti n identity) (defmethod n nil [_] :nil-method) (defmethod n :default [_] :fallback)")
				#expect(try eval("[(n nil) (n 1)]") == Value([kw("nil-method"), kw("fallback")]))
				_ = try eval("(defmulti k identity :default :fb) (defmethod k :fb [_] :fb-hit)")
				#expect(try eval("(k :anything)") == kw("fb-hit"))
				try unbind("n", "k")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
