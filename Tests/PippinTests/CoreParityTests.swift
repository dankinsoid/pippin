// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'parity-tests) " + source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalErrorScoped("(in-ns 'parity-tests) " + source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

// The clojure.core names docs/api-parity.md kept for code: where the JVM answers, the expectation is its answer.
extension CoreTests {
	@Suite(.serialized) struct CoreParityTests {
		private static let dir = FileManager.default.temporaryDirectory.appendingPathComponent("pippin-parity-\(getpid())")

		init() throws {
			clj_init()
			try? FileManager.default.createDirectory(at: Self.dir.appendingPathComponent("sub"), withIntermediateDirectories: true)
			FileManager.default.createFile(atPath: Self.dir.appendingPathComponent("sub/z.txt").path, contents: Data("z".utf8))
			for k in ["a", "b", "c", "k", "x", "ok", "no-test", "kind", "major", "minor", "incremental", "qualifier", "via", "trace", "cause",
			          "data", "type", "message", "at", "tag", "form", "q/a", "q/b", "ns/x", "ns/y", "my/kind", "other", "m", "ns/x"] { _ = kw(k) }
			_ = try cljEvalScoped("""
			(ns parity-tests)
			(def tmp "\(Self.dir.path)")
			(defn ^{:test (fn [] (assert true))} tested [] 1)
			(declare sp-seen)
			(defn msg [f] (try (f) (catch :default e (ex-message e))))
			(deftype Pt [x y])
			(defmethod print-method Pt [p w] (.write w "#pt") (print-method [1 "s"] w))
			(defrecord Rec [a])
			(defmethod print-method :my/kind [x w] (.write w "<kind>"))
			(defprotocol Lines (-readLine [r]) (-close [r]))
			(deftype LineReader [st closed]
			  Lines
			  (-readLine [_] (let [[l & more] @st] (vreset! st more) l))
			  (-close [_] (vreset! closed true)))
			(deftype MyInst [ms] Inst (inst-ms* [_] ms))
			;; get-method caches a :type keyword's dispatch the first time it prints.
			[(pr-str (->Pt 1 2) (->Rec 1) (with-meta [1] {:type :my/kind}) (with-meta [1] {:type :other}) (tagged-literal 'a/b {:x 1}))
			 (binding [*print-meta* true *print-namespace-maps* true] (pr-str (with-meta [1] {:a 1}) {:q/a 1}))
			 (with-out-str (.write *out* "x")) (chunk-cons (chunk (chunk-buffer 1)) nil) (Throwable->map (ex-info "x" {}))
			 (hash-ordered-coll [1]) (unchecked-byte 300) (ints nil) (time 1) (find-keyword "a") (replace {1 2} [1])]
			""")
		}

		@Test func portableCore() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(replace {1 :a} [1 2 1]) (replace {1 :a} '(1 2 1)) (into [] (replace {1 :a}) [1 2]) (replace [:x :y] [0 1 5])]") == [[kw("a"), 2, kw("a")], [kw("a"), 2, kw("a")], [kw("a"), 2], [kw("x"), kw("y"), 5]])
				#expect(try eval("[((comparator <) 1 2) ((comparator <) 2 1) ((comparator <) 1 1)]") == [-1, 1, 0])
				#expect(try eval("[(test #'tested) (test #'prn)]") == [kw("ok"), kw("no-test")])
				#expect(try eval("(with-out-str (time (+ 1 2)))").string?.hasPrefix("\"Elapsed time: ") == true)
				#expect(try eval("[*clojure-version* (clojure-version) *repl* *command-line-args* (binding [*command-line-args* [\"a\"]] *command-line-args*) (:dynamic (meta #'*command-line-args*))]") == [[kw("major"): 1, kw("minor"): 12, kw("incremental"): 6, kw("qualifier"): nil], "1.12.6", false, nil, ["a"], nil])
				// Clojure 1.11's keyword arguments: pairs, a map alone, or pairs then a map whose entries win.
				#expect(try eval("[(seq-to-map-for-destructuring nil) (seq-to-map-for-destructuring '({:a 1})) (seq-to-map-for-destructuring '(:a 1 :b 2)) (seq-to-map-for-destructuring '(:a 1 {:b 2 :a 5})) (seq-to-map-for-destructuring '(5))]") == [[:], [kw("a"): 1], [kw("a"): 1, kw("b"): 2], [kw("a"): 5, kw("b"): 2], 5])
				#expect(try eval("[((fn [& {:keys [a b]}] [a b]) :a 1 {:b 2}) ((partial (fn [n & {:keys [a b]}] (+ n a b)) 100 :a 1) {:a 5 :b 2})]") == [[1, 2], 107])
				#expect(try eval("(msg #(seq-to-map-for-destructuring '(:a 1 :b)))") == "Don't know how to create ISeq from: keyword")
				#expect(try eval("[(find-var 'clojure.core/map) (find-var 'clojure.core/never-a-var) (msg #(find-var 'map)) (msg #(find-var 'nope.ns/x))]").description == "[#'clojure.core/map nil \"Symbol must be namespace-qualified\" \"No such namespace: nope.ns\"]")
				#expect(try eval("[(requiring-resolve 'clojure.core/inc) (msg #(requiring-resolve 'inc))]").description == "[#'clojure.core/inc \"Not a qualified symbol: inc\"]")
				#expect(try eval("[(find-keyword \"a\") (find-keyword 'a) (find-keyword nil \"a\") (find-keyword :x) (find-keyword \"parity-never-interned\") (find-keyword 1)]") == [kw("a"), kw("a"), kw("a"), kw("x"), nil, nil])
				#expect(try eval("[(inst-ms* #inst \"2020-01-01T00:00:00.000-00:00\") (inst? (->MyInst 5)) (inst-ms (->MyInst 5)) (inst? 5) (satisfies? Inst #inst \"2020\")]") == [1577836800000, true, 5, false, true])
				#expect(try eval("(let [t (tagged-literal 'foo [1 2])] [(tagged-literal? t) (tagged-literal? 1) (:tag t) (:form t) (= t (tagged-literal 'foo [1 2])) (pr-str t)])") == [true, false, Value(symbol: "foo"), [1, 2], true, "#foo [1 2]"])
				#expect(try eval("(let [m (Throwable->map (ex-info \"outer\" {:b 2} (ex-info \"inner\" {:a 1})))] [(:cause m) (:data m) (mapv :message (:via m)) (mapv :data (:via m)) (mapv :type (:via m)) (vector? (:trace m))])").description == "[\"inner\" {:a 1} [\"outer\" \"inner\"] [{:b 2} {:a 1}] [clojure.lang.ExceptionInfo clojure.lang.ExceptionInfo] true]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A removed namespace object stays alive, as every namespace is immortal, so this one takes no baseline.
		@Test func namespacesAndAliases() throws {
			#expect(message("(remove-ns 'clojure.core)") == "Cannot remove clojure namespace")
			#expect(try eval("(do (create-ns 'parity.gone) [(some? (find-ns 'parity.gone)) (ns-name (remove-ns 'parity.gone)) (find-ns 'parity.gone) (remove-ns 'parity.never) (contains? (set (map ns-name (all-ns))) 'parity.gone)])") == [true, Value(symbol: "parity.gone"), nil, nil, false])
			#expect(try eval("(do (alias 'pset 'clojure.core) [(contains? (ns-aliases *ns*) 'pset) (ns-unalias *ns* 'pset) (contains? (ns-aliases *ns*) 'pset)])") == [true, nil, false])
		}

		// Every expected value is what JVM Clojure 1.12.6 answers; a Float result is the double the float holds.
		@Test func uncheckedCoercionsAndIntArithmetic() throws {
			let before = clj_debug_live_objects()
			do {
				let inputs = "[1 -1 300 70000 2147483648 -9223372036854775808 1.9 -1.9 1e20 ##NaN 3/2 -7/2 10000000000000000000000N 123456789012M]"
				#expect(try eval("(mapv unchecked-byte \(inputs))") == [1, -1, 44, 112, 0, 0, 1, -1, -1, 0, 1, -3, 0, 20])
				#expect(try eval("(mapv unchecked-short \(inputs))") == [1, -1, 300, 4464, 0, 0, 1, -1, -1, 0, 1, -3, 0, 6676])
				#expect(try eval("(mapv unchecked-int \(inputs))") == [1, -1, 300, 70000, -2147483648, 0, 1, -1, 2147483647, 0, 1, -3, -1304428544, -1097262572])
				#expect(try eval("(mapv unchecked-long \(inputs))") == [1, -1, 300, 70000, 2147483648, Value(Int.min), 1, -1, Value(Int.max), 0, 1, -3, 1864712049423024128, 123456789012])
				#expect(try eval("(mapv unchecked-char [65 -1 300 70000 1.9 \\a])") == [Value("A" as Unicode.Scalar), Value("\u{FFFF}" as Unicode.Scalar), Value("\u{12C}" as Unicode.Scalar), Value("\u{1170}" as Unicode.Scalar), Value("\u{1}" as Unicode.Scalar), Value("a" as Unicode.Scalar)])
				#expect(try eval("[(unchecked-int \\a) (unchecked-float 1.5) (unchecked-double 3/2) (unchecked-float 1e40)]") == [97, 1.5, 1.5, Value(Double.infinity)])
				#expect(try eval("[(msg #(unchecked-long \\a)) (msg #(unchecked-byte nil))]") == ["char cannot be cast to a number", "nil cannot be cast to a number"])
				#expect(try eval("[(unchecked-add-int 2147483647 1) (unchecked-subtract-int -2147483648 1) (unchecked-multiply-int 65536 65536) (unchecked-multiply-int -2147483648 -1) (unchecked-add-int 1.5 1)]") == [-2147483648, 2147483647, 0, -2147483648, 2])
				#expect(try eval("[(unchecked-divide-int 7 2) (unchecked-divide-int -7 2) (unchecked-divide-int -2147483648 -1) (unchecked-remainder-int -7 2) (unchecked-remainder-int -2147483648 -1)]") == [3, -3, -2147483648, -1, 0])
				#expect(try eval("[(unchecked-negate-int -2147483648) (unchecked-inc-int 2147483647) (unchecked-dec-int -2147483648)]") == [-2147483648, -2147483648, 2147483647])
				#expect(try eval("[(msg #(unchecked-add-int 2147483648 1)) (msg #(unchecked-divide-int 1 0))]") == ["Value out of range for int: 2147483648", "Divide by zero"])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func arrayCasts() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(ints nil) (alength (ints (int-array 2))) (alength (longs (long-array 1))) (alength (doubles (double-array 1))) (alength (floats (float-array 1))) (alength (shorts (short-array 1))) (alength (chars (char-array 1))) (alength (booleans (boolean-array 1))) (alength (bytes (byte-array 3)))]") == [nil, 2, 1, 1, 1, 1, 1, 1, 3])
				#expect(message("(ints (long-array 2))") == "long array cannot be cast to a int array")
				#expect(message("(longs [1])") == "vector cannot be cast to a long array")
				#expect(try eval("[(bytes? (byte-array 1)) (bytes? (int-array 1)) (bytes? nil) (bytes? [1])]") == [true, false, false, false])
				#expect(try eval("(mapv vec (to-array-2d [[1 2] [3] []]))") == [[1, 2], [3], []])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// They agree with this core's hash, which is ours; jvm-hash is the JVM's (design §10).
		@Test func collectionHashesAgreeWithHash() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(every? (fn [v] (= (hash v) (hash (apply list v)) (hash-ordered-coll v) (hash-ordered-coll (apply list v)))) [[] [1 2 3] [nil \"a\" :b 'c 1.5 2N 3/4 1.5M \\x] [[1 2] {:a 1} #{3}] (vec (range 100))])") == true)
				#expect(try eval("(every? (fn [v] (let [s (set v)] (= (hash s) (hash-unordered-coll s) (hash-unordered-coll (vec s))))) [[] [1 2 3] [nil \"a\" :b 1.5]])") == true)
				#expect(try eval("(every? (fn [m] (= (hash m) (hash-unordered-coll m))) [{} {:a 1} {:a 1 \"b\" [2] 3 nil} (sorted-map 1 2 3 4)])") == true)
				// Clojure's own suite spells the algorithm out with the -int arithmetic (data_structures.clj).
				#expect(try eval("(let [v [1 :a \"s\" [2]]] (= (hash v) (mix-collection-hash (reduce (fn [acc e] (unchecked-add-int (unchecked-multiply-int 31 acc) (hash e))) 1 v) (count v))))") == true)
				// Murmur3's mixing is the JVM's bit for bit.
				#expect(try eval("[(mix-collection-hash 1 0) (mix-collection-hash -1 3) (mix-collection-hash 2147483647 2) (msg #(mix-collection-hash 4294967297 2))]") == [-2017569654, -196466786, -1359385504, "integer overflow"])
				#expect(try eval("[(= (hash-combine 7 'a) (hash-combine 7 'a)) (int? (hash-combine 17 \"x\"))]") == [true, true])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func chunkedSeqs() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [b (chunk-buffer 4)] (chunk-append b 1) (chunk-append b 2) (chunk-append b 3) (let [c (chunk b) s (chunk-cons c (list 4 5))] [(count c) (nth c 1) (.nth c 2) (reduce + c) (chunked-seq? s) (chunked-seq? [1]) (vec s) (count (chunk-first s)) (chunk-rest s) (chunk-next s) (chunk-cons (chunk (chunk-buffer 1)) [9])]))") == [3, 2, 3, 6, true, false, [1, 2, 3, 4, 5], 3, [4, 5], [4, 5], [9]])
				#expect(try eval("[(chunk-rest (chunk-cons (chunk (doto (chunk-buffer 1) (chunk-append 1))) nil)) (chunk-next (chunk-cons (chunk (doto (chunk-buffer 1) (chunk-append 1))) nil)) (vec (->ArrayChunk nil (object-array [1 2 3]) 1 3))]") == [[], nil, [2, 3]])
				#expect(message("(chunk-append (chunk-buffer 0) 1)")?.isEmpty == false)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func printingVarsAndPrintMethod() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(binding [*print-readably* false] (pr-str \"a\" \\b)) (binding [*print-meta* true] (pr-str (with-meta [1] {:a 1}) (with-meta 'x {:k 1}) {:k (with-meta [2] {:b 2})} (with-meta 'y {:tag 'String}))) (binding [*print-meta* true *print-readably* false] (pr-str (with-meta [1] {:a 1})))]") == ["a b", "^{:a 1} [1] ^{:k 1} x {:k ^{:b 2} [2]} ^String y", "[1]"])
				#expect(try eval("(binding [*print-namespace-maps* true] [(pr-str {:ns/x 1}) (pr-str {}) (pr-str {'ns/x 1}) (pr-str {:ns/x {:ns/y 1}}) (pr-str (sorted-map :q/a 1 :q/b 2))])") == ["#:ns{:x 1}", "{}", "#:ns{x 1}", "#:ns{:x #:ns{:y 1}}", "#:q{:a 1, :b 2}"])
				#expect(try eval("[(binding [*print-dup* true] (pr-str [1 \"a\" :b])) *flush-on-newline* *print-readably* *print-meta* *print-namespace-maps*]") == ["[1 \"a\" :b]", true, true, false, false])
				// A print-method of the program's own, for a deftype, a record and a :type tag, inside collections too.
				#expect(try eval("[(pr-str (->Pt 1 2)) (pr-str [(->Pt 1 2)]) (print-str (->Pt 1 2)) (pr-str (->Rec 1)) (pr-str (with-meta [1 2] {:type :my/kind})) (pr-str (with-meta [1 2] {:type :other}))]") == ["#pt[1 \"s\"]", "[#pt[1 \"s\"]]", "#pt[1 \"s\"]", "#parity-tests.Rec{:a 1}", "<kind>", "[1 2]"])
				// *out* bound to *err* writes past every capture; with-out-str captures under any binding of *out*.
				#expect(try eval("[(with-out-str (binding [*out* *err*] (print \"to err\"))) (binding [*out* *err*] (with-out-str (print \"captured\"))) (with-out-str (.write *out* \"w\") (.write *out* 65) (.flush *out*)) (identical? *out* *err*)]") == ["", "captured", "wA", false])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func filesAndLoading() throws {
			#expect(try eval("(let [f (str tmp \"/a.txt\")] [(spit f \"hello\\nworld\") (slurp f) (spit f [1 2] :append true) (slurp f :encoding \"UTF-8\")])") == [nil, "hello\nworld", nil, "hello\nworld[1 2]"])
			#expect(try eval("[(msg #(slurp (str tmp \"/missing.txt\"))) (msg #(slurp (str tmp \"/a.txt\") :encoding \"latin1\"))]") == [Value("\(Self.dir.path)/missing.txt (No such file or directory)"), "Unsupported encoding: latin1"])
			#expect(try eval("(mapv #(subs % (count tmp)) (file-seq tmp))") == ["", "/a.txt", "/sub", "/sub/z.txt"])
			#expect(try eval("(let [closed (volatile! false)] [(with-open [r (->LineReader (volatile! [\"a\" \"b\"]) closed)] (doall (line-seq r))) @closed])") == [["a", "b"], true])
			#expect(try eval("[(with-open [] 5) (msg #(eval '(with-open [[a] [1]] a)))]") == [5, "with-open only allows Symbols in bindings"])
			#expect(try eval("(do (spit (str tmp \"/sp.clj\") \"(ns parity-tests) (def sp-seen [*source-path* (count *file*)])\") (load-file (str tmp \"/sp.clj\")) (first sp-seen))") == "sp.clj")
			#expect(try eval("[(load \"/clojure/walk\") (some? (find-ns 'clojure.walk)) (msg #(load \"/parity/nowhere\"))]") == [nil, true, "Could not locate parity/nowhere.cljc or parity/nowhere.clj on load path."])
		}
	}
}
