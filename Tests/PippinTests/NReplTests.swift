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

		// *1 *2 *3 *e persist across evals, clone hands off an independent snapshot, interrupt does not poison it.
		// An interrupt is clj_coro_cancel, so it is :cancelled too: :default lets it through (design §4).
		@Test func interruptIsTheCancelledType() throws {
			let server = try Server()
			let port = try server.start(host: "127.0.0.1", port: 0)
			let client = try Client(port: port)
			defer { client.socket.close() }

			try client.send(["op": .string("clone")])
			let session = try #require(client.recvUntilDone().last?["new-session"]?.asString)

			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("1"),
			                  "code": .string("(require '[clojure.core.async :refer [chan <!!]])")])
			_ = try client.recvUntilDone()

			// Caught by :cancelled: the eval completes normally with the handler's value, not "interrupted".
			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("2"),
			                  "code": .string("(try (<!! (chan)) (catch :cancelled e [(ex-type e) (:cancel/kind (ex-data e))]))")])
			Thread.sleep(forTimeInterval: 0.2)
			try client.send(["op": .string("interrupt"), "session": .string(session), "interrupt-id": .string("2")])
			#expect(try client.recvUntilDone().last?["status"]?.asStringList == ["done"])
			let caught = try client.recvUntilDone()
			#expect(caught.contains { $0["value"]?.asString == "[:cancelled :explicit]" })
			#expect(caught.last?["status"]?.asStringList == ["done"])

			// :default lets it by: the eval ends "interrupted", same as an untried (<!! (chan)).
			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("3"),
			                  "code": .string("(try (<!! (chan)) (catch :default e :caught))")])
			Thread.sleep(forTimeInterval: 0.2)
			try client.send(["op": .string("interrupt"), "session": .string(session), "interrupt-id": .string("3")])
			#expect(try client.recvUntilDone().last?["status"]?.asStringList == ["done"])
			let notCaught = try client.recvUntilDone()
			#expect(notCaught.last?["status"]?.asStringList?.contains("interrupted") == true)
			#expect(!notCaught.contains { $0["value"] != nil })

			try client.send(["op": .string("close"), "session": .string(session), "id": .string("4")])
			#expect(try client.recvUntilDone().last?["status"]?.asStringList == ["done"])

			server.stop()
			#expect(clj_debug_coro_settle(0, 2000), "leftover nREPL eval coroutines after the test")
		}

		// The print limits and *file* a message carries bind for that message and leave the session as it was.
		@Test func printLimitsAndFileScopeToTheMessage() throws {
			let server = try Server()
			let port = try server.start(host: "127.0.0.1", port: 0)
			let client = try Client(port: port)
			defer { client.socket.close() }

			try client.send(["op": .string("clone")])
			let session = try #require(client.recvUntilDone().last?["new-session"]?.asString)

			// nREPL's own spelling of the options map, which also elides the value the client gets back.
			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("1"),
			                  "code": .string("(pr-str (range 10))"),
			                  "nrepl.middleware.print/options": .dict(["print-length": .int(3)])])
			#expect(try client.recvUntilDone().contains { $0["value"]?.asString == "\"(0 1 2 ...)\"" })

			// cider-nrepl's spelling of the same two limits.
			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("2"),
			                  "code": .string("(pr-str [[1 2]])"),
			                  "nrepl.middleware.print/options": .dict(["level": .int(1)])])
			#expect(try client.recvUntilDone().contains { $0["value"]?.asString == "\"[#]\"" })

			// A message's limits are not the session's: the next eval sees them nil again.
			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("3"),
			                  "code": .string("[*print-length* *print-level* (pr-str (range 4))]")])
			#expect(try client.recvUntilDone().contains { $0["value"]?.asString == "[nil nil \"(0 1 2 3)\"]" })

			// load-file binds *file* to the source path, for the load's extent only.
			try client.send(["op": .string("load-file"), "session": .string(session), "id": .string("4"),
			                  "file": .string("(def seen-while-loading *file*)\n*file*\n"),
			                  "file-path": .string("probe/thing.clj"), "file-name": .string("thing.clj")])
			#expect(try client.recvUntilDone().contains { $0["value"]?.asString == "\"probe/thing.clj\"" })

			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("5"),
			                  "code": .string("[*file* seen-while-loading]")])
			#expect(try client.recvUntilDone().contains { $0["value"]?.asString == "[nil \"probe/thing.clj\"]" })

			server.stop()
			#expect(clj_debug_coro_settle(0, 2000), "leftover nREPL eval coroutines after the test")
		}

		// read-line parks for a stdin message, takes whole lines out of one chunk, and ends at nREPL's empty payload.
		@Test func readLineParksUntilStdinArrives() throws {
			let server = try Server()
			let port = try server.start(host: "127.0.0.1", port: 0)
			let client = try Client(port: port)
			defer { client.socket.close() }

			try client.send(["op": .string("clone")])
			let session = try #require(client.recvUntilDone().last?["new-session"]?.asString)

			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("1"),
			                  "code": .string("[(read-line) (read-line) (read-line)]")])
			var chunks = ["one\ntwo\n", ""]
			var requests = 0
			var value: String?
			while true {
				let message = try client.recvOne()
				let status = message["status"]?.asStringList ?? []
				if status.contains("need-input") {
					#expect(message["id"]?.asString == "1")
					requests += 1
					try client.send(["op": .string("stdin"), "session": .string(session), "id": .string("in\(requests)"),
					                  "stdin": .string(chunks.isEmpty ? "" : chunks.removeFirst())])
					continue
				}
				guard message["id"]?.asString == "1" else { continue }
				if let printed = message["value"]?.asString { value = printed }
				if status.contains("done") { break }
			}
			#expect(value == "[\"one\" \"two\" nil]")
			// The second line came out of the first chunk, so only the first and the third read asked for input.
			#expect(requests == 2)

			server.stop()
			#expect(clj_debug_coro_settle(0, 2000), "leftover nREPL eval coroutines after the test")
		}

		// One long form: its :out must arrive well before its :value, and a with-out-str inside it keeps its own.
		@Test func outputArrivesBeforeTheFormReturns() throws {
			let server = try Server()
			let port = try server.start(host: "127.0.0.1", port: 0)
			let client = try Client(port: port)
			defer { client.socket.close() }

			try client.send(["op": .string("clone")])
			let session = try #require(client.recvUntilDone().last?["new-session"]?.asString)

			try client.send(["op": .string("eval"), "session": .string(session), "id": .string("1"),
			                  "code": .string("""
			                  (do (println "while running")
			                      (with-out-str (println "captured"))
			                      (Thread/sleep 150)
			                      :finished)
			                  """)])
			var outAt: Date?
			var valueAt: Date?
			var outs: [String] = []
			while true {
				let message = try client.recvOne()
				if let text = message["out"]?.asString {
					outs.append(text)
					if outAt == nil { outAt = Date() }
				}
				if message["value"] != nil { valueAt = Date() }
				if message["status"]?.asStringList?.contains("done") == true { break }
			}
			#expect(outs == ["while running\n"], "with-out-str output must stay captured: \(outs)")
			let out = try #require(outAt)
			let value = try #require(valueAt)
			// Captured per form, both land together; streamed, the :out beats the value by the whole sleep.
			#expect(value.timeIntervalSince(out) > 0.05, "the :out waited for the form: \(value.timeIntervalSince(out)) s")

			server.stop()
			#expect(clj_debug_coro_settle(0, 2000), "leftover nREPL eval coroutines after the test")
		}

		@Test func sessionCarriesItsWholeBindingFrame() throws {
			let server = try Server()
			let port = try server.start(host: "127.0.0.1", port: 0)
			let client = try Client(port: port)
			defer { client.socket.close() }

			func eval(_ session: String, _ id: String, _ code: String) throws -> [[String: BValue]] {
				try client.send(["op": .string("eval"), "session": .string(session), "id": .string(id), "code": .string(code)])
				return try client.recvUntilDone()
			}

			try client.send(["op": .string("clone")])
			let parent = try #require(client.recvUntilDone().last?["new-session"]?.asString)

			_ = try eval(parent, "1", "10")
			_ = try eval(parent, "2", "20")
			// *1 20, *2 10, *3 nil now; cloning here, before *1 *2 *3 are themselves read (which would shift them).

			try client.send(["op": .string("clone"), "session": .string(parent)])
			let child = try #require(client.recvUntilDone().last?["new-session"]?.asString)
			#expect(try eval(child, "3", "*1").contains { $0["value"]?.asString == "20" })
			// Mutating the child's history must not reach back into the parent's.
			_ = try eval(child, "4", "1001")

			#expect(try eval(parent, "5", "[*1 *2 *3]").contains { $0["value"]?.asString == "[20 10 nil]" })

			let errored = try eval(parent, "6", "(/ 1 0)")
			#expect(errored.last?["status"]?.asStringList?.contains("eval-error") == true)
			#expect(try eval(parent, "7", "(some? *e)").contains { $0["value"]?.asString == "true" })

			// require's own value (nil) becomes the new *1; interrupting the next eval must not disturb it.
			_ = try eval(parent, "8", "(require '[clojure.core.async :refer [chan <!!]])")
			try client.send(["op": .string("eval"), "session": .string(parent), "id": .string("9"), "code": .string("(<!! (chan))")])
			Thread.sleep(forTimeInterval: 0.2)
			try client.send(["op": .string("interrupt"), "session": .string(parent), "interrupt-id": .string("9")])
			_ = try client.recvUntilDone()
			#expect(try client.recvUntilDone().last?["status"]?.asStringList?.contains("interrupted") == true)
			#expect(try eval(parent, "10", "*1").contains { $0["value"]?.asString == "nil" })

			server.stop()
			#expect(clj_debug_coro_settle(0, 2000), "leftover nREPL eval coroutines after the test")
		}
	}
}
