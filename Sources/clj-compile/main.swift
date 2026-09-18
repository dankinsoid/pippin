// @ai-generated(solo)
import CljCompiler
import CljCore
import Foundation
import Pippin

// clj-compile: loads Clojure sources through the analyzer and writes one C unit per file (NOTES.md, "Compiler").

struct Options {
	var out = "."
	var closed = false
	var line = true
	var lenient = false
	var loadPath: [String] = []
	var features: Set<String> = []
	var core = false
	var withEmbedded = false
	var inputs: [(kind: String, value: String)] = []
	var allowRefused = false
	var stats = false
}

func usage() -> Never {
	FileHandle.standardError.write(Data("""
	usage: clj-compile [--out DIR] [--closed] [--no-line] [--lenient] [--load-path P]... [--features k,...]
	                   [--core] [--with-embedded] [--allow-refused] [--stats] (--file F | --ns NS)...
	--stats reports per unit the frame slots the facts pass calls local, the ones emitted as C variables and as int64_t, and the arithmetic nodes emitted unboxed or behind a tag check.
	--core writes <out>/core.c and <out>/libs_*.c (the embedded libs) for -DCLJ_COMPILED_CORE builds; otherwise
	one <out>/<munged path>.c per loaded file plus <out>/units.txt (cname<TAB>path per line, in load order).

	""".utf8))
	exit(64)
}

var opts = Options()
var args = Array(CommandLine.arguments.dropFirst())
@MainActor func need() -> String {
	if args.isEmpty { usage() }
	return args.removeFirst()
}
while !args.isEmpty {
	let a = args.removeFirst()
	switch a {
	case "--out": opts.out = need()
	case "--closed": opts.closed = true
	case "--no-line": opts.line = false
	case "--lenient": opts.lenient = true
	case "--load-path": opts.loadPath.append(need())
	case "--features": opts.features.formUnion(need().split(separator: ",").map(String.init))
	case "--core": opts.core = true
	case "--with-embedded": opts.withEmbedded = true
	case "--allow-refused": opts.allowRefused = true
	case "--stats": opts.stats = true
	case "--file": opts.inputs.append(("file", need()))
	case "--ns": opts.inputs.append(("ns", need()))
	default: usage()
	}
}
if !opts.core && opts.inputs.isEmpty { usage() }

// The hook must be in place before clj_init so that core.clj's own forms pass through it.
var copts = cljc_options()
copts.closed = opts.closed
copts.line = opts.line
copts.skip_embedded = !opts.withEmbedded && !opts.core
let coreGuard = strdup("CLJ_COMPILED_CORE")
if opts.core { copts.guard_macro = UnsafePointer(coreGuard) }
let compiler = cljc_new(&copts)!
cljc_begin(compiler)
let rt = Runtime()
if opts.core {
	// core.clj went through the hook during clj_init; the embedded libs follow through require.
	for lib in ["clojure.set", "clojure.string", "clojure.walk", "clojure.template", "clojure.test"] {
		_ = try rt.eval("(require '\(lib))")
	}
}
Runtime.loadPath = opts.loadPath
Runtime.readerFeatures = opts.features
clj_load_set_lenient(opts.lenient)
for input in opts.inputs {
	do {
		if input.kind == "file" {
			let path = Value(input.value)
			let r = withExtendedLifetime(path) { clj_load_file(path.raw) }
			if r == CLJ_THROWN { throw ClojureError(thrown: Value(owning: clj_take_pending())) }
			clj_release(r)
		} else {
			_ = try rt.eval("(require '\(input.value))")
		}
	} catch {
		FileHandle.standardError.write(Data("clj-compile: \(input.value): \(error)\n".utf8))
		exit(1)
	}
}
cljc_end(compiler)

var status: Int32 = 0
let refusals = cljc_refusal_count(compiler)
for i in 0..<refusals {
	let r = cljc_refusal_at(compiler, i)!.pointee
	FileHandle.standardError.write(Data("refused: \(String(cString: r.file)):\(r.line):\(r.col) \(String(cString: r.kind)) — \(String(cString: r.reason))\n".utf8))
}
if refusals > 0 && !opts.allowRefused { status = 2 }

if opts.stats {
	func pct(_ a: UInt64, _ b: UInt64) -> String { b == 0 ? "-" : String(format: "%.1f %%", 100 * Double(a) / Double(b)) }
	for i in 0..<cljc_unit_count(compiler) {
		var st = cljc_slot_stats()
		cljc_unit_slots(compiler, i, &st)
		let file = String(cString: cljc_unit_file(compiler, i))
		FileHandle.standardError.write(Data("""
		slots: \(file): \(st.slots) slots, local \(st.local) (\(pct(st.local, st.slots))), promoted \(st.promoted) (\(pct(st.promoted, st.slots))) \
		of which local \(st.promoted_local); local not promoted: param \(st.local_param), pinned \(st.local_pinned), fused \(st.local_fused); \
		int64 slots \(st.int_slots), double slots \(st.double_slots), arithmetic nodes unboxed \(st.unboxed), tag-checked \(st.tag_checked), entry-checked frames \(st.entry_checked)

		""".utf8))
	}
}

func write(_ text: String, to path: String) {
	do {
		try text.write(toFile: path, atomically: true, encoding: .utf8)
	} catch {
		FileHandle.standardError.write(Data("clj-compile: cannot write \(path): \(error)\n".utf8))
		exit(1)
	}
}

let n = cljc_unit_count(compiler)
if opts.core {
	let bootDir = opts.out
	var registrations: [String] = []
	for i in 0..<n {
		let file = String(cString: cljc_unit_file(compiler, i))
		if file == CLJ_CORE_CLJ_PATH {
			let text = cljc_unit_text(compiler, i, "clj_compiled_core_init")!
			write(String(cString: text), to: "\(bootDir)/core.c")
			free(text)
		} else if file.hasPrefix("<embedded>/") {
			let stem = file.dropFirst("<embedded>/".count).replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: ".clj", with: "")
			let initName = "clj_compiled_lib_init_\(stem)"
			let text = cljc_unit_text(compiler, i, initName)!
			write(String(cString: text), to: "\(bootDir)/libs_\(stem).c")
			free(text)
			registrations.append("\tclj_compiled_register(\"\(file)\", \(initName));")
		}
	}
	var libs = "// Generated by clj-compile --core; do not edit.\n#include \"clj/compiled.h\"\n\n#ifdef CLJ_COMPILED_CORE\n"
	for reg in registrations {
		let name = reg.split(separator: ",")[1].dropLast(2).trimmingCharacters(in: .whitespaces)
		libs += "clj_value \(name)(void);\n"
	}
	libs += "\nvoid clj_compiled_libs_register(void) {\n" + registrations.joined(separator: "\n") + "\n}\n#else\ntypedef int cljc_libs_disabled;\n#endif\n"
	write(libs, to: "\(bootDir)/libs.c")
} else {
	var manifest = ""
	for i in 0..<n {
		let file = String(cString: cljc_unit_file(compiler, i))
		let cname = String(cString: cljc_unit_cname(compiler, i))
		let text = cljc_unit_text(compiler, i, nil)!
		write(String(cString: text), to: "\(opts.out)/\(cname).c")
		free(text)
		manifest += "\(cname)\t\(file)\n"
	}
	write(manifest, to: "\(opts.out)/units.txt")
}
cljc_free(compiler)
exit(status)
