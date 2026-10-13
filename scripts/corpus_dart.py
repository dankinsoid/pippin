# @ai-generated(solo)
"""corpus-bench's idiomatic Dart reference (docs/notes/benchmarks.md, "Hand-ported references"): the data workloads
hand-ported in bench/workloads/dart, one `dart compile exe` binary per workload (a Flutter release build's code), run
by corpus_refs.Refs like the Swift port."""

import os
import shutil
import subprocess
import time

import corpus_swift

TOOL = "dart"
SOURCES = "bench/workloads/dart"
NA = {w: "isolates and Streams would be a different program from core.async's channels and go blocks"
      for w in ("async-pipeline", "async-libs", "async-broadcast")}
NA["datascript"] = "not hand-ported: its subject is the Datascript library, a Datalog engine over a B-tree index"
verdict = corpus_swift.verdict


def binary(refs, w):
	return os.path.join(refs.b.out, "dart", "aot", w)


def build(refs, workloads):
	"""One executable per ported workload, from a copy of the package so the tree gets no .dart_tool; False leaves
	dart out."""
	t0 = time.monotonic()
	p = subprocess.run([TOOL, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
	refs.versions["dart"] = " ".join(p.stdout.split())[:120]
	d = os.path.join(refs.b.out, "dart")
	shutil.rmtree(d, ignore_errors=True)
	src = os.path.join(d, "src")
	shutil.copytree(os.path.join(refs.root, SOURCES), src)
	os.makedirs(os.path.join(d, "aot"))
	p = subprocess.run([TOOL, "pub", "get", "--offline"], cwd=src, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
	                   text=True, timeout=600)
	if p.returncode != 0:
		refs.b.errors.append("dart pub get of the ports exited %d: %s" % (p.returncode, p.stdout.strip()[-600:]))
		return False
	ok = True
	for w in workloads:
		if w in NA:
			continue
		p = subprocess.run([TOOL, "compile", "exe", os.path.join("bin", w.replace("-", "_") + ".dart"), "-o",
		                    binary(refs, w)], cwd=src, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
		                   timeout=900)
		refs.b.keep("%s.dart-build.txt" % w, p.stdout)
		if p.returncode != 0:
			refs.b.errors.append("dart compile exe %s exited %d: %s" % (w, p.returncode, p.stdout.strip()[-800:]))
			ok = False
	refs.cost["dart-build"] = time.monotonic() - t0
	refs.log("Dart ports built in %.0f s" % refs.cost["dart-build"])
	return ok


def cmd(refs, w, once):
	return [binary(refs, w)] + (["once"] if once else [])


def section(b, refs, add, fmt, median_of):
	add("## Idiomatic Dart")
	add("")
	add("The nine data workloads hand-ported to Dart as a Flutter developer writes them (`bench/workloads/dart`; the "
	    "rules and each port's judgment calls: docs/notes/benchmarks.md, \"Hand-ported references\"), one executable "
	    "per workload from `dart compile exe`, what a Flutter release build ships, built before the workloads, outside "
	    "every time. Warm is our policy (one iteration, then >= 3 and >= 2 s, at most 30), *1st* the first iteration "
	    "of a fresh process and *cold* that process start to exit. *same result*: the printed result against the "
	    "workload's `expected` literal. Ratios are ours over Dart's (below 1: ours is faster). %s." %
	    refs.versions.get("dart", "?"))
	add("")
	add("| workload | closed | interp | dart warm | dart 1st | dart cold | closed/dart | interp/dart | same result |")
	add("|---|---:|---:|---:|---:|---:|---:|---:|---|")
	for w in b.args.only:
		r = b.results[w]
		if ("dart", w) in refs.na:
			add("| %s | %s | %s | n/a | n/a | n/a | n/a | n/a | n/a |" % (
				w, fmt(median_of(r.get("closed"))), fmt(median_of(r.get("interp")))))
			continue
		s = median_of(r.get("dart"))
		ratios = []
		for ours in ("closed", "interp"):
			o = median_of(r.get(ours))
			ratios.append("%.2f×" % (o / s) if o and s else "—")
		add("| %s | %s | %s | %s | %s | %s | %s | %s |" % (
			w, fmt(median_of(r.get("closed"))), fmt(median_of(r.get("interp"))), fmt(s, 2),
			fmt(r.get("dart_cold_first_ms"), 1), fmt(r.get("dart_cold_process_ms"), 0), " | ".join(ratios),
			(r.get("dart") or {}).get("expected", "—")))
	add("")
	na = {}
	for (ref, w), why in refs.na.items():
		if ref == "dart":
			na.setdefault(why, []).append(w)
	if na:
		add("n/a: " + "; ".join("%s: %s" % (", ".join(sorted(ws)), why) for why, ws in sorted(na.items())) + ".")
		add("")
	add("Wall time: the Dart builds %.1f min, the runs %.1f min." % (refs.cost.get("dart-build", 0) / 60,
	                                                               refs.cost.get("dart", 0) / 60))
	add("")
