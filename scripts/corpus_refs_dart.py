# @ai-generated(solo)
"""corpus-bench's Dart references (docs/notes/benchmarks.md, "Dart references"): the workloads' sources compiled by
ClojureDart, as a `dart compile exe` binary (cljd-aot, Flutter's release mode) and on the Dart VM from kernel
(cljd-jit, Flutter's debug mode), and the data workloads hand-ported to idiomatic Dart (dart-aot,
bench/workloads/dart). Same interface as corpus_refs.Refs: prepare, steps, and section for the report."""

import os
import re
import shutil
import subprocess
import time

REFS = ["cljd-aot", "cljd-jit", "dart-aot"]
CLJD_TAG = "0.9.20261002"
CLJD_SHA = "247b3c2c5f2af8378e49a8f1271fb4e45e4a6982"
CLJD_LOAD_PATH = ["corpus/medley/src", "corpus/math-combinatorics/src", "corpus/dependency/src"]
# The per-target sources ClojureDart compiles in place of a workload's .clj, and the harness.
CLJD_SHIMS = "bench/workloads/cljd/src"


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
CLJD_PATCHES = {
	"medley/core.cljc": [
		_arm("#?(:cljs    (satisfies? cljs.core/IEditableCollection coll)", "(satisfies? IEditableCollection coll)"),
		_arm("#?(:cljs    (cljs.core/MapEntry. k v nil)", "(MapEntry k v)"),
		# ClojureDart has no PersistentQueue; `queue` and `queue?` are not called.
		_arm("#?(:cljs    cljs.core/PersistentQueue.EMPTY", '(throw (ex-info "no PersistentQueue in ClojureDart" {}))'),
		_cljd_only("(instance? #?(:cljs    cljs.core/PersistentQueue\n                :default clojure.lang.PersistentQueue) x)",
		           "false"),
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
		_cljd_only("(instance? #?(:clj  java.util.UUID\n                :cljr System.Guid\n                :cljs cljs.core/UUID) x)",
		           "(cljd.core/uuid? x)"),
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
		# ClojureDart's to-array of a seq, which sort takes, costs 0.3 ms per element (0.8 s for 3000, realized or
		# not): unchanged, this sort of 30000 is 50 s of the iteration's 51. A vector goes through List.toList.
		("(sort > (map #(mod (* % 7919) 10007) (range n)))", "(sort > (vec (map #(mod (* % 7919) 10007) (range n))))"),
	],
	"com/stuartsierra/dependency.cljc": [
		_arm("#?(:clj Long/MAX_VALUE", "9223372036854775807"),
		# A record's positional constructor takes meta, extmap and hash too in ClojureDart; its factory does not.
		("(MapDependencyGraph.\n", "(->MapDependencyGraph\n"),
	],
}
DART_PORTS = "bench/workloads/dart"
_ASYNC = ("core.async: ClojureDart has no port of it (its concurrency is Dart's Futures and Streams on one isolate)")
CLJD_NA = {"async-pipeline": _ASYNC, "async-broadcast": _ASYNC, "async-libs": _ASYNC}
_PORT_ASYNC = ("go blocks and channels have no counterpart in Dart's library; a port to Streams would be another "
               "program, not the same algorithm")
DART_NA = {"async-pipeline": _PORT_ASYNC, "async-broadcast": _PORT_ASYNC, "async-libs": _PORT_ASYNC}


def ns_file(workload):
	return workload.replace("-", "_")


def expected_literal(root, workload):
	"""The text of the workload's `expected` as its source writes it, which is the JVM's pr-str of it."""
	with open(os.path.join(root, "bench/workloads/src/workloads", ns_file(workload) + ".clj")) as f:
		m = re.search(r"^\(def expected '(.*)\)\s*$", f.read(), re.M | re.S)
	return m.group(1) if m else None


class DartRefs:
	"""Builds the ClojureDart and Dart programs once, then runs each selected one on a workload."""

	def __init__(self, bench, refs, root, parse, log):
		self.b = bench
		self.refs = refs
		self.root, self.parse, self.log = root, parse, log
		self.versions = {}
		self.na = {}
		self.cost = {r: 0.0 for r in refs}
		self.cost["cljd-build"] = 0.0
		self.cost["dart-build"] = 0.0
		self.cljd = os.path.join(bench.out, "cljd")
		self.dart = os.path.join(bench.out, "dart")

	def prepare(self, workloads):
		if not self.refs:
			return
		if not shutil.which("dart"):
			self.b.errors.append("Dart: `dart` is not on PATH (CI installs it; --refs leaves cljd-* and dart-aot out)")
			self.refs = []
			return
		p = subprocess.run(["dart", "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
		self.versions["dart"] = " ".join(p.stdout.split())[:120]
		self.versions["cljd"] = CLJD_TAG
		cljd = [r for r in self.refs if r.startswith("cljd-")]
		if cljd and not self.build_cljd([w for w in workloads if w not in CLJD_NA]):
			self.refs = [r for r in self.refs if not r.startswith("cljd-")]
		if "dart-aot" in self.refs and not self.build_dart([w for w in workloads if w not in DART_NA]):
			self.refs = [r for r in self.refs if r != "dart-aot"]
		for w in workloads:
			for ref in self.refs:
				why = (CLJD_NA if ref.startswith("cljd-") else DART_NA).get(w)
				if why:
					self.na[(ref, w)] = why

	def compile_dart(self, cwd, entry, kind, out, name):
		"""`dart compile exe|kernel`; a failure is an error of the report and leaves that program out."""
		p = subprocess.run(["dart", "compile", kind, entry, "-o", out], cwd=cwd, stdout=subprocess.PIPE,
		                   stderr=subprocess.STDOUT, text=True, timeout=900)
		self.b.keep("%s.dart-compile-%s.txt" % (name, kind), p.stdout)
		if p.returncode != 0:
			self.b.errors.append("dart compile %s %s exited %d: %s" % (kind, name, p.returncode, p.stdout.strip()[-600:]))
			return False
		return True

	def copy_patched(self, path, rel, src):
		with open(path) as fh:
			text = fh.read()
		for old, new in CLJD_PATCHES.get(rel, []):
			if old not in text:
				self.b.errors.append("the ClojureDart patch of %s finds no `%s`" % (rel, old.splitlines()[0]))
				return False
			text = text.replace(old, new)
		if rel.startswith("clojure/"):
			# ClojureDart resolves a required clojure.x as cljd.x, so the library takes that name.
			name = rel[len("clojure/"):-len(".cljc")].replace("/", ".").replace("_", "-")
			# The name follows `(ns` and the ns's metadata map, on its own line or not.
			text, n = re.subn(r"(\(ns\b(?:\s*#?\^\{.*?\})?\s*)clojure\.%s\b" % re.escape(name), r"\1cljd." + name,
			                  text, count=1, flags=re.S)
			if n != 1:
				self.b.errors.append("ClojureDart: no `(ns clojure.%s` in %s" % (name, rel))
				return False
			rel = "cljd/" + rel[len("clojure/"):]
		if rel.endswith(".clj"):
			rel += "d"
		os.makedirs(os.path.dirname(os.path.join(src, rel)), exist_ok=True)
		with open(os.path.join(src, rel), "w") as fh:
			fh.write(text)
		return True

	def build_cljd(self, workloads):
		"""One ClojureDart project for every workload, an entry namespace and a `bin/` file per workload, compiled to
		Dart in one `cljd.build compile`, then each entry to an executable and to kernel."""
		t0 = time.monotonic()
		root, d = self.root, self.cljd
		shutil.rmtree(d, ignore_errors=True)
		src, gen = os.path.join(d, "src"), os.path.join(d, "gen")
		os.makedirs(os.path.join(src, "workloads"))
		os.makedirs(os.path.join(gen, "bench"))
		for sub in ("bin", "aot", "jit"):
			os.makedirs(os.path.join(d, sub))
		# The workload and library sources, patched where CLJD_PATCHES says, a workload as .cljd: ClojureDart looks a
		# namespace up by that extension or .cljc. The shims and the harness are copied over them; every path is in
		# the project, as cljd.build wants.
		for top in ["bench/workloads/src"] + CLJD_LOAD_PATH:
			for dirpath, _, files in os.walk(os.path.join(root, top)):
				for f in files:
					rel = os.path.relpath(os.path.join(dirpath, f), os.path.join(root, top))
					if rel.startswith("workloads/"):
						if not f.endswith(".clj") or f[:-len(".clj")].replace("_", "-") in CLJD_NA:
							continue
					elif not f.endswith(".cljc"):
						continue
					if not self.copy_patched(os.path.join(root, top, rel), rel, src):
						return False
		shutil.copytree(os.path.join(root, CLJD_SHIMS), src, dirs_exist_ok=True)
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
		paths = ["src", "gen"]
		deps = ('{:paths [%s] :deps {tensegritics/clojuredart {:git/url "https://github.com/tensegritics/ClojureDart.git" '
		        ':git/tag "%s" :git/sha "%s"}} :aliases {:cljd {:main-opts ["-m" "cljd.build"]}} '
		        ':cljd/opts {:kind :dart :main %s}' % (" ".join('"%s"' % p for p in paths), CLJD_TAG, CLJD_SHA, nses[0]))
		# CI caches ClojureDart's jars with the other references, apart from the JVM's ~/.m2.
		if os.environ.get("CORPUS_BENCH_M2"):
			deps += ' :mvn/local-repo "%s"' % os.environ["CORPUS_BENCH_M2"]
		with open(os.path.join(d, "deps.edn"), "w") as f:
			f.write(deps + "}\n")
		p = subprocess.run(["clojure", "-M:cljd", "compile"] + nses, cwd=d, stdout=subprocess.PIPE,
		                   stderr=subprocess.STDOUT, text=True, timeout=3600)
		self.b.keep("cljd-build.txt", p.stdout)
		if p.returncode != 0:
			self.b.errors.append("the ClojureDart build exited %d: %s" % (p.returncode, p.stdout.strip()[-1200:]))
			return False
		for w in workloads:
			entry = os.path.join("bin", "entry_%s.dart" % ns_file(w))
			if "cljd-aot" in self.refs:
				self.compile_dart(d, entry, "exe", os.path.join(d, "aot", "entry_" + ns_file(w)), w + ".cljd")
			if "cljd-jit" in self.refs:
				self.compile_dart(d, entry, "kernel", os.path.join(d, "jit", "entry_%s.dill" % ns_file(w)), w + ".cljd")
		self.cost["cljd-build"] = time.monotonic() - t0
		self.log("ClojureDart programs built in %.0f s" % self.cost["cljd-build"])
		return True

	def build_dart(self, workloads):
		"""The ports, one executable each, built from a copy of their package so the tree stays clean of .dart_tool."""
		t0 = time.monotonic()
		d = self.dart
		shutil.rmtree(d, ignore_errors=True)
		shutil.copytree(os.path.join(self.root, DART_PORTS), os.path.join(d, "src"))
		os.makedirs(os.path.join(d, "aot"))
		p = subprocess.run(["dart", "pub", "get", "--offline"], cwd=os.path.join(d, "src"), stdout=subprocess.PIPE,
		                   stderr=subprocess.STDOUT, text=True, timeout=600)
		if p.returncode != 0:
			self.b.errors.append("dart pub get for the ports exited %d: %s" % (p.returncode, p.stdout.strip()[-600:]))
			return False
		for w in workloads:
			self.compile_dart(os.path.join(d, "src"), os.path.join("bin", ns_file(w) + ".dart"), "exe",
			                  os.path.join(d, "aot", ns_file(w)), w + ".dart")
		self.cost["dart-build"] = time.monotonic() - t0
		self.log("Dart ports built in %.0f s" % self.cost["dart-build"])
		return True

	def program(self, ref, w):
		if ref == "cljd-aot":
			return os.path.join(self.cljd, "aot", "entry_" + ns_file(w))
		if ref == "cljd-jit":
			return os.path.join(self.cljd, "jit", "entry_%s.dill" % ns_file(w))
		if ref == "dart-aot":
			return os.path.join(self.dart, "aot", ns_file(w))
		raise ValueError(ref)

	def cmd(self, ref, w, once):
		# Code compiled ahead has no tiers to warm: it takes ours and Hermes's policy, the JIT the JVM's.
		mode = ["once"] if once else ([] if ref == "cljd-jit" else ["nojit"])
		return (["dart"] if ref == "cljd-jit" else []) + [self.program(ref, w)] + mode

	def run_one(self, ref, w, once):
		name = "%s.%s%s" % (w, ref, "-once" if once else "")
		if not os.path.exists(self.program(ref, w)):
			self.b.errors.append("%s %s: not built (logs/%s.*)" % (w, ref, w))
			return None
		cmd = self.cmd(ref, w, once)
		t0 = time.monotonic()
		p, ms = self.b.bounded(cmd, None, name)
		self.cost[ref] += time.monotonic() - t0
		if p is None:
			self.b.errors.append("%s %s%s: hung past the run timeout" % (w, ref, " once" if once else ""))
			return None
		self.b.keep(name + ".txt", p.stdout + "\n--- stderr\n" + p.stderr)
		if p.returncode != 0:
			self.b.errors.append("%s %s%s: exit %d: %s" % (w, ref, " once" if once else "", p.returncode,
			                                                (p.stderr.strip() or p.stdout.strip())[-600:]))
			return None
		r = self.parse(p.stdout)
		r["process_ms"] = ms
		if ref == "dart-aot":
			# A port has no Clojure value to `=`: its printed result is checked against the source's `expected`.
			lit = expected_literal(self.root, w)
			r["expected"] = "same" if lit is not None and r.get("result") == lit else "DIFFERENT"
		return r

	def steps(self, w, res):
		"""Each reference's timed run and its cold process, a yield after each."""
		for ref in self.refs:
			if (ref, w) in self.na:
				continue
			res[ref] = self.run_one(ref, w, False)
			yield
			cold = self.run_one(ref, w, True)
			yield
			if cold:
				res[ref + "_cold_process_ms"] = cold["process_ms"]
				res[ref + "_cold_first_ms"] = cold.get("first_ms")


def section(b, refs, add, fmt, median_of, first_difference):
	"""The Dart table: each program's warm, 1st and cold, closed against them, and the result check."""
	if not refs.refs:
		return
	add("## References: ClojureDart and idiomatic Dart")
	add("")
	add("The workloads' sources compiled by ClojureDart %s to Dart, and the data workloads hand-ported to idiomatic "
	    "Dart (`bench/workloads/dart`, the fairness rules in docs/notes/benchmarks.md \"Dart references\"). *aot* is "
	    "`dart compile exe` (Flutter's release mode), timed with ours and Hermes's policy (one iteration, then >= 3 "
	    "and >= 2 s); *jit* the Dart VM from `dart compile kernel` (Flutter's debug mode), with the JVM's (>= 5 "
	    "iterations and 5 s of warm-up, then >= 5 and >= 3 s). *1st* is the first iteration of a fresh process and "
	    "*cold* that process start to exit. The programs are built before the workloads, outside every time. Ratios "
	    "are ours over theirs (below 1: ours is faster). Versions: %s." % (
		    CLJD_TAG, "; ".join("%s %s" % (k, v) for k, v in refs.versions.items())))
	add("")
	cols = []
	for r in refs.refs:
		cols += ["%s warm" % r, "%s 1st" % r, "%s cold" % r]
	ratio_cols = [("closed/cljd", "closed", "cljd-aot"), ("closed/dart", "closed", "dart-aot"),
	              ("cljd/dart", "cljd-aot", "dart-aot")]
	ratio_cols = [c for c in ratio_cols if c[2] in refs.refs and (c[1] == "closed" or c[1] in refs.refs)]
	add("| workload | closed | " + " | ".join(cols + [c[0] for c in ratio_cols]) + " | same result |")
	add("|---|---:|" + "---:|" * (len(cols) + len(ratio_cols)) + "---|")
	for w in b.args.only:
		r = b.results[w]
		cells = []
		for ref in refs.refs:
			if (ref, w) in refs.na:
				cells += ["n/a", "n/a", "n/a"]
				continue
			cells += [fmt(median_of(r.get(ref)), 1), fmt(r.get(ref + "_cold_first_ms"), 0),
			          fmt(r.get(ref + "_cold_process_ms"), 0)]
		for _, ours, ref in ratio_cols:
			o, t = median_of(r.get(ours)), median_of(r.get(ref))
			cells.append("%.2f×" % (o / t) if o and t else ("n/a" if (ref, w) in refs.na or (ours, w) in refs.na else "—"))
		verdicts = {ref: r[ref].get("expected") for ref in refs.refs if r.get(ref)}
		same = "—" if not verdicts else ("yes" if all(v == "same" for v in verdicts.values()) else
		                                 "NO: " + ", ".join("%s %s" % (k, v) for k, v in verdicts.items() if v != "same"))
		add("| %s | %s | %s | %s |" % (w, fmt(median_of(r.get("closed")), 1), " | ".join(cells), same))
	add("")
	na = sorted({(ref.split("-")[0], w, why) for (ref, w), why in refs.na.items()})
	if na:
		add("n/a: " + "; ".join("%s on %s: %s" % (w, "ClojureDart" if k == "cljd" else "the Dart port", why)
		                        for k, w, why in na) + ".")
		add("")
	diffs = []
	for w in b.args.only:
		r = b.results[w]
		jvm = (r.get("jvm") or {}).get("result")
		for ref in refs.refs:
			got = (r.get(ref) or {}).get("result")
			if jvm is not None and got is not None and got != jvm:
				diffs.append("%s %s: %s" % (w, ref, first_difference(jvm, got)))
	add("Printed results against the JVM's (a port's *same result* is its printed result against the `expected` "
	    "literal of the workload's source): " + ("; ".join(diffs) if diffs else "identical everywhere") + ".")
	add("")
	add("Wall time of the Dart references: " + ", ".join("%s %.1f min" % (k, v / 60) for k, v in refs.cost.items() if v) + ".")
	add("")
