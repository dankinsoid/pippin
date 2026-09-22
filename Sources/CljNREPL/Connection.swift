// @ai-generated(solo)
import Foundation

/// Writes queue behind a serial worker, so a slow client can't block the carrier that produced a message.
final class Connection: @unchecked Sendable {
	private let socket: TCPConnection
	private let writeQueue = DispatchQueue(label: "clj-nrepl.connection-writer")
	private let lock = NSLock()
	private var owned: Set<String> = []

	init(_ socket: TCPConnection) { self.socket = socket }

	func send(_ dict: [String: BValue]) {
		let bytes = Bencode.encode(.dict(dict))
		let socket = self.socket
		writeQueue.async { _ = try? socket.writeAll(bytes) }
	}

	func own(_ sessionID: String) { lock.withLock { _ = owned.insert(sessionID) } }
	func disown(_ sessionID: String) { lock.withLock { _ = owned.remove(sessionID) } }
	var ownedSessions: Set<String> { lock.withLock { owned } }
}
