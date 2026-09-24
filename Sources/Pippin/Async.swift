// @ai-generated(solo)
import CljCore
import Foundation

/// `callBlocking` refused: a frozen main thread is the failure mode the bridge exists to prevent (design §5).
public struct MainThreadBlocked: Error, CustomStringConvertible {
	public var description: String {
		"callBlocking would freeze the main thread: use callAsync, or callBlocking from another thread (design §5)"
	}
}

// Under a lock throughout: on_done may run on a carrier before clj_coro_spawn has returned here.
private final class CoroCall: @unchecked Sendable {
	private let lock = NSLock()
	private var continuation: CheckedContinuation<Value, any Error>?
	private var handle: Value?
	private var cancelRequested = false

	func start(_ fn: Value, _ args: [Value], _ affinity: Value.Affinity, _ continuation: CheckedContinuation<Value, any Error>) {
		lock.lock()
		self.continuation = continuation
		lock.unlock()
		// on_done takes this reference back; it may do so before the spawn returns.
		let ctx = Unmanaged.passRetained(self).toOpaque()
		let raw = withExtendedLifetime((fn, args)) {
			args.map(\.raw).withUnsafeBufferPointer {
				clj_coro_spawn(fn.raw, $0.baseAddress, $0.count, affinity.raw, CoroCall.done, ctx)
			}
		}
		guard raw != CLJ_THROWN else {
			Unmanaged<CoroCall>.fromOpaque(ctx).release()
			resume(.failure(ClojureError.takePending()))
			return
		}
		let coro = Value(owning: raw)
		lock.lock()
		handle = coro
		let pending = cancelRequested
		lock.unlock()
		// The Task was cancelled while the spawn was in flight; a finished coroutine ignores the flag.
		if pending { withExtendedLifetime(coro) { clj_coro_cancel(coro.raw) } }
	}

	func cancel() {
		lock.lock()
		cancelRequested = true
		let coro = handle
		lock.unlock()
		if let coro { withExtendedLifetime(coro) { clj_coro_cancel(coro.raw) } }
	}

	private static let done: @convention(c) (OpaquePointer?, UnsafeMutableRawPointer?) -> Void = { c, ctx in
		let call = Unmanaged<CoroCall>.fromOpaque(ctx!).takeRetainedValue()
		var threw = false
		let result = Value(borrowing: clj_coro_result(clj_from_ptr(UnsafeMutableRawPointer(c!)), &threw))
		call.resume(threw ? .failure(Value.asSwiftError(result)) : .success(result))
	}

	private func resume(_ result: Result<Value, any Error>) {
		lock.lock()
		let continuation = self.continuation
		self.continuation = nil
		lock.unlock()
		continuation?.resume(with: result)
	}
}

extension Value {
	/// A value a coroutine threw, as a Swift error: our cancellation is Swift's, so `withTaskCancellationHandler`
	/// and everything asking `is CancellationError` keep working (design §4).
	static func asSwiftError(_ thrown: Value) -> any Error {
		if thrown.isCancellation { return CancellationError() }
		return thrown.hostError ?? ClojureError(thrown: thrown)
	}

	/// Our `:cancelled` as a value — the singleton — which is what a Swift `CancellationError` becomes (design §4).
	static var cancellation: Value {
		let thrown = clj_throw_cancelled(false)
		assert(thrown == CLJ_THROWN, "clj_throw_cancelled must leave the cancellation pending")
		return Value(owning: clj_take_pending())
	}
}

extension Value {
	/// Which carrier a spawned coroutine runs on (design §4, `:affinity`).
	public enum Affinity: Sendable {
		/// Any carrier of the pool.
		case pool
		/// The carrier of `Runtime.installMainCarrier`: the body and every resume after a park run there, so the
		/// call advances only while that run loop turns, and throws when no main carrier is installed.
		case main

		var raw: Int32 { Int32(self == .main ? CLJ_AFFINITY_MAIN : CLJ_AFFINITY_POOL) }
	}

	/// Invokes the fn on a fresh coroutine and suspends the calling Task until it finishes: inside, the Clojure
	/// code may park freely (`<!`, `@`, `Thread/sleep`), since nothing of the host's is on its stack.
	///
	/// Cancelling the awaiting Task cancels the coroutine, which throws at its next park point; the call then
	/// throws `CancellationError`. Anything else thrown comes back as `callAsFunction` would deliver it.
	public func callAsync(_ args: Value..., affinity: Affinity = .pool) async throws -> Value {
		try await applyAsync(args, affinity: affinity)
	}

	/// `callAsync` over an argument array.
	public func applyAsync(_ args: [Value], affinity: Affinity = .pool) async throws -> Value {
		let call = CoroCall()
		return try await withTaskCancellationHandler {
			try await withCheckedThrowingContinuation { continuation in
				call.start(self, args, affinity, continuation)
			}
		} onCancel: {
			call.cancel()
		}
	}

	/// Starts the call and hands back its Task: nobody is waiting until someone awaits `.value`, and
	/// `Task.cancel()` cancels the coroutine. Detached, so the wait never occupies the caller's actor.
	public func callDetached(_ args: Value..., affinity: Affinity = .pool) -> Task<Value, any Error> {
		applyDetached(args, affinity: affinity)
	}

	/// `callDetached` over an argument array.
	public func applyDetached(_ args: [Value], affinity: Affinity = .pool) -> Task<Value, any Error> {
		Task.detached { try await self.applyAsync(args, affinity: affinity) }
	}

	/// The explicit opt-in that freezes the calling thread until the call finishes (the JVM's `<!!`).
	/// Refused on the main thread — that freeze is the one the design forbids outright (§5).
	///
	/// The body runs on a pool coroutine, so it may park; only this thread stands still. Called from a carrier
	/// (a host fn's body, `Value.apply`) it costs the pool that carrier until the call returns.
	public func callBlocking(_ args: Value...) throws -> Value { try applyBlocking(args) }

	/// `callBlocking` over an argument array.
	public func applyBlocking(_ args: [Value]) throws -> Value {
		if Thread.isMainThread || clj_coro_on_main_carrier() { throw MainThreadBlocked() }
		// A done callback, even an empty one, is what tells the coroutine its throw has a reader (sched.c finish).
		let spawned = withExtendedLifetime((self, args)) {
			args.map(\.raw).withUnsafeBufferPointer {
				clj_coro_spawn(raw, $0.baseAddress, $0.count, Int32(CLJ_AFFINITY_POOL), { _, _ in }, nil)
			}
		}
		if spawned == CLJ_THROWN { throw ClojureError.takePending() }
		let coro = Value(owning: spawned)
		return try withExtendedLifetime(coro) {
			clj_coro_join_blocking(coro.raw)
			var threw = false
			let result = Value(borrowing: clj_coro_result(coro.raw, &threw))
			if threw { throw Self.asSwiftError(result) }
			return result
		}
	}
}

// A park inside a host call is an error (host_depth), so the wait lives in `clojure.core/host-async-fn`, one
// frame out. Fetched by var and not evaluated here: a build with no interpreter has no reader (design §10).
private enum AsyncFn {
	static let wrap: Value = {
		clj_init()
		let name = Value(symbol: "host-async-fn")
		let v = withExtendedLifetime(name) { clj_ns_resolve(clj_ns_core(), name.raw) }
		precondition(clj_is_var(v) && clj_var_is_bound(v), "clojure.core/host-async-fn is missing from the core")
		return Value(borrowing: clj_var_root(v))
	}()
}

extension Value {
	/// A Swift `async` closure as an ordinary Clojure fn: the call starts a Task and parks the calling
	/// coroutine until the result arrives, so Clojure sees a synchronous fn (design §5, "the colour lives in
	/// Swift"). `arity` and `name` behave as in `Value(function:)`.
	///
	/// Cancellation crosses both ways: a cancelled caller cancels the Task, and a `CancellationError` out of
	/// the body arrives as our `:cancelled`. The caller must be able to park — from a synchronous host call
	/// (`Value.apply`) the fn raises the "cannot park inside a synchronous host call" error, as the design says.
	public init(asyncFunction name: String? = nil, arity: ClosedRange<Int>? = nil,
	            _ body: @escaping @Sendable ([Value]) async throws -> Value) {
		// Arity is the inner fn's, so the error names it and not the variadic wrapper.
		let inner = Value(function: name, arity: arity) { args in
			let promise = Value(owning: clj_chan_promise())
			let task = Task.detached {
				let outcome: Value
				do { outcome = Value([Value(true), try await body(args)]) } catch {
					outcome = Value([Value(false), Self.thrownValue(for: error)])
				}
				withExtendedLifetime((promise, outcome)) {
					// Nobody is left to deref after a cancelled caller walked away; the delivery is then a no-op.
					let delivered = clj_chan_deliver(promise.raw, outcome.raw)
					if delivered == CLJ_THROWN { _ = Value(owning: clj_take_pending()) } else { clj_release(delivered) }
				}
			}
			return Value([promise, Value(function: "cancel") { _ in
				task.cancel()
				return nil
			}])
		}
		self = try! AsyncFn.wrap(inner)
	}

	// What Clojure rethrows for a Swift error: its own value for a ClojureError, :cancelled for a cancelled Task.
	private static func thrownValue(for error: any Error) -> Value {
		if error is CancellationError { return cancellation }
		if let e = error as? ClojureError { return e.thrown }
		return Value(hostError: error)
	}
}

extension Runtime {
	/// `define` with a Swift `async` body: the var holds the fn of `Value(asyncFunction:)`, which Clojure calls
	/// like any other.
	@discardableResult
	public func define(_ name: String, in namespace: String = "user", arity: ClosedRange<Int>? = nil, doc: String? = nil,
	                   asyncBody: @escaping @Sendable ([Value]) async throws -> Value) -> Value {
		Self.bind(name, in: namespace, doc: doc, Value(asyncFunction: "\(namespace)/\(name)", arity: arity, asyncBody))
	}
}
