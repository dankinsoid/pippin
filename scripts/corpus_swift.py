# @ai-generated(solo)
"""corpus-bench's idiomatic Swift reference (docs/notes/benchmarks.md, "Hand-ported references"): the data workloads
hand-ported in bench/workloads/swift, one release binary per workload, run by corpus_refs.Refs like the others."""

import concurrent.futures
import os
import re
import subprocess
import time

TOOL = "swiftc"
SOURCES = "bench/workloads/swift"
# What a release App Store build compiles with: optimized, whole-module.
FLAGS = ["-O", "-wmo"]
NA = {w: "structured concurrency or actors would be a different program from core.async's channels and go blocks"
      for w in ("async-pipeline", "async-libs", "async-broadcast")}
NA["datascript"] = "not hand-ported: its subject is the Datascript library, a Datalog engine over a B-tree index"


def binary(refs, w):
	return os.path.join(refs.b.out, "swift", w)


def build(refs, workloads):
	"""One binary per ported workload, built before the workloads and outside every time; False leaves swift out."""
	t0 = time.monotonic()
	p = subprocess.run([TOOL, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=refs.b.env)
	m = re.search(r"Apple Swift version \S+", p.stdout)
	refs.versions["swift"] = m.group(0) if m else " ".join(p.stdout.split())[:120]
	os.makedirs(os.path.join(refs.b.out, "swift"), exist_ok=True)
	src = os.path.join(refs.root, SOURCES)
	harness = sorted(os.path.join(src, "harness", f) for f in os.listdir(os.path.join(src, "harness")) if f.endswith(".swift"))

	def one(w):
		cmd = [TOOL] + FLAGS + harness + [os.path.join(src, "workloads", w.replace("-", "_") + ".swift"), "-o", binary(refs, w)]
		return w, subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=refs.b.env, timeout=1200)

	ported = [w for w in workloads if w not in NA]
	ok = True
	with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(4, os.cpu_count() or 1))) as pool:
		for w, p in pool.map(one, ported):
			refs.b.keep("%s.swift-build.txt" % w, p.stdout)
			if p.returncode != 0:
				refs.b.errors.append("swiftc %s exited %d: %s" % (w, p.returncode, p.stdout.strip()[-800:]))
				ok = False
	refs.cost["swift-build"] = time.monotonic() - t0
	refs.log("Swift ports built in %.0f s" % refs.cost["swift-build"])
	return ok


def cmd(refs, w, once):
	return [binary(refs, w)] + (["once"] if once else [])


def expected_literal(root, w):
	with open(os.path.join(root, "bench/workloads/src/workloads", w.replace("-", "_") + ".clj")) as f:
		m = re.search(r"^\(def expected '(.*)\)\s*$", f.read(), re.M)
	return m.group(1) if m else None


def verdict(root, w, printed):
	"""The port has no Clojure `=`: its printed result against the workload's `expected` literal."""
	want = expected_literal(root, w)
	return "same" if printed is not None and printed == want else "DIFFERENT"


def section(b, refs, add, fmt, median_of):
	add("## Idiomatic Swift")
	add("")
	add("The nine data workloads hand-ported to Swift as an iOS developer writes them (`bench/workloads/swift`; the "
	    "rules and each port's judgment calls: docs/notes/benchmarks.md, \"Hand-ported references\"), one binary per "
	    "workload from `swiftc %s`, built before the workloads, outside every time. Warm is our policy (one iteration, "
	    "then >= 3 and >= 2 s), *1st* the first iteration of a fresh process and *cold* that process start to exit. "
	    "*same result*: the printed result against the workload's `expected` literal. Ratios are ours over Swift's "
	    "(below 1: ours is faster). %s." % (" ".join(FLAGS), refs.versions.get("swift", "?")))
	add("")
	add("| workload | closed | interp | swift warm | swift 1st | swift cold | closed/swift | interp/swift | same result |")
	add("|---|---:|---:|---:|---:|---:|---:|---:|---|")
	for w in b.args.only:
		r = b.results[w]
		if ("swift", w) in refs.na:
			add("| %s | %s | %s | n/a | n/a | n/a | n/a | n/a | n/a |" % (
				w, fmt(median_of(r.get("closed"))), fmt(median_of(r.get("interp")))))
			continue
		s = median_of(r.get("swift"))
		ratios = []
		for ours in ("closed", "interp"):
			o = median_of(r.get(ours))
			ratios.append("%.2f×" % (o / s) if o and s else "—")
		add("| %s | %s | %s | %s | %s | %s | %s | %s |" % (
			w, fmt(median_of(r.get("closed"))), fmt(median_of(r.get("interp"))), fmt(s, 2),
			fmt(r.get("swift_cold_first_ms"), 1), fmt(r.get("swift_cold_process_ms"), 0), " | ".join(ratios),
			(r.get("swift") or {}).get("expected", "—")))
	add("")
	na = {}
	for (ref, w), why in refs.na.items():
		if ref == "swift":
			na.setdefault(why, []).append(w)
	if na:
		add("n/a: " + "; ".join("%s: %s" % (", ".join(sorted(ws)), why) for why, ws in sorted(na.items())) + ".")
		add("")
	add("Wall time: the Swift builds %.1f min, the runs %.1f min." % (refs.cost.get("swift-build", 0) / 60,
	                                                                refs.cost.get("swift", 0) / 60))
	add("")
