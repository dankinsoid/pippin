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
private func m(_ d: [String: Value]) -> Value { Value(Dictionary(uniqueKeysWithValues: d.map { (kw($0.key), $0.value) })) }

private func readError(_ source: String) -> String? {
	do {
		_ = try Value.readAll(source)
		return nil
	} catch {
		return String(describing: error)
	}
}

extension CoreTests {
	@Suite struct SetTests {
		init() {
			clj_init()
			for k in ["m", "nf", "a", "b", "k", "stop", "set"] { _ = kw(k) }
			// The codec interns its keywords on first use.
			if let node = clj_analyze(CLJ_NIL, nil) {
				clj_release(clj_node_to_data(node))
				clj_release(clj_from_ptr(UnsafeMutableRawPointer(node)))
			}
		}

		private func declare(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func literalAndConstructors() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(count #{}) (count #{1 2 3}) (set? #{}) (set? {}) (set? [1]) (set? nil) (coll? #{1}) (counted? #{1})]") == [0, 3, true, false, false, false, true, true])
				#expect(try eval("(= (hash-set 1 2 3) #{3 2 1})") == true)
				#expect(try eval("(= (hash-set 1 1 2) #{1 2})") == true)
				#expect(try eval("[(= (set [1 2 2 3]) #{1 2 3}) (= (set nil) #{}) (= (set \"aab\") #{\\a \\b}) (= (set {:a 1}) #{[:a 1]}) (= (set (range 3)) #{0 1 2})]") == [true, true, true, true, true])
				#expect(try eval("(let [s (with-meta #{1} {:m 1})] [(meta s) (meta (set s)) (= s (set s))])") == Value([m(["m": 1]), nil, true]))
				#expect(try eval("(= (hash #{1 2}) (hash #{2 1}))") == true)
				#expect(try eval("(pr-str #{})") == "#{}")
				#expect(try eval("(pr-str #{1})") == "#{1}")
				#expect(try eval("(let [s (pr-str #{1 2})] (or (= s \"#{1 2}\") (= s \"#{2 1}\")))") == true)
				#expect(try eval("(str #{\"a\"})") == "#{\"a\"}")
				#expect(try eval("(pr-str [#{} {:a #{1}} '(#{})])") == "[#{} {:a #{1}} (#{})]")
				#expect(readError("#{1 1}") == "1:1: Duplicate key: 1")
				#expect(readError("#{1 [1 2] [1 2]}") == "1:1: Duplicate key: [1 2]")
				#expect(readError("#{1 2") == "1:1: EOF while reading")
				#expect(readError("#{1 2]") == "1:6: Unmatched delimiter: ]")
				#expect(readError("{1 #{3}}") == nil)
				#expect(readError("[1 #{2} 3}") == "1:10: Unmatched delimiter: }")
				// A set literal with non-constant items evaluates them and refuses duplicates as a map literal does.
				#expect(try eval("(let [x 1 y 2] (= #{x y (+ x y)} #{1 2 3}))") == true)
				#expect(message("(let [x 1 y 1] #{x y})") == "Duplicate key: 1")
				#expect(try eval("(let [x 1] (count #{x #{x} [x]}))") == 3)
				#expect(message("(disj 1 2)") == "disj not supported on this type: long")
				#expect(message("(disj [1])") == "disj not supported on this type: vector")
				#expect(message("(set 1)") == "Don't know how to create ISeq from: long")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func conjDisjLookup() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(= (conj #{} 1 2 2 3) #{1 2 3})") == true)
				#expect(try eval("(= (disj #{1 2 3} 2 4) #{1 3})") == true)
				#expect(try eval("[(= (disj #{1}) #{1}) (disj nil 1) (= (disj #{1} 1) #{}) (= (disj #{1} 2) #{1})]") == [true, nil, true, true])
				#expect(try eval("[(contains? #{1 2} 1) (contains? #{1 2} 3) (contains? #{nil} nil) (contains? #{} 1)]") == [true, false, true, false])
				#expect(try eval("[(get #{1 2} 1) (get #{1 2} 3) (get #{1 2} 3 :nf) (get #{nil} nil :nf) (#{1 2} 1) (#{1 2} 3) (#{1 2} 3 :nf) (:a #{:a}) (:b #{:a})]") == [1, nil, kw("nf"), nil, 1, nil, kw("nf"), kw("a"), nil])
				// The element the set holds comes back, not the probe.
				#expect(try eval("(let [k [1 2] s #{k}] (identical? k (get s [1 2])))") == true)
				// conj of an equal element keeps the one already there; disj of a missing one is the same set.
				#expect(try eval("(let [k [1 2] s #{k} t (conj s [1 2])] [(identical? s t) (identical? k (get t [1 2]))])") == [true, true])
				#expect(try eval("(let [s #{1}] (identical? s (disj s 2)))") == true)
				#expect(try eval("[(count (into #{} [1 2 2 3])) (= (into #{1} '(2 3)) #{1 2 3}) (= (into #{} (map inc) [1 1 2]) #{2 3})]") == [3, true, true])
				#expect(try eval("[(empty? #{}) (empty? #{1}) (= (empty #{1 2}) #{}) (empty nil) (empty 1) (empty \"s\")]") == [true, false, true, nil, nil, nil])
				#expect(try eval("[(= (empty [1]) []) (= (empty {:a 1}) {}) (= (empty '(1)) ()) (= (empty (seq [1])) ())]") == [true, true, true, true])
				#expect(try eval("(meta (empty (with-meta #{1} {:a 1})))") == m(["a": 1]))
				#expect(message("(#{1})") == "Wrong number of args (0) passed to: #{1}")
				#expect(message("(#{1} 1 2 3)") == "Wrong number of args (3) passed to: #{1}")
				#expect(message("(nth #{1} 0)") == "nth not supported on this type: set")
				#expect(message("(assoc #{1} 1 2)") == "assoc not supported on this type: set")
				#expect(message("(dissoc #{1} 1)") == "dissoc not supported on this type: set")
				#expect(message("(conj #{} #{})") == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func seqReduceAndEquality() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(seq #{}) (= (set (seq #{1 2 3})) #{1 2 3}) (count (seq #{1 2 3})) (seq? (seq #{1})) (first #{7}) (next #{7}) (rest #{7})]") == [nil, true, 3, true, 7, nil, []])
				#expect(try eval("[(reduce + #{1 2 3}) (reduce + 10 #{1 2 3}) (reduce + #{}) (reduce conj [] #{1}) (reduce (fn [a x] (reduced :stop)) 0 #{1 2})]") == [6, 16, 0, [1], kw("stop")])
				#expect(try eval("[(satisfies? IReduceInit #{}) (satisfies? IPersistentSet #{}) (satisfies? IPersistentSet {}) (satisfies? IPersistentCollection #{}) (satisfies? Counted #{}) (satisfies? IFn #{})]") == [true, true, false, true, true, true])
				#expect(try eval("[(satisfies? ILookup #{}) (satisfies? Associative #{}) (satisfies? Sequential #{}) (satisfies? Seqable #{}) (satisfies? IObj #{})]") == [false, false, false, true, true])
				#expect(try eval("[(instance? PersistentHashSet #{}) (instance? PersistentHashSet {}) (instance? IPersistentSet #{}) (= (type #{}) PersistentHashSet) (pr-str (type #{}))]") == [true, false, true, true, "set"])
				#expect(try eval("[(= #{1 2} #{2 1}) (= #{1 2} #{1}) (= #{1 2} [1 2]) (= #{1 2} '(1 2)) (= #{[1 2]} {1 2})]") == [true, false, false, false, false])
				#expect(try eval("[(= [1 2] #{1 2}) (= #{} #{}) (= #{} []) (= #{#{1}} #{#{1}}) (not= #{1} #{2})]") == [false, true, false, true, true])
				#expect(try eval("[(= (hash #{1 2 3}) (hash #{3 1 2})) (= (hash #{}) (hash #{})) (not= (hash #{1}) (hash #{2})) (not= (hash #{1 2}) (hash [1 2])) (= (hash #{1 2}) (hash (conj #{1} 2)))]") == [true, true, true, true, true])
				#expect(try eval("(let [m {#{1 2} :a} ] [(get m #{2 1}) (get m #{1})])") == [kw("a"), nil])
				#expect(try eval("(count (into #{} [#{1 2} #{2 1} [1 2] '(1 2)]))") == 2)
				#expect(try eval("[(sequential? #{}) (associative? #{}) (ifn? #{}) (seqable? #{}) (seq? #{}) (map? #{}) (vector? #{})]") == [false, false, true, true, false, false, false])
				#expect(try eval("(macroexpand-1 '(f #{1}))").description == "(f #{1})")
				let expansion = try Value(reading: "`#{1 ~(+ 1 1)}").description
				#expect(expansion.hasPrefix("(clojure.core/apply clojure.core/hash-set (clojure.core/seq (clojure.core/concat "), Comment(rawValue: expansion))
				#expect(expansion.contains("(clojure.core/list 1)") && expansion.contains("(clojure.core/list (+ 1 1))"), Comment(rawValue: expansion))
				#expect(try eval("(= `#{1 ~(+ 1 1)} #{1 2})") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func metaAndSharing() throws {
			clj_init()
			try declare("st-shared")
			let before = clj_debug_live_objects()
			do {
				let a1 = m(["a": 1])
				#expect(try eval("(let [s (with-meta #{1} {:a 1})] [(meta s) (meta (conj s 2)) (meta (disj s 1)) (= s #{1}) (meta (with-meta s nil)) (meta (vary-meta s assoc :b 2))])") == Value([a1, a1, a1, true, nil, m(["a": 1, "b": 2])]))
				// with-meta on a shared wrapper copies it; the copy shares the trie.
				#expect(try eval("(let [s #{1 2} t (with-meta s {:m 1})] [(= s t) (meta s) (meta t) (count t)])") == Value([true, nil, m(["m": 1]), 2]))
				_ = try eval("(def st-shared #{1 [2] {:k #{3}}})")
				let stored = try eval("st-shared")
				#expect(withExtendedLifetime(stored) { clj_debug_all_shared(stored.raw) })
				// conj on the published set copies the wrapper and shares nothing new into the old one.
				#expect(try eval("(let [t (conj st-shared [4 5])] [(count st-shared) (count t) (contains? t [4 5]) (contains? st-shared [4 5])])") == [3, 4, true, false])
				try unbind("st-shared")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// conj and disj on a last-use local update the trie in place, as assoc does on a map.
		@Test func inPlaceThroughLastUse() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				let consuming = clj_debug_consuming_calls()
				#expect(try eval("(loop [s #{} i 0] (if (< i 100) (recur (conj s i) (inc i)) (count s)))") == 100)
				#expect(try eval("(loop [s (set (range 100)) i 0] (if (< i 100) (recur (disj s i) (inc i)) (count s)))") == 0)
				#expect(try eval("(let [s #{1 2 3}] (count (disj (disj s 1) 2)))") == 1)
				#expect(try eval("(count (reduce conj #{} (range 50)))") == 50)
				#expect(try eval("(count (reduce disj (set (range 50)) (range 25)))") == 25)
				let expected: Int64 = 276
				if consuming >= 0 { #expect(clj_debug_consuming_calls() - consuming >= expected) }
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func distinctGroupByFrequencies() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(distinct [1 2 1 3 2 4])") == [1, 2, 3, 4])
				#expect(try eval("[(distinct []) (distinct nil) (distinct [nil nil 1])]") == [[], [], [nil, 1]])
				#expect(try eval("(pr-str (distinct \"abca\"))") == "(\\a \\b \\c)")
				#expect(try eval("(take 3 (distinct (mapcat (fn [x] [x x]) (range))))") == [0, 1, 2])
				#expect(try eval("(let [d (distinct [1 1 2])] [(realized? d) (first d) (realized? d)])") == [false, 1, true])
				#expect(try eval("(into [] (distinct) [1 1 2 3 3])") == [1, 2, 3])
				#expect(try eval("(transduce (distinct) + [1 1 2 3 3])") == 6)
				#expect(try eval("(sequence (distinct) [1 1 2])") == [1, 2])
				#expect(try eval("(vec (distinct (interpose 0 [1 2 3])))") == [1, 0, 2, 3])
				#expect(try eval("(= (group-by even? [1 2 3 4]) {true [2 4] false [1 3]})") == true)
				#expect(try eval("(= (group-by count [\"a\" \"bb\" \"c\"]) {1 [\"a\" \"c\"] 2 [\"bb\"]})") == true)
				#expect(try eval("[(group-by inc []) (group-by inc nil)]") == Value([m([:]), m([:])]))
				#expect(try eval("(= (frequencies [1 2 1 3 1]) {1 3 2 1 3 1})") == true)
				#expect(try eval("(= (frequencies \"aab\") {\\a 2 \\b 1})") == true)
				#expect(try eval("(frequencies [])") == m([:]))
				#expect(try eval("(= (frequencies #{1 2}) {1 1 2 1})") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// clojure.set is an embedded namespace loaded on the first require, so the load precedes the baseline.
		@Test func setOperations() throws {
			clj_init()
			_ = try eval("(require '[clojure.set :as cset]) [:id :n :v :x :a :b]")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(= (cset/union) #{}) (= (cset/union #{1}) #{1}) (= (cset/union #{1 2} #{2 3}) #{1 2 3}) (= (cset/union #{1} #{2} #{3} #{1}) #{1 2 3}) (= (cset/union #{} #{1 2 3}) #{1 2 3}) (= (cset/union #{1 2 3} #{}) #{1 2 3})]") == [true, true, true, true, true, true])
				#expect(try eval("[(= (cset/intersection #{1 2 3} #{2 3 4}) #{2 3}) (= (cset/intersection #{1} #{2}) #{}) (= (cset/intersection #{1 2 3} #{2 3 4} #{3}) #{3}) (= (cset/intersection #{1 2 3 4 5} #{5 1}) #{1 5}) (= (cset/intersection #{1}) #{1})]") == [true, true, true, true, true])
				#expect(try eval("[(= (cset/difference #{1 2 3} #{2}) #{1 3}) (= (cset/difference #{1 2 3} #{1 2 3 4 5}) #{}) (= (cset/difference #{1 2 3} #{2} #{3}) #{1}) (= (cset/difference #{1 2 3 4 5} #{2 3}) #{1 4 5}) (= (cset/difference #{1}) #{1})]") == [true, true, true, true, true])
				#expect(try eval("[(cset/subset? #{1} #{1 2}) (cset/subset? #{1 3} #{1 2}) (cset/subset? #{} #{}) (cset/subset? #{1 2} #{1}) (cset/superset? #{1 2} #{1}) (cset/superset? #{1} #{1 2}) (cset/superset? #{1 2} #{1 2})]") == [true, false, true, false, true, false, true])
				// The set that comes back keeps the first argument's meta, as clojure.set's do.
				#expect(try eval("(meta (cset/union (with-meta #{1} {:a 1}) #{2}))") == m(["a": 1]))
				#expect(try eval("(= (cset/select even? #{1 2 3 4}) #{2 4})") == true)
				#expect(try eval("(= (cset/rename-keys {:a 1 :b 2} {:a :x}) {:x 1 :b 2})") == true)
				#expect(try eval("(= (cset/map-invert {:a 1 :b 2}) {1 :a 2 :b})") == true)
				#expect(try eval("(= (cset/project #{{:a 1 :b 2} {:a 3 :b 4}} [:a]) #{{:a 1} {:a 3}})") == true)
				#expect(try eval("(= (cset/index #{{:a 1 :b 2} {:a 1 :b 3}} [:a]) {{:a 1} #{{:a 1 :b 2} {:a 1 :b 3}}})") == true)
				#expect(try eval("(= (cset/join #{{:id 1 :n \"a\"}} #{{:id 1 :v 2}}) #{{:id 1 :n \"a\" :v 2}})") == true)
				#expect(try eval("(= (cset/rename #{{:a 1}} {:a :b}) #{{:b 1}})") == true)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A set constant survives the tree codec; a set literal with locals is its own node kind.
		@Test func codec() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				let form = try Value(reading: "(let [x 1] [#{1 2} #{x}])")
				var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
				let node = try #require(withExtendedLifetime(form) { clj_analyze(form.raw, &env) })
				defer { clj_release(clj_from_ptr(UnsafeMutableRawPointer(node))) }
				let data = Value(owning: clj_node_to_data(node))
				let text = data.description
				#expect(text.contains("[:set [:local 0"))
				#expect(text.contains("[:const #{"))
				let read = try Value(reading: text)
				let back = try #require(withExtendedLifetime(read) { clj_node_from_data(read.raw) })
				defer { clj_release(clj_from_ptr(UnsafeMutableRawPointer(back))) }
				#expect(Value(owning: clj_node_to_data(back)).description == text)
				let exec = clj_exec_new(back)
				defer { clj_release(exec) }
				let result = Value(owning: clj_exec_run(exec))
				#expect(result.description == "[#{1 2} #{1}]" || result.description == "[#{2 1} #{1}]")
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
