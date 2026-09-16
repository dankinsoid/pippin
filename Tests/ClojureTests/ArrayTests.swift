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
	@Suite struct ArrayTests {
		init() {
			clj_init()
			for k in ["a", "b", "k", "int", "long", "byte", "short", "float", "double", "boolean", "char", "object", "i32", "u8", "f32", "i128", "nope"] { _ = kw(k) }
		}

		@Test func constructorsAndPrinting() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(pr-str (int-array 3))") == "#array[:int 0 0 0]")
				#expect(try eval("(pr-str (int-array [1 2 3]))") == "#array[:int 1 2 3]")
				#expect(try eval("(pr-str (int-array 3 7))") == "#array[:int 7 7 7]")
				#expect(try eval("(pr-str (int-array 4 [1 2]))") == "#array[:int 1 2 0 0]")
				#expect(try eval("(pr-str (int-array 0))") == "#array[:int]")
				#expect(try eval("(pr-str (long-array [1 2]))") == "#array[:long 1 2]")
				#expect(try eval("(pr-str (double-array [1 2.5]))") == "#array[:double 1.0 2.5]")
				#expect(try eval("(pr-str (float-array [0.5]))") == "#array[:float 0.5]")
				#expect(try eval("(pr-str (byte-array [1 -2]))") == "#array[:byte 1 -2]")
				#expect(try eval("(pr-str (short-array [1000]))") == "#array[:short 1000]")
				#expect(try eval("(pr-str (boolean-array [1 nil false]))") == "#array[:boolean true false false]")
				#expect(try eval("(pr-str (char-array \"abc\"))") == "#array[:char \\a \\b \\c]")
				#expect(try eval("(pr-str (object-array [:a \"b\" 1]))") == "#array[:object :a \"b\" 1]")
				#expect(try eval("(pr-str (object-array 2))") == "#array[:object nil nil]")
				// str is the same text: an array has no unreadable form to hide.
				#expect(try eval("(str (int-array [1 2]))") == "#array[:int 1 2]")
				#expect(try eval("(pr-str [(int-array [1]) {:k (int-array [2])}])") == "[#array[:int 1] {:k #array[:int 2]}]")
				#expect(try eval("(pr-str (type (int-array 1)))") == "array")
				// A float keeps 32-bit precision when it is read back as a double.
				#expect(try eval("(aget (float-array [0.1]) 0)") == Value(0.10000000149011612))
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func kindsAndMakeArray() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(pr-str (make-array :int 2))") == "#array[:int 0 0]")
				#expect(try eval("(pr-str (make-array :object 1))") == "#array[:object nil]")
				// The C spellings name the same kinds, and u8 has only one.
				#expect(try eval("(pr-str (make-array :i32 1))") == "#array[:int 0]")
				#expect(try eval("(pr-str (make-array :f32 1))") == "#array[:float 0.0]")
				#expect(try eval("(pr-str (make-array :u8 2))") == "#array[:u8 0 0]")
				#expect(try eval("(pr-str (aset (make-array :u8 1) 0 255))") == "255")
				#expect(message("(aset (make-array :u8 1) 0 256)") == "Value out of range for u8: 256")
				#expect(message("(make-array :i128 1)") == "make-array: unknown array kind: :i128")
				#expect(message("(make-array 7 1)") == "make-array: unknown array kind: 7")
				#expect(message("(make-array :int 2 2)") == "make-array: only one dimension is supported")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func getSetAndBounds() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [a (int-array [1 2 3])] [(aget a 0) (aget a 2) (alength a)])") == [1, 3, 3])
				#expect(try eval("(let [a (int-array 2)] [(aset a 0 5) (aget a 0) (pr-str a)])") == Value([5, 5, "#array[:int 5 0]"]))
				#expect(try eval("(let [a (double-array 1)] (aset a 0 1) (aget a 0))") == 1.0)
				#expect(try eval("(let [a (char-array 1)] (aset a 0 \\x) (aget a 0))") == Value("x" as Unicode.Scalar))
				// aset-int and its siblings are the same native: the array's kind decides the cast.
				#expect(try eval("(let [a (int-array 1)] (aset-int a 0 9) (aget a 0))") == 9)
				#expect(try eval("(let [a (long-array 1)] (aset-long a 0 9) (aget a 0))") == 9)
				#expect(message("(aget (int-array 2) 5)") == "Index 5 out of bounds for length 2")
				#expect(message("(aget (int-array 2) -1)") == "Index -1 out of bounds for length 2")
				#expect(message("(aset (int-array 2) 2 1)") == "Index 2 out of bounds for length 2")
				#expect(message("(aget [1 2] 0)") == "aget not supported on this type: vector")
				#expect(message("(alength [1])") == "alength not supported on this type: vector")
				#expect(message("(aclone nil)") == "aclone not supported on this type: nil")
				#expect(message("(aget (int-array 2) :a)") == "aget: keyword cannot be cast to an integer")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func writesAreRangeChecked() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(message("(aset (byte-array 1) 0 300)") == "Value out of range for byte: 300")
				#expect(message("(aset (byte-array 1) 0 -129)") == "Value out of range for byte: -129")
				#expect(message("(aset (short-array 1) 0 40000)") == "Value out of range for short: 40000")
				#expect(message("(aset (int-array 1) 0 3000000000)") == "Value out of range for int: 3000000000")
				#expect(message("(aset (char-array 1) 0 -1)") == "Value out of range for char: -1")
				#expect(message("(aset (float-array 1) 0 1e300)") == "Value out of range for float: 1.0E300")
				#expect(message("(aset (int-array 1) 0 \"a\")") == "string cannot be cast to a number")
				#expect(message("(aset (char-array 1) 0 \"a\")") == "string cannot be cast to a char")
				#expect(message("(int-array [1 \"a\"])") == "string cannot be cast to a number")
				// The edges fit, and a double is truncated toward zero as RT.intCast does.
				#expect(try eval("(let [a (byte-array 2)] (aset a 0 127) (aset a 1 -128) (pr-str a))") == "#array[:byte 127 -128]")
				#expect(try eval("(let [a (int-array 1)] (aset a 0 2.9) (aget a 0))") == 2)
				#expect(try eval("(let [a (int-array 1)] (aset a 0 -2.9) (aget a 0))") == -2)
				// A long outside the fixnum range round-trips through a bigint.
				#expect(try eval("(let [a (long-array 1)] (aset a 0 9000000000000000000N) (pr-str (aget a 0)))") == "9000000000000000000N")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func seqCountNthAndReduce() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(pr-str (seq (int-array [1 2 3])))") == "(1 2 3)")
				#expect(try eval("(seq (int-array 0))") == nil)
				#expect(try eval("[(count (int-array [1 2])) (nth (int-array [5 6]) 1) (first (int-array [7 8]))]") == [2, 6, 7])
				#expect(try eval("(pr-str (rest (int-array [1 2 3])))") == "(2 3)")
				#expect(try eval("(pr-str (vec (int-array [1 2 3])))") == "[1 2 3]")
				#expect(try eval("(reduce + (int-array [1 2 3]))") == 6)
				#expect(try eval("(reduce + 10 (double-array [1 2]))") == 13.0)
				#expect(try eval("(pr-str (into [] (map inc) (int-array [1 2])))") == "[2 3]")
				#expect(try eval("(pr-str (map inc (int-array [1 2])))") == "(2 3)")
				#expect(try eval("(reduce (fn [acc x] (if (= x 2) (reduced acc) (+ acc x))) 0 (int-array [1 2 3]))") == 1)
				#expect(try eval("[(seqable? (int-array 1)) (coll? (int-array 1)) (counted? (int-array 1)) (sequential? (int-array 1))]") == [true, false, false, false])
				#expect(try eval("(count (rest (int-array [1 2 3])))") == 2)
				#expect(message("(nth (int-array [1]) 3)") == "Index 3 out of bounds for length 1")
				#expect(try eval("(nth (int-array [1]) 3 :a)") == kw("a"))
				// Identity, as a JVM array has: equal only to itself, and usable as a map key.
				#expect(try eval("(let [a (int-array [1])] [(= a a) (= a (aclone a)) (= a [1])])") == [true, false, false])
				#expect(try eval("(let [a (int-array [1]) b (aclone a)] (get (assoc {a 1} b 2) a))") == 1)
				#expect(try eval("(let [a (int-array [1])] (= (hash a) (hash a)))") == true)
				// RT.get and RT.contains index an array; RT.contains casts the key, so a nil one throws.
				#expect(try eval("(let [a (int-array [5 6])] [(get a 1) (get a 9) (get a 9 :a) (get a :k)])") == Value([6, nil, kw("a"), nil]))
				#expect(try eval("(let [a (int-array [5])] [(contains? a 0) (contains? a 1) (contains? a -1)])") == [true, false, false])
				#expect(message("(contains? (int-array [1]) nil)") == "nil cannot be cast to a number")
				// An array implements no collection interface, so these reach nothing.
				#expect(try eval("(empty (int-array 1))") == nil)
				#expect(message("(conj (int-array 1) 2)") == "conj not supported on this type: array")
				#expect(message("(with-meta (int-array 1) {})") == "with-meta: array does not support metadata")
				#expect(try eval("(apply aget [(int-array [1 2]) 1])") == 2)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func amapAndAreduce() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(pr-str (amap (int-array [1 2 3]) i r (* 2 (aget r i))))") == "#array[:int 2 4 6]")
				#expect(try eval("(let [xs (int-array [1 2 3])] (areduce xs i ret 0 (+ ret (aget xs i))))") == 6)
				#expect(try eval("(let [xs (double-array [1.5 2.5])] (areduce xs i ret 0.0 (+ ret (aget xs i))))") == 4.0)
				// amap clones, so the source keeps its elements.
				#expect(try eval("(let [xs (int-array [1 2])] (amap xs i r (inc (aget r i))) (pr-str xs))") == "#array[:int 1 2]")
				#expect(try eval("(pr-str (amap (object-array [:a]) i r (aget r i)))") == "#array[:object :a]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func intoArrayToArrayAndVectorOf() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(pr-str (into-array [1 2]))") == "#array[:object 1 2]")
				#expect(try eval("(pr-str (into-array :int [1 2]))") == "#array[:int 1 2]")
				#expect(try eval("(pr-str (to-array [1 :a]))") == "#array[:object 1 :a]")
				#expect(try eval("(pr-str (to-array nil))") == "#array[:object]")
				#expect(try eval("(pr-str (into-array :long (range 3)))") == "#array[:long 0 1 2]")
				#expect(message("(into-array :int [1 :a])") == "keyword cannot be cast to a number")
				// vector-of is a persistent vector of cast elements, not an unboxed one.
				#expect(try eval("(pr-str (vector-of :int 1 2 3))") == "[1 2 3]")
				#expect(try eval("[(vector? (vector-of :int 1)) (= (vector-of :int 1 2) [1 2])]") == [true, true])
				#expect(try eval("(pr-str (vector-of :float 0.1))") == "[0.10000000149011612]")
				#expect(try eval("(pr-str (vector-of :int))") == "[]")
				#expect(message("(vector-of :byte 300)") == "Value out of range for byte: 300")
				#expect(message("(vector-of :nope 1)") == "vector-of: unknown array kind: :nope")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func objectArraysOwnTheirElements() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [a (object-array 2)] (aset a 0 [1 2]) (pr-str (aget a 0)))") == "[1 2]")
				// Overwriting a slot releases what was there.
				#expect(try eval("(let [a (object-array 1)] (aset a 0 [1]) (aset a 0 [2]) (pr-str a))") == "#array[:object [2]]")
				#expect(try eval("(let [a (object-array 1)] (aset a 0 [1]) (aset a 0 nil) (pr-str a))") == "#array[:object nil]")
				#expect(try eval("(let [a (object-array [[1] [2]]) b (aclone a)] (aset b 0 [3]) [(pr-str a) (pr-str b)])") == Value(["#array[:object [1] [2]]", "#array[:object [3] [2]]"]))
				#expect(try eval("(let [a (object-array 1)] (aset a 0 (int-array [1])) (pr-str a))") == "#array[:object #array[:int 1]]")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// An array handed to another thread must have its elements shared with it (object.h).
		@Test func sharedObjectArraysShareWhatIsWrittenIntoThem() throws {
			clj_init()
			let before = clj_debug_live_objects()
			do {
				let arr = try eval("(object-array 1)")
				let item = try eval("[1 2]")
				withExtendedLifetime(arr) { clj_share(arr.raw) }
				#expect(clj_is_shared(arr.raw))
				withExtendedLifetime((arr, item)) { _ = clj_array_set(arr.raw, 0, item.raw) }
				#expect(clj_debug_all_shared(arr.raw))
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
