// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// Collection of abandoned parked coroutines (design §7, «Фаза 3»; NOTES "Coroutines"): a garbage park is cancelled
// and unwinds, a park something can still wake is left alone.

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'coro-collect-tests) " + source) }

private func kw(_ s: String) -> Value { Value(keyword: s) }

private func cancelledByCollection() -> Int64 {
	var s = [Int64](repeating: 0, count: CLJ_CC_STAT_COUNT)
	clj_debug_cc_stats(&s)
	return s[CLJ_CC_STAT_COROUTINES]
}

private func parkReports() -> Int64 {
	var s = [Int64](repeating: 0, count: CLJ_CC_STAT_COUNT)
	clj_debug_cc_stats(&s)
	return s[CLJ_CC_STAT_PARK_REPORTS]
}

// The body must have parked before a collection can judge it: collect until the count moves or the wait ends.
private func collected(atLeast n: Int64, since before: Int64, ms: Int = 5000) -> Bool {
	for _ in 0..<(ms / 10) {
		clj_cc_collect()
		if cancelledByCollection() - before >= n { return true }
		usleep(10_000)
	}
	return false
}

// Collections over a while that must find nothing to cancel.
private func noneCollected(since before: Int64, ms: Int = 300) -> Bool {
	for _ in 0..<(ms / 10) {
		clj_cc_collect()
		usleep(10_000)
	}
	return cancelledByCollection() == before
}

extension CoreTests {
	@Suite struct CoroCollectTests {
		init() throws {
			clj_init()
			try cljTimingSupport()
			_ = try cljEvalScoped("""
			(ns coro-collect-tests (:require [clojure.core.async :refer [chan <! >! >!! timeout go go-loop alts! mult mix admix onto-chan! suspend! close! cancel!]]))
			(refer 'test-support)
			(defprotocol Res (-close [r]))
			(deftype Held [closed] Res (-close [_] (reset! closed true)))
			(def unwound (atom nil))
			(def res-closed (atom false))
			(def from-swift (atom nil))
			(def alts-done (atom nil))
			(def suspended nil)
			(def stale-park nil)
			;; A tagged literal hands the channel it makes to the next one, which parks on it: only the reader holds it.
			(def gap-slot (volatile! nil))
			(alter-var-root #'*data-readers* assoc
			  'gap/chan (fn [_] (let [c (chan)] (vreset! gap-slot c) c))
			  'gap/park (fn [x] (let [c @gap-slot] (vreset! gap-slot nil) (<! c) x))
			  'gap/lazy-park (fn [_] (let [c @gap-slot] (vreset! gap-slot nil) (lazy-seq (<! c) nil))))
			;; Equality that parks: program code a builtin runs, as a lazy seq's realization is.
			(deftype GapKey [c n] IEquiv (equiv [_ o] (<! c) false) IHashEq (hasheq [_] 7))
			""")
			for k in ["v", "loop", "chan", "timeout", "suspended", "deadline", "future", "alts", "let", "top"] { _ = kw(k) }
			for (_, k) in CoroCollectTests.gapShapes { _ = kw(k) }
		}

		// Each cycle hangs on a reference the parked frames' three kinds once missed (design §7, «Фаза 3»).
		static let gapShapes: [(String, String)] = [
			// C code of the runtime across the park: the reducer's accumulator, sort's seq of the items, a step's item.
			("(defn gap-reduce [] (let [c (chan)] (go (try (reduce (fn [acc _] (<! acc) acc) c [1]) (finally (reset! unwound :reduce))))) nil)", "reduce"),
			("(defn gap-sort [] (let [c (chan)] (go (try (sort (fn [a b] (<! c) 0) [c c]) (finally (reset! unwound :sort))))) nil)", "sort"),
			("(defn gap-xform [] (let [c (chan)] (go (try (into [] (map (fn [x] (<! x) x)) [c]) (finally (reset! unwound :xform))))) nil)", "xform"),
			// A compiled frame's array slot: captured by a closure, a direct fn's parameter, read through the static link.
			("(defn gap-captured [] (go (let [c (chan) f (fn [] c)] (try (<! c) (finally (reset! unwound :captured))) f)) nil)", "captured"),
			("(defn gap-direct [] (go (let [g (fn [c] (try (<! c) (finally (reset! unwound :direct))))] (g (chan)))) nil)", "direct"),
			("(defn gap-outer [] (go (let [c (chan) g (fn [] (try (<! c) (finally (reset! unwound :outer))))] (g))) nil)", "outer"),
			// The interpreter's argument buffer: an owned value beside a parking argument, a last use handed to the call.
			("(defn gap-args [] (let [d (chan)] (go (try (vector (identity d) (<! d)) (finally (reset! unwound :args))))) nil)", "args"),
			("(defn gap-handed [] (let [d (chan)] (go (try (<! (identity d)) (finally (reset! unwound :handed))))) nil)", "handed"),
			// A frame of more than 64 slots, the channel past the 64th.
			(gapWide, "wide"),
			// A call the facts call pure realizes a lazy seq whose thunk parks: the seq an argument, a slot beside an
			// owned value, an argument of a local fn.
			("(defn gap-lazy [] (let [c (chan)] (go (try (first (lazy-seq (cons (<! c) nil))) (finally (reset! unwound :lazy))))) nil)", "lazy"),
			("(defn gap-lazy-slot [] (let [d (chan)] (go (try (let [s (lazy-seq (cons (<! d) nil))] (vector (identity d) (first s))) (finally (reset! unwound :lazy-slot))))) nil)", "lazy-slot"),
			("(defn gap-lazy-fn [] (let [d (chan)] (go (try (let [head (fn [s] (first s))] (vector (identity d) (head (map (fn [x] (<! d) x) [1])))) (finally (reset! unwound :lazy-fn))))) nil)", "lazy-fn"),
			// The same through a deftype's equality, and a trie insert down a shared path whose key equality parks.
			("(defn gap-equiv [] (let [d (chan)] (go (try (vector (identity d) (= (->GapKey d 0) (->GapKey d 1))) (finally (reset! unwound :equiv))))) nil)", "equiv"),
			("(defn gap-trie [] (let [c (chan)] (go (let [m (assoc (zipmap (range 40) (range 40)) (->GapKey c 0) 0)] (try (assoc m (->GapKey c 1) 1) (finally (reset! unwound :trie))) m))) nil)", "trie"),
			// Heap stacks of the runtime's C: the printer's array elements, the reader's metadata, syntax-quote's arguments.
			("(defn gap-print [] (let [d (chan)] (go (try (pr-str (object-array [(identity d) (lazy-seq (cons (<! d) nil))])) (finally (reset! unwound :print))))) nil)", "print"),
			("(defn gap-read [] (go (try (read-string \"^{:c #gap/chan 0} [#gap/park 1]\") (finally (reset! unwound :read)))) nil)", "read"),
			("(defn gap-quote [] (go (try (read-string \"`[#gap/chan 0 #gap/lazy-park 0]\") (finally (reset! unwound :quote)))) nil)", "quote"),
			// An argument buffer of more than 64 values, the owned one past the 64th.
			(gapWideArgs, "wide-args"),
		]

		private static var gapWideArgs: String {
			let ones = [String](repeating: "1", count: 64).joined(separator: " ")
			return "(defn gap-wide-args [] (let [d (chan)] (go (try (vector \(ones) (identity d) (<! d)) (finally (reset! unwound :wide-args))))) nil)"
		}

		private static var gapWide: String {
			let names = (0..<70).map { "w\($0)" }
			let binds = names.enumerated().map { "\($1) (identity \($0))" }.joined(separator: " ")
			return "(defn gap-wide [] (go (let [\(binds) c (chan)] (try (<! c) (finally (reset! unwound :wide))) (+ \(names.joined(separator: " "))))) nil)"
		}

		// Interpreted, dev-compiled and closed-compiled: a gap of one backend is no gap of another, so each runs all.
		@Test(arguments: ["interp", "dev", "closed"], CoroCollectTests.gapShapes.map { $0.1 })
		func gapShapeIsCollected(backend: String, shape: String) throws {
			let src = CoroCollectTests.gapShapes.first { $0.1 == shape }!.0
			if backend == "interp" { _ = try eval(src) } else { try compiledEval(closed: backend == "closed") { _ = try eval(src) } }
			try abandoned("(\(src.split(separator: " ")[1]))", unwinds: shape)
		}

		private func reset() throws { _ = try eval("(reset! unwound nil) (reset! res-closed false) (reset! from-swift nil) (reset! alts-done nil)") }

		// The source leaves a parked body nothing outside holds: a collection cancels it, its finally sets unwound to
		// the keyword named, and every object it made goes.
		private func abandoned(_ source: String, unwinds: String?, _ location: SourceLocation = #_sourceLocation) throws {
			try reset()
			let base = CoroBaseline(location)
			let before = cancelledByCollection()
			_ = try eval(source)
			#expect(collected(atLeast: 1, since: before), sourceLocation: location)
			if let k = unwinds { #expect(eventually { (try? eval("@unwound")) == kw(k) }, sourceLocation: location) }
			base.check(location)
		}

		@Test func aDroppedGoLoopIsCollectedAndItsFinallyRuns() throws {
			try reset()
			let base = CoroBaseline()
			let before = cancelledByCollection()
			_ = try eval("(let [c (chan)] (go (try (loop [] (<! c) (recur)) (finally (reset! unwound :loop)))) nil)")
			#expect(collected(atLeast: 1, since: before))
			#expect(eventually { (try? eval("@unwound")) == kw("loop") })
			base.check()
		}

		// The cancel restores the stack before the unwind runs on it.
		@Test func anEvacuatedParkIsCollected() throws {
			try reset()
			let base = CoroBaseline()
			let before = cancelledByCollection()
			_ = try eval("(let [c (chan)] (go-loop [] (when (<! c) (recur))) nil)")
			#expect(eventually { clj_coro_evacuate_all() > 0 })
			#expect(collected(atLeast: 1, since: before))
			base.check()
		}

		// The timer thread holds the timeout's channel and can wake this park: only its firing ends it.
		@Test func altsOverADroppedChannelAndALiveTimeoutWaitsForTheTimeout() throws {
			try reset()
			let base = CoroBaseline()
			let before = cancelledByCollection()
			_ = try eval("(let [c (chan)] (go (let [[_ p] (alts! [c (timeout 3000)])] (reset! alts-done (if (= p c) :chan :timeout)))) nil)")
			#expect(noneCollected(since: before))
			#expect(try eval("@alts-done").isNil)
			#expect(eventually { (try? eval("@alts-done")) == kw("timeout") })
			#expect(cancelledByCollection() == before)
			base.check()
		}

		// The port vector is the parker's own temporary: the walk reads it as the coroutine's, so the ports under it
		// are held by the garbage alone.
		@Test func altsOverDroppedPorts() throws {
			try abandoned("(let [in (chan) stop (chan)] (go (try (loop [] (alts! [in stop]) (recur)) (finally (reset! unwound :alts)))) nil)", unwinds: "alts")
		}

		// mix parks in alts! over the reads vector its loop state holds; the mix, its output and its input are dropped.
		// A first mix registers its reify type for the process: that one runs to its end before the baseline.
		@Test func aDroppedMixWithAnInput() throws {
			_ = try eval("(let [out (chan) m (mix out) in (chan 1)] (admix m in) (close! out) (>!! in 1) nil)")
			try abandoned("(let [out (chan) m (mix out) in (chan)] (admix m in) nil)", unwinds: nil)
		}

		// Only the body's own frame holds the channel it parks on.
		@Test func aChannelOfTheBodysOwnLet() throws {
			try abandoned("(go (let [c (chan)] (try (<! c) (finally (reset! unwound :let))))) nil", unwinds: "let")
		}

		// The same shapes as compiled code: the frames are C, the vector a compiled literal.
		@Test(arguments: [false, true]) func compiledShapes(closed: Bool) throws {
			try compiledEval(closed: closed) {
				_ = try eval("""
				(defn cc-alts-abandon [] (let [in (chan) stop (chan)] (go (try (loop [] (alts! [in stop]) (recur)) (finally (reset! unwound :alts)))) nil))
				(defn cc-let-abandon [] (go (let [c (chan)] (try (<! c) (finally (reset! unwound :let))))) nil)
				""")
			}
			try abandoned("(cc-alts-abandon)", unwinds: "alts")
			try abandoned("(cc-let-abandon)", unwinds: "let")
		}

		// A top-level form evaluated in a coroutine: its frame has no ring entry, its argument buffers are named apart.
		// Closed compilation refuses eval.
		@Test(arguments: ["interp", "dev"]) func aTopLevelFormsBuffersAreNamed(backend: String) throws {
			let src = """
			(defn gap-top [] (go (try (eval '(let [d (clojure.core.async/chan)] (vector (identity d) (clojure.core.async/<! d))))
			  (finally (reset! unwound :top)))) nil)
			"""
			if backend == "interp" { _ = try eval(src) } else { try compiledEval(closed: false) { _ = try eval(src) } }
			try abandoned("(gap-top)", unwinds: "top")
		}

		// A wrapped ring's entries are returned frames', each of which owned the channel: never read as the park's.
		@Test func aParkAfterTheRingWrappedIsNotMisread() throws {
			try reset()
			_ = try eval("(defn gap-deep [n c] (if (pos? n) (let [x (identity c)] (+ 1 (gap-deep (dec n) x))) 0))")
			let old = clj_debug_coro_ring_capacity(16)
			defer { _ = clj_debug_coro_ring_capacity(old) }
			let base = CoroBaseline()
			let before = cancelledByCollection()
			do {
				let held = try eval("(let [c (chan)] (go (gap-deep 40 c) (reset! from-swift (<! c))) c)")
				#expect(noneCollected(since: before))
				_ = Value(owning: clj_chan_put(held.raw, kw("v").raw))
				#expect(eventually { (try? eval("@from-swift")) == kw("v") })
			}
			#expect(cancelledByCollection() == before)
			base.check()
		}

		// Its handle keeps the park, so no collection ends it, and its channel only it reaches: the stale-park report
		// names it once; the same park on a channel Swift holds is not named.
		@Test func aParkOnChannelsOnlyItReachesIsReported() throws {
			let old = clj_debug_cc_park_report_ms(100)
			defer { _ = clj_debug_cc_park_report_ms(old) }
			let base = CoroBaseline()
			let before = parkReports()
			do {
				let held = try eval("(let [c (chan)] (go (<! c)) c)")
				_ = try eval("(def stale-park (let [c (chan)] (go (<! c))))")
				#expect(eventually { clj_cc_collect(); return parkReports() > before })
				_ = clj_cc_collect()
				#expect(parkReports() == before + 1)
				_ = try eval("(cancel! stale-park) (def stale-park nil)")
				_ = Value(owning: clj_chan_put(held.raw, kw("v").raw))
			}
			base.check()
		}

		// mult's loop parks on its source with no taps; the mult, the source and the loop's channel are all dropped.
		// A first mult registers its reify type for the process: that one runs to its end before the baseline.
		@Test func aDroppedMultWithNoTaps() throws {
			_ = try eval("(let [c (chan)] (mult c) (close! c))")
			let base = CoroBaseline()
			let before = cancelledByCollection()
			_ = try eval("(let [c (chan)] (mult c) nil)")
			#expect(collected(atLeast: 1, since: before))
			base.check()
		}

		@Test func ontoChanIntoAnUndrainedBuffer() throws {
			let base = CoroBaseline()
			let before = cancelledByCollection()
			_ = try eval("(let [c (chan 1)] (onto-chan! c (range 10)) nil)")
			#expect(collected(atLeast: 1, since: before))
			base.check()
		}

		// A future parked on a promise nobody delivers: core.async's own suite leaves two of these.
		@Test func aFutureOnAnUndeliveredPromise() throws {
			try reset()
			let base = CoroBaseline()
			let before = cancelledByCollection()
			_ = try eval("(let [p (promise)] (future (try @p (finally (reset! unwound :future)))) nil)")
			#expect(collected(atLeast: 1, since: before))
			#expect(eventually { (try? eval("@unwound")) == kw("future") })
			base.check()
		}

		// The channel is held from Swift, which can still put to it: the park is alive however long it waits.
		@Test func aParkWhoseChannelSwiftHoldsIsNeverCollected() throws {
			try reset()
			let base = CoroBaseline()
			let before = cancelledByCollection()
			do {
				let held = try eval("(let [c (chan)] (go (reset! from-swift (<! c))) c)")
				#expect(noneCollected(since: before))
				// From this bare thread, not through a host call, under which a put may not park.
				_ = Value(owning: clj_chan_put(held.raw, kw("v").raw))
				#expect(eventually { (try? eval("@from-swift")) == kw("v") })
			}
			#expect(cancelledByCollection() == before)
			base.check()
		}

		@Test func aParkInsideWithOpenClosesItsResource() throws {
			try reset()
			let base = CoroBaseline()
			let before = cancelledByCollection()
			_ = try eval("(let [c (chan)] (go (with-open [r (->Held res-closed)] (<! c))) nil)")
			#expect(collected(atLeast: 1, since: before))
			#expect(eventually { (try? eval("@res-closed")) == Value(true) })
			base.check()
		}

		// A deadline only ever cancels, so its timer's reference does not keep the park: collection is that cancel, sooner.
		@Test func aPendingDeadlineDoesNotKeepAnAbandonedPark() throws {
			try reset()
			let base = CoroBaseline()
			let before = cancelledByCollection()
			_ = try eval("(with-deadline 60000 (let [c (chan)] (go (try (<! c) (finally (reset! unwound :deadline)))) nil))")
			#expect(collected(atLeast: 1, since: before))
			#expect(eventually { (try? eval("@unwound")) == kw("deadline") })
			// The finally runs inside the unwind; the deadline drops its timer when the coroutine finishes, after it.
			#expect(eventually { clj_debug_timers_held() == 0 })
			base.check()
		}

		// Its gate is woken only through a handle on the body, and the last one is gone.
		@Test func aSuspendedBodyNobodyCanResumeIsCollected() throws {
			try reset()
			let base = CoroBaseline()
			let before = cancelledByCollection()
			_ = try eval("(def suspended (go (try (loop [] (recur)) (finally (reset! unwound :suspended)))))")
			#expect(try eval("(suspend! suspended)") == Value(true))
			#expect(eventually { (try? eval("(gated? suspended)")) == Value(true) })
			_ = try eval("(def suspended nil)")
			#expect(collected(atLeast: 1, since: before))
			#expect(eventually { (try? eval("@unwound")) == kw("suspended") })
			base.check()
		}
	}
}
