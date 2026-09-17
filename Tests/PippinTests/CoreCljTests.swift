// @ai-generated(guided)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

extension CoreTests {
	@Suite struct CoreCljTests {
		private func declare(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func embeddedSourceMatchesTheFile() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				let file = URL(fileURLWithPath: #filePath)
					.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
					.appendingPathComponent("Sources/CljCore/boot/core.clj")
				let expected = try Data(contentsOf: file)
				var len = 0
				let bytes = clj_core_source(&len)
				let embedded = Data(bytes: bytes!, count: len)
				#expect(embedded == expected, "core_clj.inc is stale: run `make boot` and commit it")
				#expect(len > 0)
				let libs = file.deletingLastPathComponent().appendingPathComponent("clojure")
				let names = try FileManager.default.contentsOfDirectory(atPath: libs.path).filter { $0.hasSuffix(".clj") }.sorted()
				#expect(names == ["set.clj", "string.clj", "template.clj", "test.clj", "walk.clj"])
				for name in names {
					let source = try Data(contentsOf: libs.appendingPathComponent(name))
					var libLen = 0
					let bytes = clj_embedded_source("clojure/" + name.dropLast(4), &libLen)
					#expect(bytes != nil && Data(bytes: bytes!, count: libLen) == source, "libs_clj.inc is stale for \(name): run `make boot` and commit it")
				}
				#expect(clj_embedded_source("clojure/nope", &len) == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func macrosLiveInCore() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(macroexpand '(when a b))").description == "(if a (do b))")
				#expect(try eval("(macroexpand-1 '(when a b c))").description == "(if a (do b c))")
				#expect(try eval("#'when").description == "#'clojure.core/when")
				#expect(try eval("#'clojure.core/defn").description == "#'clojure.core/defn")
				#expect(message("when") == "Can't take value of a macro: #'clojure.core/when")
				#expect(try eval("(let [when (fn [a b] b)] (when 1 2))") == 2)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func defn() throws {
			clj_init()
			_ = kw("done")
			try declare("cc-fact", "cc-doc", "cc-many", "cc-rec")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(defn cc-fact ([n] (cc-fact n 1)) ([n acc] (if (<= n 1) acc (cc-fact (dec n) (* acc n)))))").description == "#'user/cc-fact")
				#expect(try eval("(cc-fact 10)") == 3628800)
				#expect(try eval("(defn cc-doc \"adds one\" [x] (inc x)) (cc-doc 1)") == 2)
				#expect(try eval("(defn cc-many [a & more] [a more]) (cc-many 1 2 3)") == [1, Value(list: [2, 3])])
				#expect(try eval("(defn cc-rec [n] (if (zero? n) :done (recur (dec n)))) (cc-rec 100000)") == kw("done"))
				#expect(try eval("(str cc-fact)") == "#object[fn user/cc-fact]")
				#expect(message("(cc-fact)") == "Wrong number of args (0) passed to: user/cc-fact")
				#expect(message("(defn cc-bad)") == "Parameter declaration missing")
				#expect(message("(defn cc-bad 1)") == "Parameter declaration 1 should be a vector")
				#expect(message("(defn)") == "Wrong number of args (2) passed to: clojure.core/defn")
				try unbind("cc-fact", "cc-doc", "cc-many", "cc-rec")
			}
			#expect(clj_debug_live_objects() == before + 3) // cc-bad: the var, its name symbol and the name string
		}

		@Test func conditionals() throws {
			clj_init()
			for k in ["a", "b", "c", "else", "x", "y", "no", "never", "small", "medium", "large"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(when true 1 2) (when false 1) (when nil)]") == [2, nil, nil])
				#expect(try eval("[(when-not false 1 2) (when-not true 1)]") == [2, nil])
				#expect(try eval("[(if-not false :a :b) (if-not true :a :b) (if-not nil :a) (if-not 1 :a)]") == [kw("a"), kw("b"), kw("a"), nil])
				#expect(try eval("(cond false 1 nil 2 :else 3)") == 3)
				#expect(try eval("(cond (= 1 1) :a :else :b)") == kw("a"))
				#expect(try eval("(cond false 1)") == nil)
				#expect(try eval("(cond)") == nil)
				#expect(try eval("(let [x 5] (cond (< x 3) :small (< x 10) :medium :else :large))") == kw("medium"))
				#expect(message("(cond true)") == "cond requires an even number of forms")
				#expect(try eval("(macroexpand '(cond a b))").description == "(if a b (clojure.core/cond))")
				#expect(try eval("[(if-let [x 1] (inc x) :no) (if-let [x nil] x :no) (if-let [x false] x)]") == [2, kw("no"), nil])
				#expect(try eval("[(when-let [x (first [3 4])] (inc x) (* x 2)) (when-let [x nil] :never)]") == [6, nil])
				#expect(try eval("(when-let [x 1] (when-let [y (inc x)] [x y]))") == [1, 2])
				#expect(try eval("(let [temp 9] (if-let [t temp] t))") == 9)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func andOrShortCircuit() throws {
			clj_init()
			for k in ["x", "y", "once"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(and) (and 1) (and 1 2) (and 1 nil 3) (and false 1) (and 1 false)]") == [true, 1, 2, nil, false, false])
				#expect(try eval("[(or) (or 1) (or nil 2) (or nil false) (or false nil) (or nil :x :y)]") == [nil, 1, 2, false, nil, kw("x")])
				var result: Value = nil
				var out = try capturingOutput {
					result = try eval("(and (do (println 1) false) (do (println 2) true))")
				}
				#expect(result == false)
				#expect(out == "1\n")
				out = try capturingOutput {
					result = try eval("(or (do (println 1) nil) (do (println 2) :x) (do (println 3) :y))")
				}
				#expect(result == kw("x"))
				#expect(out == "1\n2\n")
				out = try capturingOutput {
					result = try eval("(and (do (println 1) 1) (do (println 2) 2))")
				}
				#expect(result == 2)
				#expect(out == "1\n2\n")
				// The deciding value is evaluated once.
				out = try capturingOutput {
					result = try eval("(or (do (println :once) 5))")
				}
				#expect(result == 5)
				#expect(out == ":once\n")
				#expect(try eval("(let [x 1] (and x (or nil x) (and)))") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func threading() throws {
			clj_init()
			for k in ["a", "b"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(-> 5 inc (- 2) (list 1))") == Value(list: [4, 1]))
				#expect(try eval("(-> 5)") == 5)
				#expect(try eval("(-> {:a {:b 1}} :a :b)") == 1)
				#expect(try eval("(-> [1 2 3] (conj 4) count)") == 4)
				#expect(try eval("(->> 5 (- 10) inc)") == 6)
				#expect(try eval("(->> [1 2] (into [0]) (count))") == 3)
				#expect(try eval("(->> 1 (list 2 3) (cons 0))") == Value(list: [0, 2, 3, 1]))
				#expect(try eval("(macroexpand '(-> x (f a) g))").description == "(g (f x a))")
				#expect(try eval("(macroexpand '(->> x (f a) g))").description == "(g (f a x))")
				#expect(try eval("(-> 1 (->> (- 10)) (- 1))") == 8)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func loopsAssertionsAndDeclarations() throws {
			clj_init()
			for k in ["k"] { _ = kw(k) }
			try declare("cc-a", "cc-b", "cc-c")
			let before = clj_debug_live_objects()
			do {
				let out = try capturingOutput { _ = try eval("(dotimes [i 3] (println i))") }
				#expect(out == "0\n1\n2\n")
				#expect(try eval("(dotimes [i 0] (println i))") == nil)
				#expect(try eval("(let [n 2] (dotimes [i n] i))") == nil)
				#expect(try eval("(comment (nope) (defmacro nope []))") == nil)
				#expect(try eval("(assert true)") == nil)
				#expect(try eval("(assert 1 \"msg\")") == nil)
				#expect(message("(assert (= 1 2))") == "Assert failed: (= 1 2)")
				#expect(message("(assert nil \"must hold\")") == "Assert failed: must hold\\nnil")
				#expect(try eval("(declare cc-a cc-b)").description == "#'user/cc-b")
				#expect(try eval("(declare)") == nil)
				#expect(message("cc-a") == "Unbound var: #'user/cc-a")
				#expect(try eval("(defn cc-a [] (cc-b)) (defn cc-b [] :k) (cc-a)") == kw("k"))
				#expect(try eval("(macroexpand '(declare x y))").description == "(do (def x) (def y))")
				try unbind("cc-a", "cc-b", "cc-c")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func endToEndThroughRuntime() throws {
			let rt = Runtime()
			for k in ["evens", "sum"] { _ = kw(k) }
			_ = try rt.eval("(def cc-sum) (def cc-evens)")
			let before = clj_debug_live_objects()
			do {
				let program = """
				(defn cc-sum
				  "Adds the numbers in coll."
				  [coll]
				  (loop [acc 0 coll (seq coll)]
				    (if coll
				      (recur (+ acc (first coll)) (next coll))
				      acc)))

				(defn cc-evens [coll]
				  (loop [acc [] coll (seq coll)]
				    (cond
				      (empty? coll) acc
				      (even? (first coll)) (recur (conj acc (first coll)) (next coll))
				      :else (recur acc (next coll)))))

				(let [xs [1 2 3 4 5 6]]
				  (when-let [evens (seq (cc-evens xs))]
				    (println "evens:" evens)
				    {:evens (->> evens (into [])) :sum (-> xs cc-sum (* 2))}))
				"""
				var result: Value = nil
				let out = try capturingOutput { result = try rt.eval(program) }
				#expect(out == "evens: (2 4 6)\n")
				#expect(try result == Value(reading: "{:evens [2 4 6] :sum 42}"))
				_ = try rt.eval("(def cc-sum nil) (def cc-evens nil)")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The 1.11/1.12 seq and map fns, against what JVM Clojure answers.
		@Test func laterCoreFns() throws {
			clj_init()
			for k in ["a", "b", "c", "m", "pad", "p", "stop", "ten", "v", "next", "clojure.core/halt", "halt"] { _ = kw(k) }
			// A first call warms a site (a reify type, a call cache) once for the process; the baseline follows it.
			_ = try eval("(update-keys {:a 1} name) (update-vals {:a 1} inc) (vec (iteration (fn [k] (when (< k 1) 1)) :initk 0))")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(partition 2 3 [:pad] (range 7))").description == "((0 1) (3 4) (6 :pad))")
				#expect(try eval("(partition 3 3 [] (range 5))").description == "((0 1 2) (3 4))")
				#expect(try eval("(partition 3 3 (range 10 20) (range 5))").description == "((0 1 2) (3 4 10))")
				#expect(try eval("(partition 2 3 nil (range 5))").description == "((0 1) (3 4))")
				#expect(try eval("(partitionv 2 (range 5))").description == "([0 1] [2 3])")
				#expect(try eval("(partitionv 2 3 (range 7))").description == "([0 1] [3 4])")
				#expect(try eval("(partitionv 2 3 [:p] (range 7))").description == "([0 1] [3 4] [6 :p])")
				#expect(try eval("(vector? (first (partitionv 2 [1 2 3])))") == true)
				#expect(try eval("(partitionv-all 2 (range 5))").description == "([0 1] [2 3] [4])")
				#expect(try eval("(partitionv-all 2 3 (range 7))").description == "([0 1] [3 4] [6])")
				#expect(try eval("(into [] (partitionv-all 2) (range 5))").description == "[[0 1] [2 3] [4]]")
				#expect(try eval("(partitionv-all 2 [])").description == "()")
				#expect(try eval("(splitv-at 2 (range 5))").description == "[[0 1] (2 3 4)]")
				#expect(try eval("(splitv-at 9 [1 2])").description == "[[1 2] ()]")
				#expect(try eval("(reductions + [1 2 3 4])").description == "(1 3 6 10)")
				#expect(try eval("(reductions + 10 [1 2 3])").description == "(10 11 13 16)")
				#expect(try eval("(reductions + [])").description == "(0)")
				#expect(try eval("(reductions + 5 [])").description == "(5)")
				#expect(try eval("(reductions conj [] [1 2])").description == "([] [1] [1 2])")
				#expect(try eval("(take 4 (reductions + (range)))").description == "(0 1 3 6)")
				#expect(try eval("(reductions (fn [acc x] (if (> x 2) (reduced :stop) (+ acc x))) 0 [1 2 3 4])").description == "(0 1 3 :stop)")
				#expect(try eval("(reductions + (reduced 7) [1 2])").description == "(7)")
				#expect(try eval("(into [] (halt-when neg?) [1 2 -3 4])") == -3)
				#expect(try eval("(into [] (halt-when neg? (fn [acc x] [acc x])) [1 2 -3 4])") == [[1, 2], -3])
				#expect(try eval("(into [] (halt-when neg?) [1 2 3])") == [1, 2, 3])
				#expect(try eval("(transduce (comp (map inc) (halt-when #(> % 3))) conj [] [1 2 3 4])") == 4)
				#expect(try eval("(into [] (halt-when neg? (fn [acc x] (conj acc x))) [1 -2 3])") == [1, -2])
				#expect(try eval("[(count (random-sample 1.0 (range 100))) (count (random-sample 0.0 (range 100))) (count (into [] (random-sample 1.0) (range 5)))]") == [100, 0, 5])
				#expect(try eval("(let [n (count (random-sample 0.5 (range 1000)))] (< 300 n 700))") == true)
				#expect(try eval("(every? #(< -1 % 100) (random-sample 0.5 (range 100)))") == true)
				#expect(try eval("[(bounded-count 3 [1 2 3 4 5]) (bounded-count 3 (range)) (bounded-count 3 (list 1 2)) (bounded-count 10 (map inc [1 2 3])) (bounded-count 0 (range)) (bounded-count 2 nil)]") == [5, 3, 2, 3, 0, 0])
				#expect(try eval("[(boolean? true) (boolean? false) (boolean? nil) (boolean? 0) (boolean? \"true\")]") == [true, true, false, false, false])
				#expect(try eval("[(parse-boolean \"true\") (parse-boolean \"false\") (parse-boolean \"TRUE\") (parse-boolean \" true\") (parse-boolean \"\")]") == [true, false, nil, nil, nil])
				#expect(message("(parse-boolean nil)") == "nil cannot be cast to a string")
				#expect(message("(parse-boolean true)") == "boolean cannot be cast to a string")
				#expect(message("(parse-boolean 1)") == "long cannot be cast to a string")
				#expect(try eval("[(reversible? [1]) (reversible? (sorted-map)) (reversible? (sorted-set 1)) (reversible? '(1)) (reversible? {}) (reversible? nil) (reversible? (range 3))]") == [true, true, true, false, false, false, false])
				#expect(try eval("(replicate 3 :a)").description == "(:a :a :a)")
				#expect(try eval("(replicate 0 :a)").description == "()")
				#expect(try eval("(lazy-cat [1 2] (list 3) (range 4 6))").description == "(1 2 3 4 5)")
				#expect(try eval("(lazy-cat)").description == "()")
				#expect(try eval("(take 3 (lazy-cat [1] (range)))").description == "(1 0 1)")
				#expect(try eval("(let [a (atom 0)] (lazy-cat [1] (do (swap! a inc) [2])) @a)") == 0)
				#expect(try eval("(let [a (atom 0)] [(doall (lazy-cat [1] (do (swap! a inc) [2]))) @a])").description == "[(1 2) 1]")
				#expect(try eval("(update-keys {:a 1 :b 2} name)") == ["a": 1, "b": 2])
				#expect(try eval("(update-keys {} inc)") == [:])
				#expect(try eval("(meta (update-keys (with-meta {:a 1} {:m 1}) name))") == [kw("m"): 1])
				#expect(try eval("(update-keys [10 20] inc)") == [1: 10, 2: 20])
				#expect(try eval("(update-vals {:a 1 :b 2} inc)") == [kw("a"): 2, kw("b"): 3])
				#expect(try eval("(update-vals (sorted-map :b 1 :a 2) inc)").description == "{:a 3, :b 2}")
				#expect(try eval("(sorted? (update-vals (sorted-map :b 1 :a 2) inc))") == false)
				#expect(try eval("(meta (update-vals (with-meta {:a 1} {:m 1}) inc))") == [kw("m"): 1])
				#expect(try eval("(let [m {:a 1 :b 2}] (update-vals m inc) m)") == [kw("a"): 1, kw("b"): 2])
				// iteration: a paginated step fn, seqable and reducible, kf nil ending it.
				#expect(try eval("(let [step (fn [k] (when (< k 3) {:v (* 10 k) :next (inc k)}))] (vec (iteration step :initk 0 :vf :v :kf :next)))") == [0, 10, 20])
				#expect(try eval("(let [step (fn [k] (when (< k 3) {:v (* 10 k) :next (inc k)}))] (reduce + 0 (iteration step :initk 0 :vf :v :kf :next)))") == 30)
				#expect(try eval("(let [step (fn [k] (when (< k 3) {:v (* 10 k) :next (inc k)}))] (reduce (fn [acc v] (if (= v 10) (reduced :ten) acc)) nil (iteration step :initk 0 :vf :v :kf :next)))") == kw("ten"))
				#expect(try eval("(let [step (fn [k] (when (< k 3) {:v (* 10 k) :next (when (< k 1) (inc k))}))] (vec (iteration step :initk 0 :vf :v :kf :next)))") == [0, 10])
				#expect(try eval("(let [step (fn [k] (when (< k 3) {:v (* 10 k) :next (inc k)}))] (seq (iteration step :initk 5 :vf :v :kf :next)))") == nil)
				#expect(try eval("(let [calls (atom 0) step (fn [k] (swap! calls inc) (when (< k 5) (inc k)))] [(first (iteration step :initk 0)) @calls])") == [1, 1])
				#expect(try eval("(vec (iteration (fn [k] (when (< k 3) (inc k))) :initk 0 :somef some?))") == [1, 2, 3])
				#expect(try eval("(into [] (take 2) (iteration (fn [k] (inc k)) :initk 0))") == [1, 2])
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
