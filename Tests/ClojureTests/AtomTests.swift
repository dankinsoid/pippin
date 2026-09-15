// @ai-generated(guided)
import CljCore
import Dispatch
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

private let trap = "on an atom this thread is already swapping (nested swap! trap)"

extension CoreTests {
	@Suite struct AtomTests {
		init() {
			clj_init()
			for k in ["a", "b", "k", "k2", "meta", "validator", "x", "y", "other", "bad", "none", "v", "items", "fresh", "n"] { _ = kw(k) }
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
				#expect(message("(swap! 1 inc)") == "swap! expects an atom, got: fixnum")
				#expect(message("(reset! [] 1)") == "reset! expects an atom, got: vector")
				#expect(message("(swap! (atom 1) str 1 2 3 4 5 6 7 8 9)") == nil)
				#expect(try eval("(swap! (atom \"a\") str 1 2 3 4 5 6 7 8 9)") == "a123456789")
				#expect(message("(atom 1 :meta)") == "No value supplied for key: :meta")
				#expect(message("(atom 1 :meta 2)") == "atom :meta must be a map, got: fixnum")
				#expect(message("(atom 1 :validator 2)") == "atom :validator must be a fn, got: fixnum")
				#expect(try eval("(let [a (atom 1 :other 2)] @a)") == 1)
				// A throw inside f propagates; the atom keeps its value when a second holder kept the old one.
				#expect(try eval("(let [a (atom 5) w (add-watch a :k (fn [k r o n] nil))] [(try (swap! a (fn [x] (throw (ex-info \"boom\" {})))) (catch :default e (ex-message e))) @a])") == ["boom", 5])
				#expect(try eval("(let [a (atom 5)] [(try (swap! a (fn [x] (throw (ex-info \"boom\" {})))) (catch :default e (ex-message e))) @a])") == ["boom", nil])
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
				#expect(message("(add-watch (atom 1) :k 1)") == "add-watch expects a fn, got: fixnum")
				// A watch runs after the lock is released: it may deref and swap the atom.
				#expect(try eval("(let [a (atom 0) seen (atom nil)] (add-watch a :k (fn [k r o n] (when (< n 3) (swap! r inc)) (reset! seen @r))) (swap! a inc) [@a @seen])") == [3, 3])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nestedSwapTrap() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(message("(let [a (atom 0)] (swap! a (fn [x] (swap! a inc))))") == "swap! " + trap)
				#expect(message("(let [a (atom 0)] (swap! a (fn [x] @a)))") == "deref " + trap)
				#expect(message("(let [a (atom 0)] (swap! a (fn [x] (reset! a 1))))") == "reset! " + trap)
				#expect(try eval("(let [a (atom 0)] (try (set-validator! a (fn [v] (swap! a inc))) (catch :default e [(ex-message e) (ex-message (ex-cause e))])))") == ["Invalid reference state", Value("swap! " + trap)])
				#expect(message("(let [a (atom 0)] (set-validator! a (fn [v] (nil? @a))) (swap! a inc))") == "Invalid reference state")
				// The trap fires inside f, which already took the value: the atom is left at nil and usable again.
				#expect(try eval("(let [a (atom 0) b (atom 0)] (try (swap! a (fn [x] (swap! a inc))) (catch :default e nil)) [@a (reset! a 0) (swap! a inc) (swap! a (fn [x] (swap! b inc))) @b])") == [nil, 0, 1, 1, 1])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func validators() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [a (atom 1 :validator pos?)] [(swap! a inc) (try (reset! a -1) (catch :default e (ex-message e))) (try (swap! a - 5) (catch :default e (ex-message e))) @a (identical? pos? (get-validator a))])") == [2, "Invalid reference state", "Invalid reference state", 2, true])
				#expect(message("(atom -1 :validator pos?)") == "Invalid reference state")
				#expect(try eval("(let [a (atom 1)] [(get-validator a) (set-validator! a pos?) (try (set-validator! a neg?) (catch :default e (ex-message e))) (identical? pos? (get-validator a)) (set-validator! a nil) (get-validator a) (reset! a -1)])") == [nil, nil, "Invalid reference state", true, nil, nil, -1])
				#expect(try eval("(let [a (atom 1 :validator (fn [v] (if (neg? v) (throw (ex-info \"neg\" {:v v})) true)))] (try (reset! a -2) (catch :default e [(ex-message e) (ex-message (ex-cause e)) (ex-data (ex-cause e)) @a])))") == ["Invalid reference state", "neg", m(["v": -2]), 1])
				#expect(try eval("(let [a (atom 1 :validator pos?)] [(compare-and-set! a 1 2) (try (compare-and-set! a 2 -1) (catch :default e (ex-message e))) @a])") == [true, "Invalid reference state", 2])
				#expect(message("(set-validator! (atom 1) 2)") == "set-validator! expects a fn or nil, got: fixnum")
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
				#expect(message("(alter-meta! (atom 1) (fn [m] 1))") == "alter-meta! fn must return a map, got: fixnum")
				#expect(message("(reset-meta! (atom 1) 1)") == "reset-meta! expects a map, got: fixnum")
				#expect(message("(let [a (atom 1)] (alter-meta! a (fn [m] (deref a))))") == "deref " + trap)
			}
			#expect(clj_debug_live_objects() == before)
		}

		// In place only while nothing else holds the value; a second holder forces the copy.
		@Test func publicationAndInPlace() throws {
			try declare("at-state")
			let before = clj_debug_live_objects()
			do {
				_ = try eval("(def at-state (atom {:items [1 [2]]}))")
				let atom = try eval("at-state")
				let value = withExtendedLifetime(atom) { clj_atom_of(atom.raw).pointee.value }
				#expect(clj_debug_all_shared(value))
				let consuming = clj_debug_consuming_calls()
				_ = try eval("(swap! at-state assoc :k 1)")
				let afterNative = withExtendedLifetime(atom) { clj_atom_of(atom.raw).pointee.value }
				#expect(afterNative == value)
				_ = try eval("(swap! at-state (fn [m] (assoc m :k2 2)))")
				let afterClosure = withExtendedLifetime(atom) { clj_atom_of(atom.raw).pointee.value }
				#expect(afterClosure == value)
				_ = try eval("(swap! at-state (fn [m] (let [items (get m :items)] (assoc m :items (conj items 3)))))")
				let afterLet = withExtendedLifetime(atom) { clj_atom_of(atom.raw).pointee.value }
				#expect(afterLet == value)
				if consuming >= 0 { #expect(clj_debug_consuming_calls() - consuming >= 3) }
				#expect(clj_debug_all_shared(afterClosure))
				#expect(try eval("@at-state") == m(["items": [1, [2], 3], "k": 1, "k2": 2]))
				// A second holder of the old value forces a copy, and keeps its version.
				#expect(try eval("(let [old @at-state] (swap! at-state assoc :k 10) [(:k old) (:k @at-state)])") == [1, 10])
				let afterCopy = withExtendedLifetime(atom) { clj_atom_of(atom.raw).pointee.value }
				#expect(afterCopy != value)
				#expect(clj_debug_all_shared(afterCopy))
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
