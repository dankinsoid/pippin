#!/usr/bin/env python3
# @ai-generated(solo)
"""Generate the level-2 stubs of one Swift module: symbol graph -> classification -> Swift -> swiftc -> dylib.

Design §5 «Объявленная граница»: the generator is a build tool, `require-swift` runs it in dev through
`SwiftStubs.generator`, and the dylib it builds registers the module's functions with the runtime through one
`@_cdecl` entry. The classifier is `scripts/swift-reprint.py`'s, imported rather than copied, so the measurement in
docs/swift-reprint.md measures the classifier that prints the stubs.

Usage:
	scripts/swift-stubgen.py --module NAME --cache DIR --runtime-modules DIR [-I DIR]... [-L DIR]... [-l LIB]...
		[--module-map FILE]...

Prints the dylib's path as the last line. A cache entry is keyed by a fingerprint of the module's `.swiftmodule`,
the runtime's `Pippin.swiftmodule`, both scripts, the module maps, the link arguments and the compiler; a hit
prints the path and does nothing else. Every public symbol of the module is either generated or listed with its
reason in report.json beside the dylib and in the dylib's own registration.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("swift_reprint", os.path.join(HERE, "swift-reprint.py"))
reprint = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(reprint)

# What the first slice converts by value, each through Pippin's ValueCodable conformance.
SCALARS = {"s:Si": "Int", "s:Sd": "Double", "s:Sb": "Bool", "s:SS": "String"}

# Why a refused symbol is refused: the classifier's group-3 causes, then the first slice's own limits.
CAUSE_TEXT = {
	"macro": "a macro expands where it is used; there is nothing to call",
	"parameter-pack": "a parameter pack has no stub form",
	"noncopyable": "a ~Copyable value cannot sit behind a handle the runtime may copy",
	"autoclosure": "an @autoclosure parameter has no stub form",
	"property-wrapper-as-api": "a property wrapper is a declaration form, not a call",
	"variadic-parameter": "a variadic parameter has no stub form",
}


def progress(msg):
	print(msg, file=sys.stderr, flush=True)


def run(cmd, **kw):
	return subprocess.run(cmd, capture_output=True, text=True, **kw)


def find_swiftmodule(module, dirs):
	for d in dirs:
		p = os.path.join(d, f"{module}.swiftmodule")
		if os.path.exists(p):
			return p
	sys.exit(f"swift-stubgen: no {module}.swiftmodule under {', '.join(dirs) or '(no -I given)'}")


def add_path(h, path):
	"""A file, or every file under a directory in a fixed order, into the hash."""
	paths = [path] if os.path.isfile(path) else sorted(
		os.path.join(d, f) for d, _, fs in os.walk(path) for f in fs)
	for p in paths:
		h.update(p.encode() + b"\0")
		with open(p, "rb") as f:
			h.update(f.read())


def fingerprint(args, module_file, compiler):
	h = hashlib.sha256()
	h.update(f"{args.module}\0{compiler}\0{args.L}\0{args.l}\0".encode())
	for p in [module_file, os.path.join(args.runtime_modules, "Pippin.swiftmodule"), __file__,
			os.path.join(HERE, "swift-reprint.py")] + args.module_map:
		add_path(h, p)
	return h.hexdigest()[:32]


# ---------------------------------------------------------------------------
# Classification: the classifier's record, then what the first slice generates.

def stored_property_owners(paths, idx):
	"""Structs with a public stored property: design §5 moves those as a map, which the first slice has not built."""
	owners = set()
	for p in paths:
		for sym in reprint.iter_symbols(p):
			if sym["kind"]["identifier"] != "swift.property":
				continue
			decl = reprint.frag_text(sym.get("declarationFragments", []))
			if "{" not in decl:
				owner = idx.member_of.get(sym["identifier"]["precise"])
				if owner:
					owners.add(owner)
	return owners


def slot_form(role, text, usrs, idx, map_structs):
	"""('scalar', T) or ('box', T) for a slot the first slice converts; ('refused', why) otherwise."""
	t = text.strip()
	if role == "return" and t in ("", "Void", "()"):
		return ("void", None)
	usr = usrs.get(t)
	if usr in SCALARS:
		return ("scalar", t)
	rec = idx.by_usr.get(usr)
	if rec and rec["kind"] == "swift.struct" and not rec["generic"]:
		if usr in map_structs:
			return ("refused", f"{role} `{t}` has public stored properties, so it crosses as a map (design §5), "
				"which the first slice does not build")
		return ("box", t)
	what = reprint.classify_type(t, usrs, None, idx.alias_closures)
	return ("refused", f"{role} `{t}` crosses as {what}, which the first slice does not convert")


def refusal(rec, idx, map_structs):
	"""The reason a symbol is not generated, or None."""
	if rec["group"] == 3:
		return f"{rec['cause']}: {CAUSE_TEXT.get(rec['cause'], 'refused by the classifier')}"
	kind = rec["kind"]
	if kind in reprint.OPERATOR_KINDS:
		return "operator: §5's call forms have no spelling for one"
	if kind in reprint.PROPERTY_KINDS:
		return "property: read as (:field obj) over host metadata (design §5), not a stub call; not in the first slice"
	if kind != "swift.func":
		return "member of a type (method or initializer): the first slice generates free functions only"
	if rec["generics"] or re.search(r"\bSelf\b", rec["decl"]):
		return "generic: the instantiation set comes from call sites (design §5), not in the first slice"
	if rec["throws"] != "none":
		return f"{rec['throws']}: not in the first slice (design §5 «throws — четыре формы»)"
	if re.search(r"\basync\b", rec["decl"]):
		return "async: not in the first slice"
	if any(rec["mods"]):
		return "inout or an ownership modifier: not in the first slice (design §5 «mutating, inout»)"
	if rec["isolation"] not in (None, "MainActor"):
		return f"isolation @{rec['isolation']}: the first slice hops to the main actor only"
	for (role, text, _bucket), usrs in zip(rec["slots"], rec["slot_usrs"]):
		form = slot_form(role, text, usrs, idx, map_structs)
		if form[0] == "refused":
			return form[1]
	return None


# ---------------------------------------------------------------------------
# Emission.

def swift_string(s):
	return json.dumps(s, ensure_ascii=False)


def emit_function(rec, idx, map_structs):
	"""The registration of one function, as Swift: decode where the caller is, call, encode."""
	labels = reprint.labels_of(rec["title"])
	params = [(text, usrs) for (role, text, _b), usrs in zip(rec["slots"], rec["slot_usrs"]) if role == "param"]
	ret = next(((text, usrs) for (role, text, _b), usrs in zip(rec["slots"], rec["slot_usrs"]) if role == "return"), ("", {}))
	lines, call_args = [], []
	for i, ((text, usrs), label) in enumerate(zip(params, labels)):
		kind, t = slot_form("parameter", text, usrs, idx, map_structs)
		decode = f"try {t}(decoding: args[{i}])" if kind == "scalar" else f"try Pippin.SwiftStubs.unbox(args[{i}], as: {t}.self)"
		lines.append(f"let a{i} = {decode}")
		call_args.append(f"a{i}" if label == "_" else f"{label}: a{i}")
	call = f"{rec['title'].split('(')[0]}({', '.join(call_args)})"
	kind, _t = slot_form("return", ret[0], ret[1], idx, map_structs)
	result = {"void": f"{call}; return Pippin.Value.nil_", "scalar": f"return {call}.asValue",
		"box": f"return Pippin.SwiftStubs.box({call})"}[kind]
	swift_labels = "[" + ", ".join("nil" if l == "_" else swift_string(l) for l in labels) + "]"
	head = f"Pippin.SwiftStubs.Function(swiftName: {swift_string(rec['title'])}, labels: {swift_labels}"
	body = "".join(f"\t\t\t{l}\n" for l in lines)
	if rec["isolation"] == "MainActor":
		return f"{head}, mainActor: {{ args in\n{body}\t\t\treturn {{ {result} }}\n\t\t}}),\n"
	return f"{head}) {{ args in\n{body}\t\t\t{result}\n\t\t}},\n"


def emit(module, generated, refused, idx, map_structs):
	out = [f"// Generated by scripts/swift-stubgen.py from the symbol graph of {module}; do not edit.\n",
		"import Pippin\n", f"import {module}\n\n",
		f"@_cdecl(\"pippin_stubs_register_{module}\")\n",
		f"public func pippin_stubs_register_{module}() {{\n",
		f"\tPippin.SwiftStubs.register(module: {swift_string(module)}, functions: [\n"]
	lines = {}
	for rec in generated:
		lines[sum(s.count("\n") for s in out) + 1] = rec
		out.append("\t\t" + emit_function(rec, idx, map_structs))
	out.append("\t], refusals: [\n")
	for name, reason in refused:
		out.append(f"\t\tPippin.SwiftStubs.Refusal(swiftName: {swift_string(name)}, reason: {swift_string(reason)}),\n")
	out.append("\t])\n}\n")
	return "".join(out), lines


def owner_of(text_lines, line):
	"""The generated function whose emission covers a 1-based line of the file."""
	starts = sorted(k for k in text_lines if k <= line)
	return text_lines[starts[-1]] if starts else None


# ---------------------------------------------------------------------------

def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--module", required=True)
	ap.add_argument("--cache", required=True)
	ap.add_argument("--runtime-modules", required=True, help="the directory holding Pippin.swiftmodule")
	ap.add_argument("-I", action="append", default=[], help="where the module's .swiftmodule is")
	ap.add_argument("-L", action="append", default=[])
	ap.add_argument("-l", action="append", default=[])
	ap.add_argument("--module-map", action="append", default=[], help="a module map of a C module Pippin imports")
	args = ap.parse_args()
	module = args.module

	info = json.loads(run(["xcrun", "swiftc", "-print-target-info"]).stdout)
	compiler = info["compilerVersion"]
	target = info["target"]["triple"]
	sdk = run(["xcrun", "--show-sdk-path"]).stdout.strip()
	module_file = find_swiftmodule(module, args.I)
	key = fingerprint(args, module_file, compiler + target + sdk)
	home = os.path.join(os.path.abspath(args.cache), module)
	final = os.path.join(home, key)
	dylib_name = f"lib{module}PippinStubs.dylib"
	if os.path.exists(os.path.join(final, "report.json")):
		print(os.path.join(final, dylib_name))
		return
	work = f"{final}.tmp-{os.getpid()}"
	shutil.rmtree(work, ignore_errors=True)
	os.makedirs(work)

	graph_dir = os.path.join(work, "graph")
	reprint.extract(module, target, sdk, graph_dir, os.path.dirname(module_file), progress)
	paths = reprint.graph_files(graph_dir)
	idx = reprint.scan_types(paths, lambda _m: None)
	map_structs = stored_property_owners(paths, idx)
	recs = []
	for p in paths:
		for sym in reprint.iter_symbols(p):
			kind = sym["kind"]["identifier"]
			if any(c.startswith("_") for c in sym["pathComponents"]):
				continue
			if kind in reprint.FUNCLIKE_KINDS | reprint.OPERATOR_KINDS | reprint.PROPERTY_KINDS or kind == "swift.macro":
				recs.append(reprint.symbol_record(sym, idx))

	def swift_name(rec):
		return ".".join(rec["path"])

	generated, refused = [], []
	for rec in sorted(recs, key=swift_name):
		why = refusal(rec, idx, map_structs)
		if why:
			refused.append((swift_name(rec), why))
		else:
			generated.append(rec)

	# One stub swiftc rejects moves to the report with swiftc's words, so it cannot take the module down.
	source = os.path.join(work, "stubs.swift")
	dylib = os.path.join(work, dylib_name)
	while True:
		text, lines = emit(module, generated, refused, idx, map_structs)
		with open(source, "w") as f:
			f.write(text)
		cmd = ["xcrun", "swiftc", "-emit-library", "-parse-as-library", "-swift-version", "5",
			"-module-name", f"{module}PippinStubs", source, "-o", dylib, "-I", args.runtime_modules]
		for d in args.I:
			cmd += ["-I", d]
		for m in args.module_map:
			cmd += ["-Xcc", f"-fmodule-map-file={m}"]
		for d in args.L:
			cmd += ["-L", d, "-Xlinker", "-rpath", "-Xlinker", os.path.abspath(d)]
		for l in args.l:
			cmd += [f"-l{l}"]
		# Pippin and the core are the host process's own: the stub binds to the copy that loads it.
		cmd += ["-Xlinker", "-undefined", "-Xlinker", "dynamic_lookup"]
		res = run(cmd)
		if res.returncode == 0:
			break
		blamed = {}
		for m in re.finditer(r"stubs\.swift:(\d+):\d+: error: (.*)", res.stderr):
			rec = owner_of(lines, int(m.group(1)))
			if rec is not None:
				blamed.setdefault(id(rec), (rec, m.group(2)))
		if not blamed:
			shutil.rmtree(work, ignore_errors=True)
			sys.exit(f"swift-stubgen: swiftc failed on the stubs of {module}:\n{res.stderr}")
		for rec, error in blamed.values():
			generated.remove(rec)
			refused.append((swift_name(rec), f"swiftc rejected the stub: {error}"))

	report = {
		"module": module, "fingerprint": key, "compiler": compiler, "target": target,
		"generated": [{"swift": swift_name(r),
			"labels": [None if l == "_" else l for l in reprint.labels_of(r["title"])],
			"isolation": r["isolation"]} for r in generated],
		"refused": [{"swift": n, "reason": why} for n, why in refused],
	}
	with open(os.path.join(work, "report.json"), "w") as f:
		json.dump(report, f, indent=1, ensure_ascii=False)
	try:
		os.rename(work, final)
	except OSError:
		if not os.path.exists(os.path.join(final, "report.json")):
			raise
		shutil.rmtree(work, ignore_errors=True)
	print(os.path.join(final, dylib_name))


if __name__ == "__main__":
	main()
