// @ai-generated(guided)
import CljCore

// Core fns implemented in Swift and bound into clojure.core after boot: the escape hatch of the design (§6b).
// A primitive lives next to the Clojure implementation that stays its specification, and PrimitiveTests runs
// both through Runtime.differential. `compare` is the leaf: nothing in Clojure orders two strings without
// it, so its specification is Clojure's contract, checked by table.
// clj_init's host hook: the primitives are part of every boot, whichever entry point runs it.
@_cdecl("clj_host_boot")
func hostBoot() { Runtime.installPrimitives() }

extension Runtime {
	static func installPrimitives() {
		bind("compare", in: "clojure.core", doc: "Comparator. Returns -1, 0 or 1 as x is less than, equal to or greater than y; nil is less than everything.",
		     Value(function: "clojure.core/compare", arity: 2...2) { args in try Value(compare(args[0], args[1])) })
		bind("sort", in: "clojure.core", doc: "Returns a sorted sequence of the items in coll, by compare or the comparator comp.",
		     Value(function: "clojure.core/sort", arity: 1...2) { args in args.count == 1 ? try sort(args[0]) : try sort(args[1], by: args[0]) })
	}

	/// Clojure `compare` for nil, booleans, numbers, chars, strings, keywords and symbols: -1, 0 or 1. nil is
	/// below everything, `false` below `true`, a fixnum and a double compare numerically (NaN as equal to
	/// anything, as Clojure's), strings and chars by code point, keywords and symbols by namespace (an
	/// unqualified one first) then name. Values of two different types throw "<type> cannot be cast to
	/// <type>", the shape of Clojure's ClassCastException; so does any other type unless both are the same object.
	public static func compare(_ a: Value, _ b: Value) throws -> Int {
		try withExtendedLifetime((a, b)) { try compare(a.raw, b.raw) }
	}

	// Borrowed words the caller keeps alive.
	static func compare(_ a: clj_value, _ b: clj_value) throws -> Int {
		if a == b { return 0 }
		if clj_is_nil(a) { return -1 }
		if clj_is_nil(b) { return 1 }
		if clj_is_fixnum(a) && clj_is_fixnum(b) { return order(clj_fixnum_val(a), clj_fixnum_val(b)) }
		if clj_is_fixnum(a) || clj_is_double(a) {
			guard clj_is_fixnum(b) || clj_is_double(b) else { throw cast(b, "a number") }
			return order(asDouble(a), asDouble(b))
		}
		if clj_is_bool(a) {
			guard clj_is_bool(b) else { throw cast(b, "a boolean") }
			return a == CLJ_TRUE ? 1 : -1
		}
		if clj_is_char(a) {
			guard clj_is_char(b) else { throw cast(b, "a char") }
			return order(clj_char_val(a), clj_char_val(b))
		}
		if clj_is_string(a) {
			guard clj_is_string(b) else { throw cast(b, "a string") }
			return compareStrings(a, b)
		}
		if clj_is_keyword(a) {
			guard clj_is_keyword(b) else { throw cast(b, "a keyword") }
			return compareNamed(clj_keyword_ns(a), clj_keyword_name(a), clj_keyword_ns(b), clj_keyword_name(b))
		}
		if clj_is_symbol(a) {
			guard clj_is_symbol(b) else { throw cast(b, "a symbol") }
			return compareNamed(clj_symbol_ns(a), clj_symbol_name(a), clj_symbol_ns(b), clj_symbol_name(b))
		}
		if clj_is_vector(a) {
			guard clj_is_vector(b) else { throw cast(b, "a vector") }
			return try compareVectors(a, b)
		}
		throw cast(a, "Comparable")
	}

	// Clojure's APersistentVector.compareTo: the shorter vector is less, then element by element.
	private static func compareVectors(_ a: clj_value, _ b: clj_value) throws -> Int {
		let na = clj_vector_count(a), nb = clj_vector_count(b)
		if na != nb { return na < nb ? -1 : 1 }
		for i in 0..<na {
			let r = try compare(clj_vector_nth(a, i), clj_vector_nth(b, i))
			if r != 0 { return r }
		}
		return 0
	}

	private static func order<T: Comparable>(_ x: T, _ y: T) -> Int { x < y ? -1 : x > y ? 1 : 0 }

	private static func asDouble(_ v: clj_value) -> Double { clj_is_double(v) ? clj_double_val(v) : Double(clj_fixnum_val(v)) }

	private static func cast(_ v: clj_value, _ to: String) -> ClojureError {
		ClojureError(thrown: Value(exInfo: "\(String(cString: clj_type_name(v))) cannot be cast to \(to)"))
	}

	private static func compareStrings(_ a: clj_value, _ b: clj_value) -> Int {
		let x = UnsafeRawBufferPointer(start: clj_string_bytes(a), count: Int(clj_string_len(a)))
		let y = UnsafeRawBufferPointer(start: clj_string_bytes(b), count: Int(clj_string_len(b)))
		for i in 0..<min(x.count, y.count) where x[i] != y[i] { return x[i] < y[i] ? -1 : 1 }
		return order(x.count, y.count)
	}

	// Symbol.compareTo: an unqualified name before any qualified one, then the namespaces, then the names.
	private static func compareNamed(_ ans: clj_value, _ aname: clj_value, _ bns: clj_value, _ bname: clj_value) -> Int {
		if clj_is_nil(ans) != clj_is_nil(bns) { return clj_is_nil(ans) ? -1 : 1 }
		if !clj_is_nil(ans) {
			let c = compareStrings(ans, bns)
			if c != 0 { return c }
		}
		return compareStrings(aname, bname)
	}

	/// Clojure `sort` with the default ordering: the items of any seqable as a list, ascending by `compare`,
	/// stable. Throws what `compare` throws for a pair it meets and what realizing a lazy `coll` throws; a
	/// single item is never compared.
	public static func sort(_ coll: Value) throws -> Value {
		let items = try coll.retainedItems()
		defer { items.forEach(clj_release) }
		let sorted = try mergeSort(items) { try compare($0, $1) }
		return Value(owning: sorted.withUnsafeBufferPointer { clj_list_from_array($0.baseAddress, $0.count) })
	}

	/// `(sort comp coll)`: comp returns a number (its sign orders) or, as a predicate, logical true when the
	/// first argument sorts before the second, as Clojure's AFunction.compare reads a fn comparator.
	public static func sort(_ coll: Value, by comparator: Value) throws -> Value {
		let items = try coll.retainedItems()
		defer { items.forEach(clj_release) }
		let sorted = try mergeSort(items) { a, b in
			let r = try comparator(Value(borrowing: a), Value(borrowing: b))
			if let n = r.int { return n }
			if let d = r.double { return d < 0 ? -1 : d > 0 ? 1 : 0 }
			if r.isTruthy { return -1 }
			return try comparator(Value(borrowing: b), Value(borrowing: a)).isTruthy ? 1 : 0
		}
		return Value(owning: sorted.withUnsafeBufferPointer { clj_list_from_array($0.baseAddress, $0.count) })
	}

	// Bottom-up and stable; `compare` sees (earlier, later), so a mixed pair throws about the later item's type.
	// `items` is never touched: a throw leaves the caller's words to release.
	private static func mergeSort(_ items: [clj_value], _ compare: (clj_value, clj_value) throws -> Int) throws -> [clj_value] {
		var src = items, dst = items
		var width = 1
		while width < src.count {
			var lo = 0
			while lo < src.count {
				let mid = min(lo + width, src.count), hi = min(lo + 2 * width, src.count)
				var i = lo, j = mid, k = lo
				while i < mid && j < hi {
					if try compare(src[i], src[j]) <= 0 {
						dst[k] = src[i]
						i += 1
					} else {
						dst[k] = src[j]
						j += 1
					}
					k += 1
				}
				dst.replaceSubrange(k..<k + (mid - i), with: src[i..<mid])
				dst.replaceSubrange(k + (mid - i)..<hi, with: src[j..<hi])
				lo = hi
			}
			swap(&src, &dst)
			width *= 2
		}
		return src
	}

	/// The Clojure implementation `sort` is checked against: a stable merge sort over `compare`, as a fn form.
	/// Kept here so the primitive and its specification change together (PrimitiveTests, bench).
	public static let sortSpecification = """
	(fn sort-spec [coll]
	  (let [merge (fn [xs ys]
	                (loop [i 0 j 0 out []]
	                  (cond (= i (count xs)) (into out (drop j ys))
	                        (= j (count ys)) (into out (drop i xs))
	                        (<= (compare (nth xs i) (nth ys j)) 0) (recur (inc i) j (conj out (nth xs i)))
	                        :else (recur i (inc j) (conj out (nth ys j))))))
	        msort (fn msort [xs]
	                (let [n (count xs)]
	                  (if (< n 2)
	                    xs
	                    (let [h (if (even? n) (/ n 2) (/ (dec n) 2))]
	                      (merge (msort (vec (take h xs))) (msort (vec (drop h xs))))))))]
	    (apply list (msort (vec coll)))))
	"""
}

extension Value {
	// The items of any seqable, each retained; throws for a value without a seq and what realizing it throws.
	func retainedItems() throws -> [clj_value] {
		let s = withExtendedLifetime(self) { clj_seq(raw) }
		if s == CLJ_THROWN { throw ClojureError.takePending() }
		if clj_is_nil(s) { return [] }
		defer { clj_release(s) }
		var out: [clj_value] = []
		var it = clj_seq_iter_start(s)
		var item: clj_value = CLJ_NIL
		while clj_seq_iter_next(&it, &item) { out.append(clj_retain(item)) }
		if it.thrown {
			out.forEach(clj_release)
			throw ClojureError.takePending()
		}
		return out
	}
}
