#!/usr/bin/env python3
# @ai-generated(solo)
"""Runs the swift-testing suite as several `swift test` processes over disjoint sets of suites.

  test-shards.py run --gate NAME [--shards N] [--timeout S] -- SWIFT_ARGS...
  test-shards.py record MEASURED.json...

The live-object counters the suite compares are per process, so processes do not disturb each other's
baselines; inside a process the suite stays serialized (docs/notes/gates.md, "Shards"). `run` builds once,
lists every test, deals the suites out by their recorded times, runs the shards side by side, and fails
unless every listed test ran exactly once. `record` folds a run's measured times into scripts/test-times.json.
"""
import argparse
import collections
import json
import math
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TIMES = os.path.join(ROOT, "scripts/test-times.json")
# What the shards may take of physical memory together; the rest is the OS, the runner and swift-test itself.
MEMORY_SHARE = 0.7
# A test hung past CLJ_TEST_HANG_S ends its process itself; this bounds a shard stuck outside a test.
KILL_GRACE_S = 5


def log(msg):
	print(f"shards: {msg}", flush=True)


def arch():
	return platform.machine()


def physical_memory():
	return os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")


def resident_by_group():
	"""Resident bytes per process group: a shard is swift-test, its test process and whatever that spawns."""
	try:
		out = subprocess.run(["ps", "-A", "-o", "pgid=,rss="], capture_output=True, text=True, timeout=10).stdout
	except (OSError, subprocess.SubprocessError):
		return {}
	groups = collections.Counter()
	for line in out.splitlines():
		fields = line.split()
		if len(fields) == 2 and fields[0].isdigit() and fields[1].isdigit():
			groups[int(fields[0])] += int(fields[1]) * 1024
	return groups


def scratch_path(swift_args):
	for i, a in enumerate(swift_args):
		if a == "--scratch-path" and i + 1 < len(swift_args):
			return swift_args[i + 1]
		if a.startswith("--scratch-path="):
			return a.split("=", 1)[1]
	sys.exit("shards: SWIFT_ARGS must name --scratch-path: the shards share one build")


# Process groups still running, killed with the runner.
GROUPS = set()


def bounded(cmd, timeout, **kw):
	"""Runs cmd in its own process group, killed with the group after timeout seconds."""
	p = subprocess.Popen(cmd, start_new_session=True, **kw)
	GROUPS.add(p.pid)
	try:
		out, _ = p.communicate(timeout=timeout)
	except subprocess.TimeoutExpired:
		kill_group(p.pid, signal.SIGKILL)
		p.communicate()
		sys.exit(f"shards: {' '.join(cmd[:3])} still running after {timeout} s")
	finally:
		GROUPS.discard(p.pid)
	return p.returncode, out


def kill_group(pid, sig=signal.SIGTERM):
	try:
		os.killpg(pid, sig)
	except (ProcessLookupError, PermissionError):
		pass


def stop(signum, _frame):
	for pid in list(GROUPS):
		kill_group(pid, signal.SIGKILL)
	sys.exit(128 + signum)


def swift_env(sdkroot):
	"""The environment swift runs in. /usr/bin/python3 is an xcrun shim that sets SDKROOT when it is unset, and
	a build under another SDKROOT than the Makefile's other swift calls see rebuilds everything."""
	env = dict(os.environ)
	if sdkroot is not None:
		env.pop("SDKROOT", None)
		if sdkroot:
			env["SDKROOT"] = sdkroot
	return env


# ---- the test list and its units

LISTED = re.compile(r"^[A-Za-z_]\w*\.\S+$")


def list_tests(swift_args, timeout, env):
	rc, out = bounded(["swift", "test", "list", "--skip-build", "--disable-xctest"] + swift_args, timeout, env=env,
		stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
	if rc != 0:
		sys.stdout.write(out)
		sys.exit(f"shards: swift test list failed (exit {rc})")
	ids = [line.strip() for line in out.splitlines() if LISTED.match(line.strip())]
	if not ids:
		sys.stdout.write(out)
		sys.exit("shards: swift test list printed no tests")
	return ids


def split_id(test_id):
	"""'Module.A/B/f()' -> ('Module', ['A', 'B', 'f()'])."""
	module, rest = test_id.split(".", 1)
	return module, rest.split("/")


def unit_of(test_id):
	"""The suite a test is dealt out with: its own suite, or the one nested directly under a top-level suite.

	A suite is the smallest piece a shard takes: its tests may share state in order, and `make test-isolated`
	proves each one alone in a process."""
	_, path = split_id(test_id)
	suites = path[:-1] or path
	return "/".join(suites[:2])


def filter_for(test_id):
	# swift-testing matches a function by its ID with the source location appended ("f()/File.swift:7:4").
	return "^" + re.escape(test_id) + "(/|$)"


# ---- recorded times and the plan

def load_times():
	try:
		with open(TIMES) as f:
			return json.load(f)
	except FileNotFoundError:
		return {}


def recorded(times, gate):
	"""The closest recorded entry: this gate on this architecture, on the other, then `test`'s."""
	for g in (gate, "test"):
		by_arch = times.get(g, {})
		if arch() in by_arch:
			return by_arch[arch()]
		if by_arch:
			return next(iter(by_arch.values()))
	return {}


def deal(units, seconds, n):
	"""Longest first onto the least loaded shard. Deterministic for one input."""
	order = sorted(units, key=lambda u: (-seconds[u], u))
	shards = [[] for _ in range(n)]
	load = [0.0] * n
	for u in order:
		i = min(range(n), key=lambda k: (load[k], k))
		shards[i].append(u)
		load[i] += seconds[u]
	return [(s, l) for s, l in zip(shards, load) if s]


def shard_count(entry, seconds, requested, isolated):
	if requested:
		return requested, f"{requested} requested"
	cores = os.cpu_count() or 1
	total, longest = sum(seconds.values()), max(seconds.values())
	# Past total/longest shards the longest suite alone sets the wall time; more only adds boots and memory.
	useful = len(seconds) if isolated else max(1, math.ceil(total / longest))
	peak = entry.get("peak_rss_mb")
	by_memory = max(1, int(physical_memory() * MEMORY_SHARE / (peak * 2**20))) if peak else cores
	n = max(1, min(cores, by_memory, useful))
	why = (f"cores {cores}, memory {physical_memory() / 2**30:.0f} GB / "
		f"{'%d MB peak per shard' % peak if peak else 'no recorded peak'} -> {by_memory}, "
		f"suites {total:.0f} s / longest {longest:.0f} s -> {useful}")
	return n, why


# ---- running

class Shard:
	def __init__(self, index, units, planned, out, timeout):
		self.index, self.units, self.planned, self.timeout = index, units, planned, timeout
		self.log = os.path.join(out, f"shard-{index}.log")
		self.events = os.path.join(out, f"shard-{index}.events.jsonl")
		self.eval_dir = os.path.join(out, f"shard-{index}.eval")
		self.proc = None
		self.started = self.ended = None
		self.status = None
		self.rss = 0
		self.timed_out = False
		self.term_sent = None

	@property
	def name(self):
		return f"shard {self.index}"

	def start(self, swift_args, tests_by_unit, env):
		filters = []
		for u in self.units:
			for t in tests_by_unit[u]:
				filters += ["--filter", filter_for(t)]
		env = dict(env)
		# jit.c names its units form1, form2, ... per process: shards writing one directory load each other's.
		base = os.environ.get("CLJ_EVAL_DIR")
		env["CLJ_EVAL_DIR"] = os.path.join(base, f"shard-{self.index}") if base else self.eval_dir
		# swift test holds the scratch lock for its whole run; staggered starts (run) keep build.db single-writer.
		cmd = ["swift", "test", "--skip-build", "--ignore-lock", "--disable-xctest"] + swift_args + filters + [
			"--event-stream-output-path", self.events]
		with open(self.log, "wb") as f:
			self.proc = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
				env=env, start_new_session=True)
		GROUPS.add(self.proc.pid)
		self.started = time.monotonic()

	def poll(self):
		if self.status is not None:
			return True
		pid, status = os.waitpid(self.proc.pid, os.WNOHANG)
		now = time.monotonic()
		if pid == 0:
			if self.term_sent is None and now - self.started > self.timeout:
				self.timed_out = True
				self.term_sent = now
				kill_group(self.proc.pid)
			elif self.term_sent is not None and now - self.term_sent > KILL_GRACE_S:
				kill_group(self.proc.pid, signal.SIGKILL)
			return False
		self.status = os.waitstatus_to_exitcode(status)
		self.proc.returncode = self.status
		self.ended = now
		kill_group(self.proc.pid, signal.SIGKILL)  # whatever the shard left behind in its group
		GROUPS.discard(self.proc.pid)
		return True

	def has_begun(self):
		"""swift-test has handed over to the test process: its first records are in the event stream."""
		try:
			return os.path.getsize(self.events) > 0
		except OSError:
			return False

	def failed(self):
		return self.timed_out or self.status != 0


def read_events(path):
	events = []
	try:
		with open(path, encoding="utf-8", errors="replace") as f:
			for line in f:
				try:
					events.append(json.loads(line))
				except ValueError:
					pass  # a process killed mid-write leaves a partial last line
	except FileNotFoundError:
		pass
	return events


def strip_location(event_id):
	# "M.S/f()/File.swift:12:3" -> "M.S/f()"
	return re.sub(r"/[^/]+:\d+:\d+$", "", event_id)


def outcomes(shard):
	"""From a shard's event stream: the tests run, by listed ID; their start and end instants; the issues
	recorded; the tests started and never ended."""
	functions, ran, spans, issues, open_tests = set(), [], collections.defaultdict(list), [], []
	for e in read_events(shard.events):
		payload = e.get("payload", {})
		if e.get("kind") == "test":
			if payload.get("kind") == "function":
				functions.add(payload["id"])
			continue
		kind, tid = payload.get("kind"), payload.get("testID")
		if tid not in functions:
			if kind == "issueRecorded" and not payload.get("issue", {}).get("isKnown"):
				issues.append(payload)
			continue
		instant = payload.get("instant", {}).get("absolute")
		if kind == "testStarted":
			spans[strip_location(tid)].append(instant)
			open_tests.append(strip_location(tid))
		elif kind in ("testEnded", "testSkipped"):
			ran.append(strip_location(tid))
			if strip_location(tid) in open_tests:
				open_tests.remove(strip_location(tid))
			spans[strip_location(tid)].append(instant)
		elif kind == "issueRecorded" and not payload.get("issue", {}).get("isKnown"):
			issues.append(payload)
	return ran, spans, issues, open_tests


def report_failure(shard, issues, open_tests):
	why = f"still running after the {shard.timeout} s bound" if shard.timed_out else f"exit {shard.status}"
	print(f"\n=== {shard.name} FAILED: {why}; suites: {' '.join(shard.units)}", flush=True)
	for t in open_tests:
		print(f"  never ended: {t}")
	for p in issues:
		where = p.get("issue", {}).get("sourceLocation", {})
		text = " | ".join(m.get("text", "") for m in p.get("messages", []))
		print(f"  issue in {p.get('testID')} at {where.get('fileID', '?')}:{where.get('line', '?')}: {text}")
	with open(shard.log, encoding="utf-8", errors="replace") as f:
		lines = f.read().splitlines()
	# The hang report and a sanitizer's report end the process, so they run to the end of the log.
	start = next((i for i, l in enumerate(lines) if l.startswith("hang: ") or "ERROR: AddressSanitizer" in l
		or "runtime error:" in l), None)
	if start is None and (issues and not shard.timed_out and shard.status == 1):
		start = len(lines)  # ordinary failed expectations: the issues above say it all
	if start is None:
		start = max(0, len(lines) - 200)
	if start < len(lines):
		print(f"--- {shard.log} from line {start + 1}:")
		for l in lines[start:]:
			print(l)
	print(f"=== end of {shard.name}; the whole log: {shard.log}", flush=True)


def run(args):
	swift_args = args.swift_args[1:] if args.swift_args[:1] == ["--"] else args.swift_args
	scratch = scratch_path(swift_args)
	out = os.path.abspath(os.path.join(scratch, "shards"))
	env = swift_env(args.sdkroot)
	began = time.monotonic()

	log(f"building ({' '.join(swift_args)})")
	rc, _ = bounded(["swift", "build", "--build-tests"] + swift_args, args.timeout, env=env)
	if rc != 0:
		sys.exit(rc)
	built = time.monotonic()

	ids = list_tests(swift_args, args.timeout, env)
	listed = collections.Counter(ids)
	tests_by_unit = collections.defaultdict(list)
	for t in sorted(listed):
		tests_by_unit[unit_of(t)].append(t)
	module = split_id(ids[0])[0]

	entry = recorded(load_times(), args.gate)
	known = entry.get("seconds", {})
	unknown = sorted(u for u in tests_by_unit if u not in known)
	fallback = sorted(known.values())[len(known) // 2] if known else 1.0
	seconds = {u: known.get(u, fallback) for u in tests_by_unit}
	if unknown:
		log(f"no recorded time for {', '.join(unknown)}; counted as {fallback:.1f} s each")
	n, why = shard_count(entry, seconds, args.shards, args.isolated)
	n = min(n, len(tests_by_unit))
	if args.isolated:
		plan = [([u], seconds[u]) for u in sorted(tests_by_unit, key=lambda u: (-seconds[u], u))]
		log(f"{len(ids)} tests in {len(tests_by_unit)} suites, one process each, {n} at a time ({why})")
	else:
		plan = deal(sorted(tests_by_unit), seconds, n)
		log(f"{len(ids)} tests in {len(tests_by_unit)} suites over {len(plan)} shards ({why})")

	shutil.rmtree(out, ignore_errors=True)
	os.makedirs(out)
	shards = [Shard(i + 1, units, planned, out, args.timeout) for i, (units, planned) in enumerate(plan)]

	pending = list(shards)
	running = []
	sampled = 0.0
	while pending or running:
		# One shard at a time gets past swift-test's planning, the part that writes the shared build.db.
		if pending and len(running) < n and all(s.has_begun() or s.status is not None for s in running):
			s = pending.pop(0)
			s.start(swift_args, tests_by_unit, env)
			log(f"{s.name} started: {len(s.units)} suites, {sum(len(tests_by_unit[u]) for u in s.units)} tests, "
				f"~{s.planned:.0f} s planned")
			running.append(s)
		for s in list(running):
			if s.poll():
				running.remove(s)
				log(f"{s.name} {'FAILED' if s.failed() else 'passed'}: {s.ended - s.started:.0f} s "
					f"(planned {s.planned:.0f}), exit {s.status}, peak RSS {s.rss / 2**20:.0f} MB")
		if time.monotonic() - sampled >= 1:
			sampled = time.monotonic()
			resident = resident_by_group()
			for s in running:
				s.rss = max(s.rss, resident.get(s.proc.pid, 0))
		time.sleep(0.1)

	# Every listed test exactly once, whichever shard ran it.
	ran_in = collections.defaultdict(list)
	measured, failed = {}, False
	for s in shards:
		ran, spans, issues, open_tests = outcomes(s)
		for t in ran:
			ran_in[t].append(s.index)
		for u in s.units:
			instants = [x for t in tests_by_unit[u] for x in spans.get(t, []) if x is not None]
			if instants:
				measured[u] = round(max(instants) - min(instants), 3)
		if s.failed():
			failed = True
			report_failure(s, issues, open_tests)
	peak = max(s.rss for s in shards)
	with open(os.path.join(out, "times.json"), "w") as f:
		json.dump({"gate": args.gate, "arch": arch(), "module": module, "shards": len(shards), "passed": not failed,
			"peak_rss_mb": math.ceil(peak / 2**20), "seconds": measured}, f, indent=1, sort_keys=True)
	if failed:
		sys.exit(1)

	problems = []
	for t, want in sorted(listed.items()):
		got = len(ran_in.get(t, []))
		if got != want:
			problems.append(f"{t}: listed {want}, ran {got} times (shards {ran_in.get(t, [])})")
	problems += [f"{t}: ran in shards {ran_in[t]} but swift test list does not name it"
		for t in sorted(set(ran_in) - set(listed))]
	if problems:
		print("\n".join(["shards: the shards did not run every listed test exactly once:"] + problems), flush=True)
		sys.exit(1)

	log(f"{len(ids)} tests, each run once, in {len(shards)} shards: build {built - began:.0f} s, "
		f"tests {time.monotonic() - built:.0f} s, peak RSS per shard {peak / 2**20:.0f} MB")


def record(args):
	times = load_times()
	for path in args.measured:
		with open(path) as f:
			m = json.load(f)
		if not m.get("passed"):
			sys.exit(f"{path}: a failed run's times are partial; record a passing one")
		slot = times.setdefault(m["gate"], {}).setdefault(m["arch"], {})
		slot["seconds"] = dict(sorted(m["seconds"].items()))
		slot["peak_rss_mb"] = m["peak_rss_mb"]
		print(f"recorded {m['gate']} on {m['arch']}: {len(m['seconds'])} suites, peak {m['peak_rss_mb']} MB")
	with open(TIMES, "w") as f:
		json.dump(dict(sorted(times.items())), f, indent=1, sort_keys=True)
		f.write("\n")


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = ap.add_subparsers(dest="command", required=True)
	r = sub.add_parser("run")
	r.add_argument("--gate", required=True, help="whose recorded times deal the suites out")
	r.add_argument("--shards", type=int, default=int(os.environ.get("TEST_SHARDS") or 0) or None)
	r.add_argument("--timeout", type=int, default=int(os.environ.get("TEST_TIMEOUT") or 500),
		help="seconds per step: the build, the listing, each shard")
	r.add_argument("--isolated", action="store_true",
		help="every suite in a process of its own: a baseline must not lean on another suite's allocations")
	r.add_argument("--sdkroot",help="the caller's SDKROOT, empty for none; without it the runner's own is kept")
	r.add_argument("swift_args", nargs=argparse.REMAINDER)
	c = sub.add_parser("record")
	c.add_argument("measured", nargs="+", help="a run's <scratch>/shards/times.json")
	args = ap.parse_args()
	signal.signal(signal.SIGINT, stop)
	signal.signal(signal.SIGTERM, stop)
	if args.command == "run":
		run(args)
	else:
		record(args)


if __name__ == "__main__":
	main()
