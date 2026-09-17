// @ai-generated(guided)
import CljCore
import Dispatch
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
private func m(_ d: [String: Value]) -> Value { Value(Dictionary(uniqueKeysWithValues: d.map { (kw($0.key), $0.value) })) }

private let trap = "on an atom this thread is already swapping (nested swap! trap)"

extension CoreTests {
	@Suite struct AtomTests {
		init() {
			clj_init()
			for k in ["a", "b", "c", "k", "k2", "meta", "validator", "x", "y", "other", "bad", "none", "v", "items", "fresh", "n", "nf", "users", "name", "xs", "clojure.core/not-found"] { _ = kw(k) }
		}

		private func declare(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0))" }.joined(separator: " "))
		}

		private func unbind(_ names: String...) throws {
			_ = try eval(names.map { "(def \($0) nil)" }.joined(separator: " "))
		}

		@Test func derefResetSwap() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [a (atom 1)] [(atom? a) (atom? 1) @a (deref a) (reset! a 2) @a (swap! a inc) (swap! a + 10) (swap! a + 1 2) (swap! a + 1 2 3) (swap! a + 1 2 3 4) @a])") == [true, false, 1, 1, 2, 2, 3, 13, 16, 22, 32, 32])
				#expect(try eval("(let [a (atom {})] [(swap! a assoc :a 1) (swap! a assoc :b 2 :k 3) (swap! a dissoc :k) @a])") == [m(["a": 1]), m(["a": 1, "b": 2, "k": 3]), m(["a": 1, "b": 2]), m(["a": 1, "b": 2])])
				#expect(try eval("(let [a (atom [])] (swap! a (fn [v] (conj v 1))) (swap! a (fn [v x] (conj v x)) 2) (swap! a (fn [v & xs] (into v xs)) 3 4) @a)") == [1, 2, 3, 4])
				#expect(try eval("(let [a (atom 0)] [(swap-vals! a inc) (swap-vals! a + 10) (reset-vals! a 7) @a])") == [[0, 1], [1, 11], [11, 7], 7])
				#expect(try eval("[(instance? Atom (atom 1)) (= (type (atom 1)) Atom) (pr-str (atom 1)) (let [a (atom 1)] (= a a)) (= (atom 1) (atom 1)) (satisfies? IMeta (atom 1))]") == [true, true, "#object[atom]", true, false, true])
				#expect(try eval("(let [a (atom nil)] [@a (swap! a (fn [x] [x])) @a])") == [nil, [nil], [nil]])
				#expect(message("(swap! 1 inc)") == "swap! expects an atom, got: long")
				#expect(message("(reset! [] 1)") == "reset! expects an atom, got: vector")
				#expect(message("(swap! (atom 1) str 1 2 3 4 5 6 7 8 9)") == nil)
				#expect(try eval("(swap! (atom \"a\") str 1 2 3 4 5 6 7 8 9)") == "a123456789")
				#expect(message("(atom 1 :meta)") == "No value supplied for key: :meta")
				#expect(message("(atom 1 :meta 2)") == "atom :meta must be a map, got: long")
				#expect(message("(atom 1 :validator 2)") == "atom :validator must be a fn, got: long")
				#expect(try eval("(let [a (atom 1 :other 2)] @a)") == 1)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// A throw out of f propagates and the atom keeps the value f was given, whatever f did with it first.
		@Test func throwFromFLeavesTheStateUnchanged() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [a (atom 5)] [(try (swap! a (fn [x] (throw (ex-info \"boom\" {})))) (catch :default e (ex-message e))) @a])") == ["boom", 5])
				#expect(try eval("(let [a (atom 5) w (add-watch a :k (fn [k r o n] nil))] [(try (swap! a (fn [x] (throw (ex-info \"boom\" {})))) (catch :default e (ex-message e))) @a])") == ["boom", 5])
				// A native f that throws: inc on a map.
				#expect(try eval("(let [a (atom {:a {:b [1 2]} :c #{3}}) old @a] [(try (swap! a inc) (catch :default e (nil? e))) (identical? old @a) (= @a {:a {:b [1 2]} :c #{3}})])") == [false, true, true])
				// The rejection idiom: f edits a nested structure, then throws instead of returning it.
				#expect(try eval("""
				(let [a (atom {:users {1 {:name "x"}} :n 1}) old @a
				      reject (fn [s] (let [s (assoc-in s [:users 2] {:name "y"}) s (update s :n inc)] (if (< (:n s) 2) s (throw (ex-info "rejected" {:n (:n s)})))))]
				  [(try (swap! a reject) (catch :default e [(ex-message e) (ex-data e)])) (identical? old @a) @a
				   (try (swap-vals! a reject) (catch :default e (ex-message e))) (identical? old @a)])
				""") == [["rejected", Value([Value(keyword: "n"): 2])], true, Value([Value(keyword: "users"): Value([Value(1): m(["name": "x"])]), Value(keyword: "n"): 1]), "rejected", true])
				// A throw from a variadic f, and from one past the small argument buffer.
				#expect(try eval("(let [a (atom [1])] [(try (swap! a (fn [v & xs] (throw (ex-info \"v\" {}))) 1 2) (catch :default e (ex-message e))) (try (swap! a (fn [v a b c d e f g h] (throw (ex-info \"big\" {}))) 1 2 3 4 5 6 7 8) (catch :default e (ex-message e))) @a])") == ["v", "big", [1]])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Inside f (and a validator, and an alter-meta! fn) the atom still holds the value f was given.
		@Test func derefInsideFSeesTheOldValue() throws {
			try declare("at-val")
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [a (atom 1)] [(swap! a (fn [x] (+ x @a))) @a])") == [2, 2])
				#expect(try eval("(let [a (atom {:k 1}) seen (volatile! nil)] (swap! a (fn [m] (vreset! seen [(identical? m @a) (:k @a)]) (assoc m :k 2))) [@seen @a])") == [[true, 1], m(["k": 2])])
				#expect(try eval("(let [a (atom {:k 1})] [(swap-vals! a (fn [m] (assoc m :k (inc (:k @a))))) @a])") == [[m(["k": 1]), m(["k": 2])], m(["k": 2])])
				// A validator derefs the atom (through a var: a validator capturing its own atom is a cycle): the value before the change.
				#expect(try eval("(def at-val (atom 1)) (let [seen (volatile! [])] (set-validator! at-val (fn [v] (vswap! seen conj [v @at-val]) true)) (swap! at-val inc) (reset! at-val 7) (set-validator! at-val nil) [@seen @at-val])") == [[[1, 1], [2, 1], [7, 2]], 7])
				#expect(try eval("(let [a (atom 1 :meta {})] [(alter-meta! a (fn [m] (assoc m :v @a))) (meta a)])") == [m(["v": 1]), m(["v": 1])])
				try unbind("at-val")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func watches() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("""
				(let [a (atom 0) log (atom [])]
				  (add-watch a :k (fn [k r o n] (swap! log conj [k (identical? r a) o n])))
				  (swap! a inc) (reset! a 5) (swap-vals! a + 1) (reset-vals! a 9) (compare-and-set! a 9 10) (compare-and-set! a 9 11)
				  (remove-watch a :k) (swap! a inc)
				  [@a @log])
				""") == [11, [[kw("k"), true, 0, 1], [kw("k"), true, 1, 5], [kw("k"), true, 5, 6], [kw("k"), true, 6, 9], [kw("k"), true, 9, 10]]])
				// Two watches, keys by equality; a watch that throws propagates out of the swap after the store.
				#expect(try eval("""
				(let [a (atom 0) n (atom 0)]
				  (add-watch a [1] (fn [k r o n'] (swap! n inc)))
				  (add-watch a [1] (fn [k r o n'] (swap! n + 10)))
				  (add-watch a :other (fn [k r o n'] (swap! n + 100)))
				  (swap! a inc)
				  (add-watch a :bad (fn [k r o n'] (throw (ex-info "watch" {}))))
				  [@n (try (swap! a inc) (catch :default e (ex-message e))) @a])
				""") == [110, "watch", 2])
				#expect(try eval("(let [a (atom 0)] [(identical? a (add-watch a :k identity)) (identical? a (remove-watch a :k)) (identical? a (remove-watch a :none))])") == [true, true, true])
				#expect(message("(add-watch (atom 1) :k 1)") == "add-watch expects a fn, got: long")
				// A watch runs after the lock is released: it may deref and swap the atom.
				#expect(try eval("(let [a (atom 0) seen (atom nil)] (add-watch a :k (fn [k r o n] (when (< n 3) (swap! r inc)) (reset! seen @r))) (swap! a inc) [@a @seen])") == [3, 3])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nestedSwapTrap() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(message("(let [a (atom 0)] (swap! a (fn [x] (swap! a inc))))") == "swap! " + trap)
				#expect(message("(let [a (atom 0)] (swap! a (fn [x] (reset! a 1))))") == "reset! " + trap)
				#expect(message("(let [a (atom 0)] (swap! a (fn [x] (swap-vals! a inc))))") == "swap-vals! " + trap)
				#expect(message("(let [a (atom 0)] (swap! a (fn [x] (compare-and-set! a 0 1))))") == "compare-and-set! " + trap)
				#expect(message("(let [a (atom 0)] (swap-vals! a (fn [x] (swap! a inc))))") == "swap! " + trap)
				#expect(message("(let [a (atom 0)] (reset! a (swap! a (fn [x] (reset! a 1)))))") == "reset! " + trap)
				#expect(try eval("(let [a (atom 0)] (try (set-validator! a (fn [v] (swap! a inc))) (catch :default e [(ex-message e) (ex-message (ex-cause e))])))") == ["Invalid reference state", Value("swap! " + trap)])
				#expect(message("(let [a (atom 0)] (set-validator! a (fn [v] (nil? @a))) (swap! a inc))") == "Invalid reference state")
				// The trap is a throw out of f: the atom keeps its value and is usable again.
				#expect(try eval("(let [a (atom 0) b (atom 0)] (try (swap! a (fn [x] (swap! a inc))) (catch :default e nil)) [@a (swap! a inc) (swap! a (fn [x] (swap! b inc))) @b])") == [0, 1, 1, 1])
				// A swap of a different atom inside f is not nested.
				#expect(try eval("(let [a (atom 0)] (swap! a (fn [x] (swap! (atom x) inc))))") == 1)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func validators() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [a (atom 1 :validator pos?)] [(swap! a inc) (try (reset! a -1) (catch :default e (ex-message e))) (try (swap! a - 5) (catch :default e (ex-message e))) @a (identical? pos? (get-validator a))])") == [2, "Invalid reference state", "Invalid reference state", 2, true])
				#expect(message("(atom -1 :validator pos?)") == "Invalid reference state")
				// A rejected swap leaves the very same value in place, whatever f built from it.
				#expect(try eval("(let [a (atom {:n 1 :xs [1]} :validator (fn [m] (< (:n m) 2))) old @a] [(try (swap! a (fn [m] (-> m (update :n inc) (update :xs conj 2)))) (catch :default e (ex-message e))) (identical? old @a) (try (swap-vals! a update :n + 5) (catch :default e (ex-message e))) (identical? old @a)])") == ["Invalid reference state", true, "Invalid reference state", true])
				#expect(try eval("(let [a (atom 1)] [(get-validator a) (set-validator! a pos?) (try (set-validator! a neg?) (catch :default e (ex-message e))) (identical? pos? (get-validator a)) (set-validator! a nil) (get-validator a) (reset! a -1)])") == [nil, nil, "Invalid reference state", true, nil, nil, -1])
				#expect(try eval("(let [a (atom 1 :validator (fn [v] (if (neg? v) (throw (ex-info \"neg\" {:v v})) true)))] (try (reset! a -2) (catch :default e [(ex-message e) (ex-message (ex-cause e)) (ex-data (ex-cause e)) @a])))") == ["Invalid reference state", "neg", m(["v": -2]), 1])
				#expect(try eval("(let [a (atom 1 :validator pos?)] [(compare-and-set! a 1 2) (try (compare-and-set! a 2 -1) (catch :default e (ex-message e))) @a])") == [true, "Invalid reference state", 2])
				#expect(message("(set-validator! (atom 1) 2)") == "set-validator! expects a fn or nil, got: long")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func compareAndSetIsIdentity() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [a (atom [1])] [(compare-and-set! a [1] [2]) @a (compare-and-set! a @a [2]) @a (compare-and-set! a 1 3)])") == [false, [1], true, [2], false])
				#expect(try eval("(let [a (atom 1)] [(compare-and-set! a 1 2) (compare-and-set! a 1 3) @a])") == [true, false, 2])
				#expect(try eval("(let [a (atom nil)] [(compare-and-set! a nil :x) @a (compare-and-set! a nil :y) @a])") == [true, kw("x"), false, kw("x")])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func metaOnAtoms() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [a (atom 1 :meta {:a 1})] [(meta a) (alter-meta! a assoc :b 2) (meta a) (reset-meta! a {:x 1}) (meta a) (reset-meta! a nil) (meta a)])") == [m(["a": 1]), m(["a": 1, "b": 2]), m(["a": 1, "b": 2]), m(["x": 1]), m(["x": 1]), nil, nil])
				#expect(try eval("(meta (atom 1))") == nil)
				#expect(message("(alter-meta! (atom 1) (fn [m] 1))") == "alter-meta! fn must return a map, got: long")
				#expect(message("(reset-meta! (atom 1) 1)") == "reset-meta! expects a map, got: long")
				#expect(message("(let [a (atom 1)] (alter-meta! a (fn [m] (swap! a inc))))") == "swap! " + trap)
				#expect(message("(let [a (atom 1)] (alter-meta! a (fn [m] (alter-meta! a assoc :k 1))))") == "alter-meta! " + trap)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nestedUpdateHelpers() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("[(get-in {:a {:b 1}} [:a :b]) (get-in {:a {:b 1}} [:a :x]) (get-in {:a {:b 1}} [:a :x] :nf) (get-in {:a nil} [:a :b] :nf) (get-in {:a {:b nil}} [:a :b] :nf) (get-in {} []) (get-in nil [:a])]") == [1, nil, kw("nf"), kw("nf"), nil, m([:]), nil])
				#expect(try eval("(assoc-in {} [:a :b] 1)") == m(["a": m(["b": 1])]))
				#expect(try eval("(assoc-in {:a {:b 1 :c 2}} [:a :b] 9)") == m(["a": m(["b": 9, "c": 2])]))
				#expect(try eval("(assoc-in [1 2] [0] 3)") == [3, 2])
				#expect(try eval("[(update {:a 1} :a inc) (update {} :a (fn [x] [x])) (update {:a 1} :a + 1) (update {:a 1} :a + 1 2) (update {:a 1} :a + 1 2 3 4)]") == [m(["a": 2]), m(["a": [nil]]), m(["a": 2]), m(["a": 4]), m(["a": 11])])
				#expect(try eval("[(update-in {:a {:b 1}} [:a :b] inc) (update-in {} [:a :b] (fn [x] 1)) (update-in {:a {:b 1}} [:a :b] + 10 20) (update-in {:a [1 2]} [:a 1] inc)]") == [m(["a": m(["b": 2])]), m(["a": m(["b": 1])]), m(["a": m(["b": 31])]), m(["a": [1, 3]])])
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Everything stored into an atom is shared first; a swap leaves the old version to whoever holds it.
		@Test func publication() throws {
			try declare("at-state")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("(def at-state (atom {:items [1 [2]]}))")
				let atom = try eval("at-state")
				let value = withExtendedLifetime(atom) { clj_atom_of(atom.raw).pointee.value }
				#expect(clj_debug_all_shared(value))
				_ = try eval("(swap! at-state assoc :k 1)")
				let afterNative = withExtendedLifetime(atom) { clj_atom_of(atom.raw).pointee.value }
				#expect(afterNative != value)
				#expect(clj_debug_all_shared(afterNative))
				_ = try eval("(swap! at-state (fn [m] (let [items (get m :items)] (assoc m :items (conj items 3)))))")
				let afterClosure = withExtendedLifetime(atom) { clj_atom_of(atom.raw).pointee.value }
				#expect(afterClosure != afterNative)
				#expect(clj_debug_all_shared(afterClosure))
				#expect(try eval("@at-state") == m(["items": [1, [2], 3], "k": 1]))
				// A holder of the old value keeps its version.
				#expect(try eval("(let [old @at-state] (swap! at-state assoc :k 10) [(:k old) (:k @at-state)])") == [1, 10])
				// A value stored by reset! is shared before the store, with everything it reaches.
				_ = try eval("(reset! at-state {:fresh [[1] {:n #{2}}]})")
				let fresh = withExtendedLifetime(atom) { clj_atom_of(atom.raw).pointee.value }
				#expect(clj_debug_all_shared(fresh))
				// The atom's own graph: a validator and its watches are shared too.
				_ = try eval("(let [v [1]] (set-validator! at-state (fn [x] (or v true))) (add-watch at-state :k (fn [k r o n] v)))")
				#expect(withExtendedLifetime(atom) { clj_debug_all_shared(atom.raw) })
				_ = try eval("(set-validator! at-state nil) (remove-watch at-state :k)")
				try unbind("at-state")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// N threads on one atom: exact counts, exact key sets, nothing leaked, nothing torn (ASan and UBSan runs).
		@Test func contendedSwaps() throws {
			let threads = 4, rounds = 5_000
			let before = clj_debug_live_objects()
			do {
				let program = try eval("""
				(let [counter (atom 0) m (atom {}) log (atom 0)]
				  (add-watch log :k (fn [k r o n] nil))
				  [(fn [t]
				     (dotimes [i \(rounds)]
				       (swap! counter inc)
				       (swap! m assoc [t i] i)
				       (swap! log + 1))
				     t)
				   (fn [] [@counter @m @log])])
				""")
				// The closure and the atoms it captures cross to other threads: the spawn publication.
				withExtendedLifetime(program) { clj_share(program.raw) }
				let worker = try #require(program.array?[0]), reader = try #require(program.array?[1])
				var results = [Int](repeating: -1, count: threads)
				results.withUnsafeMutableBufferPointer { out in
					DispatchQueue.concurrentPerform(iterations: threads) { t in
						var arg = clj_fixnum(t)
						let r = withExtendedLifetime(worker) { withUnsafePointer(to: &arg) { clj_invoke(worker.raw, $0, 1) } }
						out[t] = r == CLJ_THROWN ? -1 : Int(clj_fixnum_val(r))
					}
				}
				#expect(results == Array(0..<threads))
				let final = try #require(Value(owning: withExtendedLifetime(reader) { clj_invoke(reader.raw, nil, 0) }).array)
				#expect(final[0] == Value(threads * rounds))
				#expect(final[2] == Value(threads * rounds))
				let dict = try #require(final[1].dictionary)
				#expect(dict.count == threads * rounds)
				for t in 0..<threads {
					for i in stride(from: 0, to: rounds, by: 997) { #expect(dict[[Value(t), Value(i)]] == Value(i)) }
				}
				#expect(withExtendedLifetime(final) { clj_debug_all_shared(final[1].raw) })
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func contendedDerefAndReset() throws {
			let threads = 4, rounds = 20_000
			let before = clj_debug_live_objects()
			do {
				let program = try eval("""
				(let [a (atom [0])]
				  (fn [t]
				    (loop [i 0 seen 0]
				      (if (< i \(rounds))
				        (do (when (even? i) (reset! a [i t]))
				            (recur (inc i) (+ seen (count @a))))
				        seen))))
				""")
				withExtendedLifetime(program) { clj_share(program.raw) }
				var sums = [Int](repeating: 0, count: threads)
				sums.withUnsafeMutableBufferPointer { out in
					DispatchQueue.concurrentPerform(iterations: threads) { t in
						var arg = clj_fixnum(t)
						let r = withExtendedLifetime(program) { withUnsafePointer(to: &arg) { clj_invoke(program.raw, $0, 1) } }
						out[t] = r == CLJ_THROWN ? -1 : Int(clj_fixnum_val(r))
					}
				}
				// Every deref saw a whole vector: 1 element before the first reset, 2 after.
				for s in sums { #expect(s >= rounds && s <= 2 * rounds) }
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
