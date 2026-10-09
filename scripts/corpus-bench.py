#!/usr/bin/env python3
# @ai-generated(solo)
"""make corpus-bench: the workloads of bench/workloads on JVM Clojure and on every backend here, with where the
compiled time goes (docs/notes/benchmarks.md, "Corpus workloads"). Writes <out>/report.md and prints it."""

import argparse
import queue
import threading
import os
import re
import shutil
import subprocess
import sys
import time

WORKLOADS = ["medley", "combinatorics", "dependency", "nested-update", "group-freq", "pipelines", "strings",
             "render", "suite-data", "async-pipeline", "async-libs", "async-broadcast"]
LOAD_PATH = ["bench/workloads/src", "corpus/medley/src", "corpus/math-combinatorics/src", "corpus/dependency/src",
             "corpus/parallel-async/src", "corpus/turbine/src"]
FEATURES = "clj"
# The Clojure the fuzzer and make api-diff pin, and the core.async make api-diff diffs ours against.
JVM_DEPS = ('{:paths [' + " ".join('"%s"' % p for p in LOAD_PATH) + '] :deps {org.clojure/clojure {:mvn/version '
            '"1.12.6"} org.clojure/core.async {:mvn/version "1.6.681"}}}')
# A run past this is a hang: sampled into the logs, killed, and an error of the report. The slowest workload takes
# about 20 s interpreted.
RUN_TIMEOUT = 240
SAMPLE_SECONDS = 8
# Past this the remaining workloads are reported as not run; the CI step's own cap is above it.
TOTAL_BUDGET = 150 * 60

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


def sh(cmd, env=None, timeout=None, cwd=ROOT, check=True):
	t0 = time.monotonic()
	p = subprocess.run(cmd, cwd=cwd, env=env, timeout=timeout, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
	if check and p.returncode != 0:
		raise RuntimeError("%s exited %d\n%s\n%s" % (" ".join(cmd), p.returncode, p.stdout[-4000:], p.stderr[-4000:]))
	return p, (time.monotonic() - t0) * 1000


def parse(text):
	"""The report lines of clj-corpus-bench and jvm.clj: `key value`, `stat name n`, `alloc_type name n`,
	`census type field n`."""
	r = {"stat": {}, "alloc_type": {}, "census": {}}
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
		elif key in ("stat", "alloc_type"):
			name, value = rest.rsplit(" ", 1)
			r[key][name] = float(value)
		elif key in ("result", "expected"):
			r[key] = rest
		elif key in ("load_ms", "first_ms", "median_ms", "min_ms", "stats_ms", "clang_ms", "rc_op_ns",
		             "rc_op_ns_cold", "rc_op_ns_stats"):
			r[key] = float(rest)
		elif key in ("iterations", "load_failures", "warmup_iterations", "held_iterations"):
			r[key] = int(rest)
	return r


class Bench:
	def __init__(self, args):
		self.args = args
		self.out = os.path.join(ROOT, args.out)
		self.logs = os.path.join(self.out, "logs")
		self.env = swift_env(args.sdkroot)
		self.lp = [a for p in LOAD_PATH for a in ("--load-path", p)]
		self.results = {w: {} for w in args.only}
		self.errors = []
		# A workload that hung once is not run again: each further run would hang the same and cost RUN_TIMEOUT.
		self.hung = set()
		os.makedirs(self.logs, exist_ok=True)

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
		sh(["swift", "build", "--scratch-path", os.path.join(ROOT, ".build/plain"), "--product", "clj-compile"],
		   env=self.env, timeout=3600)
		self.compile = os.path.join(ROOT, ".build/plain/debug/clj-compile")
		self.bin = {
			"interp": self.build("interp", []),
			"dev": self.build("dev", ["-DCLJ_COMPILED_CORE"]),
			"dev-stats": self.build("dev-stats", ["-DCLJ_COMPILED_CORE", "-DCLJ_STATS=1"]),
		}

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

	def ours(self, tag, workload, binary, extra=(), env=None):
		cmd = [binary] + self.lp + ["--features", FEATURES, "--min-iterations", "3", "--min-seconds", "2"] + list(extra) + \
		      ["workloads." + workload]
		e = dict(self.env)
		e["CLJ_EVAL_ROOT"] = ROOT
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

	def dev_units(self, workload):
		d = os.path.join(self.out, "units", workload, "dev")
		shutil.rmtree(d, ignore_errors=True)
		os.makedirs(d)
		p, _ = sh([self.compile, "--lenient", "--allow-refused"] + self.lp + ["--features", FEATURES, "--ns",
		          "workloads." + workload, "--out", d], env=self.env, timeout=RUN_TIMEOUT, check=False)
		self.keep("%s.compile-dev.txt" % workload, p.stderr)
		if p.returncode not in (0, 2):
			self.errors.append("%s: clj-compile (dev) exited %d: %s" % (workload, p.returncode, p.stderr[-600:]))
			return None
		return d

	def closed_tree(self, workload):
		"""clj-compile --core --closed of the workload, written over a copy of the sources (scripts/shake.sh)."""
		boot = os.path.join(self.out, "units", workload, "closed")
		shutil.rmtree(boot, ignore_errors=True)
		os.makedirs(boot)
		p, ms = sh([self.compile, "--core", "--closed", "--stats", "--lenient", "--allow-refused"] + self.lp +
		           ["--features", FEATURES, "--ns", "workloads." + workload, "--out", boot],
		           env=self.env, timeout=RUN_TIMEOUT, check=False)
		self.keep("%s.compile-closed.txt" % workload, p.stderr)
		if p.returncode not in (0, 2):
			self.errors.append("%s: clj-compile --closed exited %d: %s" % (workload, p.returncode, p.stderr[-600:]))
			return None
		self.results[workload]["compile_stats"] = p.stderr
		tree = os.path.join(self.out, "tree")
		os.makedirs(tree, exist_ok=True)
		sh(["rsync", "-a", "--delete", "--exclude", ".build", "--exclude", ".git", "Sources", "Tests", "Package.swift",
		    "Package.resolved", tree + "/"])
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
		if not self.args.skip_jvm:
			res["jvm"] = self.jvm(w)
			yield
			cold = self.jvm(w, "once")
			yield
			if cold:
				res["jvm_cold_process_ms"] = cold["process_ms"]
				res["jvm_cold_first_ms"] = cold.get("first_ms")
		res["interp"] = self.ours("interp", w, self.bin["interp"])
		yield
		res["dev-core"] = self.ours("dev-core", w, self.bin["dev"])
		yield
		units = self.dev_units(w)
		if units:
			res["dev"] = self.ours("dev", w, self.bin["dev"], ["--units", units, "--dylib-dir", units + "-dylib"])
			yield
			res["dev-stats"] = self.ours("dev-stats", w, self.bin["dev-stats"],
			                             ["--units", units, "--dylib-dir", units + "-dylib-stats", "--stats", "--census"])
			yield
			if not self.args.skip_sample:
				res["dev-sample"] = self.sample("dev", w, self.bin["dev"], ["--units", units, "--dylib-dir", units + "-dylib"])
				yield
				res["dev-symbols"] = self.symbols("dev")
		if self.args.skip_closed:
			return
		tree = self.closed_tree(w)
		if not tree:
			return
		try:
			closed = self.build("closed", ["-DCLJ_COMPILED_CORE", "-DCLJ_CLOSED"], package=tree)
		except RuntimeError as e:
			self.errors.append("%s: the closed build failed: %s" % (w, str(e)[-800:]))
			return
		res["closed"] = self.ours("closed", w, closed)
		yield
		if not self.args.skip_sample:
			res["closed-sample"] = self.sample("closed", w, closed)
			yield
			res["closed-symbols"] = self.symbols("closed")
		if "rc_op_ns" not in self.__dict__:
			cal = subprocess.run([closed, "--calibrate"], stdout=subprocess.PIPE, text=True, env=self.env, timeout=RUN_TIMEOUT)
			c = parse(cal.stdout)
			self.rc_op_ns, self.rc_op_ns_cold = c.get("rc_op_ns"), c.get("rc_op_ns_cold")
		try:
			stats = self.build("closed-stats", ["-DCLJ_COMPILED_CORE", "-DCLJ_CLOSED", "-DCLJ_STATS=1"], package=tree)
		except RuntimeError as e:
			self.errors.append("%s: the closed stats build failed: %s" % (w, str(e)[-800:]))
			return
		res["closed-stats"] = self.ours("closed-stats", w, stats, ["--stats", "--census"])
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
		p, _ = sh([os.path.join(ROOT, ".build/release/release/clj-facts"), ".", report,
		           os.path.join(self.out, "facts-cost.md"), "bench/workloads"], env=self.env, timeout=3600, check=False)
		self.keep("facts.txt", p.stdout + p.stderr)
		if p.returncode != 0:
			self.errors.append("clj-facts exited %d (its gate: a ⊥ or a declaration conflict): %s" % (p.returncode, p.stderr[-600:]))
		return report if os.path.exists(report) else None

	def xctrace_probe(self):
		try:
			p = subprocess.run(["xcrun", "xctrace", "record", "--template", "Time Profiler", "--time-limit", "2s",
			                    "--output", os.path.join(self.out, "probe.trace"), "--launch", "--", "/bin/sleep", "1"],
			                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=300)
		except (OSError, subprocess.TimeoutExpired) as e:
			return "did not run: %s" % e
		shutil.rmtree(os.path.join(self.out, "probe.trace"), ignore_errors=True)
		return "exit %d: %s" % (p.returncode, " ".join(p.stdout.split())[-300:])


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


def profile(text, symbols):
	"""Self samples per bucket and per function, from the 'Sort by top of stack' section of sample's report."""
	section = text.split("Sort by top of stack", 1)
	if len(section) < 2:
		return None
	body = section[1].split("Binary Images", 1)[0]
	buckets, funcs, idle, total = {}, {}, 0, 0
	for line in body.splitlines()[1:]:
		m = TOP.match(line)
		if not m:
			continue
		sym, image, n = m.group(1), m.group(2), int(m.group(3))
		sym = re.sub(r"\s+\+\s+\d+$", "", sym)
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
	add("Inline RC, measured: `clj_debug_rc_op_ns` %s ns per op on a cached header, %s ns over 1M headers in a shuffled "
	    "order (the closed shipping build, `--calibrate`); the counts are the rc columns above." % (
		fmt(getattr(b, "rc_op_ns", None), 2), fmt(getattr(b, "rc_op_ns_cold", None), 2)))
	add("")


def write_report(b, facts_path, xctrace):
	lines = []
	add = lines.append
	machine = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], stdout=subprocess.PIPE, text=True).stdout.strip()
	ncpu = subprocess.run(["sysctl", "-n", "hw.ncpu"], stdout=subprocess.PIPE, text=True).stdout.strip()
	head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, stdout=subprocess.PIPE, text=True).stdout.strip()
	add("# corpus-bench — %s, %s, %s cpus, %s" % (head, machine, ncpu, os.uname().machine))
	add("")
	add("ms per iteration of `run`, median after a warm-up (ours: 1 iteration, then >= 3 and >= 2 s; the JVM: >= 5 "
	    "iterations and 5 s, then >= 5 and >= 3 s). *JVM cold* is a fresh `clojure -M` process running `run` once, "
	    "start to exit; *1st* is that first iteration in-process. *core+interp* is the compiled core with the workload "
	    "interpreted (dev mode before compiling the program); *dev* the workload compiled by clj-compile at -O2 over the "
	    "compiled core; *closed* the whole program `--closed` (tree-shaken, direct calls).")
	add("")
	add("| workload | JVM warm | JVM 1st | JVM cold | interp | core+interp | dev | closed | closed/JVM | interp/closed | same result |")
	add("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|")
	for w in b.args.only:
		r = b.results[w]
		jvm = r.get("jvm")
		closed = median_of(r.get("closed"))
		jw = median_of(jvm)
		# `=` against the workload's expected value, in each runtime; the stats runs check none.
		verdicts = {k: r[k].get("expected") for k in ("jvm", "interp", "dev-core", "dev", "closed") if r.get(k)}
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

	add("## Runtime counters, per iteration (`-DCLJ_STATS=1`, closed; dev in parentheses where it differs)")
	add("")
	add("RC ops are every `clj_retain`/`clj_release` that reached a heap object (plain = unshared, the inline path; "
	    "shared = atomic; immortal = skipped). *rc est.* is plain ops × %s ns (`clj_debug_rc_op_ns`, an inline op on a "
	    "cached header, measured on this runner) over the closed median: a floor, since a miss on the header costs more; "
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
		if closed and getattr(b, "rc_op_ns", None):
			est = 100.0 * st.get("rc_plain", 0) * b.rc_op_ns / (closed * 1e6)
		if closed and getattr(b, "rc_op_ns_cold", None):
			cold = 100.0 * st.get("rc_plain", 0) * b.rc_op_ns_cold / (closed * 1e6)
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
	add("Dev against closed, the generic-call counters (dev / closed): " + "; ".join(
		"%s invoke %s/%s c_invoke %s/%s" % (
			w, k(((b.results[w].get("dev-stats") or {}).get("stat") or {}).get("invoke")),
			k(((b.results[w].get("closed-stats") or {}).get("stat") or {}).get("invoke")),
			k(((b.results[w].get("dev-stats") or {}).get("stat") or {}).get("c_invoke")),
			k(((b.results[w].get("closed-stats") or {}).get("stat") or {}).get("c_invoke")))
		for w in b.args.only) + ".")
	add("")

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
	add("xctrace: " + xctrace)
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


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--only", default=",".join(WORKLOADS), help="comma-separated workloads")
	ap.add_argument("--out", default=".build/corpus-bench")
	ap.add_argument("--sdkroot", help="the caller's SDKROOT, empty for none")
	ap.add_argument("--skip-jvm", action="store_true")
	ap.add_argument("--skip-closed", action="store_true")
	ap.add_argument("--skip-sample", action="store_true")
	ap.add_argument("--skip-facts", action="store_true")
	args = ap.parse_args()
	args.only = [w for w in args.only.split(",") if w]
	unknown = [w for w in args.only if w not in WORKLOADS]
	if unknown:
		sys.exit("corpus-bench: unknown workloads " + ", ".join(unknown))
	b = Bench(args)
	b.tools()
	if not args.skip_jvm:
		# The classpath is resolved once, outside every timed process.
		sh(["clojure", "-Sdeps", JVM_DEPS, "-M", "-e", "nil"], timeout=1800)
	xctrace = b.xctrace_probe()
	start = time.monotonic()
	for w in args.only:
		if time.monotonic() - start > TOTAL_BUDGET:
			b.errors.append("%s: not run, the target's %d min budget was spent" % (w, TOTAL_BUDGET // 60))
			continue
		log("=== " + w)
		b.run_workload(w)
	facts = None if args.skip_facts else b.facts()
	write_report(b, facts, xctrace)
	# A missing number is a failed run: the report says which, and the target fails.
	if b.errors:
		sys.exit(1)


if __name__ == "__main__":
	main()
