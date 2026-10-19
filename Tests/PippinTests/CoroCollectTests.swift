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
			(ns coro-collect-tests (:require [clojure.core.async :refer [chan <! >! >!! timeout go go-loop alts! mult mix admix onto-chan! suspend! close!]]))
			(refer 'test-support)
			(defprotocol Res (-close [r]))
			(deftype Held [closed] Res (-close [_] (reset! closed true)))
			(def unwound (atom nil))
			(def res-closed (atom false))
			(def from-swift (atom nil))
			(def alts-done (atom nil))
			(def suspended nil)
			""")
			for k in ["v", "loop", "chan", "timeout", "suspended", "deadline", "future", "alts", "let"] { _ = kw(k) }
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
