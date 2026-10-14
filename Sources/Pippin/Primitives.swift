// @ai-generated(guided)
import CljCore

// clj_init's host hook: the host types and the Swift stubs are part of every boot, whichever entry point runs it.
// `compare` and `sort` are C builtins (compare.c); this file keeps the host's door to them and the Clojure
// specification `sort` is checked against (CompareTests, through Runtime.differential).
@_cdecl("clj_host_boot")
func hostBoot() {
	Runtime.installHostTypes()
	Runtime.installSwiftStubs()
}

extension Runtime {
	/// Clojure `compare`: -1, 0 or 1 as a is below, equal to or above b. The ordering itself lives in C
	/// (`clj_compare`, compare.h); this is the host's door to it, and CompareTests checks it by table.
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
	/// The bench and CompareTests run it against the builtin.
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

