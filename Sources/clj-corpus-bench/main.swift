// @ai-generated(solo)
import CljCompiler
import CljCore
import Foundation
import Pippin

// clj-corpus-bench: one workload of bench/workloads, timed in-process (docs/notes/benchmarks.md, "Corpus workloads").
// The report lines are bench/workloads/jvm.clj's, so scripts/corpus-bench.py reads both sides the same way.

struct Options {
	var loadPath: [String] = []
	var features: Set<String> = []
	var units: String?
	var dylibDir: String?
	var opt = "-O2"
	var size: Int?
	var stats = false
	var census = false
	var statIterations = 2
	var hold: Double?
	var calibrate = false
	var minIterations = 5
	var maxIterations = 30
	var minSeconds = 3.0
	var ns = ""
}

func usage() -> Never {
	FileHandle.standardError.write(Data("""
	usage: clj-corpus-bench [--load-path P]... [--features k,...] [--units DIR [--dylib-dir DIR] [--opt -O2]]
	                        [--size N | --stats [--census] | --hold SECONDS | --calibrate] NS
	--units registers clj-compile's units of DIR (units.txt), built here by clang; NS/run is then compiled code.
	--size runs (NS/run* N) once and prints its result; --stats prints the runtime's event counts per iteration
	(a -DCLJ_STATS=1 build), with --census where each object of those iterations died against the fn frame it was
	born in (stats.c); --hold keeps iterating for SECONDS after the first, for a sampling profiler.

	""".utf8))
	exit(64)
}

func int(_ s: String) -> Int {
	guard let v = Int(s) else { usage() }
	return v
}

func double(_ s: String) -> Double {
	guard let v = Double(s) else { usage() }
	return v
}

func fail(_ message: String) -> Never {
	FileHandle.standardError.write(Data("clj-corpus-bench: \(message)\n".utf8))
	exit(1)
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
	case "--load-path": opts.loadPath.append(need())
	case "--features": opts.features.formUnion(need().split(separator: ",").map(String.init))
	case "--units": opts.units = need()
	case "--dylib-dir": opts.dylibDir = need()
	case "--opt": opts.opt = need()
	case "--size": opts.size = int(need())
	case "--stats": opts.stats = true
	case "--census": opts.census = true
	case "--stat-iterations": opts.statIterations = int(need())
	case "--hold": opts.hold = double(need())
	case "--calibrate": opts.calibrate = true
	case "--min-iterations": opts.minIterations = int(need())
	case "--max-iterations": opts.maxIterations = int(need())
	case "--min-seconds": opts.minSeconds = double(need())
	default:
		if a.hasPrefix("-") || !opts.ns.isEmpty { usage() }
		opts.ns = a
	}
}

func nowMs() -> Double { Double(DispatchTime.now().uptimeNanoseconds) / 1e6 }

func out(_ line: String) {
	print(line)
	fflush(stdout)
}

let runtime = Runtime()
if opts.calibrate {
	_ = clj_debug_rc_op_ns(10_000_000)
	out("rc_op_ns \(clj_debug_rc_op_ns(200_000_000))")
	out("rc_op_ns_cold \(clj_debug_rc_op_ns_cold(1 << 20, 50_000_000))")
	exit(0)
}
if opts.ns.isEmpty { usage() }

if let dir = opts.units {
	guard let root = ProcessInfo.processInfo.environment["CLJ_EVAL_ROOT"] else { fail("--units needs CLJ_EVAL_ROOT (the package root)") }
	let dylibs = opts.dylibDir ?? dir
	try? FileManager.default.createDirectory(atPath: dylibs, withIntermediateDirectories: true)
	let manifest = (try? String(contentsOfFile: "\(dir)/units.txt", encoding: .utf8)) ?? ""
	let units: [(cname: String, text: String)] = manifest.split(separator: "\n").map { line in
		let cells = line.split(separator: "\t", maxSplits: 1).map(String.init)
		guard cells.count == 2 else { fail("\(dir)/units.txt: a line is not cname<TAB>path") }
		guard let text = try? String(contentsOfFile: "\(dir)/\(cells[0]).c", encoding: .utf8) else { fail("no \(dir)/\(cells[0]).c") }
		return (cells[0], text)
	}
	let croot = strdup(root), cdir = strdup(dylibs), copt = strdup(opts.opt)
	let t0 = nowMs()
	for unit in units {
		var o = cljc_eval_options()
		o.root = UnsafePointer(croot)
		o.dir = UnsafePointer(cdir)
		o.opt = UnsafePointer(copt)
		o.keep = true
		var err = [CChar](repeating: 0, count: 2048)
		if !cljc_build_dylib(&o, unit.cname, unit.text, &err, err.count) {
			fail(String(decoding: err.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self))
		}
	}
	out("clang_ms \(nowMs() - t0)")
	for unit in units {
		guard let u = cljc_open_dylib("\(dylibs)/\(unit.cname).dylib") else { fail("\(Value(owning: clj_take_pending()))") }
		clj_compiled_register(u.pointee.path, u.pointee.`init`)
	}
}

Runtime.loadPath = opts.loadPath
Runtime.readerFeatures = opts.features
// The corpus libraries lose a form or two to JVM interop (docs/corpus.md); the workloads do not call those.
clj_load_set_lenient(true)
// Not require, which --closed shakes out of a program that never names it; the path is the units' registry key.
let relative = opts.ns.replacingOccurrences(of: "-", with: "_").replacingOccurrences(of: ".", with: "/")
let candidates = opts.loadPath.flatMap { ["\($0)/\(relative).cljc", "\($0)/\(relative).clj"] }
guard let file = candidates.first(where: { FileManager.default.fileExists(atPath: $0) }) else { fail("no file for \(opts.ns) on the load path") }
let loadStart = nowMs()
let loaded = withExtendedLifetime(Value(file)) { clj_load_file($0.raw) }
if loaded == CLJ_THROWN { fail("load \(file): \(Value(owning: clj_take_pending()))") }
clj_release(loaded)
out("load_ms \(nowMs() - loadStart)")
let failures = Value(owning: clj_load_take_failures())
let failureCount = withExtendedLifetime(failures) { clj_is_nil(failures.raw) ? 0 : Int(clj_fixnum_val(clj_count(failures.raw))) }
out("load_failures \(failureCount)")
if failureCount > 0 { FileHandle.standardError.write(Data("load failures: \(failures)\n".utf8)) }

func printed(_ v: clj_value) -> String {
	let s = Value(owning: clj_pr_str(v))
	return withExtendedLifetime(s) { String(cString: clj_string_bytes(s.raw)) }
}

@MainActor func call(_ f: Value, _ args: [clj_value] = []) -> clj_value {
	let r = withExtendedLifetime(f) { args.withUnsafeBufferPointer { clj_invoke(f.raw, $0.baseAddress, $0.count) } }
	if r == CLJ_THROWN { fail("\(opts.ns): \(Value(owning: clj_take_pending()))") }
	return r
}

if let size = opts.size {
	let runStar: Value
	do { runStar = try runtime.eval("\(opts.ns)/run*") } catch { fail("\(error)") }
	let r = call(runStar, [clj_fixnum(size)])
	out("result \(printed(r))")
	clj_release(r)
	exit(0)
}

let run: Value
do { run = try runtime.eval("\(opts.ns)/run") } catch { fail("\(error)") }

// A run that is not a function of its input would make the comparison with the JVM meaningless.
var expected = ""
var verdict = "absent"
let spec = try? runtime.eval("\(opts.ns)/expected")
let equals = try? runtime.eval("clojure.core/=")
@MainActor func timed() -> Double {
	let t = nowMs()
	let r = call(run)
	let ms = nowMs() - t
	let text = printed(r)
	if expected.isEmpty {
		expected = text
		if let spec, let equals {
			let same = withExtendedLifetime(spec) { call(equals, [r, spec.raw]) }
			verdict = clj_truthy(same) ? "same" : "DIFFERENT"
			clj_release(same)
		}
	} else if text != expected {
		fail("an iteration printed \(text), the first \(expected)")
	}
	clj_release(r)
	return ms
}

out("first_ms \(timed())")
out("expected \(verdict)")

if let hold = opts.hold {
	out("ready \(getpid())")
	let start = nowMs()
	var k = 0
	while nowMs() - start < hold * 1000 {
		_ = timed()
		k += 1
	}
	out("held_iterations \(k)")
	out("result \(expected)")
	exit(0)
}

if opts.stats {
	var s0 = [UInt64](repeating: 0, count: Int(CLJ_STAT_COUNT.rawValue)), s1 = s0
	var rc0 = [UInt64](repeating: 0, count: 3), rc1 = rc0
	guard clj_debug_stats(&s0), clj_debug_stats_rc(&rc0) else { fail("--stats needs a -DCLJ_STATS=1 build") }
	let cap = 1024
	var names0 = [UnsafePointer<CChar>?](repeating: nil, count: cap), counts0 = [UInt64](repeating: 0, count: cap)
	let n0 = clj_debug_allocs_by_type(&names0, &counts0, cap)
	let sw0 = clj_debug_coro_switches()
	if opts.census { _ = clj_debug_census_begin() }
	var ms = 0.0
	for _ in 0..<opts.statIterations { ms += timed() }
	if opts.census { clj_debug_census_end() }
	let sw1 = clj_debug_coro_switches()
	_ = clj_debug_stats(&s1)
	_ = clj_debug_stats_rc(&rc1)
	var names1 = [UnsafePointer<CChar>?](repeating: nil, count: cap), counts1 = [UInt64](repeating: 0, count: cap)
	let n1 = clj_debug_allocs_by_type(&names1, &counts1, cap)
	let k = Double(opts.statIterations)
	out("stats_ms \(ms / k)")
	for i in 0..<Int(CLJ_STAT_COUNT.rawValue) { out("stat \(String(cString: clj_debug_stat_name(Int32(i)))) \(Double(s1[i] - s0[i]) / k)") }
	for (i, name) in ["rc_plain", "rc_shared", "rc_immortal"].enumerated() { out("stat \(name) \(Double(rc1[i] - rc0[i]) / k)") }
	out("stat coro_switches \(Double(sw1 - sw0) / k)")
	out("rc_op_ns_stats \(clj_debug_rc_op_ns(20_000_000))")
	if opts.census { clj_debug_census_print(UInt32(opts.statIterations)) }
	// Two types may share a name (a deftype's and a core one), so the rows are summed by name.
	var before: [String: UInt64] = [:], after: [String: UInt64] = [:]
	for i in 0..<n0 { before[String(cString: names0[i]!), default: 0] += counts0[i] }
	for i in 0..<n1 { after[String(cString: names1[i]!), default: 0] += counts1[i] }
	for (name, count) in after.map({ ($0.key, $0.value - (before[$0.key] ?? 0)) }).filter({ $0.1 > 0 }).sorted(by: { $0.1 > $1.1 }).prefix(20) {
		out("alloc_type \(name.replacingOccurrences(of: " ", with: "_")) \(Double(count) / k)")
	}
	out("result \(expected)")
	exit(0)
}

var times: [Double] = []
while times.count < opts.maxIterations && (times.count < opts.minIterations || times.reduce(0, +) < opts.minSeconds * 1000) {
	times.append(timed())
}
let sorted = times.sorted()
out("iterations \(times.count)")
out("median_ms \(sorted[sorted.count / 2])")
out("min_ms \(sorted[0])")
out("result \(expected)")
exit(0)
