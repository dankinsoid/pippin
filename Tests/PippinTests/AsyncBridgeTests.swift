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

// Every test here spawns, and none takes a live-object baseline of its own: a coroutine still running when
// one ends fails the baseline of whatever suite runs next (CoroBaseline, ChanTests.swift).
struct SettledTrait: SuiteTrait, TestTrait, TestScoping {
	var isRecursive: Bool { true }

	func provideScope(for test: Test, testCase: Test.Case?, performing function: @Sendable () async throws -> Void) async throws {
		let before = clj_debug_live_coros()
		try await function()
		#expect(clj_debug_coro_settle(before, 5000), "\(test.name) left a coroutine running")
	}
}

extension CoreTests {
	// The async bridge of design §5, both directions, cancellation included.
	@Suite(SettledTrait()) struct AsyncBridgeTests {
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

		// Piece 4: the typed adapters of design §5 — the caller who knows the signature writes Swift types.
		@Test func aTypedAdapterConvertsTheArgumentsAndTheResult() throws {
			let answer: @Sendable () throws -> Int = try eval("(fn [] 42)").closure()
			#expect(try answer() == 42)
			let shout: @Sendable (String) throws -> String = try eval("(fn [s] (str s \"!\"))").closure()
			#expect(try shout("ab") == "ab!")
			let square: @Sendable (Double, Double) throws -> Double = try eval("(fn [a b] (+ (* a a) (* b b)))").closure()
			#expect(try square(3.0, 4.0) == 25.0)
			let between: @Sendable (Int, Int, Int) throws -> Bool = try eval("(fn [lo x hi] (and (<= lo x) (< x hi)))").closure()
			#expect(try between(0, 1, 2))
			// Clojure truthiness, so nil is false rather than a decoding failure; Bool? keeps the third value.
			let maybe: @Sendable (Int) throws -> Bool = try eval("(fn [x] (when (pos? x) :yes))").closure()
			#expect(try maybe(1))
			#expect(try !maybe(-1))
			let maybeOpt: @Sendable (Int) throws -> Bool? = try eval("(fn [x] (when (pos? x) false))").closure()
			#expect(try maybeOpt(1) == false)
			#expect(try maybeOpt(-1) == nil)
			// Collections and Values ride as themselves.
			let sum: @Sendable ([Int]) throws -> Int = try eval("(fn [xs] (reduce + xs))").closure()
			#expect(try sum([1, 2, 3]) == 6)
			let reversed: @Sendable ([Int]) throws -> [Int] = try eval("(fn [xs] (reverse xs))").closure()
			#expect(try reversed([1, 2]) == [2, 1])
			let identity: @Sendable (Value) throws -> Value = try eval("(fn [x] x)").closure()
			#expect(try identity(kw("x")) == kw("x"))
			// A void adapter drops the result, which is why Void needs no conformance of its own.
			let notes = Notes()
			let heard = Value(function: "ab-heard", arity: 1...1) { args in
				notes.note(args[0].string ?? "")
				return kw("ignored")
			}
			let tell: @Sendable (String) throws -> Void = try heard.closure()
			try tell("heard")
			#expect(notes.has("heard"))
			// A result of another kind is the one type check left, and it is the result's, not the signature's.
			let wrong: @Sendable () throws -> Int = try eval("(fn [] :not-a-number)").closure()
			#expect(throws: ValueTypeMismatch.self) { try wrong() }
			// A throw crosses as it does through callAsFunction.
			let boom: @Sendable () throws -> Int = try eval("(fn [] (throw (ex-info \"boom\" {})))").closure()
			#expect(throws: ClojureError.self) { try boom() }
		}

		// The point of the feature: the signature is checked when the wrapper is made, so the call has no check.
		@Test func aTypedAdapterChecksTheSignatureWhenItIsCreated() throws {
			let notes = Notes()
			let two = Value(function: "ab-two", arity: 2...2) { args in
				notes.note("called")
				return Value(args[0].int! + args[1].int!)
			}
			#expect(throws: ClosureSignatureMismatch.self) { let _: @Sendable (Int) throws -> Int = try two.closure() }
			#expect(throws: ClosureSignatureMismatch.self) { let _: @Sendable (Int, Int, Int) throws -> Int = try two.closure() }
			#expect(throws: ClosureSignatureMismatch.self) { let _: @Sendable (Int, Int, Int) throws -> Void = try two.closure() }
			#expect(!notes.has("called"), "the arity error must land before anything is called")
			let ok: @Sendable (Int, Int) throws -> Int = try two.closure()
			#expect(try ok(1, 2) == 3)
			// A Clojure fn's own arity table, fixed and variadic.
			let multi = try eval("(fn ([a] a) ([a b] (+ a b)))")
			#expect(throws: ClosureSignatureMismatch.self) { let _: @Sendable () throws -> Int = try multi.closure() }
			#expect(throws: ClosureSignatureMismatch.self) { let _: @Sendable (Int, Int, Int) throws -> Int = try multi.closure() }
			let variadic = try eval("(fn [a & more] (count more))")
			#expect(throws: ClosureSignatureMismatch.self) { let _: @Sendable () throws -> Int = try variadic.closure() }
			let rest: @Sendable (Int, Int, Int) throws -> Int = try variadic.closure()
			#expect(try rest(1, 2, 3) == 2)
			// A keyword is invokable but has no arity to check, so it is refused rather than checked per call.
			#expect(throws: ClosureSignatureMismatch.self) { let _: @Sendable (Value) throws -> Value = try kw("x").closure() }
			// Same check on the async adapters.
			#expect(throws: ClosureSignatureMismatch.self) { let _: @Sendable (Int) async throws -> Int = try two.closureAsync() }
		}

		// A host signature that cannot throw decides at the stub what a failure does (design §5).
		@Test func aNonThrowingAdapterAnswersThroughItsPolicy() throws {
			let notes = Notes()
			let failing = try eval("(fn [x] (throw (ex-info \"boom\" {:x x})))")
			let sync: @Sendable (Int) -> Int = try failing.closure(onFailure: { error in
				notes.note("\(error)".hasPrefix("boom") ? "boom" : "other")
				return -1
			})
			#expect(sync(7) == -1)
			#expect(notes.has("boom"))
			let void: @Sendable (Int) -> Void = try failing.closure(onFailure: { _ in notes.note("void") })
			// The policy argument is what the stub decides; the signature is still checked first.
			#expect(throws: ClosureSignatureMismatch.self) { let _: @Sendable () -> Int = try failing.closure(onFailure: Value.trap) }
			void(7)
			#expect(notes.has("void"))
			// A result of the wrong kind takes the same road as a throw.
			let mistyped: @Sendable () -> Int = try eval("(fn [] :nope)").closure(onFailure: { _ in 0 })
			#expect(mistyped() == 0)
		}

		// The async adapter spawns as callAsync does, so the Clojure body may park inside it.
		@Test func anAsyncTypedAdapterParksInsideTheCall() async throws {
			let slow: @Sendable (Int) async throws -> Int = try eval("(fn [x] (<! (timeout 5)) (* 2 x))").closureAsync()
			#expect(try await slow(21) == 42)
			let notes = Notes()
			let fire: @Sendable (String) async throws -> Void = try Value(function: "ab-fire", arity: 1...1) { args in
				notes.note(args[0].string ?? "")
				return nil
			}.closureAsync()
			try await fire("fired")
			#expect(notes.has("fired"))
			let boom: @Sendable () async throws -> Int = try eval("(fn [] (<! (timeout 1)) (throw (ex-info \"boom\" {})))").closureAsync()
			await #expect(throws: ClojureError.self) { try await boom() }
		}

		// Piece 5: `:affinity :main`. The body and its resume after a park run on the carrier the host installed.
		@Test func aMainAffinityCallRunsOnTheMainCarrier() async throws {
			let carrier = Value(function: "ab-carrier", arity: 0...0) { _ in Value(clj_coro_on_main_carrier()) }
			let f = try eval("(fn [c] (let [before (c)] (<! (timeout 1)) [before (c)]))")
			// Without a carrier the spawn itself fails: the host has to install one first.
			do {
				_ = try await f.callAsync(carrier, affinity: .main)
				Issue.record("expected the missing-carrier error")
			} catch let e as ClojureError {
				#expect(e.message.hasPrefix("No main carrier"))
			}
			// The test thread is the carrier and pumps by hand, as CoroTests does; nothing awaits until it stops.
			let done = Notes()
			clj_debug_sched_main_adopt()
			let task = f.callDetached(carrier, affinity: .main)
			let watcher = Task.detached { _ = try? await task.value; done.note("task") }
			var pumps = 0
			while !done.has("task") && pumps < 20000 {
				clj_sched_main_pump()
				usleep(200)
				pumps += 1
			}
			clj_debug_sched_main_abandon()
			#expect(pumps < 20000, "the main-affinity call never finished")
			_ = await watcher.value
			#expect(try await task.value == Value([Value(true), Value(true)]))
			// The default is the pool, where the same fn sees no main carrier at either point.
			#expect(try await f.callAsync(carrier) == Value([Value(false), Value(false)]))
		}
	}
}
