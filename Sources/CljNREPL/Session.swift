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
	private var lines: Value?
	// The text a `read` left on its line, for the next `read` or `read-line` of any later eval of this session.
	private let pushback = Value(owning: clj_volatile_new(CLJ_NIL))
	private var partialLine = ""
	private var inputEnded = false

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

	/// `*in*` for one eval: the session's lines plus the fn that asks this request's client for the next chunk.
	func inputBinding(id: String, conn: Connection) -> Value {
		let channel = lock.withLock { linesChannel() }
		let sessionID = self.id
		let request = Value(function: "clj-nrepl/need-input", arity: 0...0) { _ in
			conn.send(["id": .string(id), "session": .string(sessionID), "status": .list([.string("need-input")])])
			return .nil_
		}
		return withExtendedLifetime((channel, request)) {
			Value([Value(keyword: "lines"): channel, Value(keyword: "request"): request, Value(keyword: "pushback"): pushback])
		}
	}

	/// The `stdin` op's payload: whole lines only, and an empty payload is end of input (nREPL's own EOF).
	func acceptInput(_ text: String) {
		var ready: [String] = []
		var end = false
		lock.lock()
		if text.isEmpty {
			if !partialLine.isEmpty { ready.append(partialLine) }
			partialLine = ""
			end = !inputEnded
			inputEnded = true
		} else {
			partialLine += text
			while let newline = partialLine.firstIndex(of: "\n") {
				ready.append(String(partialLine[partialLine.startIndex..<newline]))
				partialLine = String(partialLine[partialLine.index(after: newline)...])
			}
		}
		let channel = linesChannel()
		lock.unlock()
		for line in ready { put(channel, line.hasSuffix("\r") ? String(line.dropLast()) : line) }
		if end { closeChannel(channel) }
	}

	/// End of input for a client that left: a read parked on it answers nil rather than holding its coroutine.
	func endInput() {
		lock.lock()
		let channel = lines
		partialLine = ""
		let end = !inputEnded
		inputEnded = true
		lock.unlock()
		if end, let channel { closeChannel(channel) }
	}

	private func linesChannel() -> Value {
		if let lines { return lines }
		let channel = Value(owning: clj_chan_new(CLJ_NIL))
		lines = channel
		return channel
	}

	// put!, not >!: the connection's reader thread must not park, and a line waits in the channel's queue anyway.
	private func put(_ channel: Value, _ line: String) {
		let value = Value(line)
		discardResult(withExtendedLifetime((channel, value)) { clj_chan_put_cb(channel.raw, value.raw, CLJ_NIL, true) })
	}

	private func closeChannel(_ channel: Value) {
		discardResult(withExtendedLifetime(channel) { clj_chan_close(channel.raw) })
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
