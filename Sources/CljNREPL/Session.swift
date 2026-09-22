// @ai-generated(solo)
import CljCore
import Foundation
import Pippin

/// One nREPL session: its whole dynamic binding frame, at most one eval in flight (keeps it race-free).
public final class Session {
	public let id: String
	private let lock = NSLock()
	private var bindings: Value
	private var busy = false
	private var inFlight: (id: String, coro: Value)?
	private var pending: [() -> Void] = []

	public init(id: String = UUID().uuidString, frame: Value) {
		self.id = id
		bindings = frame
	}

	public convenience init(id: String = UUID().uuidString, namespace: String = "user") {
		self.init(id: id, frame: ReplVars.defaultFrame(namespace: namespace))
	}

	/// The frame to push for the next eval, and what `clone` hands a new session (persistent: sharing it costs nothing).
	var currentFrame: Value { lock.withLock { bindings } }

	public var currentNamespace: String { ReplVars.namespace(in: currentFrame) }

	/// Replaces the frame with what the coroutine's eval captured; skipped for a cancelled eval (below).
	func updateFrame(_ frame: Value) { lock.withLock { bindings = frame } }

	/// Runs `work` now if idle, else queues it; `work` must call `finishEval()` when its eval completes.
	func scheduleEval(_ work: @escaping () -> Void) {
		lock.lock()
		if busy {
			pending.append(work)
			lock.unlock()
		} else {
			busy = true
			lock.unlock()
			work()
		}
	}

	func setInFlight(id: String, coro: Value) {
		lock.withLock { inFlight = (id, coro) }
	}

	/// Called from the eval coroutine's `onDone`: hands the slot to the next queued eval, or frees it.
	func finishEval() {
		lock.lock()
		inFlight = nil
		guard !pending.isEmpty else {
			busy = false
			lock.unlock()
			return
		}
		let next = pending.removeFirst()
		lock.unlock()
		next()
	}

	enum InterruptResult { case idle, mismatch, interrupted }

	func interrupt(matching wantID: String?) -> InterruptResult {
		lock.lock()
		guard let cur = inFlight else {
			lock.unlock()
			return .idle
		}
		if let wantID, wantID != cur.id {
			lock.unlock()
			return .mismatch
		}
		lock.unlock()
		clj_coro_cancel(cur.coro.raw) // the existing cancellation (NOTES "Coroutines"); the eval sees it as a throw
		return .interrupted
	}
}

extension NSLock {
	func withLock<T>(_ body: () -> T) -> T {
		lock()
		defer { unlock() }
		return body()
	}
}
