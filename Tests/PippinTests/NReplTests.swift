// @ai-generated(solo)
import CljCore
import CljNREPL
import Foundation
import Testing

// Nests inside CoreTests: spawns real coroutines against the process-wide live-object counters (.serialized).
extension CoreTests {
	// End to end over a real socket: clone, eval, interrupt a form parked on a channel, then complete.
	@Suite struct NReplTests {
		private final class Client {
			let socket: TCPConnection
			var buffer: [UInt8] = []
			init(port: UInt16) throws { socket = try TCPConnection(connectingTo: "127.0.0.1", port: port) }

			func send(_ dict: [String: BValue]) throws { try socket.writeAll(Bencode.encode(.dict(dict))) }

			func recvOne() throws -> [String: BValue] {
				while true {
					if let (value, consumed) = try Bencode.decode(buffer), case .dict(let dict) = value {
						buffer.removeFirst(consumed)
						return dict
					}
					let chunk = socket.read()
					try #require(!chunk.isEmpty, "connection closed while waiting for a reply")
					buffer.append(contentsOf: chunk)
				}
			}

			func recvUntilDone() throws -> [[String: BValue]] {
				var out: [[String: BValue]] = []
				while true {
					let m = try recvOne()
					out.append(m)
					if m["status"]?.asStringList?.contains("done") == true { return out }
				}
			}
		}

		@Test func evalInterruptAndCompleteOverTheWire() throws {
			let server = try Server()
			let port = try server.start(host: "127.0.0.1", port: 0)
			let client = try Client(port: port)
			defer { client.socket.close() }

			try client.send(["op": .string("clone")])
			let session = try #require(client.recvUntilDone().last?["new-session"]?.asString)

			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("1"), "code": .string("(+ 1 2)")])
			let evalReply = try client.recvUntilDone()
			#expect(evalReply.contains { $0["value"]?.asString == "3" })
			#expect(evalReply.last?["status"]?.asStringList == ["done"])

			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("2"),
			                  "code": .string("(require '[clojure.core.async :refer [chan <!!]])")])
			_ = try client.recvUntilDone()

			// A form genuinely parked on a channel take, interrupted mid-flight.
			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("3"), "code": .string("(<!! (chan))")])
			Thread.sleep(forTimeInterval: 0.2)
			try client.send(["op": .string("interrupt"), "session": .string(session), "interrupt-id": .string("3")])
			#expect(try client.recvUntilDone().last?["status"]?.asStringList == ["done"])
			let interrupted = try client.recvUntilDone()
			#expect(interrupted.last?["status"]?.asStringList?.contains("interrupted") == true)

			// The session survives the interrupt and keeps its *ns*/history for the next eval.
			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("4"), "code": .string("(+ 40 2)")])
			#expect(try client.recvUntilDone().contains { $0["value"]?.asString == "42" })

			try client.send(["op": .string("complete"), "session": .string(session), "id": .string("5"),
			                  "symbol": .string("defr"), "ns": .string("clojure.core")])
			let completions = try client.recvUntilDone().last?["completions"]
			guard case .list(let items) = completions else {
				Issue.record("no completions in the reply")
				return
			}
			let candidates = items.compactMap { item -> String? in
				guard case .dict(let d) = item else { return nil }
				return d["candidate"]?.asString
			}
			#expect(candidates.contains("defrecord"))

			try client.send(["op": .string("close"), "session": .string(session), "id": .string("6")])
			#expect(try client.recvUntilDone().last?["status"]?.asStringList == ["done"])

			server.stop()
			#expect(clj_debug_coro_settle(0, 2000), "leftover nREPL eval coroutines after the test")
		}
	}
}
