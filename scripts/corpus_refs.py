# @ai-generated(solo)
"""The reference runtimes of make corpus-bench beside the JVM: babashka, and ClojureScript :advanced on Node and on
Hermes (docs/notes/benchmarks.md, "Reference runtimes"). Each runs the workloads' sources with jvm.clj's report
lines; scripts/corpus-bench.py times our builds and calls this module once per workload."""

import os
import re
import shutil
import subprocess
import time

import corpus_cljd
import corpus_dart
import corpus_swift

REFS = ["jvm", "bb", "cljs-node", "cljs-hermes", "swift", "dart", "cljd-aot", "cljd-jit"]
REF_LABEL = {"jvm": "JVM", "bb": "bb", "cljs-node": "cljs-node", "cljs-hermes": "cljs-hermes", "swift": "swift",
             "dart": "dart", "cljd-aot": "cljd-aot", "cljd-jit": "cljd-jit"}
# References whose own module builds and reports them: the hand ports, and ClojureDart's two modes of one build.
PORTS = {"swift": corpus_swift, "dart": corpus_dart, "cljd-aot": corpus_cljd.AOT, "cljd-jit": corpus_cljd.JIT}
CLJS_VERSION = "1.12.145"
CLJS_DEPS = ('{:deps {org.clojure/clojurescript {:mvn/version "%s"} org.clojure/core.async {:mvn/version '
             '"1.6.681"} datascript/datascript {:mvn/version "1.8.1"} persistent-sorted-set/persistent-sorted-set '
             '{:mvn/version "0.3.2"}}}' % CLJS_VERSION)
CLJS_LOAD_PATH = ["corpus/medley/src", "corpus/math-combinatorics/src", "corpus/dependency/src"]
# The per-target sources: workloads.util's generator and the async workloads without <!!.
CLJS_SHIMS = "bench/workloads/cljs/src"
# Workloads whose ClojureScript run returns a channel: the harness times it to its delivery.
CLJS_CHANNEL = {"async-pipeline", "async-broadcast"}
CLJS_NA = {"async-libs": "turbine runs its routes on threads (`thread`, `<!!`, `>!!`), which ClojureScript has not"}
BB_NA = {"datascript": "its Datom, Entity and LRU are deftypes over clojure.lang interfaces (`ILookup`, `Seqable`, "
                       "`IPersistentCollection`, `IHashEq`), where babashka's deftype takes protocols, `Object` and "
                       "whole map types, and on the JVM its index is persistent-sorted-set's Java classes"}


def ns_file(workload):
	return workload.replace("-", "_")


class Refs:
	"""Locates the tools, builds the ClojureScript bundles once, and runs every selected reference on a workload."""

	def __init__(self, bench, refs, root, load_path, parse, log):
		self.b = bench
		self.refs = refs
		self.root, self.load_path, self.parse, self.log = root, load_path, parse, log
		self.tool = {}
		self.versions = {}
		self.na = {}
		self.cost = {r: 0.0 for r in refs}
		self.cost["cljs-build"] = 0.0
		self.cljs = os.path.join(bench.out, "cljs")

	def others(self):
		return [r for r in self.refs if r != "jvm"]

	def find(self, name, ref):
		path = shutil.which(name)
		if not path:
			self.b.errors.append("%s: `%s` is not on PATH (CI installs it; --refs leaves the reference out)" % (ref, name))
		self.tool[name] = path
		return path

	def version(self, ref, cmd):
		p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
		self.versions[ref] = " ".join(p.stdout.split())[:120]

	def prepare(self, workloads):
		"""The tools and the bundles; a reference whose tool or build is missing is reported and left out."""
		ready = []
		for ref in self.others():
			if ref == "bb" and self.find("bb", ref):
				self.version(ref, ["bb", "--version"])
				ready.append(ref)
			if ref == "cljs-node" and self.find("node", ref):
				self.version(ref, ["node", "--version"])
				ready.append(ref)
			if ref == "cljs-hermes" and self.find("hermes", ref) and self.find("hermesc", ref):
				p = subprocess.run(["hermes", "--version"], stdout=subprocess.PIPE, text=True)
				m = re.search(r"Hermes release version: (\S+).*?HBC bytecode version: (\d+)", p.stdout, re.S)
				self.versions[ref] = "Hermes %s, bytecode %s" % m.groups() if m else p.stdout[:120]
				ready.append(ref)
			if ref in PORTS and self.find(PORTS[ref].TOOL, ref) and PORTS[ref].build(self, workloads):
				ready.append(ref)
		self.refs = [r for r in self.refs if r == "jvm" or r in ready]
		if any(r.startswith("cljs-") for r in self.refs):
			if not self.build_cljs([w for w in workloads if w not in CLJS_NA]):
				self.refs = [r for r in self.refs if not r.startswith("cljs-")]
		for w in workloads:
			for ref in self.refs:
				if ref.startswith("cljs-") and w in CLJS_NA:
					self.na[(ref, w)] = CLJS_NA[w]
				if ref == "bb" and w in BB_NA:
					self.na[(ref, w)] = BB_NA[w]
			for ref in self.refs:
				if ref in PORTS and w in PORTS[ref].NA:
					self.na[(ref, w)] = PORTS[ref].NA[w]

	def build_cljs(self, workloads):
		"""One :advanced bundle per workload (bench/workloads/cljs/build.clj), and for Hermes its bytecode."""
		t0 = time.monotonic()
		src, gen = os.path.join(self.cljs, "src"), os.path.join(self.cljs, "gen")
		shutil.rmtree(self.cljs, ignore_errors=True)
		os.makedirs(os.path.join(src, "workloads"))
		os.makedirs(os.path.join(gen, "bench"))
		root = self.root
		# The workload sources unchanged, as .cljs: ClojureScript looks a namespace up by that extension.
		for f in os.listdir(os.path.join(root, "bench/workloads/src/workloads")):
			base = f[:-len(".clj")]
			if f.endswith(".clj") and base.replace("_", "-") not in CLJS_NA and \
			   not os.path.exists(os.path.join(root, CLJS_SHIMS, "workloads", base + ".cljs")):
				shutil.copy(os.path.join(root, "bench/workloads/src/workloads", f), os.path.join(src, "workloads", base + ".cljs"))
		entries = []
		for w in workloads:
			path = os.path.join(gen, "bench", "entry_%s.cljs" % ns_file(w))
			with open(path, "w") as f:
				if w in CLJS_CHANNEL:
					f.write("(ns bench.entry-%s (:require [bench.harness :as h] [workloads.%s :as w] "
					        "[cljs.core.async :as a]))\n(h/main (fn [k] (a/take! (w/run) k)) w/expected)\n" % (w, w))
				else:
					f.write("(ns bench.entry-%s (:require [bench.harness :as h] [workloads.%s :as w]))\n"
					        "(h/main (fn [k] (k (w/run))) w/expected)\n" % (w, w))
			entries.append(path)
		paths = [src, os.path.join(root, CLJS_SHIMS), gen] + [os.path.join(root, p) for p in CLJS_LOAD_PATH]
		deps = CLJS_DEPS[:-1] + " :paths [" + " ".join('"%s"' % p for p in paths) + "]"
		# CI caches the ClojureScript jars with the other references, apart from the JVM's ~/.m2.
		if os.environ.get("CORPUS_BENCH_M2"):
			deps += ' :mvn/local-repo "%s"' % os.environ["CORPUS_BENCH_M2"]
		deps += "}"
		p = subprocess.run(["clojure", "-Sdeps", deps, "-M", os.path.join(root, "bench/workloads/cljs/build.clj"),
		                    self.cljs] + entries, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
		                   timeout=3600)
		self.b.keep("cljs-build.txt", p.stdout + "\n--- stderr\n" + p.stderr)
		if p.returncode != 0:
			self.b.errors.append("the ClojureScript build exited %d: %s" % (p.returncode, p.stderr.strip()[-800:]))
			return False
		if "cljs-hermes" in self.refs:
			for w in workloads:
				# The Hermes CLI takes no script arguments; the mode is a global ahead of the bundle.
				for mode, args in (("warm", '["nojit"]'), ("once", '["once"]')):
					js = os.path.join(self.cljs, "entry_%s.%s.js" % (ns_file(w), mode))
					with open(js, "w") as out, open(os.path.join(self.cljs, "entry_%s.js" % ns_file(w))) as bundle:
						out.write("var BENCH_ARGS = %s;\n" % args)
						shutil.copyfileobj(bundle, out)
					# What a React Native release build ships: bytecode compiled ahead, optimized.
					h = subprocess.run(["hermesc", "-O", "-emit-binary", "-out", js[:-3] + ".hbc", js],
					                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=600)
					if h.returncode != 0:
						self.b.errors.append("hermesc %s exited %d: %s" % (w, h.returncode, h.stdout[-600:]))
		self.cost["cljs-build"] = time.monotonic() - t0
		self.log("ClojureScript bundles built in %.0f s" % self.cost["cljs-build"])
		return True

	def cmd(self, ref, w, once):
		root = self.root
		if ref in PORTS:
			return PORTS[ref].cmd(self, w, once)
		if ref == "bb":
			cp = ":".join(os.path.join(root, p) for p in self.load_path)
			return ["bb", "-cp", cp, os.path.join(root, "bench/workloads/jvm.clj"), "workloads." + w] + (["once"] if once else [])
		if ref == "cljs-node":
			return ["node", os.path.join(self.cljs, "entry_%s.js" % ns_file(w))] + (["once"] if once else [])
		if ref == "cljs-hermes":
			return ["hermes", os.path.join(self.cljs, "entry_%s.%s.hbc" % (ns_file(w), "once" if once else "warm"))]
		raise ValueError(ref)

	def run_one(self, ref, w, once):
		name = "%s.%s%s" % (w, ref, "-once" if once else "")
		t0 = time.monotonic()
		p, ms = self.b.bounded(self.cmd(ref, w, once), None, name)
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
		if hasattr(PORTS.get(ref), "verdict"):
			r["expected"] = PORTS[ref].verdict(self.root, w, r.get("result"))
		r["clock_hires"] = "clock_hires false" not in p.stdout
		return r

	def steps(self, w, res):
		"""Each reference's warm run and its cold process, a yield after each."""
		for ref in self.others():
			if (ref, w) in self.na:
				continue
			res[ref] = self.run_one(ref, w, False)
			yield
			cold = self.run_one(ref, w, True)
			yield
			if cold:
				res[ref + "_cold_process_ms"] = cold["process_ms"]
				res[ref + "_cold_first_ms"] = cold.get("first_ms")


def section(b, refs, add, fmt, median_of):
	clj_section(b, refs, add, fmt, median_of)
	for ref in refs.others():
		if ref in PORTS:
			PORTS[ref].section(b, refs, add, fmt, median_of)


def clj_section(b, refs, add, fmt, median_of):
	"""The References table: each reference's warm, 1st and cold, our builds against them, and the result check."""
	others = [r for r in refs.others() if r not in PORTS]
	if not others:
		return
	add("## References: babashka and ClojureScript")
	add("")
	add("The workloads' sources on babashka (SCI, interpreted: the reference for *interp*) and as ClojureScript %s "
	    "`:advanced` bundles, one per workload, on Node (V8) and on Hermes (React Native's engine; bytecode from "
	    "`hermesc -O`, what a release app ships). Warm is a median per iteration: bb runs jvm.clj itself and Node its "
	    "port (`bench/workloads/cljs/src/bench/harness.cljs`), both with the JVM's policy (>= 5 iterations and 5 s of "
	    "warm-up, then >= 5 and >= 3 s); Hermes, which has no JIT, ours (one iteration, then >= 3 and >= 2 s). *1st* "
	    "is the first iteration of a fresh process and *cold* that process start to exit. The bundles are built before "
	    "the workloads, outside every time. Ratios are ours over theirs (below 1: ours is faster). Versions: %s." % (
		    CLJS_VERSION, "; ".join("%s %s" % (r, refs.versions.get(r, "?")) for r in others)))
	add("")
	cols = []
	for r in others:
		cols += ["%s warm" % r, "%s 1st" % r, "%s cold" % r]
	ratio_cols = [("closed/bb", "closed", "bb"), ("interp/bb", "interp", "bb"), ("closed/node", "closed", "cljs-node"),
	              ("closed/hermes", "closed", "cljs-hermes")]
	ratio_cols = [c for c in ratio_cols if c[2] in others]
	add("| workload | JVM warm | " + " | ".join(cols + [c[0] for c in ratio_cols]) + " | same result |")
	add("|---|---:|" + "---:|" * (len(cols) + len(ratio_cols)) + "---|")
	coarse = []
	for w in b.args.only:
		r = b.results[w]
		cells = []
		for ref in others:
			if (ref, w) in refs.na:
				cells += ["n/a", "n/a", "n/a"]
				continue
			rr = r.get(ref)
			cells += [fmt(median_of(rr), 1), fmt(r.get(ref + "_cold_first_ms"), 0), fmt(r.get(ref + "_cold_process_ms"), 0)]
			if rr and not rr.get("clock_hires", True):
				coarse.append("%s %s" % (w, ref))
		for _, ours, ref in ratio_cols:
			o, t = median_of(r.get(ours)), median_of(r.get(ref))
			cells.append("%.2f×" % (o / t) if o and t else ("n/a" if (ref, w) in refs.na else "—"))
		verdicts = {ref: r[ref].get("expected") for ref in others if r.get(ref)}
		same = "—" if not verdicts else ("yes" if all(v == "same" for v in verdicts.values()) else
		                                 "NO: " + ", ".join("%s %s" % (k, v) for k, v in verdicts.items() if v != "same"))
		add("| %s | %s | %s | %s |" % (w, fmt(median_of(r.get("jvm")), 1), " | ".join(cells), same))
	add("")
	na = sorted({(w, "babashka" if ref == "bb" else "ClojureScript", why) for (ref, w), why in refs.na.items() if ref in others})
	if na:
		add("n/a: " + "; ".join("%s on %s: %s" % (w, on, why) for w, on, why in na) + ".")
		add("")
	add("Per-target sources (`bench/workloads/cljs/src`): `workloads.util` computes the generator's product with "
	    "`Math/imul` (a double loses the low bits past 2^53; the states are the JVM's), and async-pipeline and "
	    "async-broadcast return the channel of their result where the JVM blocks on it with `<!!`, which one thread "
	    "lacks; their work is the .clj file's. The rest compile unchanged. datascript runs datascript 1.8.1 and "
	    "persistent-sorted-set 0.3.2 from their jars on the JVM and ClojureScript, and their port to this runtime "
	    "here (`corpus/datascript/SOURCE`). json parses and writes with clojure.data.json 2.5.2: the jar on the JVM, "
	    "bb's built-in, the port here (`corpus/data-json/SOURCE`); data.json is JVM-only, so ClojureScript's "
	    "`workloads.json-host` is what an app there writes instead, `JSON.parse` with `js->clj` and `clj->js` with "
	    "`JSON.stringify`.")
	add("")
	# `=` holds across a JS number's long/double ambiguity, so the printed result is compared to the JVM's too.
	diffs = []
	for w in b.args.only:
		r = b.results[w]
		jvm = (r.get("jvm") or {}).get("result")
		for ref in others:
			got = (r.get(ref) or {}).get("result")
			if jvm is not None and got is not None and got != jvm:
				diffs.append("%s %s: %s" % (w, ref, first_difference(jvm, got)))
	add("Printed results against the JVM's: " + ("; ".join(diffs) if diffs else "identical everywhere") + ".")
	add("")
	if coarse:
		add("Timed by `Date.now` (1 ms; the Hermes CLI has no `performance.now`): " + ", ".join(coarse) + ".")
		add("")
	add("Wall time of the references: " + ", ".join("%s %.1f min" % (k, v / 60) for k, v in refs.cost.items()
	                                               if v and k.split("-")[0] not in {p.split("-")[0] for p in PORTS}) + ".")
	add("")


def first_difference(a, b):
	"""The first differing token of two printed values, with what the JVM printed."""
	ta, tb = re.findall(r'"(?:[^"\\]|\\.)*"|[^\s\[\]{}()]+', a), re.findall(r'"(?:[^"\\]|\\.)*"|[^\s\[\]{}()]+', b)
	diff = [(x, y) for x, y in zip(ta, tb) if x != y]
	if len(ta) != len(tb):
		diff.append(("%d tokens" % len(ta), "%d tokens" % len(tb)))
	return ", ".join("`%s` for the JVM's `%s`" % (y, x) for x, y in diff[:4]) + (" (%d more)" % (len(diff) - 4) if len(diff) > 4 else "")
