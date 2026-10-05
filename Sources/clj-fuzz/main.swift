// @ai-generated(solo)
import CljCompiler
import CljCore
import Foundation
import Pippin

// The fuzzer's runner (docs/notes/fuzzing.md): a full host, since clj-load leaves `sort` unbound.
// CljCompiler linked in is what CLJ_EVAL=compiled needs.
let args = Array(CommandLine.arguments.dropFirst())
guard args.count == 1 else {
	FileHandle.standardError.write(Data("usage: clj-fuzz <file.clj>\n".utf8))
	exit(2)
}

let runtime = Runtime()
_ = runtime
let path = Value(args[0])
let r = withExtendedLifetime(path) { clj_load_file(path.raw) }
fflush(stdout)
if r == CLJ_THROWN {
	let ex = Value(owning: clj_take_pending())
	FileHandle.standardError.write(Data("clj-fuzz: \(ex)\n".utf8))
	exit(1)
}
clj_release(r)
exit(0)
