# @ai-generated(solo)
"""corpus-bench's ClojureDart reference (docs/notes/benchmarks.md, "ClojureDart"): the workloads' sources compiled to
Dart, run as a `dart compile exe` binary (cljd-aot, a Flutter release build) and on the Dart VM from kernel (cljd-jit,
a debug build). corpus_refs.PORTS holds its two modes, which share one build and one report table."""

import os
import re
import shutil
import subprocess
import time

import corpus_refs

TAG = "0.9.20261002"
SHA = "247b3c2c5f2af8378e49a8f1271fb4e45e4a6982"
LOAD_PATH = ["corpus/medley/src", "corpus/math-combinatorics/src", "corpus/dependency/src"]
# The per-target sources ClojureDart compiles in place of a workload's .clj, and the harness.
SHIMS = "bench/workloads/cljd/src"


def _arm(anchor, cljd):
	"""A `:cljd` arm put first in the reader conditional that starts at anchor (ClojureDart reads only :cljd and
	:default, and these libraries' host code sits under :clj or :default)."""
	return anchor, "#?(:cljd " + cljd + "\n   " + anchor[len("#?("):]


def _cljd_only(form, cljd):
	"""form under :default, cljd under :cljd: for an `instance?` of a host class, whose type must be a literal."""
	return form, "#?(:cljd " + cljd + "\n   :default " + form + ")"


# The corpus libraries' host-specific spots, rewritten in the copy ClojureDart compiles; each anchor must occur, so
# a change upstream fails the build instead of compiling something else. What the workloads call keeps its algorithm;
# the rest only has to compile.
PATCHES = {
	"medley/core.cljc": [
		_arm("#?(:cljs    (satisfies? cljs.core/IEditableCollection coll)", "(satisfies? IEditableCollection coll)"),
		_arm("#?(:cljs    (cljs.core/MapEntry. k v nil)", "(MapEntry k v)"),
		# ClojureDart has no PersistentQueue; `queue` and `queue?` are not called.
		_arm("#?(:cljs    cljs.core/PersistentQueue.EMPTY", '(throw (ex-info "no PersistentQueue in ClojureDart" {}))'),
		_cljd_only("(instance? #?(:cljs    cljs.core/PersistentQueue\n"
		           "                :default clojure.lang.PersistentQueue) x)", "false"),
		_arm("#?(:cljs    (or (true? x) (false? x))", "(dart/is? x bool)"),
		# partition-between and window buffer in a growable Dart List where the JVM uses an ArrayList.
		_arm("#?(:clj  (java.util.ArrayList.)", "#dart []"),
		_arm("#?(:cljr    (zero? (.-Count part))", "(.-isEmpty part)"),
		_arm("#?(:cljr .ToArray :default .toArray)", ".toList"),
		_arm("#?(:clj  (java.util.ArrayList. ^int n)", "#dart []"),
		_arm("#?(:clj  (.size part)", "(.-length part)"),
		_arm("#?(:clj  (.remove part 0)", "(.removeAt part 0)"),
		_arm("#?(:clj  (.toArray part)", "(.toList part)"),
		# A defn inside letfn compiles to Dart naming the letfn's fn out of its scope: first* goes top level.
		("(letfn [(first* [s v] (if s (first s) v))]\n", "(defn- first* [s v] (if s (first s) v))\n(do\n"),
		_cljd_only("(instance? #?(:clj  java.util.UUID\n                :cljr System.Guid\n"
		           "                :cljs cljs.core/UUID) x)", "(cljd.core/uuid? x)"),
		_cljd_only("(instance? #?(:clj  java.util.regex.Pattern\n                :cljr System.Text.RegularExpressions.Regex\n"
		           "                :cljs js/RegExp) x)", "(dart/is? x RegExp)"),
	],
	"clojure/math/combinatorics.cljc": [
		# ClojureDart has no *' (nor bigints): a Dart int wraps past 2^63, which no workload value reaches.
		_arm("#?(:cljs (def *' *))", "(def *' *)"),
		_arm("#?(:cljs (def +' +))", "(def +' +)"),
		_arm("#?(:clj (Long/valueOf 1) :cljs 1)", "1"),
		_arm("#?(:clj (Long/valueOf 0) :cljs 0)", "0"),
		_arm("#?(:clj\n(defmacro ^:private reify-bool", "(defn- reify-bool [statement] (if statement 1 0))"),
	],
	"workloads/suite_data.clj": [
		# ClojureDart's to-array of a seq, which sort calls, took 0.3 ms per element on the Intel dev Mac (0.8 s for
		# 3000, realized or not): unchanged, this sort is 50 s of a 51 s iteration. A vector goes through List.toList.
		("(sort > (map #(mod (* % 7919) 10007) (range n)))", "(sort > (vec (map #(mod (* % 7919) 10007) (range n))))"),
	],
	"com/stuartsierra/dependency.cljc": [
		_arm("#?(:clj Long/MAX_VALUE", "9223372036854775807"),
		# A record's positional constructor takes meta, extmap and hash too in ClojureDart; its factory does not.
		("(MapDependencyGraph.\n", "(->MapDependencyGraph\n"),
	],
}
NA = {w: "ClojureDart has no core.async (its concurrency is Dart's Futures and Streams on one isolate)"
      for w in ("async-pipeline", "async-libs", "async-broadcast")}
MODES = ["cljd-aot", "cljd-jit"]


def ns_file(workload):
	return workload.replace("-", "_")


def project(refs):
	return os.path.join(refs.b.out, "cljd")


def program(refs, ref, w):
	if ref == "cljd-aot":
		return os.path.join(project(refs), "aot", "entry_" + ns_file(w))
	return os.path.join(project(refs), "jit", "entry_%s.dill" % ns_file(w))


def copy_patched(refs, path, rel, src):
	with open(path) as fh:
		text = fh.read()
	for old, new in PATCHES.get(rel, []):
		if old not in text:
			refs.b.errors.append("the ClojureDart patch of %s finds no `%s`" % (rel, old.splitlines()[0]))
			return False
		text = text.replace(old, new)
	if rel.startswith("clojure/"):
		# ClojureDart resolves a required clojure.x as cljd.x, so the library takes that name.
		name = rel[len("clojure/"):-len(".cljc")].replace("/", ".").replace("_", "-")
		# The name follows `(ns` and the ns's metadata map, on its own line or not.
		text, n = re.subn(r"(\(ns\b(?:\s*#?\^\{.*?\})?\s*)clojure\.%s\b" % re.escape(name), r"\1cljd." + name,
		                  text, count=1, flags=re.S)
		if n != 1:
			refs.b.errors.append("ClojureDart: no `(ns clojure.%s` in %s" % (name, rel))
			return False
		rel = "cljd/" + rel[len("clojure/"):]
	if rel.endswith(".clj"):
		rel += "d"
	os.makedirs(os.path.dirname(os.path.join(src, rel)), exist_ok=True)
	with open(os.path.join(src, rel), "w") as fh:
		fh.write(text)
	return True


def compile_dart(refs, cwd, entry, kind, out, name):
	"""`dart compile exe|kernel`; a failure is an error of the report and leaves that program out."""
	p = subprocess.run(["dart", "compile", kind, entry, "-o", out], cwd=cwd, stdout=subprocess.PIPE,
	                   stderr=subprocess.STDOUT, text=True, timeout=900)
	refs.b.keep("%s.dart-compile-%s.txt" % (name, kind), p.stdout)
	if p.returncode != 0:
		refs.b.errors.append("dart compile %s %s exited %d: %s" % (kind, name, p.returncode, p.stdout.strip()[-600:]))


def build(refs, workloads):
	"""One ClojureDart project for every workload, an entry namespace and a `bin/` file per workload, compiled to Dart
	in one `cljd.build compile`, then each entry to an executable and to kernel. corpus_refs calls it once per mode:
	the first call builds both."""
	if not hasattr(refs, "cljd_built"):
		refs.cljd_built = build_all(refs, [w for w in workloads if w not in NA])
	return refs.cljd_built


def build_all(refs, workloads):
	p = subprocess.run(["dart", "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
	refs.versions["dart"] = " ".join(p.stdout.split())[:120]
	refs.versions["cljd"] = TAG
	if not workloads:
		return True
	t0 = time.monotonic()
	root, d = refs.root, project(refs)
	shutil.rmtree(d, ignore_errors=True)
	src, gen = os.path.join(d, "src"), os.path.join(d, "gen")
	os.makedirs(os.path.join(src, "workloads"))
	os.makedirs(os.path.join(gen, "bench"))
	for sub in ("bin", "aot", "jit"):
		os.makedirs(os.path.join(d, sub))
	# cljd.build wants every path inside the project; a namespace is found as .cljd or .cljc.
	for top in ["bench/workloads/src"] + LOAD_PATH:
		for dirpath, _, files in os.walk(os.path.join(root, top)):
			for f in files:
				rel = os.path.relpath(os.path.join(dirpath, f), os.path.join(root, top))
				if rel.startswith("workloads/"):
					if not f.endswith(".clj") or f[:-len(".clj")].replace("_", "-") in NA:
						continue
				elif not f.endswith(".cljc"):
					continue
				if not copy_patched(refs, os.path.join(root, top, rel), rel, src):
					return False
	shutil.copytree(os.path.join(root, SHIMS), src, dirs_exist_ok=True)
	nses = []
	for w in workloads:
		with open(os.path.join(gen, "bench", "entry_%s.cljd" % ns_file(w)), "w") as f:
			f.write("(ns bench.entry-%s (:require [bench.harness :as h] [workloads.%s :as w]))\n"
			        "(defn main [args] (h/run-main w/run w/expected args))\n" % (w, w))
		with open(os.path.join(d, "bin", "entry_%s.dart" % ns_file(w)), "w") as f:
			# ClojureDart names a namespace's library after it, dashes kept.
			f.write('export "../lib/cljd-out/bench/entry-%s.dart" show main;\n' % w)
		nses.append("bench.entry-" + w)
	with open(os.path.join(d, "pubspec.yaml"), "w") as f:
		f.write("name: cljd_bench\nenvironment:\n  sdk: ^3.0.0\n")
	deps = ('{:paths ["src" "gen"] :deps {tensegritics/clojuredart {:git/url '
	        '"https://github.com/tensegritics/ClojureDart.git" :git/tag "%s" :git/sha "%s"}} '
	        ':aliases {:cljd {:main-opts ["-m" "cljd.build"]}} :cljd/opts {:kind :dart :main %s}' % (TAG, SHA, nses[0]))
	# CI caches ClojureDart's jars with the other references, apart from the JVM's ~/.m2.
	if os.environ.get("CORPUS_BENCH_M2"):
		deps += ' :mvn/local-repo "%s"' % os.environ["CORPUS_BENCH_M2"]
	with open(os.path.join(d, "deps.edn"), "w") as f:
		f.write(deps + "}\n")
	p = subprocess.run(["clojure", "-M:cljd", "compile"] + nses, cwd=d, stdout=subprocess.PIPE,
	                   stderr=subprocess.STDOUT, text=True, timeout=3600)
	refs.b.keep("cljd-build.txt", p.stdout)
	if p.returncode != 0:
		refs.b.errors.append("the ClojureDart build exited %d: %s" % (p.returncode, p.stdout.strip()[-1200:]))
		return False
	for w in workloads:
		entry = os.path.join("bin", "entry_%s.dart" % ns_file(w))
		if "cljd-aot" in refs.refs:
			compile_dart(refs, d, entry, "exe", program(refs, "cljd-aot", w), w + ".cljd")
		if "cljd-jit" in refs.refs:
			compile_dart(refs, d, entry, "kernel", program(refs, "cljd-jit", w), w + ".cljd")
	refs.cost["cljd-build"] = time.monotonic() - t0
	refs.log("ClojureDart programs built in %.0f s" % refs.cost["cljd-build"])
	return True


def cmd(refs, ref, w, once):
	path = program(refs, ref, w)
	if not os.path.exists(path):
		return ["sh", "-c", "echo 'not built: its dart compile failed' >&2; exit 1"]
	# Code compiled ahead has no tiers to warm: it takes ours and Hermes's policy, the JIT the JVM's.
	mode = ["once"] if once else ([] if ref == "cljd-jit" else ["nojit"])
	return (["dart"] if ref == "cljd-jit" else []) + [path] + mode


def section(b, refs, add, fmt, median_of):
	"""The ClojureDart table: each mode's warm, 1st and cold, closed against them, and the result check."""
	modes = [r for r in MODES if r in refs.refs]
	if not modes or getattr(refs, "cljd_reported", False):
		return
	refs.cljd_reported = True
	add("## ClojureDart")
	add("")
	add("The workloads' sources compiled by ClojureDart %s to Dart (docs/notes/benchmarks.md, \"ClojureDart\"; the "
	    "corpus libraries patched where they keep host code, suite-data sorting a vector): *aot* is `dart compile exe`, "
	    "what a Flutter release build ships, timed with ours and Hermes's policy (one iteration, then >= 3 and >= 2 s, "
	    "at most 30); *jit* the Dart VM from `dart compile kernel`, as a Flutter debug build runs, with the JVM's (>= 5 "
	    "iterations and 5 s of warm-up, then >= 5 and >= 3 s). *1st* is the first iteration of a fresh process and "
	    "*cold* that process start to exit; built before the workloads, outside every time. Ratios are ours over "
	    "theirs (below 1: ours is faster); *cljd/dart* is ClojureDart's AOT over the idiomatic Dart port's. %s." % (
		    TAG, refs.versions.get("dart", "?")))
	add("")
	cols = []
	for r in modes:
		cols += ["%s warm" % r, "%s 1st" % r, "%s cold" % r]
	ratio_cols = [("closed/%s" % r, "closed", r) for r in modes]
	if "cljd-aot" in modes and "dart" in refs.refs:
		ratio_cols.append(("cljd/dart", "cljd-aot", "dart"))
	add("| workload | closed | " + " | ".join(cols + [c[0] for c in ratio_cols]) + " | same result |")
	add("|---|---:|" + "---:|" * (len(cols) + len(ratio_cols)) + "---|")
	for w in b.args.only:
		r = b.results[w]
		if w in NA:
			add("| %s | %s | %s | n/a |" % (w, fmt(median_of(r.get("closed"))), " | ".join(["n/a"] * (len(cols) + len(ratio_cols)))))
			continue
		cells = []
		for ref in modes:
			cells += [fmt(median_of(r.get(ref)), 1), fmt(r.get(ref + "_cold_first_ms"), 0),
			          fmt(r.get(ref + "_cold_process_ms"), 0)]
		for _, ours, ref in ratio_cols:
			o, t = median_of(r.get(ours)), median_of(r.get(ref))
			cells.append("%.2f×" % (o / t) if o and t else "—")
		verdicts = {ref: r[ref].get("expected") for ref in modes if r.get(ref)}
		same = "—" if not verdicts else ("yes" if all(v == "same" for v in verdicts.values()) else
		                                 "NO: " + ", ".join("%s %s" % (k, v) for k, v in verdicts.items() if v != "same"))
		add("| %s | %s | %s | %s |" % (w, fmt(median_of(r.get("closed")), 1), " | ".join(cells), same))
	add("")
	add("n/a: the async workloads; %s." % NA["async-libs"])
	add("")
	diffs = []
	for w in b.args.only:
		r = b.results[w]
		jvm = (r.get("jvm") or {}).get("result")
		for ref in modes:
			got = (r.get(ref) or {}).get("result")
			if jvm is not None and got is not None and got != jvm:
				diffs.append("%s %s: %s" % (w, ref, corpus_refs.first_difference(jvm, got)))
	add("*same result* is ClojureDart's `=` against `expected`. Printed results against the JVM's: " +
	    ("; ".join(diffs) if diffs else "identical everywhere") + ".")
	add("")
	add("Wall time: the ClojureDart build %.1f min (the compile to Dart and every `dart compile`), the runs %s." % (
		refs.cost.get("cljd-build", 0) / 60, ", ".join("%s %.1f min" % (m, refs.cost.get(m, 0) / 60) for m in modes)))
	add("")


class Mode:
	"""One of corpus_refs.PORTS' entries: the AOT or the JIT of the one ClojureDart build."""
	TOOL = "dart"
	NA = NA
	build = staticmethod(build)
	section = staticmethod(section)

	def __init__(self, ref):
		self.ref = ref

	def cmd(self, refs, w, once):
		return cmd(refs, self.ref, w, once)


AOT, JIT = Mode("cljd-aot"), Mode("cljd-jit")
