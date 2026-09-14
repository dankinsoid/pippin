// @ai-generated(guided)
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
	@Suite struct DestructureTests {
		// Keywords intern permanently: every keyword a test reads or builds is interned before the baseline.
		private func intern(_ keywords: String...) {
			for k in keywords { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func sequential() throws {
			clj_init()
			intern("as", "keys", "k")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [[a b] [1 2]] [a b])") == [1, 2])
				#expect(try eval("(let [[a b c] [1 2]] [a b c])") == [1, 2, nil])
				#expect(try eval("(let [[a & r] [1 2 3]] [a r])") == [1, Value(list: [2, 3])])
				#expect(try eval("(let [[a & r] [1]] [a r])") == [1, nil])
				#expect(try eval("(let [[a b :as all] [1 2 3]] [a b all])") == [1, 2, [1, 2, 3]])
				#expect(try eval("(let [[a & r :as all] '(1 2 3)] [a r all])") == [1, Value(list: [2, 3]), Value(list: [1, 2, 3])])
				#expect(try eval("(let [[[a b] [c]] [[1 2] [3]]] [a b c])") == [1, 2, 3])
				#expect(try eval("(let [[a b] nil] [a b])") == [nil, nil])
				#expect(try eval("(let [[a b] \"xy\"] (str b a))") == "yx")
				#expect(try eval("(let [[_ _ c] [1 2 3]] c)") == 3)
				#expect(try eval("(let [[a & [b c]] [1 2 3]] [a b c])") == [1, 2, 3])
				#expect(try eval("(let [[a & {:keys [k]}] [1 :k 2]] [a k])") == [1, 2])
				#expect(try eval("(let [[a b] [1 2] [a] [b]] a)") == 2)
				#expect(try eval("(let [[a] [1] f (fn [] a)] (f))") == 1)
				// Plain symbols pass through untouched; only destructured pairs expand.
				#expect(try eval("(destructure '[a 1 b 2])") == Value(reading: "[a 1 b 2]"))
				#expect(try eval("(count (destructure '[[a b] v]))") == 6)
				// [vec__N v a (nth vec__N 0 nil) b (nth vec__N 1 nil)], the gensym shared by every access.
				#expect(try eval("(let [[g v a x b y] (destructure '[[a b] v])] [v a b (= (second x) g) (= (second y) g) (first x) (nth x 2) (nth y 2) (nth y 3)])")
					== Value(reading: "[v a b true true clojure.core/nth 0 1 nil]"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func associative() throws {
			clj_init()
			intern("a", "b", "c", "k", "z", "pair", "nested", "keys", "strs", "syms", "as", "or", "ns/a", "ns/b", "ns/c", "ns/keys", "ns/syms", "foo")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [{a :a b :b} {:a 1 :b 2}] [a b])") == [1, 2])
				#expect(try eval("(let [{a :a :or {a 9}} {}] a)") == 9)
				#expect(try eval("(let [{a :a :or {a 9}} {:a nil}] a)") == nil)
				#expect(try eval("(let [d 5 {a :a :or {a d}} {}] a)") == 5)
				#expect(try eval("(let [{:keys [a b] :or {b 5}} {:a 1}] [a b])") == [1, 5])
				#expect(try eval("(let [{:keys [a b] :as m} {:a 1 :b 2}] [a b m])") == [1, 2, Value(reading: "{:a 1 :b 2}")])
				#expect(try eval("(let [{:strs [a]} {\"a\" 1}] a)") == 1)
				#expect(try eval("(let [{:syms [a]} {'a 1}] a)") == 1)
				#expect(try eval("(let [{:ns/keys [a b]} {:ns/a 1 :ns/b 2}] [a b])") == [1, 2])
				#expect(try eval("(let [{:keys [ns/a :b :ns/c]} {:ns/a 1 :b 2 :ns/c 3}] [a b c])") == [1, 2, 3])
				#expect(try eval("(let [{:ns/syms [a]} {'ns/a 1}] a)") == 1)
				#expect(try eval("(let [{[x y] :pair z :z} {:pair [1 2] :z 3}] [x y z])") == [1, 2, 3])
				#expect(try eval("(let [{{c :c} :nested} {:nested {:c 4}}] c)") == 4)
				#expect(try eval("(let [{a :a} nil] a)") == nil)
				#expect(try eval("(let [{a 0 b 1} [10 20]] [a b])") == [10, 20])
				#expect(try eval("(let [{a/b :k} {:k 1}] b)") == 1)
				// A seq of key/value pairs, or a single trailing map, is treated as the map (kwargs).
				#expect(try eval("(let [{a :a} '(:a 1)] a)") == 1)
				#expect(try eval("(let [{a :a} '({:a 7})] a)") == 7)
				#expect(try eval("(let [{a :a} ()] a)") == nil)
				#expect(try eval("(macroexpand-1 '(let [a 1] a))") == Value(reading: "(let* [a 1] a)"))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func loopFnAndMacros() throws {
			clj_init()
			intern("keys", "or", "n", "acc", "a", "b", "x", "y", "no")
			try declare("ds-sum", "ds-kw", "ds-m", "ds-m2", "ds-walk")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(loop [[a & r] [1 2 3] acc []] (if a (recur r (conj acc a)) acc))") == [1, 2, 3])
				#expect(try eval("(loop [{:keys [n acc]} {:n 3 :acc 0}] (if (zero? n) acc (recur {:n (dec n) :acc (+ acc n)})))") == 6)
				#expect(try eval("(loop [[a b] [1 2] i 0] (if (< i 3) (recur [b a] (inc i)) [a b]))") == [2, 1])
				#expect(try eval("(macroexpand-1 '(loop [a 1] a))") == Value(reading: "(loop* [a 1] a)"))
				#expect(try eval("((fn [[a b]] (+ a b)) [1 2])") == 3)
				#expect(try eval("((fn ([[a]] a) ([[a] {:keys [b]}] (+ a b))) [1] {:b 2})") == 3)
				#expect(try eval("((fn ([[a]] a) ([[a] {:keys [b]}] (+ a b))) [5])") == 5)
				#expect(try eval("((fn [a & [b c]] [a b c]) 1 2)") == [1, 2, nil])
				#expect(try eval("((fn [[a & r] acc] (if a (recur r (conj acc a)) acc)) [1 2] [])") == [1, 2])
				#expect(try eval("((fn f [[a & r] acc] (if a (f r (conj acc a)) acc)) [1 2] [])") == [1, 2])
				#expect(try eval("(macroexpand-1 '(fn [a] a))") == Value(reading: "(fn* ([a] a))"))
				#expect(try eval("(macroexpand-1 '(fn f ([a] a) ([a b] b)))") == Value(reading: "(fn* f ([a] a) ([a b] b))"))
				#expect(try eval("(defn ds-sum [[a b]] (+ a b)) (ds-sum [1 2])") == 3)
				#expect(try eval("(defn ds-kw [& {:keys [x y] :or {y 10}}] [x y]) (ds-kw :x 1)") == [1, 10])
				#expect(try eval("(ds-kw)") == [nil, 10])
				#expect(try eval("(defn ds-walk [[a & r] acc] (if a (recur r (conj acc a)) acc)) (ds-walk '(1 2 3) [])") == [1, 2, 3])
				#expect(try eval("(defmacro ds-m [[a b] & body] `(+ ~a ~b ~@body)) (ds-m [1 2] 3)") == 6)
				#expect(try eval("(defmacro ds-m2 [{:keys [x]}] x) (ds-m2 {:x 5})") == 5)
				#expect(try eval("(if-let [[a b] [1 2]] (+ a b) :no)") == 3)
				#expect(try eval("(if-let [[a] nil] a :no)") == kw("no"))
				#expect(try eval("(when-let [{:keys [a]} {:a 1}] (inc a))") == 2)
				let out = try capturingOutput { _ = try eval("(dotimes [i 2] (println i))") }
				#expect(out == "0\n1\n")
				try unbind("ds-sum", "ds-kw", "ds-m", "ds-m2", "ds-walk")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func errors() throws {
			clj_init()
			intern("a", "foo")
			let before = clj_debug_live_objects()
			do {
				#expect(message("(let [1 2] 1)") == "Unsupported binding form: 1")
				#expect(message("(let [\"s\" 2] 1)") == "Unsupported binding form: s")
				#expect(message("(let [:a 1] 1)") == "Unsupported binding form: :a")
				#expect(message("(let [{:foo x} {}] x)") == "Unsupported binding key: :foo")
				#expect(message("(let [[a & b c] [1]] a)") == "Unsupported binding form, only :as can follow & parameter")
				#expect(message("(let [[a] 1] a)") == "nth not supported on this type: fixnum")
				#expect(message("(let [{a :a} 1] a)") == nil)
				#expect(message("(let (a 1) a)") == "let requires a vector for its binding")
				#expect(message("(let [a] a)") == "let requires an even number of forms in binding vector")
				#expect(message("(loop (a 1) a)") == "loop requires a vector for its binding")
				#expect(message("(loop [a] a)") == "loop requires an even number of forms in binding vector")
				#expect(message("(let [a/b 1] b)") == "Can't let qualified name: a/b")
				#expect(message("(fn [1] 1)") == "Unsupported binding form: 1")
				#expect(message("(fn)") == "Parameter declaration missing")
				#expect(message("(fn x)") == "Parameter declaration missing")
				#expect(message("(fn 1)") == "Parameter declaration 1 should be a vector")
				#expect(message("(fn (1))") == "Parameter declaration 1 should be a vector")
				#expect(message("(fn [a b] 1 2) (fn ([x] 1) ([y] 2))") == "Can't have 2 overloads with same arity")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
