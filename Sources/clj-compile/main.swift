// @ai-generated(solo)
import CljCompiler
import CljCore
import Foundation
import Pippin

// clj-compile: loads Clojure sources through the analyzer and writes one C unit per file (NOTES.md, "Compiler").

struct Options {
	var out = "."
	var closed = false
	var instrument = false
	var line = true
	var lenient = false
	var loadPath: [String] = []
	var features: Set<String> = []
	var core = false
	var withEmbedded = false
	var inputs: [(kind: String, value: String)] = []
	var allowRefused = false
	var stats = false
	var noShake = false
	var shakeDrop: [String] = []
}

func usage() -> Never {
	FileHandle.standardError.write(Data("""
	usage: clj-compile [--out DIR] [--closed] [--instrument] [--no-line] [--lenient] [--load-path P]... [--features k,...]
	                   [--core] [--with-embedded] [--allow-refused] [--stats] [--no-shake] [--shake-drop NS/NAME]... (--file F | --ns NS)...
	--instrument emits the profiler and signpost hooks in every fn (a plain unit has none; core.c takes -DCLJC_INSTRUMENT).
	--closed with --core and a --file/--ns entry shakes the core and the embedded libs: a def of theirs that the
	root set does not reach is left out and its var gets a tripwire root (NOTES.md, "Compiler": tree shaking).
	--no-shake keeps every def, which is the before half of the size measurement; --shake-drop drops a named def
	however reachable it is, which is how the gate proves the tripwire fires.
	--stats reports per unit the frame slots the facts pass calls local, the ones emitted as C variables and as int64_t, the arithmetic nodes emitted unboxed or behind a tag check, the protocol sites by their dispatch (direct arm, switch, cache), and the slot arrays direct callers allocate against their callees' frames.
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
	case "--instrument": opts.instrument = true
	case "--no-line": opts.line = false
	case "--lenient": opts.lenient = true
	case "--load-path": opts.loadPath.append(need())
	case "--features": opts.features.formUnion(need().split(separator: ",").map(String.init))
	case "--core": opts.core = true
	case "--with-embedded": opts.withEmbedded = true
	case "--allow-refused": opts.allowRefused = true
	case "--stats": opts.stats = true
	case "--no-shake": opts.noShake = true
	case "--shake-drop": opts.shakeDrop.append(need())
	case "--file": opts.inputs.append(("file", need()))
	case "--ns": opts.inputs.append(("ns", need()))
	default: usage()
	}
}
if !opts.core && opts.inputs.isEmpty { usage() }

// The hook must be in place before clj_init so that core.clj's own forms pass through it.
var copts = cljc_options()
copts.closed = opts.closed
copts.instrument = opts.instrument
copts.line = opts.line
copts.skip_embedded = !opts.withEmbedded && !opts.core
copts.no_shake = opts.noShake
let dropped = opts.shakeDrop.map { UnsafePointer(strdup($0)!) }
let droppedBuf = UnsafeMutableBufferPointer<UnsafePointer<CChar>?>.allocate(capacity: max(dropped.count, 1))
for (i, d) in dropped.enumerated() { droppedBuf[i] = d }
if !dropped.isEmpty {
	copts.force_drop = UnsafePointer(droppedBuf.baseAddress!)
	copts.nforce_drop = dropped.count
}
let coreGuard = strdup("CLJ_COMPILED_CORE")
if opts.core { copts.guard_macro = UnsafePointer(coreGuard) }
let compiler = cljc_new(&copts)!
cljc_begin(compiler)
let rt = Runtime()
if opts.core && opts.inputs.isEmpty {
	// core.clj went through the hook during clj_init; the embedded libs follow through require.
	for lib in ["clojure.set", "clojure.string", "clojure.walk", "clojure.template", "clojure.test", "clojure.core.async", "clojure.core.async.impl.buffers"] {
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

var shake = cljc_shake_report()
cljc_shake_report_of(compiler, &shake)
if opts.closed {
	let head = shake.ran
		? "shake: \(shake.dropped) of \(shake.candidates) candidate defs dropped, \(shake.defs - shake.dropped) kept, over \(shake.embedded_units) core/embedded and \(shake.entry_units) entry units"
		: "shake: not run - \(String(cString: shake.skipped!))"
	FileHandle.standardError.write(Data("\(head)\n".utf8))
	// Where the tripwire, and not the analysis, is what keeps a wrong claim from becoming a wrong answer.
	let escapes = cljc_shake_escape_count(compiler)
	if shake.ran && escapes > 0 {
		var seen = Set<String>()
		for i in 0..<escapes {
			let e = cljc_shake_escape_at(compiler, i)!.pointee
			let name = String(cString: e.reason)
			if !seen.insert(name).inserted { continue }
			FileHandle.standardError.write(Data("shake: reflective: \(String(cString: e.file)):\(e.line):\(e.col) \(name)\n".utf8))
		}
		FileHandle.standardError.write(Data("shake: \(escapes) reflective sites over \(seen.count) names; a shaken def reached through one is fatal, not wrong\n".utf8))
	}
}

if opts.stats {
	func pct(_ a: UInt64, _ b: UInt64) -> String { b == 0 ? "-" : String(format: "%.1f %%", 100 * Double(a) / Double(b)) }
	for i in 0..<cljc_unit_count(compiler) {
		var st = cljc_slot_stats()
		cljc_unit_slots(compiler, i, &st)
		let file = String(cString: cljc_unit_file(compiler, i))
		FileHandle.standardError.write(Data("""
		slots: \(file): \(st.slots) slots, local \(st.local) (\(pct(st.local, st.slots))), promoted \(st.promoted) (\(pct(st.promoted, st.slots))) \
		of which local \(st.promoted_local); local not promoted: param \(st.local_param), pinned \(st.local_pinned), fused \(st.local_fused); \
		int64 slots \(st.int_slots), double slots \(st.double_slots), arithmetic nodes unboxed \(st.unboxed), tag-checked \(st.tag_checked), entry-checked frames \(st.entry_checked); \
		protocol sites direct \(st.proto_direct), switch \(st.proto_switch), cache \(st.proto_cache), satisfies?/extends? folded \(st.proto_folded); \
		direct-call arrays \(st.direct_array) of \(st.direct_slots) callee slots; \
		workers \(st.workers), primitive sites \(st.prim_sites) of which bound \(st.prim_bound); keyword-lookup sites \(st.kw_sites); \
		reuse: cons in a dying cell \(st.reuse_tokens), next/rest handed over \(st.reuse_consumes); \
		defs \(st.defs) of which dropped \(st.defs_dropped), kept \(st.def_bytes) C bytes

		""".utf8))
		var defs: [(String, UInt64)] = []
		for k in 0..<cljc_unit_def_count(compiler, i) {
			var d = cljc_def_size()
			cljc_unit_def_at(compiler, i, k, &d)
			if !d.dropped { defs.append((String(cString: d.name), d.bytes)) }
		}
		let top = defs.sorted { $0.1 > $1.1 }.prefix(12).map { "\($0.0) \($0.1)" }
		if !top.isEmpty {
			FileHandle.standardError.write(Data("survivors: \(file): \(top.joined(separator: ", "))\n\n".utf8))
		}
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
	// An entry unit is registered too, so clj_load_file of the program runs it against the core written beside it.
	var registrations: [(path: String, initName: String)] = []
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
			registrations.append((file, initName))
		} else {
			let cname = String(cString: cljc_unit_cname(compiler, i))
			let initName = "clj_compiled_unit_init_\(cname)"
			let text = cljc_unit_text(compiler, i, initName)!
			write(String(cString: text), to: "\(bootDir)/unit_\(cname).c")
			free(text)
			registrations.append((file, initName))
		}
	}
	var libs = "// Generated by clj-compile --core; do not edit.\n#include \"clj/compiled.h\"\n\n#ifdef CLJ_COMPILED_CORE\n"
	for reg in registrations { libs += "clj_value \(reg.initName)(void);\n" }
	libs += "\nvoid clj_compiled_libs_register(void) {\n"
	for reg in registrations { libs += "\tclj_compiled_register(\"\(reg.path)\", \(reg.initName));\n" }
	libs += "}\n#else\ntypedef int cljc_libs_disabled;\n#endif\n"
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
