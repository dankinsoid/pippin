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
			(ns coro-collect-tests (:require [clojure.core.async :refer [chan <! >! >!! timeout go go-loop alts! mult onto-chan! suspend!]]))
			(refer 'test-support)
			(defprotocol Res (-close [r]))
			(deftype Held [closed] Res (-close [_] (reset! closed true)))
			(def unwound (atom nil))
			(def res-closed (atom false))
			(def from-swift (atom nil))
			(def alts-done (atom nil))
			(def suspended nil)
			""")
			for k in ["v", "loop", "chan", "timeout", "suspended", "deadline", "future"] { _ = kw(k) }
		}

		private func reset() throws { _ = try eval("(reset! unwound nil) (reset! res-closed false) (reset! from-swift nil) (reset! alts-done nil)") }

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

		// The literal port vector is a frame's own temporary, so the channel under it reads as held: what wakes this
		// park is the timer, and only its firing ends it.
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

		// mult's loop parks on its source with no taps; the mult, the source and the loop's channel are all dropped.
		@Test func aDroppedMultWithNoTaps() throws {
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
			#expect(clj_debug_timers_held() == 0)
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
