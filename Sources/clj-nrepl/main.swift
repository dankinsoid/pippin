// @ai-generated(solo)
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif
import CljCore
import CljNREPL
import Foundation

// clj-nrepl: an nREPL server over this runtime (design §5c). `.nrepl-port` is what CIDER/Calva auto-connect to.

var host = "127.0.0.1"
var port: UInt16 = 0
var portFilePath: String? = ".nrepl-port"

func usage() -> Never {
	FileHandle.standardError.write(Data("usage: clj-nrepl [--port N] [--bind HOST] [--port-file PATH] [--no-port-file]\n".utf8))
	exit(64)
}

var args = Array(CommandLine.arguments.dropFirst())
@MainActor func need() -> String {
	if args.isEmpty { usage() }
	return args.removeFirst()
}
while !args.isEmpty {
	switch args.removeFirst() {
	case "--port": port = UInt16(need()) ?? 0
	case "--bind": host = need()
	case "--port-file": portFilePath = need()
	case "--no-port-file": portFilePath = nil
	default: usage()
	}
}

let server: Server
let bound: UInt16
do {
	server = try Server()
	bound = try server.start(host: host, port: port)
} catch {
	FileHandle.standardError.write(Data("clj-nrepl: \(error)\n".utf8))
	exit(1)
}

// The dev flag of design §4 «Var и ленивые def»: a load forces what it deferred, so an init's error shows at the load.
if ProcessInfo.processInfo.environment["CLJ_LAZY_DEFS"] == nil { clj_lazy_defs_set_default(CLJ_LAZY_DEFS_AFTER_LOAD) }

nonisolated(unsafe) var portFile: String?
if let portFilePath {
	portFile = portFilePath
	try? "\(bound)".write(toFile: portFilePath, atomically: true, encoding: .utf8)
	signal(SIGINT, cleanUpAndExit)
	signal(SIGTERM, cleanUpAndExit)
}

// nrepl.el and other clients grep exactly this line to learn the port from stdout.
print("nREPL server started on port \(bound) on host \(host) - nrepl://\(host):\(bound)")

while true { pause() }

nonisolated func cleanUpAndExit(_ sig: Int32) {
	if let portFile { unlink(portFile) }
	exit(0)
}
