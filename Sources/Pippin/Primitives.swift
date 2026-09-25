// @ai-generated(guided)
import CljCore

// Core fns implemented in Swift and bound into clojure.core after boot: the escape hatch of the design (§6b).
// A primitive lives next to the Clojure implementation that stays its specification, and PrimitiveTests runs
// both through Runtime.differential. Both bodies are now one C call each (compare.h): the ordering and the
// merge sort live in the core, where the sorted collections reach them without a host at all. `compare` is
// the leaf with no Clojure specification — nothing in Clojure orders two strings — and is checked by table.
// clj_init's host hook: the primitives are part of every boot, whichever entry point runs it.
@_cdecl("clj_host_boot")
func hostBoot() {
	Runtime.installHostTypes()
	Runtime.installPrimitives()
}

extension Runtime {
	static func installPrimitives() {
		bind("compare", in: "clojure.core", doc: "Comparator. Returns -1, 0 or 1 as x is less than, equal to or greater than y; nil is less than everything.",
		     Value(function: "clojure.core/compare", arity: 2...2) { args in try Value(compare(args[0], args[1])) })
		bind("sort", in: "clojure.core", doc: "Returns a sorted sequence of the items in coll, by compare or the comparator comp.",
		     Value(function: "clojure.core/sort", arity: 1...2) { args in args.count == 1 ? try sort(args[0]) : try sort(args[1], by: args[0]) })
	}

	/// Clojure `compare`: -1, 0 or 1 as a is below, equal to or above b. The ordering itself lives in C
	/// (`clj_compare`, compare.h); this is the host's door to it, and PrimitiveTests checks it by table.
	public static func compare(_ a: Value, _ b: Value) throws -> Int {
		try withExtendedLifetime((a, b)) { try compare(a.raw, b.raw) }
	}

	// Borrowed words the caller keeps alive.
	static func compare(_ a: clj_value, _ b: clj_value) throws -> Int {
		var out: Int32 = 0
		if clj_compare(a, b, &out) == CLJ_THROWN { throw ClojureError.takePending() }
		return Int(out)
	}

	/// Clojure `sort` with the default ordering: the items of any seqable as a list, ascending by
	/// `compare`, stable. Throws what `compare` throws for a pair it meets and what realizing a lazy
	/// `coll` throws; a single item is never compared.
	public static func sort(_ coll: Value) throws -> Value {
		try sort(coll, comparator: CLJ_NIL)
	}

	/// `(sort comp coll)`: comp returns a number (its sign orders) or, as a predicate, logical true when the
	/// first argument sorts before the second, as Clojure's AFunction.compare reads a fn comparator.
	public static func sort(_ coll: Value, by comparator: Value) throws -> Value {
		// A nil comparator is the C default, so the explicit 2-arity refuses it as invoking nil would.
		guard comparator.raw != CLJ_NIL else { throw ClojureError(thrown: Value(exInfo: "nil cannot be invoked")) }
		return try withExtendedLifetime(comparator) { try sort(coll, comparator: comparator.raw) }
	}

	private static func sort(_ coll: Value, comparator: clj_value) throws -> Value {
		let sorted = withExtendedLifetime(coll) { clj_sort(coll.raw, comparator) }
		if sorted == CLJ_THROWN { throw ClojureError.takePending() }
		return Value(owning: sorted)
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

