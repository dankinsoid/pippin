// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// Polled with 1 ms timers each waited out: none is left pending under a live-object baseline.
func cljTimingSupport() throws {
	_ = try cljEvalScoped("""
		(ns test-support (:require [clojure.core.async :refer [<!! timeout poll!]]))
		(defn await-true
		  "(pred)'s first truthy answer, polled every millisecond; :timed-out after ms polls."
		  [ms pred]
		  (loop [i 0] (or (pred) (if (< i ms) (do (<!! (timeout 1)) (recur (inc i))) :timed-out))))
		(defn join
		  "The value of the body behind ch, or :timed-out after ms. A body whose value is nil reads as :timed-out."
		  [ch ms]
		  (await-true ms #(poll! ch)))
		""")
	let rt = Runtime()
	rt.define("gated?", in: "test-support", arity: 1...1) { args in Value(clj_debug_chan_gated(args[0].raw)) }
	rt.define("pending-takes", in: "test-support", arity: 1...1) { args in Value(Int(clj_debug_chan_pending(args[0].raw, false))) }
	rt.define("pending-puts", in: "test-support", arity: 1...1) { args in Value(Int(clj_debug_chan_pending(args[0].raw, true))) }
}

func eventually(ms: Int = 10_000, _ cond: () throws -> Bool) rethrows -> Bool {
	for _ in 0..<ms {
		if try cond() { return true }
		usleep(1000)
	}
	return try cond()
}
