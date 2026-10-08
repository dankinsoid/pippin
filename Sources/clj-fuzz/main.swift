// @ai-generated(solo)
import CljCompiler
import CljCore
import Foundation
import Pippin

// The fuzzer's runner (docs/notes/fuzzing.md): a full host, since clj-load leaves `sort` unbound.
// CljCompiler linked in is what CLJ_EVAL=compiled needs.

var args = Array(CommandLine.arguments.dropFirst())
// clj-compile's output for the file: one clang run per case, not one per form (fuzz/compiled.sh).
var unitsDir: String?
if args.count == 3 && args[0] == "--units" {
	unitsDir = args[1]
	args.removeFirst(2)
}
guard args.count == 1 else {
	FileHandle.standardError.write(Data("usage: clj-fuzz [--units DIR] <file.clj>\n".utf8))
	exit(2)
}

func fail(_ message: String) -> Never {
	FileHandle.standardError.write(Data("clj-fuzz: \(message)\n".utf8))
	exit(1)
}

let runtime = Runtime()
_ = runtime
if let dir = unitsDir {
	guard let root = ProcessInfo.processInfo.environment["CLJ_EVAL_ROOT"] else { fail("--units needs CLJ_EVAL_ROOT (the package root)") }
	let manifest = (try? String(contentsOfFile: "\(dir)/units.txt", encoding: .utf8)) ?? ""
	let croot = strdup(root), cdir = strdup(dir)
	defer { free(croot); free(cdir) }
	var o = cljc_eval_options()
	o.root = UnsafePointer(croot)
	o.dir = UnsafePointer(cdir)
	o.keep = true
	for line in manifest.split(separator: "\n") {
		let cells = line.split(separator: "\t", maxSplits: 1).map(String.init)
		guard cells.count == 2 else { fail("\(dir)/units.txt: a line is not cname<TAB>path") }
		guard let text = try? String(contentsOfFile: "\(dir)/\(cells[0]).c", encoding: .utf8) else { fail("no \(dir)/\(cells[0]).c") }
		var err = [CChar](repeating: 0, count: 2048)
		if !cljc_build_dylib(&o, cells[0], text, &err, err.count) { fail(String(decoding: err.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)) }
		guard let unit = cljc_open_dylib("\(dir)/\(cells[0]).dylib") else { fail("\(Value(owning: clj_take_pending()))") }
		clj_compiled_register(unit.pointee.path, unit.pointee.`init`)
	}
}
let path = Value(args[0])
let r = withExtendedLifetime(path) { clj_load_file(path.raw) }
fflush(stdout)
if r == CLJ_THROWN {
	let ex = Value(owning: clj_take_pending())
	fail("\(ex)")
}
clj_release(r)
exit(0)
