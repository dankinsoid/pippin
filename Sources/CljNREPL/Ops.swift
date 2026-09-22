// @ai-generated(solo)
import Foundation
import Pippin

enum Ops {
	static func reply(_ req: [String: BValue], _ extra: [String: BValue]) -> [String: BValue] {
		var out = extra
		if let id = req["id"] { out["id"] = id }
		if let session = req["session"] { out["session"] = session }
		return out
	}

	static func clone(_ server: Server, _ msg: [String: BValue], _ conn: Connection) {
		let base = msg["session"]?.asString.flatMap(server.session)
		let frame = base?.currentFrame ?? ReplVars.defaultFrame(namespace: "user")
		let session = Session(frame: frame)
		server.addSession(session)
		conn.own(session.id)
		conn.send(reply(msg, ["new-session": .string(session.id), "status": .list([.string("done")])]))
	}

	static func close(_ server: Server, _ msg: [String: BValue], _ conn: Connection) {
		if let id = msg["session"]?.asString {
			server.removeSession(id)
			conn.disown(id)
		}
		conn.send(reply(msg, ["status": .list([.string("done")])]))
	}

	static func describe(_ server: Server, _ msg: [String: BValue], _ conn: Connection) {
		let ops: [String: BValue] = Dictionary(uniqueKeysWithValues:
			["clone", "close", "describe", "eval", "load-file", "complete", "info", "interrupt", "stdin"].map { ($0, .dict([:])) })
		conn.send(reply(msg, [
			"ops": .dict(ops),
			"versions": .dict([
				"nrepl": .dict(["major": .int(0), "minor": .int(9), "incremental": .int(0)]),
				"pippin": .dict(["major": .int(0), "minor": .int(1), "incremental": .int(0)]),
			]),
			"status": .list([.string("done")]),
		]))
	}

	static func eval(_ server: Server, _ msg: [String: BValue], _ conn: Connection) {
		let session = server.resolveSession(msg, conn)
		let id = msg["id"]?.asString ?? ""
		let code = msg["code"]?.asString ?? ""
		let ns = msg["ns"]?.asString
		session.scheduleEval {
			let coro = spawnCoroutine({
				Evaluator.run(code: code, overrideNamespace: ns, session: session, id: id, conn: conn)
			}, onDone: session.finishEval)
			session.setInFlight(id: id, coro: coro)
		}
	}

	// A whole-file load: mirrors clojure.main's load-file, one final value rather than one per top-level form.
	static func loadFile(_ server: Server, _ msg: [String: BValue], _ conn: Connection) {
		let session = server.resolveSession(msg, conn)
		let id = msg["id"]?.asString ?? ""
		let code = msg["file"]?.asString ?? ""
		session.scheduleEval {
			let coro = spawnCoroutine({
				Evaluator.run(code: code, overrideNamespace: nil, session: session, id: id, conn: conn, onlyLastValue: true)
			}, onDone: session.finishEval)
			session.setInFlight(id: id, coro: coro)
		}
	}

	static func interrupt(_ server: Server, _ msg: [String: BValue], _ conn: Connection) {
		guard let sid = msg["session"]?.asString, let session = server.session(sid) else {
			conn.send(reply(msg, ["status": .list([.string("session-not-found"), .string("done")])]))
			return
		}
		switch session.interrupt(matching: msg["interrupt-id"]?.asString) {
		case .idle: conn.send(reply(msg, ["status": .list([.string("session-idle"), .string("done")])]))
		case .mismatch: conn.send(reply(msg, ["status": .list([.string("interrupt-id-mismatch"), .string("done")])]))
		case .interrupted: conn.send(reply(msg, ["status": .list([.string("done")])]))
		}
	}

	// No *in*/read-line in the language yet (NOTES "nREPL"): accepted for protocol compliance, wired nowhere.
	static func stdin(_ server: Server, _ msg: [String: BValue], _ conn: Connection) {
		conn.send(reply(msg, ["status": .list([.string("done")])]))
	}

	static func complete(_ server: Server, _ msg: [String: BValue], _ conn: Connection) {
		let session = server.resolveSession(msg, conn)
		let prefix = msg["symbol"]?.asString ?? ""
		let ns = msg["ns"]?.asString ?? session.currentNamespace
		var out: [String: BValue] = ["status": .list([.string("done")])]
		if !prefix.isEmpty, let result = try? server.helpers.complete.apply([Value(ns), Value(prefix)]), let items = result.array {
			out["completions"] = .list(items.compactMap { item -> BValue? in
				guard let d = item.dictionary, let candidate = field(d, "candidate") else { return nil }
				var entry: [String: BValue] = ["candidate": .string(candidate)]
				if let nsField = field(d, "ns") { entry["ns"] = .string(nsField) }
				return .dict(entry)
			})
		}
		conn.send(reply(msg, out))
	}

	static func info(_ server: Server, _ msg: [String: BValue], _ conn: Connection) {
		let session = server.resolveSession(msg, conn)
		let symbol = msg["symbol"]?.asString ?? ""
		let ns = msg["ns"]?.asString ?? session.currentNamespace
		guard !symbol.isEmpty, let result = try? server.helpers.info.apply([Value(ns), Value(symbol)]), !result.isNil, let d = result.dictionary else {
			conn.send(reply(msg, ["status": .list([.string("no-info"), .string("done")])]))
			return
		}
		var out: [String: BValue] = ["status": .list([.string("done")])]
		for (k, v) in d {
			guard case .keyword(let key) = k.kind else { continue }
			switch v.kind {
			case .string(let s): out[key] = .string(s)
			case .int(let n): out[key] = .int(n)
			case .bool(true): out[key] = .string("true")
			default: break
			}
		}
		conn.send(reply(msg, out))
	}

	// Keyed by the Clojure keyword `:candidate`/`:ns` a helper's map answers with.
	private static func field(_ d: [Value: Value], _ keyword: String) -> String? {
		d[Value(keyword: keyword)]?.string
	}
}
