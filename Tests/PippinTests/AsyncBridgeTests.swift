// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'async-bridge-tests) " + source) }

private func kw(_ s: String) -> Value { Value(keyword: s) }

private struct Boom: Error, Equatable {
	let code: Int
}

// What the Swift side of a crossing saw, written from whatever thread got there first.
private final class Notes: @unchecked Sendable {
	private let lock = NSLock()
	private var seen: Set<String> = []

	func note(_ what: String) {
		lock.lock()
		seen.insert(what)
		lock.unlock()
	}

	func has(_ what: String) -> Bool {
		lock.lock()
		defer { lock.unlock() }
		return seen.contains(what)
	}

	// The other side runs on its own carrier or executor: every cross-thread expectation is a wait with a deadline.
	func await_(_ what: String, ms: Int = 5000) async -> Bool {
		for _ in 0..<ms {
			if has(what) { return true }
			try? await Task.sleep(nanoseconds: 1_000_000)
		}
		return false
	}
}

extension CoreTests {
	// The async bridge of design §5, both directions, cancellation included.
	@Suite struct AsyncBridgeTests {
		let rt = Runtime()

		init() throws {
			clj_init()
			_ = try cljEvalScoped("(ns async-bridge-tests (:require [clojure.core.async :refer [chan <! >! <!! >!! close! timeout go cancel! cancelled? poll!]]))")
			for k in ["took", "slept", "cancelled", "done", "boom", "gate", "x"] { _ = kw(k) }
			_ = try cljEvalScoped("(in-ns 'async-bridge-tests) (declare gate outcome)")
		}

		@Test func callAsyncRunsTheFnOnACoroutineThatMayPark() async throws {
			let f = try eval("(fn [x] (<! (timeout 5)) (* 2 x))")
			#expect(try await f.callAsync(21) == 42)
			#expect(try await eval("(fn [] (<!! (go :took)))").callAsync() == kw("took"))
		}

		@Test func callAsyncThrowsWhatTheCoroutineThrew() async throws {
			let f = try eval("(fn [] (throw (ex-info \"boom\" {:k 1})))")
			await #expect(throws: ClojureError.self) { try await f.callAsync() }
			do {
				_ = try await f.callAsync()
			} catch let e as ClojureError {
				#expect(e.message == "boom")
				#expect(e.trace.count > 0)
			}
			// A Swift error thrown inside comes back as itself, as through callAsFunction.
			let failing = Value(function: "ab-fail") { _ in throw Boom(code: 7) }
			let caller = try eval("(fn [f] (<! (timeout 1)) (f))")
			do {
				_ = try await caller.callAsync(failing)
				Issue.record("expected the Swift error")
			} catch let e as Boom {
				#expect(e == Boom(code: 7))
			}
		}

		@Test func callDetachedIsAHandleOnARunningCoroutine() async throws {
			let f = try eval("(fn [] (<! (timeout 5)) :done)")
			let task = f.callDetached()
			#expect(try await task.value == kw("done"))
		}

		// Cancellation during the wait, Swift → Clojure: the coroutine is parked on a channel nobody feeds.
		@Test func cancellingTheAwaitingTaskCancelsTheCoroutine() async throws {
			let notes = Notes()
			let note = Value(function: "ab-note") { args in
				notes.note(args[0].string ?? args[0].description)
				return nil
			}
			_ = try eval("(def gate (chan))")
			let gate = try eval("gate")
			let f = try eval("(fn [note] (try (<! gate) :took (catch :cancelled e (note \"cancelled\") (throw e))))")
			let task = f.callDetached(note)
			// The coroutine is on the gate's take queue: only then is the cancel one that lands during the wait.
			var waited = 0
			while withExtendedLifetime(gate, { clj_debug_chan_pending(gate.raw, false) }) == 0 && waited < 5000 {
				try await Task.sleep(nanoseconds: 1_000_000)
				waited += 1
			}
			#expect(waited < 5000)
			task.cancel()
			await #expect(throws: CancellationError.self) { try await task.value }
			#expect(notes.has("cancelled"))
			_ = try eval("(close! gate) (def gate nil)")
		}

		// A Task cancelled before the await even starts still spawns and cancels: the flag is not lost.
		@Test func aTaskCancelledBeforeTheCallStillEndsCancelled() async throws {
			let f = try eval("(fn [] (<! (chan)) :never)")
			let task = f.callDetached()
			task.cancel()
			await #expect(throws: CancellationError.self) { try await task.value }
		}

		@Test func callBlockingRunsOnTheCallingThread() async throws {
			let f = try eval("(fn [] (<! (timeout 5)) :slept)")
			let value = try await Task.detached { try f.callBlocking() }.value
			#expect(value == kw("slept"))
			let failing = try eval("(fn [] (throw (ex-info \"boom\" {})))")
			await #expect(throws: ClojureError.self) { try await Task.detached { try failing.callBlocking() }.value }
		}

		@Test func callBlockingRefusesOnTheMainThread() async throws {
			let f = try eval("(fn [] :done)")
			await MainActor.run {
				#expect(throws: MainThreadBlocked.self) { try f.callBlocking() }
			}
		}

		// Piece 3: Clojure sees an ordinary fn; the park happens in the wrapper, after the Swift frame returned.
		@Test func aSwiftAsyncClosureIsAnOrdinaryClojureFn() async throws {
			let double = Value(asyncFunction: "ab-double", arity: 1...1) { args in
				try? await Task.sleep(nanoseconds: 1_000_000)
				return Value(args[0].int! * 2)
			}
			#expect(double.isFn)
			#expect(try await eval("(fn [f] (<! (go (f 21))))").callAsync(double) == 42)
			#expect(try await eval("(fn [f] (<! (go (+ (f 1) (f 2)))))").callAsync(double) == 6)
			// The arity is the inner fn's, so the error names it.
			#expect(try await eval("(fn [f] (<! (go (try (f 1 2) (catch :default e (ex-message e))))))").callAsync(double)
				== Value("Wrong number of args (2) passed to: ab-double"))
		}

		// A Swift async fn called from a synchronous host call: the park is the error the design promises (§5).
		@Test func aSwiftAsyncClosureRefusesInsideASynchronousHostCall() throws {
			let f = Value(asyncFunction: "ab-sync") { _ in Value(1) }
			do {
				_ = try f()
				Issue.record("expected the park error")
			} catch let e as ClojureError {
				#expect(e.message.hasPrefix("Cannot park inside a synchronous host call"))
			}
		}

		@Test func aSwiftErrorFromAnAsyncClosureIsCaughtInClojure() async throws {
			let failing = Value(asyncFunction: "ab-boom") { _ in
				try await Task.sleep(nanoseconds: 1_000_000)
				throw Boom(code: 3)
			}
			#expect(try await eval("(fn [f] (<! (go (try (f) (catch :default e (ex-message e))))))").callAsync(failing)
				== Value("Boom(code: 3)"))
			// Uncaught, it reaches the host as the Swift error itself.
			let caller = try eval("(fn [f] (f))")
			do {
				_ = try await caller.callAsync(failing)
				Issue.record("expected the Swift error")
			} catch let e as Boom {
				#expect(e == Boom(code: 3))
			}
		}

		// A CancellationError out of the Swift body is our :cancelled, not a host error (design §4).
		@Test func aCancellationErrorFromTheBodyArrivesAsCancelled() async throws {
			let f = Value(asyncFunction: "ab-cancel") { _ in throw CancellationError() }
			#expect(try await eval("(fn [f] (<! (go (try (f) (catch :cancelled e (ex-type e))))))").callAsync(f) == kw("cancelled"))
		}

		// Cancellation during the wait, Clojure → Swift: the go is cancelled while the Task is still sleeping.
		@Test func cancellingTheParkedCoroutineCancelsTheTask() async throws {
			let notes = Notes()
			rt.define("ab-slow", in: "async-bridge-tests", arity: 0...0, asyncBody: { _ in
				notes.note("entered")
				do {
					try await Task.sleep(nanoseconds: 30_000_000_000)
				} catch {
					notes.note("cancelled")
					throw error
				}
				return kw("never")
			})
			let go = try eval("(go (try (ab-slow) (catch :cancelled e :cancelled)))")
			#expect(await notes.await_("entered"))
			#expect(try withExtendedLifetime(go) { Value(owning: clj_chan_cancel(go.raw)) } == true)
			#expect(try await eval("(fn [g] (<! g))").callAsync(go) == kw("cancelled"))
			#expect(await notes.await_("cancelled"))
			_ = try eval("(def ab-slow nil)")
		}

		// The nesting the two pieces make possible: a go calls a Swift async fn, which calls back into Clojure.
		@Test func aGoCallsSwiftWhichCallsClojureBack() async throws {
			let inner = try eval("(fn [x] (<! (timeout 5)) (inc x))")
			let roundTrip = Value(asyncFunction: "ab-round", arity: 1...1) { args in
				try await inner.callAsync(args[0])
			}
			#expect(try await eval("(fn [f] (<! (go (f (f 40)))))").callAsync(roundTrip) == 42)
		}
	}
}
