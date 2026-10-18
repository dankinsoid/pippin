// @ai-generated(solo)
// The host of a closed build with its Swift stubs linked in (scripts/swift-closed.sh); it configures no generator.
import CljCore
import Foundation
import Pippin

let args = CommandLine.arguments
guard args.count == 2 || args.count == 3 else {
	FileHandle.standardError.write(Data("usage: \(args[0]) <entry.clj> [<ns/fn>]\n".utf8))
	exit(2)
}
let rt = Runtime()

func fail(_ what: String, _ error: any Error) -> Never {
	FileHandle.standardError.write(Data("\(what): \(error)\n".utf8))
	exit(1)
}

// Off the main thread, so a @MainActor stub's hop lands on the main queue dispatchMain serves.
let program = Thread {
	let path = Value(args[1])
	let r = withExtendedLifetime(path) { clj_load_file(path.raw) }
	if r == CLJ_THROWN { fail(args[1], ClojureError(thrown: Value(owning: clj_take_pending()))) }
	clj_release(r)
	if args.count == 3 {
		do {
			_ = try rt.eval("(\(args[2]))")
		} catch {
			fail(args[2], error)
		}
	}
	exit(0)
}
// A Thread's default 512 KB is short of what a deep load takes on the main thread's 8 MB.
program.stackSize = 8 << 20
program.start()
dispatchMain()
