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

# What crosses by value, each through Pippin's ValueCodable conformance.
SCALARS = {"s:Si": "Int", "s:Sd": "Double", "s:Sb": "Bool", "s:SS": "String"}

# Why a refused symbol is refused: the classifier's group-3 causes.
CAUSE_TEXT = {
	"macro": "a macro expands where it is used; there is nothing to call",
	"parameter-pack": "a parameter pack has no stub form",
	"noncopyable": "a ~Copyable value cannot sit behind a handle the runtime may copy",
	"autoclosure": "an @autoclosure parameter has no stub form",
	"property-wrapper-as-api": "a property wrapper is a declaration form, not a call",
	"variadic-parameter": "a variadic parameter has no stub form",
}

# Ownership that changes nothing for a copyable value (design §5 «`mutating`, `inout`»); ~Copyable is refused earlier.
NEUTRAL_MODIFIERS = re.compile(r"^\s*(?:borrowing|consuming|__owned|__shared)\s+")
CLOSURE = re.compile(r"^(?:@escaping\s+|@Sendable\s+)*\((.*)\)\s*(async\s+)?(throws(?:\s*\([^)]*\))?\s+)?->\s*(.+)$", re.S)
MAX_CLOSURE_PARAMS = 3			# the arities of Value.closure()


class Refused(Exception):
	pass


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
# Classification: the classifier's record, then the stubs it becomes.

def stored_property_owners(paths, idx):
	"""Structs with a public stored property: design §5 moves those as a map, which is not built."""
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


class Types:
	"""How a slot's type crosses: ('scalar'|'box'|'object', spelling), ('closure', params, result), ('void',)."""

	def __init__(self, idx, map_structs):
		self.idx = idx
		self.map_structs = map_structs

	def nominal(self, usr, spelling, what):
		if usr in SCALARS:
			return ("scalar", spelling)
		rec = self.idx.by_usr.get(usr)
		if rec and rec["kind"] in ("swift.struct", "swift.class") and rec["generic"]:
			raise Refused(f"{what} `{spelling}` is generic: the instantiation set comes from call sites (design §5)")
		if rec and rec["kind"] == "swift.struct":
			if usr in self.map_structs:
				raise Refused(f"{what} `{spelling}` has public stored properties, so it crosses as a map (design §5), "
					"which is not built")
			return ("box", spelling)
		if rec and rec["kind"] == "swift.class":
			return ("object", spelling)
		return None

	def form(self, role, text, usrs):
		t = NEUTRAL_MODIFIERS.sub("", text.strip()).strip()
		if role == "result" and t in ("", "Void", "()"):
			return ("void",)
		if reprint._find_top(t, "->") >= 0:
			if role != "parameter":
				raise Refused(f"{role} `{t}` is a closure, which crosses only as a parameter")
			return self.closure(t, usrs)
		form = self.nominal(usrs.get(t) or usrs.get(t.split(".")[-1]), t, role)
		if form:
			return form
		if t.endswith(("?", "!")):
			raise Refused(f"{role} `{t}` is an optional, which no stub converts yet")
		what = reprint.classify_type(t, usrs, None, self.idx.alias_closures)
		raise Refused(f"{role} `{t}` crosses as {what}, which no stub converts yet")

	def closure(self, t, usrs):
		"""Only what `rethrows` needs (design §5 «`throws` — четыре формы»): a throwing function of scalars."""
		m = CLOSURE.match(t)
		why = f"closure parameter `{t}`"
		if not m:
			raise Refused(f"{why}: not a plain function type")
		params, is_async, throws, result = m.groups()
		if is_async:
			raise Refused(f"{why} is async, which no stub adapts yet")
		if not throws or re.search(r"\bNever\b", throws):
			raise Refused(f"{why} does not throw: it needs the onFailure policy configured at the stub (design §5), "
				"which has no configuration yet")
		parts = [p.strip() for p in reprint._split_top(params, [","])] if params.strip() else []
		if len(parts) > MAX_CLOSURE_PARAMS:
			raise Refused(f"{why} takes more than {MAX_CLOSURE_PARAMS} arguments")
		for p in parts:
			if usrs.get(p) not in SCALARS:
				raise Refused(f"{why}: its argument `{p}` is not Int, Double, Bool or String")
		result = result.strip()
		if result not in ("Void", "()") and usrs.get(result) not in SCALARS:
			raise Refused(f"{why}: its result `{result}` is not Int, Double, Bool, String or Void")
		return ("closure", f"@Sendable ({', '.join(parts)}) throws -> {result}")


def swift_name(rec):
	return ".".join(rec["path"])


def owner_of(rec, idx):
	"""(owner record, spelling) of a member of a type of this module, or (None, None) for a free symbol."""
	path = rec["path"]
	if len(path) == 1:
		return None, None
	owner = idx.by_path.get(path[:-1])
	if owner is None:
		raise Refused("member of a type of another module: no stub reaches it")
	kind = owner["kind"]
	spelling = ".".join(path[:-1])
	if kind == "swift.enum":
		raise Refused("member of an enum: an enum crosses as a keyword (design §5), which is not built")
	if kind == "swift.protocol":
		raise Refused("protocol member: generic over the conformer, whose instantiations come from call sites (design §5)")
	if kind == "swift.actor":
		raise Refused("actor member: isolation to an actor instance is not built")
	if kind not in ("swift.struct", "swift.class"):
		raise Refused(f"member of a {kind}: no stub form")
	for k in range(1, len(path) - 1):
		if (idx.by_path.get(path[:k]) or {}).get("generic"):
			raise Refused(f"member of `{spelling}`, which is generic: the instantiation set comes from call sites (design §5)")
	if owner["generic"]:
		raise Refused(f"member of `{spelling}`, which is generic: the instantiation set comes from call sites (design §5)")
	return owner, spelling


def settable(rec):
	decl = rec["decl"]
	brace = decl.find("{")
	if brace >= 0:
		return bool(re.search(r"\bset\b", decl[brace:]))
	return bool(re.search(r"\bvar\b", decl))


def effects_of(rec):
	"""('none'|'throws'|'never', the typed error or None)."""
	form, typed = rec["throws"], rec.get("throws_type")
	if form == "throws(E)":
		return ("never", None) if typed == "Never" else ("throws", typed)
	if form in ("throws", "rethrows"):
		return ("throws", None)
	return ("none", None)


def plans(rec, idx, types):
	"""The stubs one symbol becomes: a function is one, a settable property two (get, set)."""
	if rec["group"] == 3:
		raise Refused(f"{rec['cause']}: {CAUSE_TEXT.get(rec['cause'], 'refused by the classifier')}")
	if reprint.is_unavailable(rec):
		raise Refused("unavailable on this platform")
	kind = rec["kind"]
	if kind in reprint.OPERATOR_KINDS:
		raise Refused("operator: §5's call forms have no spelling for one")
	if rec["generics"] or re.search(r"\bSelf\b", rec["decl"]):
		raise Refused("generic: the instantiation set comes from call sites (design §5)")
	if rec["isolation"] not in (None, "MainActor"):
		raise Refused(f"isolation @{rec['isolation']}: only the main actor is hopped to")
	owner, owner_spelling = owner_of(rec, idx)
	receiver = None
	if owner is not None and kind in ("swift.method", "swift.property"):
		receiver = ("box" if owner["kind"] == "swift.struct" else "object", owner_spelling)
	throws, typed = effects_of(rec)
	is_async = rec.get("async", False)
	base = dict(rec=rec, owner=owner_spelling, receiver=receiver, isolation=rec["isolation"], is_async=is_async,
		throws=throws, typed=typed, rethrows=rec["throws"] == "rethrows")

	if kind in reprint.PROPERTY_KINDS:
		name = rec["path"][-1]
		value = types.form("property", rec["slots"][0][1], rec["slot_usrs"][0])
		if value[0] == "closure":
			raise Refused("property of a function type: a closure crosses only as a parameter")
		get = dict(base, role="get", member=name, swift=swift_name(rec), params=[], result=value, mutating=False)
		out = [get]
		if settable(rec):
			# A setter neither throws nor suspends; a struct's is `mutating set`, so it answers the new value.
			out.append(dict(base, role="set", member=name, swift=f"{swift_name(rec)} (set)", throws="none", typed=None,
				is_async=False, params=[(None, value, False)], result=("void",),
				mutating=receiver is not None and receiver[0] == "box"))
		return out

	if kind == "swift.init" and re.search(r"\binit[?!]", rec["decl"]):
		raise Refused("failable initializer: its result is an optional, which no stub converts yet")
	labels = reprint.labels_of(rec["title"]) or []
	params, slots = [], [(s, u) for s, u in zip(rec["slots"], rec["slot_usrs"]) if s[0] == "param"]
	if len(labels) != len(slots):
		raise Refused(f"the title `{rec['title']}` names {len(labels)} labels for {len(slots)} parameters")
	for i, (((_role, text, _bucket), usrs), label) in enumerate(zip(slots, labels)):
		mod = rec["mods"][i] if i < len(rec["mods"]) else ""
		if re.search(r"\b(isolated|sending)\b", mod):
			raise Refused(f"parameter `{text}`: `{mod.strip()}` is not built")
		inout = bool(re.search(r"\binout\b", mod))
		t = re.sub(r"^\s*inout\s+", "", text)
		form = types.form("parameter", t, usrs)
		if inout and form[0] == "closure":
			raise Refused(f"parameter `{text}`: an inout closure has no stub form")
		params.append((None if label == "_" else label, form, inout))
	if kind == "swift.init":
		result = ("box" if owner["kind"] == "swift.struct" else "object", owner_spelling)
	else:
		ret = next(((text, usrs) for (role, text, _b), usrs in zip(rec["slots"], rec["slot_usrs"]) if role == "return"), ("", {}))
		result = types.form("result", ret[0], ret[1])
	mutating = bool(re.search(r"\bmutating\b", rec["decl"])) and receiver is not None and receiver[0] == "box"
	member = "" if kind == "swift.init" else rec["title"].split("(")[0]
	role = "init" if kind == "swift.init" else ("method" if receiver else ("static" if owner else "func"))
	return [dict(base, role=role, member=member, swift=swift_name(rec), params=params, result=result, mutating=mutating)]


def var_base(stub):
	"""What the var is spelled from (SwiftStubs.Function.base): a setter is `setName`, as level 1 spells `setName:`."""
	if stub["role"] == "set":
		m = stub["member"]
		return "set" + m[:1].upper() + m[1:]
	return stub["member"]


def labels_of(stub):
	"""The Clojure call's labels, the receiver first and unlabelled."""
	return ([None] if stub["receiver"] else []) + [label for label, _form, _inout in stub["params"]]


def refuse_clashes(stubs, refused):
	"""Overloads of one var that the labels cannot tell apart are all refused, none picked (design §5)."""
	shapes = {}
	for s in stubs:
		shapes.setdefault((s["owner"], var_base(s), tuple(labels_of(s))), []).append(s)
	keep = []
	for s in stubs:
		clash = shapes[(s["owner"], var_base(s), tuple(labels_of(s)))]
		if len(clash) == 1:
			keep.append(s)
			continue
		others = "; ".join(f"`{o['rec']['decl']}`" for o in clash if o is not s)
		refused.append((s["swift"], f"overload: `{s['rec']['decl']}` has the var and labels of {others}, and labels "
			"cannot tell overloads apart (overloads by type, design §9)"))
	return keep


# ---------------------------------------------------------------------------
# Emission.

def swift_string(s):
	return json.dumps(s, ensure_ascii=False)


def decode(form, arg):
	kind, t = form[0], form[1]
	if kind == "scalar":
		return f"try {t}(decoding: {arg})"
	if kind == "box":
		return f"try Pippin.SwiftStubs.unbox({arg}, as: {t}.self)"
	if kind == "object":
		return f"try Pippin.SwiftStubs.unbox(object: {arg}, as: {t}.self)"
	return f"try {arg}.closure() as {t}"


def encode(form, expr):
	kind = form[0]
	if kind == "scalar":
		return f"{expr}.asValue"
	if kind == "box":
		return f"Pippin.SwiftStubs.box({expr})"
	return f"Pippin.SwiftStubs.box(object: {expr})"


def effects_text(stub):
	words = (["async"] if stub["is_async"] else []) + (["rethrows"] if stub["rethrows"] else
		[f"throws({stub['typed']})"] if stub["typed"] else ["throws"] if stub["throws"] == "throws" else [])
	return " ".join(words)


def emit_function(stub):
	"""The registration of one stub, as Swift: decode where the caller is, call, encode (design §5)."""
	decoded, body = [], []
	k = 0
	if stub["receiver"]:
		decoded.append(f"let d0 = {decode(stub['receiver'], 'args[0]')}")
		k = 1
	for i, (_label, form, _inout) in enumerate(stub["params"]):
		decoded.append(f"let d{k + i} = {decode(form, f'args[{k + i}]')}")
	# What the call mutates is a fresh var inside the call: a closure that may run on another thread captures lets.
	recv = "d0"
	if stub["mutating"]:
		body.append("var recv = d0")
		recv = "recv"
	args = []
	for i, (label, _form, inout) in enumerate(stub["params"]):
		name = f"d{k + i}"
		if inout:
			body.append(f"var a{k + i} = {name}")
			name = f"&a{k + i}"
		args.append(name if label is None else f"{label}: {name}")
	member = stub["member"]
	target = {"init": f"{stub['owner']}", "method": f"{recv}.{member}", "static": f"{stub['owner']}.{member}",
		"func": member, "get": None, "set": None}[stub["role"]]
	prefix = ("try " if stub["throws"] == "throws" else "") + ("await " if stub["is_async"] else "")
	if stub["role"] in ("get", "set"):
		place = f"{recv}.{member}" if stub["receiver"] else (f"{stub['owner']}.{member}" if stub["owner"] else member)
		call = f"{prefix}{place}" if stub["role"] == "get" else f"{place} = {args[0]}"
	else:
		call = f"{prefix}{target}({', '.join(args)})"
	outs = []
	if stub["mutating"]:
		outs.append(encode(stub["receiver"], "recv"))
	for i, (_label, form, inout) in enumerate(stub["params"]):
		if inout:
			outs.append(encode(form, f"a{k + i}"))
	if stub["result"][0] == "void":
		body.append(call)
	else:
		body.append(f"let r = {call}")
		outs.append(encode(stub["result"], "r"))
	if len(outs) == 1:
		body.append(f"return {outs[0]}")
	elif not outs:
		body.append("return Pippin.Value.nil_")
	else:
		body.append(f"return Pippin.SwiftStubs.outcome([{', '.join(outs)}])")

	head = f"Pippin.SwiftStubs.Function(swiftName: {swift_string(stub['swift'])}"
	if stub["owner"]:
		head += f", owner: {swift_string(stub['owner'])}"
	head += f", base: {swift_string(var_base(stub))}"
	head += ", labels: [" + ", ".join("nil" if l is None else swift_string(l) for l in labels_of(stub)) + "]"
	effects = effects_text(stub)
	if effects:
		head += f", effects: {swift_string(effects)}"
	pre = "".join(f"\t\t\t{l}\n" for l in decoded)
	inner = "".join(f"\t\t\t\t{l}\n" for l in body)
	if stub["is_async"]:
		return f"{head}, async: {{ args in\n{pre}\t\t\treturn {{\n{inner}\t\t\t}}\n\t\t}}),\n"
	if stub["isolation"] == "MainActor":
		return f"{head}, mainActor: {{ args in\n{pre}\t\t\treturn {{\n{inner}\t\t\t}}\n\t\t}}),\n"
	flat = "".join(f"\t\t\t{l}\n" for l in body)
	return f"{head}) {{ args in\n{pre}{flat}\t\t}},\n"


def emit(module, generated, refused):
	out = [f"// Generated by scripts/swift-stubgen.py from the symbol graph of {module}; do not edit.\n",
		"import Pippin\n", f"import {module}\n\n",
		f"@_cdecl(\"pippin_stubs_register_{module}\")\n",
		f"public func pippin_stubs_register_{module}() {{\n",
		f"\tPippin.SwiftStubs.register(module: {swift_string(module)}, functions: [\n"]
	lines = {}
	for stub in generated:
		lines[sum(s.count("\n") for s in out) + 1] = stub
		out.append("\t\t" + emit_function(stub))
	out.append("\t], refusals: [\n")
	for name, reason in refused:
		out.append(f"\t\tPippin.SwiftStubs.Refusal(swiftName: {swift_string(name)}, reason: {swift_string(reason)}),\n")
	out.append("\t])\n}\n")
	return "".join(out), lines


def blamed_stub(text_lines, line):
	"""The generated stub whose emission covers a 1-based line of the file."""
	starts = sorted(k for k in text_lines if k <= line)
	return text_lines[starts[-1]] if starts else None


# ---------------------------------------------------------------------------

# Symbols with no call form at all, listed so that the report names every public symbol.
UNCALLABLE = {
	"swift.subscript": "subscript: §5's call forms have no spelling for one",
	"swift.type.subscript": "subscript: §5's call forms have no spelling for one",
	"swift.enum.case": "enum case: an enum crosses as a keyword (design §5), which is not built",
}


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
	types = Types(idx, stored_property_owners(paths, idx))
	recs, refused = [], []
	for p in paths:
		for sym in reprint.iter_symbols(p):
			kind = sym["kind"]["identifier"]
			if any(c.startswith("_") for c in sym["pathComponents"]):
				continue
			if kind in UNCALLABLE:
				refused.append((".".join(sym["pathComponents"]), UNCALLABLE[kind]))
			elif kind in reprint.FUNCLIKE_KINDS | reprint.OPERATOR_KINDS | reprint.PROPERTY_KINDS or kind == "swift.macro":
				recs.append(reprint.symbol_record(sym, idx))

	generated = []
	for rec in sorted(recs, key=swift_name):
		try:
			generated += plans(rec, idx, types)
		except Refused as why:
			refused.append((swift_name(rec), str(why)))
	generated = refuse_clashes(generated, refused)
	refused.sort()

	# One stub swiftc rejects moves to the report with swiftc's words, so it cannot take the module down.
	source = os.path.join(work, "stubs.swift")
	dylib = os.path.join(work, dylib_name)
	while True:
		text, lines = emit(module, generated, refused)
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
			stub = blamed_stub(lines, int(m.group(1)))
			if stub is not None:
				blamed.setdefault(id(stub), (stub, m.group(2)))
		if not blamed:
			shutil.rmtree(work, ignore_errors=True)
			sys.exit(f"swift-stubgen: swiftc failed on the stubs of {module}:\n{res.stderr}")
		for stub, error in blamed.values():
			generated.remove(stub)
			refused.append((stub["swift"], f"swiftc rejected the stub: {error}"))

	report = {
		"module": module, "fingerprint": key, "compiler": compiler, "target": target,
		"generated": [{"swift": s["swift"], "owner": s["owner"], "base": var_base(s), "labels": labels_of(s),
			"isolation": s["isolation"], "effects": effects_text(s)} for s in generated],
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
