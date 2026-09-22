// @ai-generated(solo)
import CljCore
import Foundation
import Pippin

/// One process, one `Runtime`, shared by every session. Never installs the main carrier: headless, so
/// `go-main`/`:affinity :main` code errors immediately rather than hanging (NOTES "Scheduler").
public final class Server: @unchecked Sendable {
	let runtime = Runtime()
	let helpers: NReplHelpers
	private var sessions: [String: Session] = [:]
	private let lock = NSLock()
	private var listener: TCPListener?

	public init() throws {
		helpers = try NReplHelpers(runtime)
	}

	func session(_ id: String) -> Session? { lock.withLock { sessions[id] } }
	func addSession(_ s: Session) { lock.withLock { sessions[s.id] = s } }
	func removeSession(_ id: String) { lock.withLock { _ = sessions.removeValue(forKey: id) } }

	// Adopts a session id the client names but we don't know, so the reply echoes back the id it expects.
	func resolveSession(_ msg: [String: BValue], _ conn: Connection) -> Session {
		let requested = msg["session"]?.asString
		if let requested, let existing = session(requested) { return existing }
		let created = Session(id: requested ?? UUID().uuidString)
		addSession(created)
		conn.own(created.id)
		return created
	}

	/// Starts listening and returns the bound port (resolved when `port` is 0).
	public func start(host: String, port: UInt16) throws -> UInt16 {
		let listener = try TCPListener(host: host, port: port)
		self.listener = listener
		let bound = listener.port
		Thread.detachNewThread { [self] in acceptLoop(listener) }
		return bound
	}

	/// Stops accepting new connections; open ones finish on their own once their client disconnects.
	public func stop() { listener?.stop() }

	private func acceptLoop(_ listener: TCPListener) {
		while true {
			guard let socket = try? listener.accept() else {
				if listener.isStopped { return }
				continue
			}
			let conn = Connection(socket)
			Thread.detachNewThread { [self] in readLoop(socket, conn) }
		}
	}

	private func readLoop(_ socket: TCPConnection, _ conn: Connection) {
		defer {
			for id in conn.ownedSessions { removeSession(id) }
			socket.close()
		}
		var buffer: [UInt8] = []
		while true {
			let chunk = socket.read()
			if chunk.isEmpty { return }
			buffer.append(contentsOf: chunk)
			while true {
				do {
					guard let (value, consumed) = try Bencode.decode(buffer) else { break }
					buffer.removeFirst(consumed)
					if case .dict(let dict) = value { dispatch(dict, conn) }
				} catch {
					return // a malformed message forfeits the connection; a real client never sends one
				}
			}
		}
	}

	private func dispatch(_ msg: [String: BValue], _ conn: Connection) {
		switch msg["op"]?.asString {
		case "clone": Ops.clone(self, msg, conn)
		case "close": Ops.close(self, msg, conn)
		case "describe": Ops.describe(self, msg, conn)
		case "eval": Ops.eval(self, msg, conn)
		case "load-file": Ops.loadFile(self, msg, conn)
		case "complete": Ops.complete(self, msg, conn)
		case "info": Ops.info(self, msg, conn)
		case "interrupt": Ops.interrupt(self, msg, conn)
		case "stdin": Ops.stdin(self, msg, conn)
		default: conn.send(Ops.reply(msg, ["status": .list([.string("done"), .string("unknown-op")])]))
		}
	}
}
