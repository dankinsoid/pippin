// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// Abandoned parked coroutines must all be collected, with the frame oracle on (docs/notes/fuzzing.md, "Leak fuzzing").

private let fuzzPrograms = ProcessInfo.processInfo.environment["CLJ_LEAK_FUZZ"].flatMap(Int.init) ?? 6
private let fuzzSeed = ProcessInfo.processInfo.environment["CLJ_LEAK_FUZZ_SEED"].flatMap(UInt64.init) ?? 1

private struct SplitMix {
	var state: UInt64
	mutating func next() -> UInt64 {
		state &+= 0x9E37_79B9_7F4A_7C15
		var z = state
		z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
		z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
		return z ^ (z >> 31)
	}
	mutating func below(_ n: Int) -> Int { Int(next() % UInt64(n)) }
	mutating func pick<T>(_ xs: [T]) -> T { xs[below(xs.count)] }
}

// One program: channels, data and closures over them, and go blocks that park in the runtime's callbacks.
struct LeakProgram {
	var rng: SplitMix
	var chans: [String] = []

	init(seed: UInt64) { rng = SplitMix(state: seed) }

	private mutating func chan() -> String { rng.pick(chans) }

	private mutating func coll() -> String {
		let c = chan()
		return rng.pick(["[1 2 3]", "(range 3)", "(list \(c) \(c))", "#{1 2}", "[\(c) 1]", "(vector \(c))", "(map inc [1 2])"])
	}

	// An expression that parks on one of the channels, possibly from inside a runtime callback.
	private mutating func park(_ depth: Int) -> String {
		let c = chan(), d = chan()
		let base = rng.pick(["(<! \(c))", "(>! \(c) [\(d)])", "(first (alts! [\(c) \(d)]))", "(<! \(c))"])
		if depth == 0 || rng.below(3) == 0 { return base }
		let p = park(depth - 1)
		let coll = coll()
		return rng.pick([
			"(reduce (fn [acc x] \(p) (conj acc x)) [] \(coll))",
			"(doall (map (fn [x] \(p) x) \(coll)))",
			"(into [] (map (fn [x] \(p) x)) \(coll))",
			"(mapv (fn [x] \(p) x) \(coll))",
			"(sort-by (fn [x] \(p) (hash x)) \(coll))",
			"(first (filter (fn [x] \(p) true) \(coll)))",
			"(some (fn [x] \(p) false) \(coll))",
			"(group-by (fn [x] \(p) (hash x)) \(coll))",
			"(update {:k [\(c)]} :k (fn [v] \(p) v))",
			"(reduce-kv (fn [m k v] \(p) (assoc m k v)) {} {:a \(c) :b 2})",
			"(let [a (atom [\(c)])] (swap! a (fn [v] (conj v (count v)))) \(p) @a)",
			"(let [v [\(c) {:c \(d)}] f (fn [] v)] \(p) (f))",
			"(apply (fn [& xs] \(p) xs) \(coll))",
			"(transduce (map (fn [x] \(p) x)) conj \(coll))",
			"(lazy-seq (cons \(p) nil))",
			"(first (lazy-seq (cons \(p) nil)))",
		])
	}

	private mutating func spawn() -> String {
		let body = park(2)
		switch rng.below(5) {
		case 0: return "(go-loop [n 0] \(body) (recur (inc n)))"
		case 1: return "(let [m (mult \(chan())) t (chan)] (tap m t) (go (try (<! t) \(body) (catch :default _ nil))))"
		case 2: return "(let [out (chan) mx (mix out) in \(chan())] (admix mx in) (go (try \(body) (catch :default _ nil))))"
		case 3: return "(let [a (atom nil)] (go (reset! a [\(chan())]) (try \(body) (catch :default _ nil)) @a))"
		default: return "(go (try \(body) (catch :default _ nil)))"
		}
	}

	mutating func source(_ name: String) -> String {
		let n = 1 + rng.below(3)
		var binds: [String] = []
		for i in 0..<n {
			chans.append("c\(i)")
			binds.append("c\(i) " + (rng.below(3) == 0 ? "(chan \(1 + rng.below(2)))" : "(chan)"))
		}
		let spawns = (0..<(1 + rng.below(3))).map { _ in spawn() }
		return "(defn \(name) [] (let [\(binds.joined(separator: " "))] \(spawns.joined(separator: " ")) nil))"
	}
}

extension CoreTests {
	@Suite struct LeakFuzzTests {
		init() throws {
			clj_init()
			try cljTimingSupport()
			_ = try cljEvalScoped("""
			(ns leak-fuzz-tests (:require [clojure.core.async :refer [chan <! >! alts! go go-loop mult tap mix admix]]))
			""")
			for k in ["k", "a", "b", "c"] { _ = Value(keyword: k) }
			// A first mix and mult register their types for the process: before any baseline.
			_ = try cljEvalScoped("(in-ns 'leak-fuzz-tests) (let [out (chan 1) m (mix out) t (chan 1)] (admix m (chan)) (tap (mult (chan)) t) nil)")
		}

		private static func run(_ backend: String, _ i: Int) throws {
			let seed = fuzzSeed &+ UInt64(i) &* 7919 &+ UInt64(backend.utf8.count)
			var program = LeakProgram(seed: seed)
			let name = "leak-\(backend)-\(seed)"
			let src = program.source(name)
			let def = { _ = try cljEvalScoped("(in-ns 'leak-fuzz-tests) " + src) }
			if backend == "interp" { try def() } else { try compiledEval(closed: backend == "closed", def) }
			let base = CoroBaseline()
			_ = try cljEvalScoped("(in-ns 'leak-fuzz-tests) (\(name))")
			let gone = eventually(ms: 10_000) {
				clj_cc_collect()
				return clj_debug_live_coros() == base.coros
			}
			#expect(gone, "seed \(seed): parked coroutines left after collection of \(src)")
			let objects = settledLiveObjects("after \(name)")
			#expect(objects == base.objects, "seed \(seed): \(objects - base.objects) objects left after \(src)")
		}

		@Test(arguments: ["interp", "dev", "closed"])
		func abandonedProgramsLeaveNothing(backend: String) throws {
			for i in 0..<(backend == "interp" ? fuzzPrograms : max(1, fuzzPrograms / 2)) { try LeakFuzzTests.run(backend, i) }
		}
	}
}
