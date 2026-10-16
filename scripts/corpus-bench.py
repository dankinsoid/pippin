#!/usr/bin/env python3
# @ai-generated(solo)
"""make corpus-bench: the workloads of bench/workloads on JVM Clojure and on every backend here, with where the
compiled time goes (docs/notes/benchmarks.md, "Corpus workloads"). Writes <out>/report.md and prints it."""

import argparse
import concurrent.futures
import json
import math
import queue
import statistics
import threading
import os
import re
import shutil
import subprocess
import sys
import time

import corpus_refs

WORKLOADS = ["medley", "combinatorics", "dependency", "nested-update", "group-freq", "pipelines", "strings",
             "render", "suite-data", "async-pipeline", "async-libs", "async-broadcast", "datascript", "json"]
LOAD_PATH = ["bench/workloads/src", "corpus/medley/src", "corpus/math-combinatorics/src", "corpus/dependency/src",
             "corpus/parallel-async/src", "corpus/turbine/src", "corpus/datascript/src", "corpus/data-json/src"]
# Ours only (not corpus_refs.PORTS, the other-language references): the ports here would shadow the jars on the JVM's
# classpath (datascript's persistent-sorted-set is Java there) and bb's built-in clojure.data.json.
OURS_ONLY = ["corpus/datascript/src", "corpus/data-json/src"]
DATASCRIPT_DEPS = 'datascript/datascript {:mvn/version "1.8.1"} persistent-sorted-set/persistent-sorted-set {:mvn/version "0.3.2"}'
DATA_JSON_DEPS = 'org.clojure/data.json {:mvn/version "2.5.2"}'
FEATURES = "clj"
# The Clojure the fuzzer and make api-diff pin, and the core.async make api-diff diffs ours against.
JVM_DEPS = ('{:paths [' + " ".join('"%s"' % p for p in LOAD_PATH if p not in OURS_ONLY) + '] :deps{org.clojure/clojure '
            '{:mvn/version "1.12.6"} org.clojure/core.async {:mvn/version "1.6.681"} ' + DATASCRIPT_DEPS + ' ' +
            DATA_JSON_DEPS + '}}')
# A run past this is a hang: sampled into the logs, killed, and an error of the report. The slowest workload,
# datascript, takes about 30 s interpreted.
RUN_TIMEOUT = 240
SAMPLE_SECONDS = 8
# Past this the remaining workloads are reported as not run; the CI step's own cap is above it.
TOTAL_BUDGET = 150 * 60
# At most this many repetitions of a workload whose run takes seconds, unless --reps is given: datascript's 8 pairs
# would cost the default --base job about 25 min more than 2 do (bench/RESULTS.md, "Datascript").
REPS_CAP = {"datascript": 2}
# (minutes outside the repetitions, minutes per repetition of both sides), arm64 against a base, run 38919426472.
WORKLOAD_MINUTES = {
	"medley": (3.4, 0.33), "combinatorics": (3.0, 0.45), "dependency": (4.3, 0.46), "nested-update": (2.8, 0.36),
	"group-freq": (2.9, 0.33), "pipelines": (3.2, 0.36), "strings": (2.4, 0.21), "render": (2.4, 0.30),
	"suite-data": (2.4, 0.29), "async-pipeline": (2.9, 0.32), "async-libs": (2.0, 0.33), "async-broadcast": (2.1, 0.19),
	"datascript": (6.0, 1.65), "json": (2.6, 0.49)}
# Workload minutes per shard: with about 10 min of builds before them and the merge, a run stays near 30 min.
SHARD_BUDGET = 17
# The plan's concurrent macOS jobs: a shard past them would wait for a runner (docs/notes/benchmarks.md).
MAX_SHARDS = 5
# The timed modes, in the order each repetition runs them.
TIMED = ["interp", "dev-core", "dev", "closed"]
TIMED_LABEL = {"interp": "interp", "dev-core": "core+interp", "dev": "dev", "closed": "closed"}
HARNESS = "Sources/clj-corpus-bench/main.swift"

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def log(msg):
	print("corpus-bench: " + msg, flush=True)


def swift_env(sdkroot):
	# /usr/bin/python3 is an xcrun shim that sets SDKROOT; a build under another one rebuilds everything.
	env = dict(os.environ)
	if sdkroot is not None:
		env.pop("SDKROOT", None)
		if sdkroot:
			env["SDKROOT"] = sdkroot
	return env


def machine():
	brand = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], stdout=subprocess.PIPE, text=True).stdout.strip()
	ncpu = subprocess.run(["sysctl", "-n", "hw.ncpu"], stdout=subprocess.PIPE, text=True).stdout.strip()
	return "%s, %s cpus, %s" % (brand, ncpu, os.uname().machine)


def sh(cmd, env=None, timeout=None, cwd=ROOT, check=True):
	t0 = time.monotonic()
	p = subprocess.run(cmd, cwd=cwd, env=env, timeout=timeout, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
	if check and p.returncode != 0:
		raise RuntimeError("%s exited %d\n%s\n%s" % (" ".join(cmd), p.returncode, p.stdout[-4000:], p.stderr[-4000:]))
	return p, (time.monotonic() - t0) * 1000


def parse(text):
	"""The report lines of clj-corpus-bench and jvm.clj: `key value`, `stat name n`, `alloc_type name n`,
	`census type field n`, `census_site type fn@line:col born frame callee`, `reuse_type name taken copied`,
	`token_type name used`."""
	r = {"stat": {}, "alloc_type": {}, "census": {}, "census_site": [], "reuse_type": {}, "token_type": {}}
	for line in text.splitlines():
		parts = line.split(" ", 1)
		if len(parts) != 2:
			continue
		key, rest = parts
		if key == "census":
			cells = rest.split(" ")
			if len(cells) == 3:
				# Two types may share a name (a deftype's and a core one): summed.
				row = r["census"].setdefault(cells[0], {})
				row[cells[1]] = row.get(cells[1], 0.0) + float(cells[2])
		elif key == "census_site":
			cells = rest.split(" ")
			if len(cells) == 5:
				r["census_site"].append((cells[0], cells[1], float(cells[2]), float(cells[3]), float(cells[4])))
		elif key in ("stat", "alloc_type", "token_type"):
			name, value = rest.rsplit(" ", 1)
			r[key][name] = float(value)
		elif key == "reuse_type":
			name, taken, copied = rest.rsplit(" ", 2)
			r[key][name] = (float(taken), float(copied))
		elif key in ("result", "expected"):
			r[key] = rest
		elif key in ("load_ms", "first_ms", "median_ms", "min_ms", "stats_ms", "clang_ms", "rc_op_ns",
		             "rc_op_ns_cold", "rc_op_ns_stats"):
			r[key] = float(rest)
		elif key in ("iterations", "load_failures", "warmup_iterations", "held_iterations"):
			r[key] = int(rest)
	return r


class Side:
	"""A source tree under test with its own builds: this checkout (head) or the base worktree. Its result keys and
	build directories carry its prefix; every side runs the head's workload sources, from ROOT."""

	def __init__(self, name, root, prefix):
		self.name, self.root, self.prefix = name, root, prefix
		self.bin = {}
		self.compile = None
		self.rev = None
		self.env = {}  # over the bench's: what its compiles and runs see


class Bench:
	def __init__(self, args):
		self.args = args
		self.out = os.path.join(ROOT, args.out)
		self.logs = os.path.join(self.out, "logs")
		self.env = swift_env(args.sdkroot)
		self.lp = [a for p in LOAD_PATH for a in ("--load-path", p)]
		self.results = {w: {} for w in args.only}
		self.wall = {}
		self.machine = machine()
		self.errors = []
		# A workload that hung once is not run again: each further run would hang the same and cost RUN_TIMEOUT.
		self.hung = set()
		self.head = Side("head", ROOT, "")
		self.base = None
		self.sides = [self.head]
		self.refs = corpus_refs.Refs(self, args.refs, ROOT, [p for p in LOAD_PATH if p not in OURS_ONLY], parse, log)
		os.makedirs(self.logs, exist_ok=True)

	def lp_of(self, side):
		"""The load path a side runs: the head's sources, or with --base-sources the base's own."""
		if side is self.head or not self.args.base_sources:
			return self.lp
		return [a for p in LOAD_PATH for a in ("--load-path", os.path.join(side.root, p))]

	def keep(self, name, text):
		with open(os.path.join(self.logs, name), "w") as f:
			f.write(text)

	# ---- builds

	def build(self, scratch, flags, package=None):
		cmd = ["swift", "build", "-c", "release", "--scratch-path", os.path.join(self.out, scratch),
		       "--product", "clj-corpus-bench"]
		if package:
			cmd += ["--package-path", package]
		for f in flags:
			cmd += ["-Xcc", f]
		log("build %s %s" % (scratch, " ".join(flags)))
		_, ms = sh(cmd, env=self.env, timeout=3600)
		log("build %s took %.0f s" % (scratch, ms / 1000))
		return os.path.join(self.out, scratch, "release", "clj-corpus-bench")

	def tools(self):
		if self.args.base:
			self.base = self.base_tree()
			self.sides = [self.base, self.head]
		self.head.rev = sh(["git", "rev-parse", "HEAD"])[0].stdout.strip()
		# Nothing is timed yet, and one build leaves cores idle while it compiles boot/core.c alone.
		with concurrent.futures.ThreadPoolExecutor(len(self.sides)) as pool:
			for f in [pool.submit(self.side_tools, side) for side in self.sides]:
				f.result()
		if self.args.interp_ab:
			self.head.bin["interp-stats"] = self.build("interp-stats", ["-DCLJ_STATS=1"])

	def side_tools(self, side):
		package = None if side is self.head else side.root
		plain = os.path.join(ROOT, ".build/plain") if side is self.head else os.path.join(self.out, "base-plain")
		cmd = ["swift", "build", "--scratch-path", plain, "--product", "clj-compile"]
		sh(cmd + (["--package-path", package] if package else []), env=self.env, timeout=3600)
		side.compile = os.path.join(plain, "debug/clj-compile")
		try:
			side.bin["interp"] = self.build(side.prefix + "interp", [], package=package)
		except RuntimeError as e:
			if side is self.head:
				raise
			log("this harness does not build against the base; the base runs its own: %s" % str(e)[-400:])
			sh(["git", "checkout", "--", HARNESS], cwd=side.root)
			side.bin["interp"] = self.build(side.prefix + "interp", [], package=package)
		side.bin["dev"] = self.build(side.prefix + "dev", ["-DCLJ_COMPILED_CORE"], package=package)
		side.bin["dev-stats"] = self.build(side.prefix + "dev-stats", ["-DCLJ_COMPILED_CORE", "-DCLJ_STATS=1"],
		                                   package=package)

	def base_tree(self):
		root = os.path.join(self.out, "base-src")
		p, _ = sh(["sh", "scripts/base-tree.sh", self.args.base, root])
		side = Side("base", root, "base-")
		side.rev = p.stdout.strip()
		# This tree's harness on both sides, so both print what this script parses.
		shutil.copy(os.path.join(ROOT, HARNESS), os.path.join(root, HARNESS))
		if self.args.base_no_opt:
			# a kill switch's share: the same tree with those optimizations off in its compiles and runs
			side.env["CLJ_NO_OPT"] = self.args.base_no_opt
		log("base %s = %s, in %s%s" % (self.args.base, side.rev, root, " with CLJ_NO_OPT=" + self.args.base_no_opt if self.args.base_no_opt else ""))
		return side

	def drop_base_tree(self):
		if self.base:
			sh(["git", "worktree", "remove", "--force", self.base.root], check=False)

	# ---- runs

	def bounded(self, cmd, env, name):
		"""(completed process, ms), or (None, ms) for a run past RUN_TIMEOUT, whose threads are sampled first."""
		t0 = time.monotonic()
		proc = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
		try:
			out, err = proc.communicate(timeout=RUN_TIMEOUT)
		except subprocess.TimeoutExpired:
			subprocess.run(["/usr/bin/sample", str(proc.pid), "2", "-mayDie", "-file",
			                os.path.join(self.logs, name + ".hang.txt")], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
			proc.kill()
			out, err = proc.communicate()
			self.keep(name + ".txt", out + "\n--- stderr\n" + err)
			return None, (time.monotonic() - t0) * 1000
		return subprocess.CompletedProcess(cmd, proc.returncode, out, err), (time.monotonic() - t0) * 1000

	def ours(self, tag, workload, binary, extra=(), env=None, side=None):
		cmd = [binary] + self.lp_of(side or self.head) + ["--features", FEATURES, "--min-iterations", "3", "--min-seconds", "2"] + list(extra) + \
		      ["workloads." + workload]
		e = dict(self.env, **(side or self.head).env)
		# The units are compiled against this root's headers, which must be the binary's own.
		e["CLJ_EVAL_ROOT"] = (side or self.head).root
		if env:
			e.update(env)
		p, ms = self.bounded(cmd, e, "%s.%s" % (workload, tag))
		if p is None:
			self.hung.add(workload)
			self.errors.append("%s %s: hung past %d s; its stacks are logs/%s.%s.hang.txt" % (workload, tag, RUN_TIMEOUT, workload, tag))
			return None
		self.keep("%s.%s.txt" % (workload, tag), p.stdout + "\n--- stderr\n" + p.stderr)
		if p.returncode != 0:
			self.errors.append("%s %s: exit %d: %s" % (workload, tag, p.returncode, p.stderr.strip()[-600:]))
			return None
		r = parse(p.stdout)
		r["process_ms"] = ms
		log("%s %s: %s" % (workload, tag, {k: v for k, v in r.items() if k in ("median_ms", "first_ms", "stats_ms")}))
		return r

	def jvm(self, workload, mode=None):
		cmd = ["clojure", "-Sdeps", JVM_DEPS, "-M", "bench/workloads/jvm.clj", "workloads." + workload]
		if mode:
			cmd.append(mode)
		p, ms = self.bounded(cmd, None, "%s.jvm%s" % (workload, "-" + mode if mode else ""))
		if p is None:
			self.errors.append("%s jvm %s: hung past %d s" % (workload, mode or "", RUN_TIMEOUT))
			return None
		self.keep("%s.jvm%s.txt" % (workload, "-" + mode if mode else ""), p.stdout + "\n--- stderr\n" + p.stderr)
		if p.returncode != 0:
			self.errors.append("%s jvm %s: exit %d: %s" % (workload, mode or "", p.returncode, p.stderr.strip()[-600:]))
			return None
		r = parse(p.stdout)
		r["process_ms"] = ms
		return r

	def dev_units(self, workload, side):
		d = os.path.join(self.out, "units", workload, side.prefix + "dev")
		shutil.rmtree(d, ignore_errors=True)
		os.makedirs(d)
		p, _ = sh([side.compile, "--lenient", "--allow-refused"] + self.lp_of(side) + ["--features", FEATURES, "--ns",
		          "workloads." + workload, "--out", d], env=dict(self.env, **side.env), timeout=RUN_TIMEOUT, check=False)
		self.keep("%s.%scompile-dev.txt" % (workload, side.prefix), p.stderr)
		if p.returncode not in (0, 2):
			self.errors.append("%s: %s clj-compile (dev) exited %d: %s" % (workload, side.name, p.returncode, p.stderr[-600:]))
			return None
		return d

	def closed_tree(self, workload, side):
		"""clj-compile --core --closed of the workload, written over a copy of the sources (scripts/shake.sh)."""
		boot = os.path.join(self.out, "units", workload, side.prefix + "closed")
		shutil.rmtree(boot, ignore_errors=True)
		os.makedirs(boot)
		p, ms = sh([side.compile, "--core", "--closed", "--stats", "--lenient", "--allow-refused"] + self.lp_of(side) +
		           ["--features", FEATURES, "--ns", "workloads." + workload, "--out", boot],
		           env=dict(self.env, **side.env), timeout=RUN_TIMEOUT, check=False)
		self.keep("%s.%scompile-closed.txt" % (workload, side.prefix), p.stderr)
		if p.returncode not in (0, 2):
			self.errors.append("%s: %s clj-compile --closed exited %d: %s" % (workload, side.name, p.returncode, p.stderr[-600:]))
			return None
		self.results[workload][side.prefix + "compile_stats"] = p.stderr
		tree = os.path.join(self.out, side.prefix + "tree")
		os.makedirs(tree, exist_ok=True)
		sh(["rsync", "-a", "--delete", "--exclude", ".build", "--exclude", ".git", "Sources", "Tests", "Package.swift",
		    "Package.resolved", tree + "/"], cwd=side.root)
		bootdir = os.path.join(tree, "Sources/CljCore/boot")
		for f in os.listdir(bootdir):
			if f.endswith(".c"):
				os.remove(os.path.join(bootdir, f))
		for f in os.listdir(boot):
			if f.endswith(".c"):
				shutil.copy(os.path.join(boot, f), bootdir)
		return tree

	def sample(self, tag, workload, binary, extra=(), env=None):
		cmd = [binary] + self.lp + ["--features", FEATURES] + list(extra) + ["--hold", str(SAMPLE_SECONDS + 4),
		                                                                    "workloads." + workload]
		e = dict(self.env)
		e["CLJ_EVAL_ROOT"] = ROOT
		if env:
			e.update(env)
		path = os.path.join(self.logs, "%s.%s.sample.txt" % (workload, tag))
		proc = subprocess.Popen(cmd, cwd=ROOT, env=e, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
		# The first iteration may hang (docs/notes/channels.md): the wait for "ready" is bounded like any run.
		lines = queue.Queue()
		threading.Thread(target=lambda: [lines.put(l) for l in proc.stdout] and None, daemon=True).start()
		pid, deadline = None, time.monotonic() + RUN_TIMEOUT
		while pid is None and time.monotonic() < deadline and proc.poll() is None:
			try:
				line = lines.get(timeout=1)
			except queue.Empty:
				continue
			if line.startswith("ready "):
				pid = line.split()[1]
		if pid is None:
			subprocess.run(["/usr/bin/sample", str(proc.pid), "2", "-mayDie", "-file",
			                os.path.join(self.logs, "%s.%s.hang.txt" % (workload, tag))],
			               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
			proc.kill()
			proc.wait()
			self.hung.add(workload)
			self.errors.append("%s %s sample: no first iteration within %d s" % (workload, tag, RUN_TIMEOUT))
			return None
		s = subprocess.run(["/usr/bin/sample", pid, str(SAMPLE_SECONDS), "-mayDie", "-file", path],
		                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=RUN_TIMEOUT)
		try:
			proc.communicate(timeout=RUN_TIMEOUT)
		except subprocess.TimeoutExpired:
			proc.kill()
			proc.communicate()
		if s.returncode != 0 or not os.path.exists(path):
			self.errors.append("%s %s sample: /usr/bin/sample exited %d: %s" % (workload, tag, s.returncode, s.stderr[-400:]))
			return None
		with open(path) as f:
			return f.read()

	# ---- the per-workload sequence

	def run_workload(self, w):
		res = self.results[w]
		try:
			for _ in self.steps(w, res):
				if w in self.hung:
					log("%s hung; its remaining runs are skipped" % w)
					return
		except subprocess.TimeoutExpired as e:
			self.hung.add(w)
			self.errors.append("%s: %s ran past %s s" % (w, " ".join(e.cmd[:2]), e.timeout))

	def steps(self, w, res):
		"""The runs of one workload, a yield after each so a hang ends the workload."""
		if "jvm" in self.refs.refs:
			res["jvm"] = self.jvm(w)
			yield
			cold = self.jvm(w, "once")
			yield
			if cold:
				res["jvm_cold_process_ms"] = cold["process_ms"]
				res["jvm_cold_first_ms"] = cold.get("first_ms")
		yield from self.refs.steps(w, res)
		if self.args.interp_ab:
			hb = self.head.bin
			off = {self.args.interp_ab: "1"}
			res["interp-off"] = self.ours("interp-off", w, hb["interp"], env=off)
			yield
			res["interp-stats"] = self.ours("interp-stats", w, hb["interp-stats"], ["--stats"])
			yield
			res["interp-stats-off"] = self.ours("interp-stats-off", w, hb["interp-stats"], ["--stats"], env=off)
			yield
		# What each side times: (binary, extra args) per mode; a mode whose build failed is absent.
		timed, units, stats = {}, {}, {}

		def prepare(side):
			t = timed[side.name] = {"interp": (side.bin["interp"], []), "dev-core": (side.bin["dev"], [])}
			units[side.name] = u = self.dev_units(w, side)
			if u:
				t["dev"] = (side.bin["dev"], ["--units", u, "--dylib-dir", u + "-dylib"])
			if self.args.skip_closed:
				return
			tree = self.closed_tree(w, side)
			if not tree:
				return
			try:
				t["closed"] = (self.build(side.prefix + "closed", ["-DCLJ_COMPILED_CORE", "-DCLJ_CLOSED"], package=tree), [])
			except RuntimeError as e:
				self.errors.append("%s: the %s closed build failed: %s" % (w, side.name, str(e)[-800:]))
				return
			try:
				stats[side.name] = self.build(side.prefix + "closed-stats",
				                              ["-DCLJ_COMPILED_CORE", "-DCLJ_CLOSED", "-DCLJ_STATS=1"], package=tree)
			except RuntimeError as e:
				self.errors.append("%s: the %s closed stats build failed: %s" % (w, side.name, str(e)[-800:]))

		# Untimed, so the sides prepare at once, the closed stats build too, before any run.
		with concurrent.futures.ThreadPoolExecutor(len(self.sides)) as pool:
			for f in [pool.submit(prepare, side) for side in self.sides]:
				f.result()
		yield
		# A pair is two adjacent runs of one mode; who goes first alternates, so drift and warm-up hit both sides.
		runs = {}
		pairs = res["pairs"] = {m: [] for m in TIMED}
		for rep in range(self.args.reps if self.args.reps_given else min(self.args.reps, REPS_CAP.get(w, self.args.reps))):
			order = self.sides if rep % 2 == 0 else self.sides[::-1]
			for mode in TIMED:
				got = {}
				for side in order:
					if mode not in timed[side.name]:
						continue
					binary, extra = timed[side.name][mode]
					tag = side.prefix + mode + (".r%d" % rep if self.args.reps > 1 else "")
					r = self.ours(tag, w, binary, extra, side=side)
					if r and r.get("median_ms") is not None:
						rs = runs.setdefault(side.prefix + mode, [])
						rs.append(r)
						# The pairs compare fastest iterations: in two A/A runs on the shared runner their ratio's
						# interval was narrower than the medians' (docs/notes/benchmarks.md).
						got[side.name] = r.get("min_ms")
						mins = [x["min_ms"] for x in rs if x.get("min_ms") is not None]
						# A side's number is the median over repetitions of each run's median.
						res[side.prefix + mode] = dict(
							rs[0], median_ms=statistics.median(x["median_ms"] for x in rs), runs=len(rs),
							min_ms=statistics.median(mins) if mins else None,
							expected=next((x.get("expected") for x in rs if x.get("expected") != "same"), rs[0].get("expected")))
					yield
				if got.get("base") and got.get("head"):
					pairs[mode].append((got["base"], got["head"]))
		for side in self.sides:
			u = units[side.name]
			if u:
				res[side.prefix + "dev-stats"] = self.ours(side.prefix + "dev-stats", w, side.bin["dev-stats"],
				                                           ["--units", u, "--dylib-dir", u + "-dylib-stats", "--stats",
				                                            "--census"], side=side)
				yield
		u = units["head"]
		if u and not self.args.skip_sample:
			res["dev-sample"] = self.sample("dev", w, self.head.bin["dev"], ["--units", u, "--dylib-dir", u + "-dylib"])
			yield
			res["dev-symbols"] = self.symbols("dev")
		closed = timed["head"].get("closed")
		if closed and not self.args.skip_sample:
			res["closed-sample"] = self.sample("closed", w, closed[0])
			yield
			res["closed-symbols"] = self.symbols("closed")
		if closed and "rc_op_ns" not in self.__dict__:
			cal = subprocess.run([closed[0], "--calibrate"], stdout=subprocess.PIPE, text=True, env=self.env, timeout=RUN_TIMEOUT)
			c = parse(cal.stdout)
			self.rc_op_ns, self.rc_op_ns_cold = c.get("rc_op_ns"), c.get("rc_op_ns_cold")
		for side in self.sides:
			if side.name not in stats:
				continue
			res[side.prefix + "closed-stats"] = self.ours(side.prefix + "closed-stats", w, stats[side.name],
			                                              ["--stats", "--census"], side=side)
			yield

	def symbols(self, scratch):
		"""Symbol -> the source file it was compiled from, over the object files of a build (nm)."""
		if scratch == "dev" and getattr(self, "dev_symbols", None):
			return self.dev_symbols
		base = os.path.join(self.out, scratch, "release")
		table = {}
		for target in ("CljCore.build", "CljCompiler.build"):
			top = os.path.join(base, target)
			for dirpath, _, files in os.walk(top):
				for f in files:
					if not f.endswith(".o"):
						continue
					path = os.path.join(dirpath, f)
					p = subprocess.run(["nm", "-U", "-j", path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
					src = os.path.relpath(path, top)[:-2]
					for name in p.stdout.split():
						table.setdefault(name[1:] if name.startswith("_") else name, src)
		if scratch == "dev":
			self.dev_symbols = table
		return table

	def facts(self):
		sh(["swift", "build", "--scratch-path", os.path.join(ROOT, ".build/release"), "-c", "release", "--product",
		    "clj-facts"], env=self.env, timeout=3600)
		report = os.path.join(self.out, "facts-coverage.md")
		# corpus/datascript and corpus/data-json have no manifest of their own (no test suite to run): their roots for
		# this report are apart.
		p, _ = sh([os.path.join(ROOT, ".build/release/release/clj-facts"), ".", report,
		           os.path.join(self.out, "facts-cost.md"), "bench/workloads/datascript", "bench/workloads/data-json",
		           "bench/workloads"],
		          env=self.env, timeout=3600, check=False)
		self.keep("facts.txt", p.stdout + p.stderr)
		if p.returncode != 0:
			self.errors.append("clj-facts exited %d (its gate: a ⊥ or a declaration conflict): %s" % (p.returncode, p.stderr[-600:]))
		return report if os.path.exists(report) else None

	def xctrace_probe(self):
		try:
			# About 20 s when it works; on one runner of run 38952649569 it hung to the end of a 300 s timeout.
			p = subprocess.run(["xcrun", "xctrace", "record", "--template", "Time Profiler", "--time-limit", "2s",
			                    "--output", os.path.join(self.out, "probe.trace"), "--launch", "--", "/bin/sleep", "1"],
			                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
		except (OSError, subprocess.TimeoutExpired) as e:
			return "did not run: %s" % e
		shutil.rmtree(os.path.join(self.out, "probe.trace"), ignore_errors=True)
		return "exit %d: %s" % (p.returncode, " ".join(p.stdout.split())[-300:])

	def dump(self, facts, xctrace):
		"""<out>/results.json: what --merge writes the report of all shards from."""
		results = {}
		for w in self.args.only:
			res = results[w] = dict(self.results[w])
			for tag in ("dev", "closed"):
				rows = top_of_stack(res.get(tag + "-sample") or "")
				if res.get(tag + "-symbols") is not None:
					# The table names every symbol of the build; the profile looks up the sampled ones.
					names = {sym for sym, _, _ in rows or []}
					res[tag + "-symbols"] = {k: v for k, v in res[tag + "-symbols"].items() if k in names}
			res["_rc_op_ns"], res["_rc_op_ns_cold"] = getattr(self, "rc_op_ns", None), getattr(self, "rc_op_ns_cold", None)
		doc = {"args": vars(self.args), "head": self.head.rev, "base": self.base.rev if self.base else None,
		       "machine": self.machine, "durations": self.durations, "xctrace": xctrace, "errors": self.errors,
		       "facts": bool(facts), "wall": self.wall, "results": results,
		       "refs": {"refs": self.refs.refs, "versions": self.refs.versions, "cost": self.refs.cost,
		                "na": [[ref, w, why] for (ref, w), why in self.refs.na.items()]}}
		with open(os.path.join(self.out, "results.json"), "w") as f:
			json.dump(doc, f)


# ---- the sampled profile

IDLE = re.compile(r"^(__psynch_cvwait|__semwait_signal|mach_msg2?_trap|__workq_kernreturn|kevent.*|__ulock_wait2?|"
                  r"__psynch_mutexwait|__select|__wait4|poll|__sigsuspend|semaphore_.*wait.*|__psynch_rw_.*|swtch_pri)$")

FILE_BUCKETS = [
	("rc", ["rc.c"]),
	("cycle collector", ["cc.c"]),
	("alloc/free", ["alloc.c"]),
	("generic dispatch", ["fn.c", "proto.c", "record.c"]),
	("seqs and laziness", ["seq.c", "cons.c", "list.c", "coll.c", "reduce.c", "fusion.c"]),
	("hash/equality", ["core.c", "jvm_hash.c", "compare.c"]),
	("collections", ["map.c", "vector.c", "set.c", "shape.c", "sorted.c", "array.c", "queue.c", "box.c"]),
	("strings/printing/regex", ["string.c", "builtins_string.c", "regex.c", "printer.c", "builtins_format.c"]),
	("numbers", ["number.c", "long.c", "bigint.c", "ratio.c", "decimal.c", "builtins_number.c"]),
	("coroutines/channels", ["coro.c", "sched.c", "chan.c", "cmutex.c", "atom.c"]),
	("C builtins", ["builtins.c", "builtins_array.c", "builtins_ns.c", "intrinsics.c"]),
	("vars/ns/symbols", ["var.c", "ns.c", "symbol.c", "keyword.c"]),
	("compiled-code support", ["compiled.c"]),
	("interpreter", ["eval.c", "analyzer.c", "optimizer.c", "specialize.c", "facts.c", "summary.c", "epoch.c",
	                 "callers.c", "load.c", "node_data.c", "reader.c"]),
]
FILE_TO_BUCKET = {f: b for b, files in FILE_BUCKETS for f in files}


def bucket(symbol, image, symbols):
	if IDLE.match(symbol):
		return "idle (a blocked thread)"
	if symbol in ("clj_double_new", "clj_long_box"):
		return "boxing"
	if re.search(r"(_finalize|_free|_dealloc|_destroy|release_children|dealloc_\w+)$", symbol):
		return "alloc/free"
	if re.search(r"(_hash|_equals|_equiv|hasheq)$", symbol):
		return "hash/equality"
	if "libsystem_malloc" in image:
		return "alloc/free"
	if image.endswith(".dylib") and re.match(r"u\d+_", image):
		return "compiled program"
	src = symbols.get(symbol)
	if src:
		if src.startswith("boot/unit_"):
			return "compiled program"
		if src.startswith("boot/"):
			return "compiled core.clj and libs"
		b = FILE_TO_BUCKET.get(os.path.basename(src))
		if b:
			return b
		return "runtime: " + os.path.basename(src)
	if "libsystem_kernel" in image:
		return "kernel"
	if "libsystem_platform" in image:
		return "memmove/memset"
	return "other: " + image


TOP = re.compile(r"^\s+(.+?)\s+\(in (.+?)\)\s+(\d+)\s*$")


def top_of_stack(text):
	"""(symbol, image, samples) of the 'Sort by top of stack' section of sample's report, None without one."""
	section = text.split("Sort by top of stack", 1)
	if len(section) < 2:
		return None
	rows = []
	for line in section[1].split("Binary Images", 1)[0].splitlines()[1:]:
		m = TOP.match(line)
		if m:
			rows.append((re.sub(r"\s+\+\s+\d+$", "", m.group(1)), m.group(2), int(m.group(3))))
	return rows


def profile(text, symbols):
	"""Self samples per bucket and per function, from the 'Sort by top of stack' section of sample's report."""
	rows = top_of_stack(text)
	if rows is None:
		return None
	buckets, funcs, idle, total = {}, {}, 0, 0
	for sym, image, n in rows:
		b = bucket(sym, image, symbols)
		if b.startswith("idle"):
			idle += n
			continue
		total += n
		buckets[b] = buckets.get(b, 0) + n
		funcs[(sym, b)] = funcs.get((sym, b), 0) + n
	return {"buckets": buckets, "funcs": funcs, "busy": total, "idle": idle}


# ---- the report

def fmt(v, digits=1):
	if v is None:
		return "—"
	if isinstance(v, float):
		return ("%." + str(digits) + "f") % v
	return str(v)


def median_of(r):
	return r.get("median_ms") if r else None


KINDS = ["frame", "callee", "up1", "up2", "up3", "escaped", "other_exec", "outside"]


def census_rows(census):
	"""Per type and for all types: born, deaths by kind, alive at the end, and the attribute counts over all kinds."""
	rows = {}
	total = {}
	for name, f in census.items():
		if name == "_meta":
			continue
		row = {"born": f.get("born", 0.0), "born_built": f.get("born_built", 0.0)}
		for kind in KINDS:
			row[kind] = f.get(kind, 0.0)
			for attr in ("retained", "shared", "built", "reuse1", "reuse4"):
				row[attr] = row.get(attr, 0.0) + f.get(kind + "." + attr, 0.0)
				row[kind + "." + attr] = f.get(kind + "." + attr, 0.0)
		row["alive"] = row["born"] - sum(row[kind] for kind in KINDS)
		rows[name] = row
		for key, v in row.items():
			total[key] = total.get(key, 0.0) + v
	return rows, total


def interp_ab_section(b, add):
	def k(v):
		if v is None:
			return "—"
		return "%.2fM" % (v / 1e6) if v >= 1e6 else "%.1fk" % (v / 1e3) if v >= 1e3 else "%.0f" % v

	var = b.args.interp_ab
	add("## Interpreter A/B (`%s=1` is *off*; ms the interp build's median, counters `-DCLJ_STATS=1` per iteration)" % var)
	add("")
	add("| workload | ms on | ms off | allocs on | allocs off | MB on | MB off | invoke on | invoke off | lazy forced | fn on | fn off | lazy-seq on | lazy-seq off |")
	add("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
	for w in b.args.only:
		r = b.results[w]
		on, off = r.get("interp-stats") or {}, r.get("interp-stats-off") or {}
		so, sf = on.get("stat") or {}, off.get("stat") or {}
		ao, af = on.get("alloc_type") or {}, off.get("alloc_type") or {}
		add("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
			w, fmt(median_of(r.get("interp"))), fmt(median_of(r.get("interp-off"))), k(so.get("alloc")), k(sf.get("alloc")),
			fmt(so.get("alloc_bytes", 0) / 1e6, 1) if so else "—", fmt(sf.get("alloc_bytes", 0) / 1e6, 1) if sf else "—",
			k(so.get("invoke")), k(sf.get("invoke")), k(so.get("lazy_force")), k(ao.get("fn")), k(af.get("fn")),
			k(ao.get("lazy-seq")), k(af.get("lazy-seq"))))
	add("")


def kfmt(v):
	if v is None:
		return "—"
	if abs(v) >= 1e6:
		return "%.2fM" % (v / 1e6)
	if abs(v) >= 1e3:
		return "%.1fk" % (v / 1e3)
	return "%.0f" % v


AB_COUNTERS = [("allocs", "alloc"), ("MB", "alloc_bytes"), ("frees", "free"), ("rc plain", "rc_plain"),
               ("rc shared", "rc_shared"), ("lazy forced", "lazy_force"), ("invoke", "invoke"), ("c_invoke", "c_invoke"),
               ("apply", "apply"), ("reuse in place", "reuse_taken"), ("reuse copied", "reuse_copied"),
               ("token used", "token_used"), ("cc candidates", "cc_candidates"), ("cc collections", "cc_collections"),
               ("cc visited", "cc_visited"), ("cc unfiled", "cc_unfiled")]


def median_interval(sorted_values):
	"""(lo, hi, coverage): the sign test's distribution-free interval for the median, the narrowest pair of order
	statistics covering it with probability >= 90 %, or the extremes when no pair reaches that."""
	n = len(sorted_values)
	k, below = 1, 1.0 / 2 ** n
	while k < n // 2 and 2 * (below + math.comb(n, k) / 2 ** n) <= 0.10:
		below += math.comb(n, k) / 2 ** n
		k += 1
	return sorted_values[k - 1], sorted_values[n - k], 1 - 2 * below


def ab_section(b, add):
	base, head = b.base, b.head
	add("## Same runner: head %s against base %s %s%s" % (head.rev[:10], b.args.base, base.rev[:10],
	    " with CLJ_NO_OPT=" + b.args.base_no_opt if b.args.base_no_opt else ""))
	add("")
	full = median_interval(list(range(b.args.reps)))[2] if b.args.reps > 1 else None
	add("Both trees built and run on one runner, %s; %d repetitions, each running every "
	    "mode on both sides back to back, the base first in even repetitions and the head first in odd ones. A cell "
	    "is the median over the pairs of head / base, each side's number the run's fastest iteration, and in brackets "
	    "the sign test's interval for that median (%s coverage with every pair present; the pair count follows when "
	    "one is missing). `*` marks an interval without 1.000: a change, or the %s chance a cell of an A/A run takes. "
	    "*geomean* is over the workloads with every pair present." % (
		    "each side its own workload sources and ports (--base-sources)" if b.args.base_sources
		    else "the head's workload sources on both", b.args.reps, "%.0f %%" % (100 * full) if full else "no", "%.0f %%" % (100 * (1 - full)) if full else "full"))
	add("")
	add("| workload | " + " | ".join(TIMED_LABEL[m] for m in TIMED) + " |")
	add("|---|" + "---:|" * len(TIMED))
	logs = {m: [] for m in TIMED}
	for w in b.args.only:
		r = b.results[w]
		cells = []
		for mode in TIMED:
			ratios = sorted(h / bs for bs, h in r.get("pairs", {}).get(mode, []))
			if not ratios:
				cells.append("—")
				continue
			med = statistics.median(ratios)
			lo, hi, _ = median_interval(ratios)
			if len(ratios) == b.args.reps:
				logs[mode].append(math.log(med))
			cells.append("%.3f [%.3f–%.3f]%s%s" % (med, lo, hi, "*" if lo > 1 or hi < 1 else "",
			                                      "" if len(ratios) == b.args.reps else " (%d)" % len(ratios)))
		add("| %s | %s |" % (w, " | ".join(cells)))
	add("| *geomean* | %s |" % " | ".join("%.3f" % math.exp(sum(v) / len(v)) if v else "—" for v in logs.values()))
	add("")
	add("ms, base → head: the median over the repetitions of each run's fastest iteration (the pairs' number), then of "
	    "each run's median iteration (the main table's):")
	add("")
	add("| workload | " + " | ".join(TIMED_LABEL[m] for m in TIMED) + " |")
	add("|---|" + "---:|" * len(TIMED))
	for w in b.args.only:
		r = b.results[w]
		add("| %s | %s |" % (w, " | ".join("%s → %s; %s → %s" % (
			fmt((r.get("base-" + m) or {}).get("min_ms")), fmt((r.get(m) or {}).get("min_ms")),
			fmt(median_of(r.get("base-" + m))), fmt(median_of(r.get(m)))) for m in TIMED)))
	add("")
	for kind in ("closed-stats", "dev-stats"):
		add("Counters per iteration, %s build (`-DCLJ_STATS=1`): the head's value, its change against the base in "
		    "parentheses, `=` for none:" % kind.split("-")[0])
		add("")
		add("| workload | " + " | ".join(name for name, _ in AB_COUNTERS) + " |")
		add("|---|" + "---:|" * len(AB_COUNTERS))
		for w in b.args.only:
			hs, bs = b.results[w].get(kind), b.results[w].get("base-" + kind)
			if not hs or not bs:
				add("| %s | — |" % w)
				continue
			cells = []
			for _, key in AB_COUNTERS:
				h, o = hs["stat"].get(key), bs["stat"].get(key)
				if key == "alloc_bytes":
					h, o = (h / 1e6 if h is not None else None), (o / 1e6 if o is not None else None)
				if h is None or o is None:
					cells.append(kfmt(h))
				elif h == o:
					cells.append("%s (=)" % (fmt(h, 1) if key == "alloc_bytes" else kfmt(h)))
				else:
					delta = "%+.1f %%" % (100.0 * (h - o) / o) if o else "from 0"
					cells.append("%s (%s)" % (fmt(h, 1) if key == "alloc_bytes" else kfmt(h), delta))
			add("| %s | %s |" % (w, " | ".join(cells)))
		add("")


def census_section(b, add):
	add("## Allocation census (`--census`, closed stats build; dev where closed did not run)")
	add("")
	add("Every object allocated in the two stats iterations, by where it died against the Clojure fn frame it was born "
	    "in (interpreted `run_body` and compiled bodies push a census frame; C builtins are not frames, so their "
	    "allocations belong to the calling fn). *frame*: died in its birth frame; *callee*: died deeper while the birth "
	    "frame still ran; *up1/up2/up3+*: the birth frame returned and the closest frame that held it all along is 1, 2, "
	    "3+ calls above; *escaped*: out of the outermost fn (back to the host); *other exec*: died in another coroutine "
	    "or thread; *outside*: born outside any fn; *alive*: not dead when the iterations ended. *reuse≤1/≤4*: the "
	    "death was followed, in the frame it happened in, by an allocation of its size class as the next / within the "
	    "next 4 allocations of that execution (a Perceus reuse pair). *retained*: its count passed 1 at least once. "
	    "*built*: born under `into`, a transient op, `frequencies`, `group-by`, `zipmap`, `mapv`, `filterv`, "
	    "`update-vals`/`-keys`. Percentages of the type's born; types are the top 6 by count.")
	add("")
	add("| workload | type | born/iter | frame | callee | up1 | up2 | up3+ | escaped | other exec | outside | alive | reuse≤1 | reuse≤4 | retained | built |")
	add("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")

	def pct(row, key):
		return "%.1f" % (100.0 * row.get(key, 0.0) / row["born"]) if row.get("born") else "—"

	def k(v):
		return "%.2fM" % (v / 1e6) if v >= 1e6 else "%.1fk" % (v / 1e3) if v >= 1e3 else "%.0f" % v

	shares = []
	for w in b.args.only:
		r = b.results[w]
		s = r.get("closed-stats") or r.get("dev-stats")
		census = (s or {}).get("census")
		if not census:
			add("| %s | — |" % w)
			continue
		rows, total = census_rows(census)
		top = sorted(rows.items(), key=lambda kv: -kv[1]["born"])[:6]
		for name, row in [("**all**", total)] + top:
			cells = [pct(row, key) for key in KINDS + ["alive", "reuse1", "reuse4", "retained", "born_built"]]
			add("| %s | %s | %s | %s |" % (w, name, k(row["born"]), " | ".join(cells)))
		shares.append((w, total, census.get("_meta", {}).get("stale", 0.0)))
	add("")
	add("What each mechanism could address, % of all allocations (overlapping: a reuse pair or a linear object is "
	    "also in one lifetime class). *frame region*: frame + callee; *interprocedural region*: up1–up3+ (up1 alone in "
	    "parentheses); *drop-reuse*: reuse≤4 (≤1 in parentheses); *linear*: died never retained; *escaping*: escaped + "
	    "other exec + alive; *built*: born under a builder.")
	add("")
	add("| workload | born/iter | frame region | interprocedural (up1) | drop-reuse (≤1) | linear | escaping | outside | built | stale records |")
	add("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
	for w, t, stale in shares:
		born = t["born"] or 1.0
		died = sum(t[kind] for kind in KINDS)

		def p(v):
			return "%.1f" % (100.0 * v / born)
		add("| %s | %s | %s | %s (%s) | %s (%s) | %s | %s | %s | %s | %.0f |" % (
			w, k(t["born"]), p(t["frame"] + t["callee"]), p(t["up1"] + t["up2"] + t["up3"]), p(t["up1"]),
			p(t["reuse4"]), p(t["reuse1"]), p(died - t["retained"]), p(t["escaped"] + t["other_exec"] + t["alive"]),
			p(t["outside"]), p(t["born_built"]), stale))
	add("")
	add("Birth sites of the frame region: per workload, the (fn, type) pairs whose objects died in their birth frame "
	    "or a callee, the top 8 by that count. *site* is the birth frame's fn as `name@line:col` (`fn` when "
	    "anonymous); *share* is of all the workload's allocations.")
	add("")
	add("| workload | type | site | born/iter | frame | callee | share |")
	add("|---|---|---|---:|---:|---:|---:|")
	for w in b.args.only:
		r = b.results[w]
		s = r.get("closed-stats") or r.get("dev-stats") or {}
		census = s.get("census")
		if not census:
			continue
		_, total = census_rows(census)
		every = total.get("born") or 1.0
		for tname, site, born, frame, callee in sorted(s.get("census_site", []), key=lambda e: -(e[3] + e[4]))[:8]:
			add("| %s | %s | `%s` | %s | %s | %s | %.1f |" % (w, tname, site, k(born), k(frame), k(callee), 100.0 * (frame + callee) / every))
	add("")
	add("Inline RC, measured: `clj_debug_rc_op_ns` %s ns per op on a cached header, %s ns over 1M headers in a shuffled "
	    "order (the closed shipping build, `--calibrate`); the counts are the rc columns above." % (
		fmt(getattr(b, "rc_op_ns", None), 2), fmt(getattr(b, "rc_op_ns_cold", None), 2)))
	add("")


def write_report(b, facts_path, xctrace):
	lines = []
	add = lines.append
	against = " against base %s %s" % (b.args.base, b.base.rev[:10]) if b.base else ""
	add("# corpus-bench — %s%s, %s" % (b.head.rev[:10], against, b.machine))
	add("")
	if getattr(b, "shards", None) and len(b.shards) > 1:
		add("**%d shards, one runner each**: every shard built and timed its own workloads%s, so each ratio, interval "
		    "and `*` is one runner's, as in a single job. The absolute ms of two workloads in different shards come from "
		    "different runners, which spread ±20–30 %% from each other: compare them loosely. Shards: %s." % (
			    len(b.shards), ", head and base side by side" if b.base else "",
			    "; ".join("%d %s (%s)" % (i, ", ".join(ws), m) for i, ws, m, _ in b.shards)))
		add("")
	add("ms per iteration of `run`, median after a warm-up (ours: 1 iteration, then >= 3 and >= 2 s; the JVM: >= 5 "
	    "iterations and 5 s, then >= 5 and >= 3 s). *JVM cold* is a fresh `clojure -M` process running `run` once, "
	    "start to exit; *1st* is that first iteration in-process. *core+interp* is the compiled core with the workload "
	    "interpreted (dev mode before compiling the program); *dev* the workload compiled by clj-compile at -O2 over the "
	    "compiled core; *closed* the whole program `--closed` (tree-shaken, direct calls). Ours are the head's, over "
	    "%d repetition(s): the median of the runs' medians.%s" % (
		    b.args.reps, " *same result* covers the base's runs too." if b.base else ""))
	add("")
	add("| workload | JVM warm | JVM 1st | JVM cold | interp | core+interp | dev | closed | closed/JVM | interp/closed | same result |")
	add("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|")
	for w in b.args.only:
		r = b.results[w]
		jvm = r.get("jvm")
		closed = median_of(r.get("closed"))
		jw = median_of(jvm)
		# `=` against the workload's expected value, in each runtime; the stats runs check none.
		verdicts = {k: r[k].get("expected") for k in ("jvm", "interp", "interp-off", "dev-core", "dev", "closed", "base-interp",
		                                              "base-dev-core", "base-dev", "base-closed") if r.get(k)}
		same = "—" if not verdicts else ("yes" if all(v == "same" for v in verdicts.values()) else
		                                 "NO: " + ", ".join("%s %s" % (k, v) for k, v in verdicts.items() if v != "same"))
		add("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
			w, fmt(jw, 2), fmt(r.get("jvm_cold_first_ms"), 0), fmt(r.get("jvm_cold_process_ms"), 0),
			fmt(median_of(r.get("interp"))), fmt(median_of(r.get("dev-core"))), fmt(median_of(r.get("dev"))), fmt(closed),
			fmt(closed / jw if closed and jw else None, 1) + "×" if closed and jw else "—",
			fmt(median_of(r.get("interp")) / closed if closed and r.get("interp") else None, 1) + "×" if closed and r.get("interp") else "—",
			same))
	add("")
	add("Load (ms, in-process `require`, after boot): " + ", ".join(
		"%s %s/%s/%s" % (w, fmt((b.results[w].get("interp") or {}).get("load_ms"), 0),
		                 fmt((b.results[w].get("dev") or {}).get("load_ms"), 0),
		                 fmt((b.results[w].get("closed") or {}).get("load_ms"), 0)) for w in b.args.only) +
	    " (interp/dev/closed). Forms lost to JVM interop in a lenient load: " + ", ".join(
		"%s %s" % (w, (b.results[w].get("interp") or {}).get("load_failures", "—")) for w in b.args.only) + ".")
	add("")
	corpus_refs.section(b, b.refs, add, fmt, median_of)
	if b.base:
		ab_section(b, add)

	add("## Runtime counters, per iteration (`-DCLJ_STATS=1`, closed; dev in parentheses where it differs)")
	add("")
	add("RC ops are every `clj_retain`/`clj_release` that reached a heap object (plain = unshared, the inline path; "
	    "shared = atomic; immortal = skipped). *rc est.* is plain ops × %s ns (`clj_debug_rc_op_ns`, an inline op on a "
	    "cached header, measured on the workload's runner) over the closed median: a floor, since a miss on the header costs more; "
	    "*cold* the same at %s ns, a miss on every header (`clj_debug_rc_op_ns_cold`, 1M headers shuffled). "
	    "Calls: `clj_invoke` (generic), compiled generic sites (`clj_c_invoke`), `apply`, protocol methods called as "
	    "values and compiled inline-cache misses. Allocation types are the top ones by count." % (
		fmt(getattr(b, "rc_op_ns", None), 2), fmt(getattr(b, "rc_op_ns_cold", None), 2)))
	add("")
	add("| workload | stats ms | rc plain | rc shared | rc immortal | rc est. | rc est. cold | allocs | MB | frees | doubles | longs | lazy forced | invoke | c_invoke | apply | proto generic | proto miss | hash | equals | coro switches |")
	add("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")

	def k(v):
		if v is None:
			return "—"
		if v >= 1e6:
			return "%.2fM" % (v / 1e6)
		if v >= 1e3:
			return "%.1fk" % (v / 1e3)
		return "%.0f" % v

	for w in b.args.only:
		r = b.results[w]
		s = r.get("closed-stats") or r.get("dev-stats")
		if not s:
			add("| %s | — |" % w)
			continue
		st, at = s["stat"], s["alloc_type"]
		closed = median_of(r.get("closed"))
		est = cold = None
		# A merged report's workloads each carry the calibration of the runner that timed them.
		op_ns = r["_rc_op_ns"] if "_rc_op_ns" in r else getattr(b, "rc_op_ns", None)
		op_ns_cold = r["_rc_op_ns_cold"] if "_rc_op_ns_cold" in r else getattr(b, "rc_op_ns_cold", None)
		if closed and op_ns:
			est = 100.0 * st.get("rc_plain", 0) * op_ns / (closed * 1e6)
		if closed and op_ns_cold:
			cold = 100.0 * st.get("rc_plain", 0) * op_ns_cold / (closed * 1e6)
		add("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
			w, fmt(s.get("stats_ms"), 0), k(st.get("rc_plain")), k(st.get("rc_shared")), k(st.get("rc_immortal")),
			fmt(est, 0) + " %" if est is not None else "—", fmt(cold, 0) + " %" if cold is not None else "—", k(st.get("alloc")), fmt(st.get("alloc_bytes", 0) / 1e6, 1),
			k(st.get("free")), k(at.get("double")), k(at.get("long")), k(st.get("lazy_force")), k(st.get("invoke")),
			k(st.get("c_invoke")), k(st.get("apply")), k(st.get("proto_generic")), k(st.get("proto_miss")),
			k(st.get("hash")), k(st.get("equals")), k(st.get("coro_switches"))))
	add("")
	add("Allocations by type (closed, per iteration, top 8):")
	add("")
	for w in b.args.only:
		s = b.results[w].get("closed-stats") or b.results[w].get("dev-stats")
		if s:
			top = sorted(s["alloc_type"].items(), key=lambda kv: -kv[1])[:8]
			add("- **%s**: %s" % (w, ", ".join("%s %s" % (n, k(c)) for n, c in top)))
	add("")
	add("Reuse at rc 1 (closed, per iteration; `clj_is_unique` asked of a heap object, in place / copied, top 6 types):")
	add("")
	for w in b.args.only:
		s = b.results[w].get("closed-stats") or b.results[w].get("dev-stats")
		if s:
			st = s["stat"]
			top = sorted(s.get("reuse_type", {}).items(), key=lambda kv: -(kv[1][0] + kv[1][1]))[:6]
			add("- **%s**: %s in place / %s copied; %s" % (w, k(st.get("reuse_taken")), k(st.get("reuse_copied")),
			    ", ".join("%s %s/%s" % (n, k(t), k(c)) for n, (t, c) in top) or "none"))
	add("")
	add("Reuse tokens of compiled code (per iteration, dev / closed; made: a dying value offered its cell, taken: unique "
	    "and unshared, used: a new object built in it, skipped: fields kept in place; top 4 types built):")
	add("")
	for w in b.args.only:
		cells = []
		for kind in ("dev-stats", "closed-stats"):
			s = b.results[w].get(kind)
			if s:
				st = s["stat"]
				top = sorted(s.get("token_type", {}).items(), key=lambda kv: -kv[1])[:4]
				cells.append("%s made, %s taken, %s used, %s skipped (%s)" % (
					k(st.get("token_made")), k(st.get("token_taken")), k(st.get("token_used")), k(st.get("token_skipped")),
					", ".join("%s %s" % (n, k(u)) for n, u in top) or "none"))
		if cells:
			add("- **%s**: %s" % (w, " / ".join(cells)))
	add("")
	add("Dev against closed, the generic-call counters (dev / closed): " + "; ".join(
		"%s invoke %s/%s c_invoke %s/%s" % (
			w, k(((b.results[w].get("dev-stats") or {}).get("stat") or {}).get("invoke")),
			k(((b.results[w].get("closed-stats") or {}).get("stat") or {}).get("invoke")),
			k(((b.results[w].get("dev-stats") or {}).get("stat") or {}).get("c_invoke")),
			k(((b.results[w].get("closed-stats") or {}).get("stat") or {}).get("c_invoke")))
		for w in b.args.only) + ".")
	add("")
	add("Store check census (closed stats build, per iteration; design §7, «Проверка цикла в момент записи»): edges "
	    "written between unshared objects, the first edge into an object (the `HEAPREF` header write), edges into a "
	    "reference type and into one with `HEAPREF`, edges into an immutable owner with `HEAPREF` (in place, late "
	    "init); a walk where the owner is a reference type with `HEAPREF` and the value has `REACH_LOCAL` (lazy: a "
	    "lazy-seq realization), the "
	    "nodes walked to the end (cap 1024), walks past 64 and the cap, walks that met the owner; stores into a shared "
	    "reference type and of a `REACH` value; the candidates filed, and the shared ones among them; the checks run "
	    "(cc.c, marks skipped, budget 256 nodes), the nodes they entered, the cycles they closed, those past the budget.")
	add("")
	add("| workload | edges | heapref set | into mutable | mutable w/ heapref | imm w/ heapref | walks | lazy | nodes | >64 | capped | cycles | shared | shared reach | cc candidates | of them shared | checks | check nodes | check cycles | deferred |")
	add("|---|" + "---:|" * 20)
	for w in b.args.only:
		s = b.results[w].get("closed-stats") or b.results[w].get("dev-stats")
		if not s:
			continue
		st = s["stat"]
		add("| %s | %s |" % (w, " | ".join(k(st.get(n)) for n in (
			"sc_edges", "sc_heapref_set", "sc_mut", "sc_mut_heapref", "sc_imm_heapref", "sc_walks", "sc_walks_lazy",
			"sc_walk_nodes", "sc_walk_long", "sc_walk_capped", "sc_cycles", "sc_shared", "sc_shared_reach",
			"cc_candidates", "cc_shared_filed", "cc_store_checks", "cc_store_nodes", "cc_store_cycles",
			"cc_store_deferred"))))
	add("")

	if b.args.interp_ab:
		interp_ab_section(b, add)

	census_section(b, add)

	for tag in ("closed", "dev"):
		add("## Sampled profile, %s (`/usr/bin/sample`, %d s at 1 ms, self time, idle threads left out)" % (tag, SAMPLE_SECONDS))
		add("")
		add("Self samples by where the code lives: runtime C by source file (an inline helper — the RC fast path, "
		    "`clj_c_invoke` — counts in its caller), compiled core.clj and the embedded libs (`boot/`), the compiled "
		    "program (the workload and the corpus library), system malloc and the kernel.")
		add("")
		names = []
		profs = {}
		for w in b.args.only:
			r = b.results[w]
			text = r.get(tag + "-sample")
			if text:
				p = profile(text, r.get(tag + "-symbols") or {})
				if p:
					profs[w] = p
					for name in p["buckets"]:
						if name not in names:
							names.append(name)
		totals = {n: sum(p["buckets"].get(n, 0) / max(p["busy"], 1) for p in profs.values()) for n in names}
		names.sort(key=lambda n: -totals[n])
		if profs:
			add("| workload | busy samples | " + " | ".join(names) + " |")
			add("|---|---:|" + "---:|" * len(names))
			for w, p in profs.items():
				add("| %s | %d | " % (w, p["busy"]) + " | ".join(
					"%.1f" % (100.0 * p["buckets"].get(n, 0) / max(p["busy"], 1)) for n in names) + " |")
			add("")
			for w, p in profs.items():
				top = sorted(p["funcs"].items(), key=lambda kv: -kv[1])[:12]
				add("- **%s** top functions: %s" % (w, ", ".join(
					"`%s` %.1f %%" % (sym, 100.0 * n / max(p["busy"], 1)) for (sym, _), n in top)))
			add("")
		else:
			add("No profile.")
			add("")

	add("## What the compiler consumed (`clj-compile --closed --stats`, the program's own units)")
	add("")
	for w in b.args.only:
		text = b.results[w].get("compile_stats") or ""
		rows = [l for l in text.splitlines() if l.startswith("slots: ") and "<embedded>" not in l and "core.clj" not in l]
		for row in rows:
			add("- %s: %s" % (w, row[len("slots: "):]))
	add("")
	if facts_path:
		add("## Facts coverage with the workloads as a library (`clj-facts . <out> <cost> bench/workloads`)")
		add("")
		with open(facts_path) as f:
			text = f.read()
		for line in text.splitlines():
			if line.startswith("| library") or line.startswith("|---") or line.startswith("| workloads ") or \
			   line.startswith("| **library code**"):
				add(line)
		add("")
	add("xctrace: " + (xctrace or "probed in another shard"))
	add("")
	if getattr(b, "shards", None):
		add("Wall time per shard: " + "; ".join("%d (%s) the builds before the workloads %.1f min, the workloads %.1f min" % (
			i, ", ".join(ws), d[0] / 60, d[1] / 60) for i, ws, _, d in b.shards) + ".")
	else:
		add("Wall time: the builds before the workloads %.1f min, the workloads %.1f min." % (
			b.durations[0] / 60, b.durations[1] / 60))
	add("")
	add("Wall time per workload: " + ", ".join("%s %.1f min" % (w, b.wall[w] / 60) for w in b.args.only
	                                           if w in b.wall) + ".")
	add("")
	if b.errors:
		add("## Errors")
		add("")
		for e in b.errors:
			add("- " + e.replace("\n", " "))
		add("")
	path = os.path.join(b.out, "report.md")
	with open(path, "w") as f:
		f.write("\n".join(lines))
	print("\n".join(lines), flush=True)




# ---- shards: each its own runner, head and base side by side on it; --merge writes their one report

def reps_of(args, w):
	return args.reps if args.reps_given else min(args.reps, REPS_CAP.get(w, args.reps))


def estimate(args, w):
	outside, per_rep = WORKLOAD_MINUTES[w]
	return outside + reps_of(args, w) * per_rep * (1 if args.base else 0.5)


def deal(args, n):
	"""n shards' workloads, each in --only's order, and their estimated minutes: the longest workload first, each
	onto the least loaded shard."""
	loads, parts = [0.0] * n, [[] for _ in range(n)]
	for w in sorted(args.only, key=lambda w: -estimate(args, w)):
		i = loads.index(min(loads))
		loads[i] += estimate(args, w)
		parts[i].append(w)
	return [[w for w in args.only if w in part] for part in parts], loads


def shard_count(args):
	if args.shards:
		return args.shards
	most = min(MAX_SHARDS, len(args.only))
	return next((n for n in range(1, most + 1) if max(deal(args, n)[1]) <= SHARD_BUDGET), most)


def shard_index(doc):
	i, n = doc["args"]["shard"].split("/") if doc["args"].get("shard") else ("1", "1")
	return int(i), int(n)


class Merged:
	"""The part of a Bench write_report reads, put together from the shards' results.json."""


def merge(dirs, out):
	docs = []
	for d in dirs:
		path = os.path.join(d, "results.json")
		if os.path.exists(path):
			with open(path) as f:
				docs.append((d, json.load(f)))
		else:
			log("%s has no results.json" % d)
	if not docs:
		return "corpus-bench: no results.json in " + ", ".join(dirs)
	docs.sort(key=lambda dd: shard_index(dd[1]))
	first = docs[0][1]
	for d, doc in docs[1:]:
		for key in ("head", "base"):
			if doc[key] != first[key]:
				return "corpus-bench: %s timed %s %s, %s %s: not one run's shards" % (d, key, doc[key], docs[0][0], first[key])
		if doc["args"]["all"] != first["args"]["all"] or shard_index(doc)[1] != shard_index(first)[1]:
			return "corpus-bench: %s and %s deal different workloads or shard counts" % (d, docs[0][0])
	b = Merged()
	b.args = argparse.Namespace(**first["args"])
	b.args.only = first["args"]["all"]
	b.out, b.logs = out, os.path.join(out, "logs")
	os.makedirs(b.logs, exist_ok=True)
	b.head = Side("head", ROOT, "")
	b.head.rev = first["head"]
	b.base = None
	if first["base"]:
		b.base = Side("base", None, "base-")
		b.base.rev = first["base"]
	b.results = {w: {} for w in b.args.only}
	b.errors, b.wall, b.shards, machines, xctrace, facts = [], {}, [], [], [], None
	b.refs = corpus_refs.Refs.__new__(corpus_refs.Refs)
	b.refs.b, b.refs.refs, b.refs.versions, b.refs.na, b.refs.cost = b, [], {}, {}, {}
	n = shard_index(first)[1]
	missing = sorted(set(range(1, n + 1)) - {shard_index(doc)[0] for _, doc in docs})
	ran = {w for _, doc in docs for w in doc["args"]["only"]}
	b.errors += ["%s: no results, shard %s of %d wrote none (its job's log says why)" % (
		w, " or ".join(map(str, missing)), n) for w in b.args.only if w not in ran]
	for d, doc in docs:
		i = shard_index(doc)[0]
		b.results.update(doc["results"])
		b.errors += doc["errors"]
		b.wall.update(doc["wall"])
		b.shards.append((i, doc["args"]["only"], doc["machine"], doc["durations"]))
		if doc["machine"] not in machines:
			machines.append(doc["machine"])
		if doc["xctrace"] and doc["xctrace"] not in xctrace:
			xctrace.append(doc["xctrace"])
		refs = doc["refs"]
		b.refs.refs = sorted(set(b.refs.refs) | set(refs["refs"]), key=corpus_refs.REFS.index)
		b.refs.versions.update(refs["versions"])
		b.refs.na.update({(ref, w): why for ref, w, why in refs["na"]})
		for k, v in refs["cost"].items():
			b.refs.cost[k] = b.refs.cost.get(k, 0.0) + v
		if doc["facts"]:
			facts = os.path.join(out, "facts-coverage.md")
			shutil.copy(os.path.join(d, "facts-coverage.md"), facts)
		src = os.path.join(d, "logs")
		for top, _, files in os.walk(src):
			for f in files:
				rel = os.path.relpath(os.path.join(top, f), src)
				dst = os.path.join(b.logs, rel)
				if os.path.exists(dst):
					dst = os.path.join(b.logs, os.path.dirname(rel), "shard%d.%s" % (i, f))
				os.makedirs(os.path.dirname(dst), exist_ok=True)
				shutil.copy(os.path.join(top, f), dst)
	b.machine = "; ".join(machines)
	for key in ("rc_op_ns", "rc_op_ns_cold"):
		got = sorted({r["_" + key] for r in b.results.values() if r.get("_" + key) is not None})
		setattr(b, key, None if not got else got[0] if len(got) == 1 else "%.2f–%.2f" % (got[0], got[-1]))
	write_report(b, facts, "; ".join(xctrace))
	return 1 if b.errors else 0


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--only", default=",".join(WORKLOADS), help="comma-separated workloads")
	ap.add_argument("--out", default=".build/corpus-bench")
	ap.add_argument("--sdkroot", help="the caller's SDKROOT, empty for none")
	ap.add_argument("--refs", default=",".join(corpus_refs.REFS),
	                help="comma-separated reference runtimes, run once per workload: " + ", ".join(corpus_refs.REFS))
	ap.add_argument("--skip-jvm", action="store_true", help="--refs without jvm")
	ap.add_argument("--skip-closed", action="store_true")
	ap.add_argument("--skip-sample", action="store_true")
	ap.add_argument("--skip-facts", action="store_true")
	ap.add_argument("--interp-ab", metavar="VAR", help="also time and count the interpreter with VAR=1 in its environment")
	ap.add_argument("--base", help="a ref built and timed beside this tree on the same runner, alternated")
	ap.add_argument("--base-sources", action="store_true",
	                help="the base runs its own workload sources and ports, not this tree's: a port's change timed")
	ap.add_argument("--base-no-opt", metavar="NAMES",
	                help="CLJ_NO_OPT for the base's compiles and runs: with --base=HEAD, the share of those optimizations")
	ap.add_argument("--reps", type=int, help="timed runs per mode and side (default 8 with --base, else 1)")
	ap.add_argument("--plan", action="store_true", help="print the deal of the workloads into shards as JSON and exit")
	ap.add_argument("--shards", type=int, help="the shard count of --plan, instead of the fewest within SHARD_BUDGET")
	ap.add_argument("--shard", metavar="I/N", help="run the I-th of N shards' workloads; --merge joins their outputs")
	ap.add_argument("--merge", nargs="+", metavar="DIR", help="one report from shards' outputs (results.json, logs)")
	args = ap.parse_args()
	if args.merge:
		sys.exit(merge(args.merge, os.path.join(ROOT, args.out)))
	args.only = [w for w in args.only.split(",") if w]
	args.refs = [r for r in args.refs.split(",") if r and not (args.skip_jvm and r == "jvm")]
	unknown = [r for r in args.refs if r not in corpus_refs.REFS]
	if unknown:
		sys.exit("corpus-bench: unknown references " + ", ".join(unknown))
	unknown = [w for w in args.only if w not in WORKLOADS]
	if unknown:
		sys.exit("corpus-bench: unknown workloads " + ", ".join(unknown))
	args.reps_given = args.reps is not None
	if args.reps is None:
		args.reps = 8 if args.base else 1
	if args.reps < 1:
		sys.exit("corpus-bench: --reps must be at least 1")
	if args.shards is not None and not 1 <= args.shards <= len(args.only):
		sys.exit("corpus-bench: --shards must be between 1 and the workload count")
	if args.plan:
		n = shard_count(args)
		parts, loads = deal(args, n)
		print(json.dumps({"shards": n, "deal": parts, "minutes": [round(m, 1) for m in loads]}))
		return
	args.all = args.only
	probe = True
	if args.shard:
		m = re.fullmatch(r"(\d+)/(\d+)", args.shard)
		if not m or not 1 <= int(m.group(1)) <= int(m.group(2)) <= len(args.only):
			sys.exit("corpus-bench: --shard is I/N with 1 <= I <= N <= the workload count")
		i, n = int(m.group(1)), int(m.group(2))
		parts, loads = deal(args, n)
		args.only = parts[i - 1]
		# clj-facts and the xctrace probe answer the same on every runner: the least loaded shard runs them for all.
		probe = i - 1 == loads.index(min(loads))
		args.skip_facts = args.skip_facts or not probe
		log("shard %d/%d: %s" % (i, n, ", ".join(args.only)))
	b = Bench(args)
	t0 = time.monotonic()
	try:
		b.tools()
		if "jvm" in args.refs:
			# The classpath is resolved once, outside every timed process.
			sh(["clojure", "-Sdeps", JVM_DEPS, "-M", "-e", "nil"], timeout=1800)
		b.refs.prepare(args.only)
		xctrace = b.xctrace_probe() if probe else None
		start = time.monotonic()
		for w in args.only:
			if time.monotonic() - start > TOTAL_BUDGET:
				b.errors.append("%s: not run, the target's %d min budget was spent" % (w, TOTAL_BUDGET // 60))
				continue
			log("=== " + w)
			t = time.monotonic()
			b.run_workload(w)
			b.wall[w] = time.monotonic() - t
		b.durations = (start - t0, time.monotonic() - start)
		facts = None if args.skip_facts else b.facts()
	finally:
		b.drop_base_tree()
	b.dump(facts, xctrace)
	write_report(b, facts, xctrace)
	# A missing number is a failed run: the report says which, and the target fails.
	if b.errors:
		sys.exit(1)


if __name__ == "__main__":
	main()
