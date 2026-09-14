// @ai-generated(guided)
import CljCore
import Dispatch
import Testing
@testable import Clojure

private func kw(_ s: String) -> Value { Value(keyword: s) }

private func message(_ rt: Runtime, _ source: String) -> String? {
	do {
		_ = try rt.eval(source)
		return nil
	} catch let e as ClojureError {
		return e.message
	} catch {
		return nil
	}
}

extension CoreTests {
	// deftype and reify implementing core interfaces: the type's slots trampoline into the method fns.
	@Suite struct DeftypeSlotsTests {
		let rt = Runtime()

		init() {
			// Method names become interned keywords in the impl maps; keywords used in expectations too.
			for k in ["seq", "first", "next", "more", "rest", "cons", "count", "valAt", "invoke", "hasheq", "equiv",
			          "ex-message", "ex-data", "ex-cause", "getMessage", "getData", "empty", "applyTo", "nope",
			          "a", "b", "k", "n", "x", "y", "code", "none", "found", "nf", "dflt", "two-arity", "here", "plain",
			          "other", "caught", "default", "else", "nth"] { _ = kw(k) }
		}

		// Vars are immortal: declare them before a baseline.
		private func declare(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		// Other suites declare some of the same names before their baselines: leave nothing alive under them.
		private func unbind(_ names: String...) throws {
			_ = try rt.eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		// A countdown seq: (->Down 3) is (3 2 1). next constructs its own type, so ->Down must resolve inside the body.
		private static let down = """
		(deftype Down [n]
		  ISeq
		  (seq [this] this)
		  (first [_] n)
		  (next [_] (when (> n 1) (->Down (dec n))))
		  Sequential)
		"""

		@Test func seqConsumedByCore() throws {
			try declare("Down", "->Down")
			_ = try rt.eval(Self.down)
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(map inc (->Down 3))") == Value(list: [4, 3, 2]))
				#expect(try rt.eval("(reduce + (->Down 4))") == 10)
				#expect(try rt.eval("(into [] (->Down 3))") == [3, 2, 1])
				#expect(try rt.eval("(take 2 (->Down 5))") == Value(list: [5, 4]))
				#expect(try rt.eval("(count (->Down 3))") == 3)
				#expect(try rt.eval("(vec (rest (->Down 3)))") == [2, 1])
				#expect(try rt.eval("[(first (->Down 3)) (second (->Down 3)) (last (->Down 3)) (nth (->Down 3) 1)]") == [3, 2, 1, 2])
				#expect(try rt.eval("(vec (cons 9 (->Down 2)))") == [9, 2, 1])
				#expect(try rt.eval("(apply + (->Down 3))") == 6)
				#expect(try rt.eval("(vec (concat (->Down 2) (->Down 1)))") == [2, 1, 1])
				#expect(try rt.eval("(vec (reverse (->Down 3)))") == [1, 2, 3])
				#expect(try rt.eval("(butlast (->Down 3))") == Value(list: [3, 2]))
				#expect(try rt.eval("(pr-str (->Down 3))") == "(3 2 1)")
				#expect(try rt.eval("(str (->Down 2))") == "(2 1)")
				#expect(try rt.eval("[(seq? (->Down 1)) (seqable? (->Down 1)) (sequential? (->Down 1)) (coll? (->Down 1)) (counted? (->Down 1)) (list? (->Down 1))]")
					== [true, true, true, true, false, false])
				#expect(try rt.eval("[(satisfies? ISeq (->Down 1)) (instance? Seqable (->Down 1)) (extends? Sequential Down) (satisfies? Counted (->Down 1))]")
					== [true, true, true, false])
				#expect(try rt.eval("(instance? Down (->Down 1))") == true)
				#expect(try rt.eval("(nth (->Down 2) 5 :none)") == kw("none"))
				#expect(message(rt, "(nth (->Down 2) 5)") == "Index 5 out of bounds for length 2")
				// The Swift side walks it as any seq.
				#expect(try rt.eval("(->Down 3)").list == [3, 2, 1])
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Down", "->Down")
		}

		@Test func sequentialEqualityAndHash() throws {
			try declare("Down", "->Down")
			_ = try rt.eval(Self.down)
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(= (->Down 3) '(3 2 1)) (= '(3 2 1) (->Down 3)) (= (->Down 3) [3 2 1]) (= (->Down 3) (->Down 3)) (= (->Down 3) (->Down 2)) (= (->Down 3) '(3 2))]")
					== [true, true, true, true, false, false])
				#expect(try rt.eval("[(= (hash (->Down 3)) (hash '(3 2 1))) (= (hash (->Down 3)) (hash (->Down 2)))]") == [true, false])
				#expect(try rt.eval("(get {'(3 2 1) :found} (->Down 3))") == kw("found"))
				#expect(try rt.eval("(= (->Down 3) 3)") == false)
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Down", "->Down")
		}

		@Test func seqWithMoreCountAndCons() throws {
			try declare("Pair", "->Pair")
			// count under ISeq fills the slot without the Counted bit.
			_ = try rt.eval("""
			(deftype Pair [a b]
			  ISeq
			  (seq [this] this)
			  (first [_] a)
			  (more [_] (list b))
			  (count [_] 2)
			  (cons [this x] (list x a b))
			  Sequential)
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(vec (->Pair 1 2))") == [1, 2])
				#expect(try rt.eval("[(rest (->Pair 1 2)) (next (->Pair 1 2)) (count (->Pair 1 2)) (counted? (->Pair 1 2))]") == [Value(list: [2]), Value(list: [2]), 2, false])
				#expect(try rt.eval("(conj (->Pair 1 2) 0)") == Value(list: [0, 1, 2]))
				#expect(try rt.eval("(into (->Pair 1 2) [7 8])") == Value(list: [8, 7, 1, 2]))
				#expect(try rt.eval("(= (->Pair 1 2) '(1 2))") == true)
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Pair", "->Pair")
		}

		@Test func fnCalledDirectlyAndPassed() throws {
			try declare("Adder", "->Adder")
			_ = try rt.eval("""
			(deftype Adder [n]
			  IFn
			  (invoke [_ x] (+ n x))
			  (invoke [_ x y] (+ n x y)))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("((->Adder 10) 5)") == 15)
				#expect(try rt.eval("((->Adder 10) 5 6)") == 21)
				#expect(try rt.eval("(map (->Adder 100) [1 2 3])") == Value(list: [101, 102, 103]))
				#expect(try rt.eval("(apply (->Adder 1) [2 3])") == 6)
				#expect(try rt.eval("(reduce (->Adder 0) [1 2 3 4])") == 10)
				#expect(try rt.eval("[(ifn? (->Adder 1)) (fn? (->Adder 1)) (satisfies? IFn (->Adder 1)) (instance? IFn (->Adder 1))]") == [true, false, true, true])
				#expect(message(rt, "((->Adder 1))") == "Wrong number of args (0) passed to: user.Adder")
				#expect(message(rt, "((->Adder 1) 1 2 3)") == "Wrong number of args (3) passed to: user.Adder")
				// Callable from Swift through the same slot.
				#expect(try rt.eval("(->Adder 2)")(40) == 42)
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Adder", "->Adder")
		}

		@Test func lookupWithGetAndKeyword() throws {
			try declare("Env", "->Env", "Env2", "->Env2")
			_ = try rt.eval("""
			(deftype Env [m]
			  ILookup
			  (valAt [_ k] (get m k))
			  (valAt [_ k nf] (get m k nf)))
			(deftype Env2 [m]
			  ILookup
			  (valAt [_ k] (get m k :two-arity)))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(get (->Env {:a 1}) :a) (get (->Env {:a 1}) :b) (get (->Env {:a 1}) :b :nf) (:a (->Env {:a 1})) (:b (->Env {:a 1}) :dflt)]")
					== [1, nil, kw("nf"), 1, kw("dflt")])
				#expect(try rt.eval("[(satisfies? ILookup (->Env {})) (associative? (->Env {}))]") == [true, false])
				// Only a 2-arity valAt: get without a not-found reaches it, as RT.get does; a not-found needs the 3-arity.
				#expect(try rt.eval("[(get (->Env2 {:a 1}) :a) (get (->Env2 {}) :b) (:a (->Env2 {:a 1}))]") == [1, kw("two-arity"), 1])
				#expect(message(rt, "(get (->Env2 {}) :b :nf)") == "Wrong number of args (3) passed to: fn")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Env", "->Env", "Env2", "->Env2")
		}

		@Test func counted() throws {
			try declare("Sized", "->Sized", "Bad", "->Bad")
			_ = try rt.eval("""
			(deftype Sized [n] Counted (count [_] n))
			(deftype Bad [v] Counted (count [_] v))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(count (->Sized 7)) (counted? (->Sized 7)) (satisfies? Counted (->Sized 7)) (seqable? (->Sized 7))]") == [7, true, true, false])
				#expect(message(rt, "(count (->Bad :x))") == "count of user.Bad must return a non-negative integer, got: keyword")
				#expect(message(rt, "(count (->Bad -1))") == "count of user.Bad must return a non-negative integer, got: fixnum")
				#expect(message(rt, "(seq (->Sized 1))") == "Don't know how to create ISeq from: user.Sized")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Sized", "->Sized", "Bad", "->Bad")
		}

		@Test func exceptionInfoThrownAndCaught() throws {
			try declare("Fail", "->Fail", "Plain", "->Plain", "Loud", "->Loud")
			_ = try rt.eval("""
			(deftype Fail [code]
			  IExceptionInfo
			  (ex-message [_] (str "failed with " code))
			  (ex-data [_] {:code code}))
			(deftype Plain [] IExceptionInfo)
			(deftype Loud [] IExceptionInfo (ex-message [_] (throw (ex-info "inner" {}))))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(try (throw (->Fail 7)) (catch ExceptionInfo e [(ex-message e) (ex-data e) (ex-cause e) (instance? Fail e)]))")
					== ["failed with 7", [kw("code"): 7], nil, true])
				#expect(try rt.eval("[(satisfies? IExceptionInfo (->Fail 1)) (instance? ExceptionInfo (->Fail 1)) (instance? IExceptionInfo (->Fail 1))]") == [true, false, true])
				// Methods the form leaves out read as nil, like Throwable's getMessage.
				#expect(try rt.eval("(try (throw (->Plain)) (catch ExceptionInfo e [(ex-message e) (ex-data e)]))") == [nil, nil])
				// A deftype error may be the cause of an ex-info.
				#expect(try rt.eval("(ex-message (ex-cause (ex-info \"outer\" {} (->Fail 3))))") == "failed with 3")
				// Uncaught, it reaches the host as a ClojureError with the slots' values.
				let err = { () -> ClojureError? in
					do { _ = try rt.eval("(throw (->Fail 9))") } catch let e as ClojureError { return e } catch {}
					return nil
				}()
				#expect(err?.message == "failed with 9")
				#expect(err?.data == [kw("code"): 9])
				// A slot that throws while the host reads the error: the inner exception's text stands in.
				let inner = { () -> ClojureError? in
					do { _ = try rt.eval("(throw (->Loud))") } catch let e as ClojureError { return e } catch {}
					return nil
				}()
				#expect(inner?.message == "inner")
				#expect(message(rt, "(try (throw (->Loud)) (catch ExceptionInfo e (ex-message e)))") == "inner")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Fail", "->Fail", "Plain", "->Plain", "Loud", "->Loud")
		}

		@Test func reifyImplementingSeqCapturesLocal() throws {
			try declare("upto")
			_ = try rt.eval("""
			(defn upto [n]
			  (let [k n]
			    (reify
			      ISeq
			      (seq [this] this)
			      (first [_] k)
			      (next [_] (when (> k 1) (upto (dec k))))
			      Sequential
			      Counted
			      (count [_] k))))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(vec (upto 3))") == [3, 2, 1])
				#expect(try rt.eval("[(count (upto 4)) (counted? (upto 4)) (= (upto 2) '(2 1)) (seq? (upto 1))]") == [4, true, true, true])
				#expect(try rt.eval("(map inc (upto 2))") == Value(list: [3, 2]))
				#expect(try rt.eval("(pr-str (upto 2))") == "(2 1)")
				#expect(try rt.eval("(let [x 5 r (reify IFn (invoke [_ y] (* x y)))] [(r 2) (map r [1 2])])") == [10, Value(list: [5, 10])])
				#expect(try rt.eval("(let [r (reify ILookup (valAt [_ k nf] (if (= k :a) 1 nf)))] [(:a r) (get r :b :nf)])") == [1, kw("nf")])
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("upto")
		}

		@Test func slotsAreWriteOnce() throws {
			try declare("Plain", "->Plain", "Desc", "describe")
			_ = try rt.eval("(deftype Plain [x]) (defprotocol Desc (describe [this])) (extend-type Plain Desc (describe [_] :plain))")
			let before = clj_debug_live_objects()
			do {
				#expect(message(rt, "(extend-type Plain ISeq (first [s] nil))") == "ISeq is a core interface, not a protocol: the core-interface slots of a type are write-once")
				#expect(message(rt, "(extend Plain Counted {:count (fn [_] 1)})") == "Counted is a core interface, not a protocol: the core-interface slots of a type are write-once")
				#expect(message(rt, "(extend-type String ISeq (first [s] nil))") == "ISeq is a core interface, not a protocol: the core-interface slots of a type are write-once")
				// A protocol still extends a user type after creation; the same body again keeps the object count.
				_ = try rt.eval("(extend-type Plain Desc (describe [_] :plain))")
				#expect(try rt.eval("(describe (->Plain 1))") == kw("plain"))
				#expect(try rt.eval("[(seq? (->Plain 1)) (ifn? (->Plain 1)) (counted? (->Plain 1))]") == [false, false, false])
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Plain", "->Plain", "Desc", "describe")
		}

		@Test func formErrors() throws {
			try declare("T", "->T", "Desc", "describe")
			_ = try rt.eval("(defprotocol Desc (describe [this]))")
			let before = clj_debug_live_objects()
			do {
				#expect(message(rt, "(deftype T [] ISeq (nope [_] 1))") == "No method :nope in interface ISeq")
				#expect(message(rt, "(deftype T [] IPersistentCollection (empty [_] nil))") == "No slot for method :empty in interface IPersistentCollection")
				#expect(message(rt, "(deftype T [] IFn (applyTo [_ args] nil))") == "No slot for method :applyTo in interface IFn")
				#expect(message(rt, "(deftype T [] Counted (count [_] 1) ISeq (count [_] 2))") == "Duplicate method :count in interface ISeq")
				#expect(message(rt, "(deftype T [] Indexed (nth [_ i] i))") == "Indexed cannot be implemented by deftype: no slots behind it")
				#expect(message(rt, "(deftype T [] IPersistentMap)") == "IPersistentMap cannot be implemented by deftype: no slots behind it")
				#expect(message(rt, "(deftype T [] Desc (nope [_] 1))") == "No method :nope in protocol Desc")
				#expect(message(rt, "(deftype T [] inc (describe [_] 1))") == "fn is not a protocol")
				// A declared interface with a missing method keeps its slot, which throws when reached.
				_ = try rt.eval("(deftype T [] ISeq (seq [this] this) (first [_] 1))")
				#expect(message(rt, "(next (->T))") == "No implementation of method: :next found for type: user.T")
				#expect(message(rt, "(conj (->T) 1)") == "No implementation of method: :cons found for type: user.T")
				#expect(message(rt, "(vec (->T))") == "No implementation of method: :next found for type: user.T")
				_ = try rt.eval("(deftype T [] Seqable (seq [_] [1 2]))")
				#expect(message(rt, "(first (->T))") == "seq of user.T must return a seq or nil, got: vector")
				#expect(try rt.eval("(seqable? (->T))") == true)
				try unbind("T", "->T")
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("T", "->T", "Desc", "describe")
		}

		@Test func methodThrowingThroughSlot() throws {
			try declare("Boom", "->Boom", "Eq", "->Eq")
			_ = try rt.eval("""
			(deftype Boom [at n]
			  ISeq
			  (seq [this] this)
			  (first [_] (if (= n at) (throw (ex-info "boom" {:n n})) n))
			  (next [_] (when (< n 5) (->Boom at (inc n))))
			  Sequential
			  Counted
			  (count [_] (throw (ex-info "no count" {})))
			  IFn
			  (invoke [_ x] (throw (ex-info "no call" {:x x})))
			  ILookup
			  (valAt [_ k nf] (throw (ex-info "no lookup" {}))))
			(deftype Eq [v]
			  IEquiv
			  (equiv [_ o] (throw (ex-info "no equiv" {})))
			  IHashEq
			  (hasheq [_] (throw (ex-info "no hash" {}))))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("(vec (->Boom 9 1))") == [1, 2, 3, 4, 5])
				#expect(message(rt, "(vec (->Boom 3 1))") == "boom")
				#expect(message(rt, "(doall (map inc (->Boom 3 1)))") == "boom")
				#expect(message(rt, "(pr-str (->Boom 2 1))") == "boom")
				#expect(message(rt, "(first (->Boom 1 1))") == "boom")
				#expect(message(rt, "(nth (->Boom 2 1) 4)") == "boom")
				#expect(message(rt, "(count (->Boom 9 1))") == "no count")
				#expect(message(rt, "((->Boom 9 1) 1)") == "no call")
				#expect(message(rt, "(:k (->Boom 9 1))") == "no lookup")
				#expect(try rt.eval("(try (reduce + (->Boom 4 1)) (catch ExceptionInfo e (:n (ex-data e))))") == 4)
				// = and hash cannot throw: the exception is dropped, the values compare unequal, a throwing hasheq
				// hashes 0 and a throwing walk hashes what it yielded (NOTES.md).
				#expect(try rt.eval("[(= (->Eq 1) (->Eq 1)) (hash (->Eq 1)) (= (->Boom 3 1) '(1 2 3 4 5)) (= (hash (->Boom 3 1)) (hash '(1 2)))]") == [false, 0, false, true])
				// Nothing stays pending after a dropped exception.
				#expect(try rt.eval("(try (+ 1 1) (catch :default e :caught))") == 2)
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Boom", "->Boom", "Eq", "->Eq")
		}

		@Test func equivAndHasheq() throws {
			try declare("Pt2", "->Pt2")
			_ = try rt.eval("""
			(deftype Pt2 [x y]
			  IEquiv
			  (equiv [_ o] (and (instance? Pt2 o) (= x (:x o)) (= y (:y o))))
			  IHashEq
			  (hasheq [_] (+ (* 31 x) y))
			  ILookup
			  (valAt [_ k nf] (cond (= k :x) x (= k :y) y :else nf)))
			""")
			let before = clj_debug_live_objects()
			do {
				#expect(try rt.eval("[(= (->Pt2 1 2) (->Pt2 1 2)) (= (->Pt2 1 2) (->Pt2 2 1)) (= (->Pt2 1 2) [1 2]) (hash (->Pt2 1 2)) (= (hash (->Pt2 1 2)) (hash (->Pt2 1 2)))]")
					== [true, false, false, 33, true])
				#expect(try rt.eval("(get {(->Pt2 1 2) :here} (->Pt2 1 2))") == kw("here"))
				#expect(try rt.eval("[(satisfies? IEquiv (->Pt2 1 2)) (satisfies? IHashEq (->Pt2 1 2)) (satisfies? IHashEq [1])]") == [true, true, false])
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Pt2", "->Pt2")
		}

		@Test func concurrentWalkOfSharedSeq() throws {
			try declare("Down", "->Down", "shared")
			_ = try rt.eval(Self.down)
			_ = try rt.eval("(def shared (->Down 200))")
			let before = clj_debug_live_objects()
			do {
				let threads = 8, rounds = 20
				var sums = [Int](repeating: 0, count: threads)
				sums.withUnsafeMutableBufferPointer { sp in
					nonisolated(unsafe) let sp = sp
					DispatchQueue.concurrentPerform(iterations: threads) { i in
						var sum = 0
						for _ in 0..<rounds {
							// Every walk runs the next/first methods on this thread over the one shared chain.
							if let n = try? rt.eval("(count shared)").int, let s = try? rt.eval("(reduce + (map inc shared))").int, (try? rt.eval("(= shared (range 200 0 -1))")) == true {
								sum += n + s
							}
						}
						sp[i] = sum
					}
				}
				#expect(sums == Array(repeating: rounds * (200 + 200 * 201 / 2 + 200), count: threads))
			}
			#expect(clj_debug_live_objects() == before)
			try unbind("Down", "->Down", "shared")
		}
	}
}
