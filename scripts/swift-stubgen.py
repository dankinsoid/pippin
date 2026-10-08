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
import collections
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

# What crosses by value, each through Pippin's ValueCodable conformance; a fixed-width number checks its range.
SCALARS = {
	"s:Si": "Int", "s:Sd": "Double", "s:Sb": "Bool", "s:SS": "String", "s:Su": "UInt", "s:Sf": "Float",
	"s:s4Int8V": "Int8", "s:s5Int16V": "Int16", "s:s5Int32V": "Int32", "s:s5Int64V": "Int64",
	"s:s5UInt8V": "UInt8", "s:s6UInt16V": "UInt16", "s:s6UInt32V": "UInt32", "s:s6UInt64V": "UInt64",
	"s:14CoreFoundation7CGFloatV": "CGFloat", "s:14CoreGraphics7CGFloatV": "CGFloat",
	# The core's own values (design §5 «Значения ядра на границе»): inst, uuid, URI.
	"s:10Foundation4DateV": "Date", "s:10Foundation4UUIDV": "UUID", "s:10Foundation3URLV": "URL",
}
OPTIONAL, ARRAY, DICTIONARY, SET = "s:Sq", "s:Sa", "s:SD", "s:Sh"

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


# One part of a symbol refused while the rest is generated: a setter whose value has no way in.
Refusal = collections.namedtuple("Refusal", "swift reason")

# A receiver of these is a value: a mutating member answers its new value.
VALUE_KINDS = ("box", "map", "keyword")


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

def stored_fields(paths, idx):
	"""Every stored instance property of the module's types at any access level, from the private graph: the
	public one cannot tell a struct whose state is all public from one with a private field beside it."""
	fields = {}
	for p in paths:
		for sym in reprint.iter_symbols(p):
			if sym["kind"]["identifier"] != "swift.property":
				continue
			decl = reprint.frag_text(sym.get("declarationFragments", []))
			owner = idx.member_of.get(sym["identifier"]["precise"])
			if "{" in decl or not owner:
				continue
			fields.setdefault(owner, []).append(dict(
				name=sym["pathComponents"][-1], access=sym.get("accessLevel"), decl=decl,
				type=reprint.decl_value_type(decl), usrs=reprint.frag_usrs(sym.get("declarationFragments", [])),
				settable=bool(re.search(r"\bvar\b", decl)) and not re.search(r"\(set\)", decl)))
	return fields


def same_type(a, b):
	return re.sub(r"\s+", "", a) == re.sub(r"\s+", "", b)


class Shapes:
	"""Which of the module's own types cross as data (design §5 «Перечисление кейвордом, структура мапой»): an enum
	without payloads as a keyword, a struct whose stored properties are all public and cross as a map."""

	def __init__(self, idx, fields, cases, inits):
		self.idx = idx
		self.keywords = {}		# enum USR -> case names, sorted
		self.maps = {}			# struct USR -> its fields, sorted by name
		self.inits = inits		# struct USR -> [(labels, param texts, throws)] of its public non-failable inits
		self.back = {}			# map struct USR -> (labels of the init, fields set after it, throws) or None
		self.checking = set()	# map structs a check_decode is inside of: a field may reach its own type
		for usr, cs in cases.items():
			rec = idx.by_usr.get(usr)
			if rec and not rec["generic"] and cs and not any(payload for _n, payload, _p in cs):
				self.keywords[usr] = sorted(n for n, _p, _q in cs)
		for usr, fs in fields.items():
			rec = idx.by_usr.get(usr)
			if (rec and rec["kind"] == "swift.struct" and not rec["generic"] and fs
					and all(f["access"] in ("public", "open") and not re.search(r"\blazy\b", f["decl"]) for f in fs)):
				self.maps[usr] = sorted(fs, key=lambda f: f["name"])

	def settle(self, types):
		"""A struct is a map only when every field crosses; a field may be another map, so this is a fixpoint."""
		changed = True
		while changed:
			changed = False
			for usr, fs in list(self.maps.items()):
				try:
					for f in fs:
						f["form"] = types.parse("field", f["type"], f["usrs"])
				except Refused:
					del self.maps[usr]
					changed = True
		for usr, fs in self.maps.items():
			self.back[usr] = self.way_back(usr, fs)

	def way_back(self, usr, fs):
		"""The public init whose labels are fields of the same types, the rest assigned after it; the widest wins."""
		names = {f["name"]: f for f in fs}
		best = None
		for labels, texts, throws in self.inits.get(usr, []):
			if any(l not in names or not same_type(names[l]["type"], t) for l, t in zip(labels, texts)):
				continue
			rest = [f for f in fs if f["name"] not in labels]
			if any(not f["settable"] for f in rest):
				continue
			if best is None or len(labels) > len(best[0]):
				best = (labels, [f["name"] for f in rest], throws)
		return best

	def demote(self, usr):
		self.keywords.pop(usr, None)
		self.maps.pop(usr, None)
		self.back.pop(usr, None)


class Form:
	"""How one type crosses: Swift expressions that decode a `Value` into it and encode it back (design §5).

	A composite form is built from its parts' forms, so an element crosses inside a collection exactly as it
	would alone. `x` is a Swift expression; a nested form's expression sees its own closure's `$0`."""
	kind = None

	def decode(self, x):
		raise NotImplementedError

	def encode(self, x):
		raise NotImplementedError

	def check_decode(self):
		"""Refuses a form that can cross out but not in: a map struct with no public way back from a map."""
		for part in self.parts():
			part.check_decode()

	def parts(self):
		return []


class Keyword(Form):
	"""An enum without payloads: one generated pair of functions per type."""
	kind = "keyword"

	def __init__(self, t):
		self.t = t

	def decode(self, x):
		return f"try dec_{ident(self.t)}({x})"

	def encode(self, x):
		return f"enc_{ident(self.t)}({x})"


class MapStruct(Form):
	kind = "map"

	def __init__(self, t, usr, shapes):
		self.t, self.usr, self.shapes = t, usr, shapes

	def decode(self, x):
		return f"try dec_{ident(self.t)}({x})"

	def encode(self, x):
		return f"enc_{ident(self.t)}({x})"

	def check_decode(self):
		if self.usr in self.shapes.checking:
			return
		if self.shapes.back.get(self.usr) is None:
			raise Refused(f"`{self.t}` crosses out as a map but has no public init taking its stored properties "
				"by name, so no map comes back as one (design §5 «Структуры несимметричны»)")
		self.shapes.checking.add(self.usr)
		try:
			for f in self.shapes.maps[self.usr]:
				f["form"].check_decode()
		finally:
			self.shapes.checking.discard(self.usr)


def ident(spelling):
	return spelling.replace(".", "_")


class Void(Form):
	kind = "void"


class Scalar(Form):
	kind = "scalar"

	def __init__(self, t):
		self.t = t

	def decode(self, x):
		return f"try {self.t}(decoding: {x})"

	def encode(self, x):
		return f"{x}.asValue"


class Box(Form):
	kind = "box"

	def __init__(self, t):
		self.t = t

	def decode(self, x):
		return f"try Pippin.SwiftStubs.unbox({x}, as: {self.t}.self)"

	def encode(self, x):
		return f"Pippin.SwiftStubs.box({x})"


class Object(Form):
	kind = "object"

	def __init__(self, t):
		self.t = t

	def decode(self, x):
		return f"try Pippin.SwiftStubs.unbox(object: {x}, as: {self.t}.self)"

	def encode(self, x):
		return f"Pippin.SwiftStubs.box(object: {x})"


class Closure(Form):
	kind = "closure"

	def __init__(self, t):
		self.t = t

	def decode(self, x):
		return f"try {x}.closure() as {self.t}"


class Optional(Form):
	kind = "optional"

	def __init__(self, inner):
		self.inner = inner

	def parts(self):
		return [self.inner]

	def decode(self, x):
		return f"try Pippin.SwiftStubs.optional({x}) {{ {self.inner.decode('$0')} }}"

	def encode(self, x):
		return f"Pippin.SwiftStubs.optional({x}) {{ {self.inner.encode('$0')} }}"


class Array(Form):
	kind = "array"

	def __init__(self, element):
		self.element = element

	def parts(self):
		return [self.element]

	def decode(self, x):
		return f"try Pippin.SwiftStubs.array({x}) {{ {self.element.decode('$0')} }}"

	def encode(self, x):
		return f"Pippin.SwiftStubs.vector({x}) {{ {self.element.encode('$0')} }}"


class Set(Form):
	kind = "set"

	def __init__(self, element):
		self.element = element

	def parts(self):
		return [self.element]

	def decode(self, x):
		return f"try Pippin.SwiftStubs.set({x}) {{ {self.element.decode('$0')} }}"

	def encode(self, x):
		return f"Pippin.SwiftStubs.hashSet({x}) {{ {self.element.encode('$0')} }}"


class Dictionary(Form):
	kind = "dictionary"

	def __init__(self, key, value):
		self.key, self.value = key, value

	def parts(self):
		return [self.key, self.value]

	def decode(self, x):
		return (f"try Pippin.SwiftStubs.dictionary({x}, key: {{ {self.key.decode('$0')} }}, "
			f"value: {{ {self.value.decode('$0')} }})")

	def encode(self, x):
		return (f"Pippin.SwiftStubs.map({x}, key: {{ {self.key.encode('$0')} }}, "
			f"value: {{ {self.value.encode('$0')} }})")


class Tuple(Form):
	kind = "tuple"

	def __init__(self, elements):
		self.elements = elements

	def parts(self):
		return self.elements

	def decode(self, x):
		parts = ", ".join(e.decode(f"t[{i}]") for i, e in enumerate(self.elements))
		return f"try Pippin.SwiftStubs.tuple({x}, count: {len(self.elements)}) {{ t in ({parts}) }}"

	def encode(self, x):
		parts = ", ".join(e.encode(f"t.{i}") for i, e in enumerate(self.elements))
		return f"Pippin.SwiftStubs.vector(tuple: {x}) {{ t in [{parts}] }}"


class Types:
	"""How a slot's type crosses, as a Form; a type no form fits is a refusal naming it."""

	def __init__(self, idx, shapes):
		self.idx = idx
		self.shapes = shapes

	def nominal(self, usr, spelling, what):
		if usr in SCALARS:
			return Scalar(spelling)
		rec = self.idx.by_usr.get(usr)
		if rec and rec["kind"] in ("swift.struct", "swift.class", "swift.enum") and rec["generic"]:
			raise Refused(f"{what} `{spelling}` is generic: the instantiation set comes from call sites (design §5)")
		if rec:
			return self.of_type(rec, spelling)
		return None

	def of_type(self, rec, spelling):
		"""The form of a type of this module, by what it is and what Shapes made of it."""
		usr, kind = rec["usr"], rec["kind"]
		if kind == "swift.struct":
			return MapStruct(spelling, usr, self.shapes) if usr in self.shapes.maps else Box(spelling)
		if kind == "swift.enum":
			return Keyword(spelling) if usr in self.shapes.keywords else Box(spelling)
		if kind == "swift.class":
			return Object(spelling)
		return None

	def form(self, role, text, usrs):
		t = NEUTRAL_MODIFIERS.sub("", text.strip()).strip()
		if role == "result" and t in ("", "Void", "()"):
			return Void()
		return self.parse(role, t, usrs)

	def parse(self, role, t, usrs):
		"""role: what the refusal calls the slot; only a parameter, or an optional one, takes a closure."""
		t = t.strip()
		if reprint._find_top(t, "->") >= 0:
			if role != "parameter":
				raise Refused(f"{role} `{t}` is a closure, which crosses only as a parameter")
			return self.closure(t, usrs)
		if t.endswith(("?", "!")):
			return self.optional(role, t, t[:-1], usrs)
		if t.startswith("(") and t.endswith(")") and reprint._balanced(t[1:-1]):
			parts = [p.strip() for p in reprint._split_top(t[1:-1], [","])]
			if len(parts) == 1:
				return self.parse(role, parts[0], usrs)
			if any(reprint._find_top(p, ":") >= 0 for p in parts):
				raise Refused(f"{role} `{t}` is a labelled tuple, whose form is a map by its labels (design §5), "
					"which is not built")
			return Tuple([self.parse("tuple element", p, usrs) for p in parts])
		if t.startswith("[") and t.endswith("]") and reprint._balanced(t[1:-1]):
			parts = reprint._split_top(t[1:-1], [":"])
			if len(parts) == 2:
				return Dictionary(self.parse("dictionary key", parts[0], usrs), self.parse("dictionary value", parts[1], usrs))
			return Array(self.parse("array element", t[1:-1], usrs))
		m = re.match(r"^([\w.]+)\s*<(.*)>$", t, re.S)
		if m and reprint._balanced(m.group(2)):
			head = usrs.get(m.group(1)) or usrs.get(m.group(1).split(".")[-1])
			args = [a.strip() for a in reprint._split_top(m.group(2), [","])]
			if head == OPTIONAL and len(args) == 1:
				return self.optional(role, t, args[0], usrs)
			if head == ARRAY and len(args) == 1:
				return Array(self.parse("array element", args[0], usrs))
			if head == SET and len(args) == 1:
				return Set(self.parse("set element", args[0], usrs))
			if head == DICTIONARY and len(args) == 2:
				return Dictionary(self.parse("dictionary key", args[0], usrs), self.parse("dictionary value", args[1], usrs))
		form = self.nominal(usrs.get(t) or usrs.get(t.split(".")[-1]), t, role)
		if form:
			return form
		what = reprint.classify_type(t, usrs, None, self.idx.alias_closures)
		raise Refused(f"{role} `{t}` crosses as {what}, which no stub converts yet")

	def optional(self, role, t, inner, usrs):
		# Clojure has one nil, so `.some(nil)` and `nil` of a double optional would be one value.
		form = self.parse(role, inner, usrs)
		if isinstance(form, Optional):
			raise Refused(f"{role} `{t}` is an optional of an optional, whose two nils Clojure cannot tell apart")
		return Optional(form)

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
		return Closure(f"@Sendable ({', '.join(parts)}) throws -> {result}")


def swift_name(rec):
	return ".".join(rec["path"])


def owner_of(rec, idx, types):
	"""(owner record, spelling) of a member of a type of this module, or (None, None) for a free symbol."""
	path = rec["path"]
	if len(path) == 1:
		return None, None
	owner = idx.by_path.get(path[:-1])
	if owner is None:
		raise Refused("member of a type of another module: no stub reaches it")
	kind = owner["kind"]
	spelling = ".".join(path[:-1])
	if kind == "swift.protocol":
		raise Refused("protocol member: generic over the conformer, whose instantiations come from call sites (design §5)")
	if kind == "swift.actor":
		raise Refused("actor member: isolation to an actor instance is not built")
	if kind not in ("swift.struct", "swift.class", "swift.enum"):
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
	owner, owner_spelling = owner_of(rec, idx, types)
	receiver = None
	if owner is not None and kind in ("swift.method", "swift.property"):
		receiver = types.of_type(owner, owner_spelling)
	throws, typed = effects_of(rec)
	is_async = rec.get("async", False)
	base = dict(rec=rec, owner=owner_spelling, receiver=receiver, isolation=rec["isolation"], is_async=is_async,
		throws=throws, typed=typed, rethrows=rec["throws"] == "rethrows")

	if receiver is not None:
		receiver.check_decode()
	value_receiver = receiver is not None and receiver.kind in VALUE_KINDS

	if kind in reprint.PROPERTY_KINDS:
		name = rec["path"][-1]
		value = types.form("property", rec["slots"][0][1], rec["slot_usrs"][0])
		get = dict(base, role="get", member=name, swift=swift_name(rec), params=[], result=value, mutating=False)
		out = [get]
		if settable(rec):
			# A setter neither throws nor suspends; a value type's is `mutating set`, so it answers the new value.
			try:
				value.check_decode()
				out.append(dict(base, role="set", member=name, swift=f"{swift_name(rec)} (set)", throws="none",
					typed=None, is_async=False, params=[(None, value, False)], result=Void(), mutating=value_receiver))
			except Refused as why:
				out.append(Refusal(f"{swift_name(rec)} (set)", str(why)))
		return out

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
		if inout and reprint._find_top(t, "->") >= 0:
			raise Refused(f"parameter `{text}`: an inout closure has no stub form")
		form.check_decode()
		params.append((None if label == "_" else label, form, inout))
	if kind == "swift.init":
		result = types.of_type(owner, owner_spelling)
		if re.search(r"\binit[?!]", rec["decl"]):
			result = Optional(result)
	else:
		ret = next(((text, usrs) for (role, text, _b), usrs in zip(rec["slots"], rec["slot_usrs"]) if role == "return"), ("", {}))
		result = types.form("result", ret[0], ret[1])
	mutating = bool(re.search(r"\bmutating\b", rec["decl"])) and value_receiver
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
	return form.decode(arg)


def encode(form, expr):
	return form.encode(expr)


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
	if stub["result"].kind == "void":
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


def emit_keyword(module, t, names):
	n = ident(t)
	out = [f"private let keys_{n} = Pippin.SwiftStubs.Keys(type: {swift_string(f'{module}.{t}')}, "
		f"[{', '.join(swift_string(c) for c in names)}])\n\n",
		f"private func enc_{n}(_ v: {t}) -> Pippin.Value {{\n\tswitch v {{\n"]
	out += [f"\tcase .`{c}`: return keys_{n}[{i}]\n" for i, c in enumerate(names)]
	out.append(f"\t}}\n}}\n\nprivate func dec_{n}(_ v: Pippin.Value) throws -> {t} {{\n\tswitch try keys_{n}.index(of: v) {{\n")
	out += [f"\tcase {i}: return .`{c}`\n" for i, c in enumerate(names)]
	out.append("\tdefault: preconditionFailure(\"an index past the cases\")\n\t}\n}\n\n")
	return "".join(out)


def emit_map(module, t, fields, back):
	n = ident(t)
	out = [f"private let keys_{n} = Pippin.SwiftStubs.Keys(type: {swift_string(f'{module}.{t}')}, "
		f"[{', '.join(swift_string(f['name']) for f in fields)}])\n\n",
		f"private func enc_{n}(_ v: {t}) -> Pippin.Value {{\n",
		f"\treturn keys_{n}.map([{', '.join(f['form'].encode('v.`' + f['name'] + '`') for f in fields)}])\n}}\n\n"]
	if back is not None:
		labels, rest, throws = back
		required = ", ".join("false" if isinstance(f["form"], Optional) else "true" for f in fields)
		out.append(f"private func dec_{n}(_ v: Pippin.Value) throws -> {t} {{\n"
			f"\tlet f = try keys_{n}.fields(of: v, required: [{required}])\n")
		index = {f["name"]: i for i, f in enumerate(fields)}
		out += [f"\tlet a{i} = {f['form'].decode(f'f[{i}]')}\n" for i, f in enumerate(fields)]
		call = f"{t}({', '.join(f'{l}: a{index[l]}' for l in labels)})"
		out.append(f"\t{'var' if rest else 'let'} s = {'try ' if throws else ''}{call}\n")
		out += [f"\ts.`{name}` = a{index[name]}\n" for name in rest]
		out.append("\treturn s\n}\n\n")
	return "".join(out)


def emit(module, generated, refused, shapes, idx):
	"""The stub file and which stub or type each line belongs to, so swiftc's error can be blamed on it."""
	out = [f"// Generated by scripts/swift-stubgen.py from the symbol graph of {module}; do not edit.\n",
		"import Foundation\nimport Pippin\n", f"import {module}\n\n"]
	lines = {}

	def at():
		return sum(s.count("\n") for s in out) + 1

	for usr, names in sorted(shapes.keywords.items()):
		lines[at()] = ("type", usr)
		out.append(emit_keyword(module, ".".join(idx.by_usr[usr]["path"]), names))
	for usr, fields in sorted(shapes.maps.items()):
		lines[at()] = ("type", usr)
		out.append(emit_map(module, ".".join(idx.by_usr[usr]["path"]), fields, shapes.back.get(usr)))
	lines[at()] = None
	out += [f"@_cdecl(\"pippin_stubs_register_{module}\")\n",
		f"public func pippin_stubs_register_{module}() {{\n",
		f"\tPippin.SwiftStubs.register(module: {swift_string(module)}, functions: [\n"]
	for stub in generated:
		lines[at()] = stub
		out.append("\t\t" + emit_function(stub))
	lines[at()] = None
	out.append("\t], refusals: [\n")
	for name, reason in refused:
		out.append(f"\t\tPippin.SwiftStubs.Refusal(swiftName: {swift_string(name)}, reason: {swift_string(reason)}),\n")
	out.append("\t])\n}\n")
	return "".join(out), lines


def blamed_stub(text_lines, line):
	"""The generated stub, or ("type", usr), whose emission covers a 1-based line of the file."""
	starts = sorted(k for k in text_lines if k <= line)
	return text_lines[starts[-1]] if starts else None


# ---------------------------------------------------------------------------

# Symbols with no call form at all, listed so that the report names every public symbol.
UNCALLABLE = {
	"swift.subscript": "subscript: §5's call forms have no spelling for one",
	"swift.type.subscript": "subscript: §5's call forms have no spelling for one",
}


def build(recs, idx, types, shapes, cases, refused_before):
	"""The stubs and the refusals for the current Shapes; a type swiftc rejected has been demoted already."""
	refused = list(refused_before)
	for usr, cs in cases.items():
		if usr in shapes.keywords:
			continue
		for name, payload, path in cs:
			refused.append((path, "enum case with a payload: its enum crosses as a box, and a case has no constructor "
				"yet (design §5)" if payload else "enum case: its enum crosses as a box (see the type's refusal)"))
	generated = []
	for rec in sorted(recs, key=swift_name):
		try:
			for item in plans(rec, idx, types):
				if isinstance(item, Refusal):
					refused.append(tuple(item))
				else:
					generated.append(item)
		except Refused as why:
			refused.append((swift_name(rec), str(why)))
	generated = refuse_clashes(generated, refused)
	refused.sort()
	return generated, refused


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
	private_dir = os.path.join(work, "graph-private")
	reprint.extract(module, target, sdk, private_dir, os.path.dirname(module_file), progress, access="private")
	private_paths = reprint.graph_files(private_dir)
	fields = stored_fields(private_paths, reprint.scan_types(private_paths, lambda _m: None))

	recs, uncallable, cases = [], [], {}
	for p in paths:
		for sym in reprint.iter_symbols(p):
			kind = sym["kind"]["identifier"]
			path = sym["pathComponents"]
			if any(c.startswith("_") for c in path):
				continue
			if kind == "swift.enum.case":
				decl = reprint.frag_text(sym.get("declarationFragments", []))
				owner = idx.member_of.get(sym["identifier"]["precise"])
				cases.setdefault(owner, []).append((path[-1].split("(")[0], "(" in decl, ".".join(path)))
			elif kind in UNCALLABLE:
				uncallable.append((".".join(path), UNCALLABLE[kind]))
			elif kind in reprint.FUNCLIKE_KINDS | reprint.OPERATOR_KINDS | reprint.PROPERTY_KINDS or kind == "swift.macro":
				recs.append(reprint.symbol_record(sym, idx))
	inits = {}
	for rec in recs:
		owner = idx.by_path.get(rec["path"][:-1])
		labels = reprint.labels_of(rec["title"]) or []
		if (rec["kind"] != "swift.init" or owner is None or owner["kind"] != "swift.struct" or "_" in labels
				or re.search(r"\binit[?!]", rec["decl"]) or rec.get("async")):
			continue
		texts = [text for role, text, _b in rec["slots"] if role == "param"]
		inits.setdefault(owner["usr"], []).append((labels, texts, rec["throws"] != "none"))
	shapes = Shapes(idx, fields, cases, inits)
	types = Types(idx, shapes)
	shapes.settle(types)

	# What swiftc rejects goes to the report, a rejected type falls back to a box: one symbol never takes the module down.
	source = os.path.join(work, "stubs.swift")
	dylib = os.path.join(work, dylib_name)
	rejected = list(uncallable)
	while True:
		generated, refused = build(recs, idx, types, shapes, cases, rejected)
		text, lines = emit(module, generated, refused, shapes, idx)
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
			owner = blamed_stub(lines, int(m.group(1)))
			if owner is not None:
				blamed.setdefault(owner[1] if isinstance(owner, tuple) else id(owner), (owner, m.group(2)))
		if not blamed:
			shutil.rmtree(work, ignore_errors=True)
			sys.exit(f"swift-stubgen: swiftc failed on the stubs of {module}:\n{res.stderr}")
		for owner, error in blamed.values():
			if isinstance(owner, tuple):
				name = ".".join(idx.by_usr[owner[1]]["path"])
				shapes.demote(owner[1])
				rejected.append((name, f"swiftc rejected its keyword or map form, so it crosses as a box: {error}"))
			else:
				rejected.append((owner["swift"], f"swiftc rejected the stub: {error}"))
		shapes.settle(types)

	report = {
		"module": module, "fingerprint": key, "compiler": compiler, "target": target,
		"generated": [{"swift": s["swift"], "owner": s["owner"], "base": var_base(s), "labels": labels_of(s),
			"isolation": s["isolation"], "effects": effects_text(s)} for s in generated],
		"keywords": sorted(".".join(idx.by_usr[u]["path"]) for u in shapes.keywords),
		"maps": sorted(".".join(idx.by_usr[u]["path"]) for u in shapes.maps),
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


