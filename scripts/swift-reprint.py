#!/usr/bin/env python3
# @ai-generated(solo)
"""Measure what share of a Swift module's public API reprints into a compilable stub.

The experiment of design.md §10 step 8: by declarations, without an application. Reads
`swift-symbolgraph-extract` output, classifies every parameter and return type into §5's four
buckets (value / collection of values / closure / opaque handle), places each function-like symbol
into one of three groups (crosses as data / crosses as a handle / refused; 1 and 2 both cross),
counts the tail by cause, and then checks the classification by emitting real stubs for a sample and
running swiftc on them.

Python, not Swift: the input is a 450 MB JSON document and the work is data munging over it. A Swift
version would need a package target, a build, and a JSON decoding layer for a schema that exists only
to be counted once — and the script must stay runnable without building the runtime at all.

Usage:
	scripts/swift-reprint.py --work DIR --out docs/swift-reprint.md \\
		Foundation SwiftUI \\
		--spm https://github.com/apple/swift-collections.git=1.1.0=OrderedCollections

A bare NAME is extracted from the SDK. NAME=DIR extracts with -I DIR. --spm URL=VERSION=PRODUCT
collects into one throwaway SwiftPM package that the script writes and builds under --work.
"""

import argparse
import collections
import concurrent.futures
import glob
import json
import os
import platform
import random
import re
import shutil
import subprocess
import sys

# §5's four buckets; keyed by USR, not by name, because a module may declare its own `Date`.
VALUE_USRS = {
	"s:Sb": "Bool",
	"s:SS": "String",
	"s:Ss": "Substring",
	"s:SJ": "Character",
	"s:Si": "Int", "s:Su": "UInt",
	"s:Sd": "Double", "s:Sf": "Float",
	"s:s4Int8V": "Int8", "s:s5Int16V": "Int16", "s:s5Int32V": "Int32", "s:s5Int64V": "Int64",
	"s:s5UInt8V": "UInt8", "s:s6UInt16V": "UInt16", "s:s6UInt32V": "UInt32", "s:s6UInt64V": "UInt64",
	"s:s7Float16V": "Float16", "s:s7Float80V": "Float80",
	"s:14CoreFoundation7CGFloatV": "CGFloat", "s:14CoreGraphics7CGFloatV": "CGFloat",
	"c:@SA@NSDecimal": "Decimal", "c:@T@NSTimeInterval": "TimeInterval",
	"s:10Foundation4DataV": "Data",
	"s:10Foundation4DateV": "Date",
	"s:s5NeverO": "Never",
}

# Types named in the sensitivity line of the report: plausible additions to the value set.
SENSITIVITY_USRS = {
	"s:10Foundation3URLV": "URL",
	"s:10Foundation4UUIDV": "UUID",
	"s:10Foundation15AttributedStringV": "AttributedString",
	"s:10Foundation8CalendarV": "Calendar",
	"s:10Foundation6LocaleV": "Locale",
	"s:10Foundation8TimeZoneV": "TimeZone",
	"s:10Foundation11IndexPathV": "IndexPath",
	"s:s11AnyHashableV": "AnyHashable",
}

COLLECTION_HEADS = {
	"s:Sa": 1,			# Array
	"s:Sh": 1,			# Set
	"s:SD": 2,			# Dictionary
	"s:s15ContiguousArrayV": 1,
	"s:s10ArraySliceV": 1,
}

BUCKET_VALUE = "value"
BUCKET_COLLECTION = "collection"
BUCKET_CLOSURE = "closure"
BUCKET_HANDLE = "handle"
BUCKET_GENERIC = "generic"		# a type parameter: the bucket is whatever the call site instantiates
CROSSABLE = {BUCKET_VALUE, BUCKET_COLLECTION, BUCKET_CLOSURE}

# The SwiftPM probe builds for the host, so the SDK's graphs and the typecheck use the host's architecture too.
HOST_TARGET = f"{platform.machine()}-apple-macosx15.0"

FUNCLIKE_KINDS = {"swift.method", "swift.type.method", "swift.init", "swift.func"}
OPERATOR_KINDS = {"swift.func.op"}
PROPERTY_KINDS = {"swift.property", "swift.type.property", "swift.var"}
TYPE_KINDS = {"swift.struct", "swift.class", "swift.enum", "swift.protocol", "swift.actor", "swift.typealias"}

# Causes of group 3, in the order a symbol is attributed to one when it has several.
CAUSE_ORDER = [
	"macro",
	"parameter-pack",
	"noncopyable",
	"autoclosure",
	"property-wrapper-as-api",
	"variadic-parameter",
]

# Detected and reported, but not a refusal: the criterion is that a value crosses back, and an
# existential does (design §5, "Экзистенциалы оттуда же ушли").
INFORMATIONAL_CAUSES = ["self-requirement-protocol"]


# ---------------------------------------------------------------------------
# Reading the graph.

def _iter_array(text, key):
	start = text.index(key)
	dec = json.JSONDecoder()
	i = text.index("[", start) + 1
	n = len(text)
	while True:
		while i < n and text[i] in " \t\r\n,":
			i += 1
		if i >= n or text[i] == "]":
			return
		obj, i = dec.raw_decode(text, i)
		yield obj


def iter_graph(path, want_relationships=False):
	"""Stream the graph. A 450 MB document parsed whole costs several GB; raw_decode over the arrays
	keeps one element alive at a time. Yields ("sym", obj) then ("rel", obj)."""
	with open(path, "rb") as f:
		text = f.read().decode("utf-8")
	for obj in _iter_array(text, '"symbols":['):
		yield "sym", obj
	if want_relationships and '"relationships":[' in text:
		for obj in _iter_array(text, '"relationships":['):
			yield "rel", obj


def iter_symbols(path):
	for tag, obj in iter_graph(path):
		if tag == "sym":
			yield obj


def graph_files(directory):
	return sorted(os.path.join(directory, f) for f in os.listdir(directory) if f.endswith(".symbols.json"))


def graph_metadata(path):
	with open(path, "rb") as f:
		head = f.read(4096).decode("utf-8", "replace")
	m = re.search(r'"generator":"([^"]*)"', head)
	gen = m.group(1) if m else "?"
	m = re.search(r'"operatingSystem":\{"name":"([^"]*)","minimumVersion":\{"major":(\d+),"minor":(\d+)', head)
	plat = f"{m.group(1)} {m.group(2)}.{m.group(3)}" if m else "?"
	return gen, plat


# ---------------------------------------------------------------------------
# Fragments.

def frag_text(frags):
	return "".join(f["spelling"] for f in frags)


def frag_usrs(frags):
	"""spelling -> USR for the type identifiers of one expression."""
	out = {}
	for f in frags:
		if f.get("kind") in ("typeIdentifier", "typeIdentifier.reference") and "preciseIdentifier" in f:
			out.setdefault(f["spelling"], f["preciseIdentifier"])
	return out


BUILDER_ATTR = re.compile(r"^@\w*Builder$")


def param_attrs(param):
	"""(attribute spellings, the fragments after them) for one parameter.

	Attributes come before the label, so a scan for the label has to start past them."""
	frags = param.get("declarationFragments", [])
	attrs, cur, i = [], "", 0
	while i < len(frags):
		f = frags[i]
		if f["kind"] == "attribute":
			cur += f["spelling"]
			i += 1
			continue
		if cur:
			attrs.append(cur)
			cur = ""
			if f["kind"] == "text" and not f["spelling"].strip():
				i += 1
				continue
		break
	if cur:
		attrs.append(cur)
	return attrs, frags[i:]


def builder_attrs(param):
	return [a for a in param_attrs(param)[0] if BUILDER_ATTR.match(a)]


def param_type_frags(param):
	"""Drop the label and any result-builder attribute, leaving the type expression.

	A builder attribute transforms a closure *literal* written at the call site and says nothing about
	the type a stub passes: `@ViewBuilder content: () -> C` accepts any `() -> C` value."""
	attrs, frags = param_attrs(param)
	keep = [{"kind": "attribute", "spelling": a + " "} for a in attrs if not BUILDER_ATTR.match(a)]
	for i, f in enumerate(frags):
		if f["kind"] == "text" and ":" in f["spelling"]:
			rest = f["spelling"].split(":", 1)[1]
			tail = frags[i + 1:]
			if rest.strip():
				tail = [{"kind": "text", "spelling": rest}] + tail
			return keep + tail
		if f["kind"] not in ("identifier", "externalParam", "internalParam", "text"):
			break
	return keep + frags


def _param_span(decl):
	"""The indices of the parentheses around a declaration's parameter list, past its generic parameters."""
	angle = 0
	open_at = -1
	for i, c in enumerate(decl):
		if c == "<":
			angle += 1
		elif c == ">":
			angle -= 1
		elif c == "(" and angle == 0:
			open_at = i
			break
	if open_at < 0:
		return None
	depth = 0
	for i in range(open_at, len(decl)):
		if decl[i] in "([<":
			depth += 1
		elif _closes(decl, i):
			depth -= 1
			if depth == 0:
				return open_at, i
	return None


def decl_param_list(decl):
	"""The parameter list of a declaration, split at top-level commas, each as (label, type).

	`functionSignature` loses ownership modifiers — `hash(into hasher: inout Hasher)` arrives there as
	plain `Hasher` — so a stub that wants to call the symbol has to read them off the declaration."""
	span = _param_span(decl)
	if span is None:
		return []
	inner = decl[span[0] + 1:span[1]]
	if not inner.strip():
		return []
	out = []
	for part in _split_top(inner, [","]):
		head, sep, ty = part.partition(":")
		if not sep:
			out.append(("_", part.strip()))
		else:
			out.append((head.strip().split()[0] if head.strip() else "_", ty.strip()))
	return out


def decl_value_type(decl):
	"""The type of a `var`/`let` declaration, without its accessor block."""
	i = _find_top(decl, ":")
	if i < 0:
		return ""
	ty = decl[i + 1:]
	j = _find_top(ty, "{")
	if j >= 0:
		ty = ty[:j]
	return ty.strip()


# Enough structure for §5's switch, not a parser for Swift types.

def _split_top(s, seps):
	"""Split on separators that are not inside brackets."""
	out, depth, last = [], 0, 0
	i = 0
	while i < len(s):
		c = s[i]
		if c in "([<{":
			depth += 1
		elif _closes(s, i):
			depth -= 1
		elif depth == 0:
			for sep in seps:
				if s.startswith(sep, i):
					out.append(s[last:i])
					i += len(sep)
					last = i
					break
			else:
				i += 1
				continue
			continue
		i += 1
	out.append(s[last:])
	return out


def _find_top(s, needle):
	depth = 0
	i = 0
	while i < len(s):
		c = s[i]
		if depth == 0 and s.startswith(needle, i):
			return i
		if c in "([<{":
			depth += 1
		elif _closes(s, i):
			depth -= 1
		i += 1
	return -1


def _balanced(s):
	depth = 0
	for i, c in enumerate(s):
		if c in "([<{":
			depth += 1
		elif _closes(s, i):
			depth -= 1
			if depth < 0:
				return False
	return depth == 0


def _closes(s, i):
	"""A closing bracket, which the `>` of an arrow `->` is not."""
	return s[i] in ")]>}" and not (s[i] == ">" and i > 0 and s[i - 1] == "-")


LEADING_MODIFIERS = re.compile(
	r"^\s*(?:@\w+(?:\([^)]*\))?\s+|inout\s+|__owned\s+|__shared\s+|isolated\s+|borrowing\s+|consuming\s+|sending\s+|repeat\s+|each\s+)+")


def classify_type(text, usrs, value_usrs=None, aliases=frozenset()):
	"""Bucket one type expression."""
	value_usrs = VALUE_USRS if value_usrs is None else value_usrs
	s = text.strip()
	s = LEADING_MODIFIERS.sub("", s).strip()
	if not s:
		return BUCKET_VALUE			# an empty return list is Void
	if _find_top(s, "->") >= 0:
		return BUCKET_CLOSURE
	# A typealias can hide a function type: the printed parameter is a nominal name.
	if usrs.get(s.strip().rstrip("?!")) in aliases:
		return BUCKET_CLOSURE
	# Optional and IUO are transparent: §5 maps Optional to nil.
	while s.endswith("?") or s.endswith("!"):
		s = s[:-1].strip()
		s = LEADING_MODIFIERS.sub("", s).strip()
	while s.endswith("..."):			# a variadic parameter is an array of its element
		s = s[:-3].strip()
	if s in ("Void", "()", "( )"):
		return BUCKET_VALUE
	if s.startswith("(") and s.endswith(")") and _balanced(s[1:-1]):
		inner = s[1:-1]
		parts = _split_top(inner, [","])
		if len(parts) == 1:
			return classify_type(inner, usrs, value_usrs, aliases)
		return BUCKET_HANDLE		# a tuple: §5 has no bucket for it
	if s.startswith("[") and s.endswith("]") and _balanced(s[1:-1]):
		inner = s[1:-1]
		parts = _split_top(inner, [":"])
		comps = parts if len(parts) == 2 else [inner]
		return _collection_of(comps, usrs, value_usrs, aliases)
	if s.startswith("some ") or s.startswith("any ") or len(_split_top(s, ["&"])) > 1:
		return BUCKET_HANDLE
	m = re.match(r"^([\w.]+)\s*<(.*)>$", s, re.S)
	if m and _balanced(m.group(2)):
		head, args = m.group(1), m.group(2)
		usr = usrs.get(head.split(".")[-1])
		if usr in COLLECTION_HEADS:
			return _collection_of(_split_top(args, [","]), usrs, value_usrs, aliases)
		return BUCKET_HANDLE
	base = s.split(".")[-1] if s.endswith((".Type", ".Protocol")) else s
	if base.endswith(".Type") or base.endswith(".Protocol"):
		return BUCKET_HANDLE
	usr = usrs.get(s) or usrs.get(s.split(".")[-1])
	if usr in value_usrs:
		return BUCKET_VALUE
	if usr is None and re.match(r"^[A-Za-z_]\w*$", s):
		# No USR on the leaf: a generic parameter or `Self`. The call site decides (§5's generics rule).
		return BUCKET_GENERIC
	if s == "Self" or s.startswith("Self."):
		return BUCKET_GENERIC
	return BUCKET_HANDLE


def _collection_of(components, usrs, value_usrs, aliases=frozenset()):
	for c in components:
		b = classify_type(c, usrs, value_usrs, aliases)
		if b not in (BUCKET_VALUE, BUCKET_COLLECTION):
			return BUCKET_HANDLE
	return BUCKET_COLLECTION


# ---------------------------------------------------------------------------
# Pass 1: the type index.

class TypeIndex:
	def __init__(self):
		self.by_path = {}			# ("SwiftUI","Text") -> record
		self.by_usr = {}
		self.noncopyable = set()
		self.propwrapper = set()
		self.no_public_init = set()	# structs, filled after the scan
		self.proto_selfreq = set()	# protocols with Self requirements and no primary associated types
		self._struct_usrs = {}
		self._has_init = set()
		self._proto_members = collections.defaultdict(list)
		self._proto_assoc = set()
		self._proto_primary = set()
		self.conformers = collections.defaultdict(list)		# protocol USR -> concrete type USRs
		self.alias_closures = set()		# typealiases whose right-hand side is a function type
		self.member_of = {}		# member USR -> owning type USR, the only link to a type outside the module


def scan_types(paths, progress):
	idx = TypeIndex()
	for p in paths:
		progress(f"  index {os.path.basename(p)}")
		for tag, sym in iter_graph(p, want_relationships=True):
			if tag == "rel":
				if sym["kind"] == "conformsTo":
					idx.conformers[sym["target"]].append(sym["source"])
				elif sym["kind"] == "memberOf":
					idx.member_of[sym["source"]] = sym["target"]
				continue
			kind = sym["kind"]["identifier"]
			path = tuple(sym["pathComponents"])
			usr = sym["identifier"]["precise"]
			decl = frag_text(sym.get("declarationFragments", []))
			if kind in TYPE_KINDS:
				rec = {
					"kind": kind, "usr": usr, "path": path, "decl": decl,
					"generic": any(f["kind"] == "genericParameter" for f in sym.get("declarationFragments", [])),
					"isolation": isolation_of(decl),
					"gen": sym.get("swiftGenerics") or {},
				}
				idx.by_path[path] = rec
				idx.by_usr[usr] = rec
				if "~Copyable" in decl:
					idx.noncopyable.add(usr)
				if "@propertyWrapper" in decl:
					idx.propwrapper.add(usr)
				if kind == "swift.typealias":
					eq = _find_top(decl, "=")
					if eq >= 0 and _find_top(decl[eq + 1:], "->") >= 0:
						idx.alias_closures.add(usr)
				if kind == "swift.struct":
					idx._struct_usrs[path] = usr
				if kind == "swift.protocol":
					if re.search(r"protocol\s+\w+\s*<", decl):
						idx._proto_primary.add(usr)
			elif kind == "swift.associatedtype":
				parent = path[:-1]
				idx._proto_assoc.add(parent)
			elif kind == "swift.init":
				idx._has_init.add(path[:-1])
			if kind in FUNCLIKE_KINDS | PROPERTY_KINDS | OPERATOR_KINDS:
				parent = path[:-1]
				if parent and re.search(r"\bSelf\b", decl):
					idx._proto_members[parent].append(decl)
	for path, usr in idx._struct_usrs.items():
		if path not in idx._has_init:
			idx.no_public_init.add(usr)
	for path, rec in idx.by_path.items():
		if rec["kind"] != "swift.protocol":
			continue
		selfreq = path in idx._proto_assoc or bool(idx._proto_members.get(path))
		if selfreq and rec["usr"] not in idx._proto_primary:
			idx.proto_selfreq.add(rec["usr"])
	return idx


GLOBAL_ACTOR = re.compile(r"@(\w*Actor)\b")


def isolation_of(decl):
	m = GLOBAL_ACTOR.search(decl)
	if m and m.group(1) not in ("globalActor",):
		return m.group(1)
	return None


# ---------------------------------------------------------------------------
# Pass 2: classification.

THROWS_TYPED_TYPE = re.compile(r"\bthrows\s*\(([^)]*)\)")


def decl_parts(decl, kind):
	"""(head, effects): what precedes the parameter list, and the effects after it or a property's accessors.

	A closure parameter has its own `throws`, `async` and `@MainActor`; read off the whole declaration, they
	would be taken for the function's."""
	if kind in PROPERTY_KINDS:
		brace = decl.find("{")
		colon = _find_top(decl, ":")
		return (decl[:colon] if colon >= 0 else decl), (decl[brace:] if brace >= 0 else "")
	span = _param_span(decl)
	if span is None:
		return decl, ""
	rest = decl[span[1] + 1:]
	arrow = _find_top(rest, "->")
	if arrow >= 0:
		rest = rest[:arrow]
	return decl[:span[0]], re.split(r"\bwhere\b", rest)[0]


def existential_of(slots, protocols):
	"""Whether a slot asks for `any P` of one of these protocols.

	Mentioning the protocol is not asking for its existential: `T: P` is a constraint the call site
	instantiates (§5, generics) and `some P` is an opaque type swiftc resolves — both reprint and compile.
	A comma or a bracket between `any` and the name blocks the match, so `(any Error, some P)` does not
	count. Bare existentials spelled without `any` are missed; Swift 6 prints them with it."""
	for _role, text, usrs, _bucket in slots:
		for name, usr in usrs.items():
			if usr in protocols and re.search(rf"\bany\b[\w\s.&]*\b{re.escape(name)}\b", text):
				return True
	return False


def symbol_record(sym, idx):
	kind = sym["kind"]["identifier"]
	decl = frag_text(sym.get("declarationFragments", []))
	fs = sym.get("functionSignature") or {}
	path = tuple(sym["pathComponents"])
	parent = idx.by_path.get(path[:-1])

	decl_usrs = frag_usrs(sym.get("declarationFragments", []))
	slots = []			# (role, text, usrs, bucket)
	all_usrs = set(decl_usrs.values())
	mods = []
	if kind in PROPERTY_KINDS:
		t = decl_value_type(decl)
		slots.append(("value", t, decl_usrs, classify_type(t, decl_usrs, None, idx.alias_closures)))
	else:
		decl_params = decl_param_list(decl)
		for i, p in enumerate(fs.get("parameters", [])):
			frags = param_type_frags(p)
			t, u = frag_text(frags), frag_usrs(frags)
			mod = ""
			if i < len(decl_params):
				m = re.match(r"^((?:inout|borrowing|consuming|__owned|__shared|isolated|sending)\s+)+", decl_params[i][1])
				mod = m.group(0) if m else ""
			mods.append(mod)
			all_usrs |= set(u.values())
			slots.append(("param", mod + t, u, classify_type(t, u, None, idx.alias_closures)))
		rets = fs.get("returns")
		if rets is not None:
			t, u = frag_text(rets), frag_usrs(rets)
			all_usrs |= set(u.values())
			slots.append(("return", t, u, classify_type(t, u, None, idx.alias_closures)))
		elif kind == "swift.init":
			# The graph gives an initialiser no return; what it constructs still has to cross.
			t = ".".join(path[:-1])
			owner = parent["usr"] if parent else idx.member_of.get(sym["identifier"]["precise"], "?")
			u = {path[-2]: owner} if len(path) > 1 else {}
			all_usrs |= set(u.values())
			slots.append(("return", t, u, classify_type(t, u, None, idx.alias_closures)))

	generics = [g["name"] for g in (sym.get("swiftGenerics") or {}).get("parameters", [])]
	param_text = " ".join(s[1] for s in slots if s[0] == "param")
	ret_text = " ".join(s[1] for s in slots if s[0] == "return")
	return_only_generics = [g for g in generics
		if re.search(rf"\b{re.escape(g)}\b", ret_text) and not re.search(rf"\b{re.escape(g)}\b", param_text)]

	causes = []
	if kind == "swift.macro":
		causes.append("macro")
	# `each T` and `repeat each T`/`repeat (…)`, not a function or parameter that happens to be named each.
	if re.search(r"(?<!func )\beach\s+[A-Za-z_]|\brepeat\s+(?:each\b|\()", decl):
		causes.append("parameter-pack")
	if "~Copyable" in decl or (all_usrs & idx.noncopyable) or (parent and parent["usr"] in idx.noncopyable):
		causes.append("noncopyable")
	if "@autoclosure" in decl:
		causes.append("autoclosure")
	wrapper_slots = sum(1 for _r, _t, us, _b in slots if set(us.values()) & idx.propwrapper)
	if any((idx.by_path.get(path[:i]) or {}).get("usr") in idx.propwrapper for i in range(1, len(path))):
		# The wrapper *is* the declaration form (`@State var`); a wrapper in a slot is a constructible handle.
		causes.append("property-wrapper-as-api")
	if existential_of(slots, idx.proto_selfreq):
		causes.append("self-requirement-protocol")
	if "..." in param_text:
		causes.append("variadic-parameter")

	if any(c in CAUSE_ORDER for c in causes):
		group = 3
	elif all(b in CROSSABLE for _, _, _, b in slots):
		group = 1
	else:
		group = 2

	head, effects = decl_parts(decl, kind)
	typed = THROWS_TYPED_TYPE.search(effects)
	if typed:
		throws = "throws(E)"
	elif re.search(r"\brethrows\b", effects):
		throws = "rethrows"
	elif re.search(r"\bthrows\b", effects):
		throws = "throws"
	else:
		throws = "none"

	if re.search(r"\bnonisolated\b", head):
		isolation = None
	else:
		isolation = isolation_of(head) or (parent["isolation"] if parent else None)
	struct_param_no_init = bool({u for _, _, us, _ in slots for u in us.values()
		if u in idx.no_public_init} & {u for r, _, us, _ in slots if r == "param" for u in us.values()})

	return {
		"usr": sym["identifier"]["precise"],
		"kind": kind,
		"path": path,
		"title": sym.get("names", {}).get("title", path[-1] if path else ""),
		"decl": decl,
		"slots": [(r, t, b) for r, t, _, b in slots],
		"slot_usrs": [u for _, _, u, _ in slots],
		"mods": mods,
		"builder_params": sum(1 for p in fs.get("parameters", []) if builder_attrs(p)),
		"wrapper_slots": wrapper_slots,
		"group": group,
		"causes": causes,
		"cause": next((c for c in CAUSE_ORDER if c in causes), None),
		"generics": generics,
		"return_only_generics": return_only_generics,
		"throws": throws,
		"throws_type": typed.group(1).strip() if typed else None,
		"async": bool(re.search(r"\basync\b", effects)),
		"isolation": isolation,
		"struct_param_no_public_init": struct_param_no_init,
		"generic_only_reason": group == 2 and all(
			b in CROSSABLE or b == BUCKET_GENERIC for _, _, _, b in slots),
		"handle_slots": [t for _, t, _, b in slots if b == BUCKET_HANDLE],
		"availability": sym.get("availability", []),
		"sym": sym,
	}


# ---------------------------------------------------------------------------
# Stub emission and swiftc verification.

def is_unavailable(rec):
	for a in rec["availability"]:
		if a.get("isUnconditionallyUnavailable") or a.get("unavailable"):
			return True
		if a.get("domain") in ("macOS", "macos") and "obsoleted" in a:
			return True
	return False


# §5 takes the instantiation set from call sites; with no application there is none, so pick a witness.
STUB_ERROR = "PippinStubError"
WITNESS_PREFERENCE = ["Int", "String", "Double", "Array<Int>", STUB_ERROR, "AnyObject"]
PROTOCOL_WITNESSES = {
	"Hashable": {"Int", "String", "Double", "Array<Int>"},
	"Equatable": {"Int", "String", "Double", "Array<Int>"},
	"Comparable": {"Int", "String", "Double"},
	"Sendable": {"Int", "String", "Double", "Array<Int>"},
	"Codable": {"Int", "String", "Double", "Array<Int>"},
	"Decodable": {"Int", "String", "Double", "Array<Int>"},
	"Encodable": {"Int", "String", "Double", "Array<Int>"},
	"CustomStringConvertible": {"Int", "String", "Double"},
	"LosslessStringConvertible": {"Int", "String", "Double"},
	"CustomDebugStringConvertible": {"Int", "String", "Double"},
	"ExpressibleByStringLiteral": {"String"},
	"StringProtocol": {"String"},
	"Numeric": {"Int", "Double"},
	"SignedNumeric": {"Int", "Double"},
	"AdditiveArithmetic": {"Int", "Double"},
	"Strideable": {"Int", "Double"},
	"BinaryInteger": {"Int"},
	"FixedWidthInteger": {"Int"},
	"SignedInteger": {"Int"},
	"FloatingPoint": {"Double"},
	"BinaryFloatingPoint": {"Double"},
	"Sequence": {"Array<Int>", "String"},
	"Collection": {"Array<Int>", "String"},
	"BidirectionalCollection": {"Array<Int>", "String"},
	"RandomAccessCollection": {"Array<Int>"},
	"RangeReplaceableCollection": {"Array<Int>", "String"},
	"MutableCollection": {"Array<Int>"},
	"Error": {STUB_ERROR},
	"AnyObject": {"AnyObject"},
	"Any": set(WITNESS_PREFERENCE),
}


def module_witness(usr, idx):
	"""A concrete, non-generic type of this module that conforms to the protocol."""
	for src in idx.conformers.get(usr, []):
		rec = idx.by_usr.get(src)
		if rec and not rec["generic"] and rec["kind"] in ("swift.struct", "swift.class", "swift.enum", "swift.actor") \
				and src not in idx.noncopyable:
			return ".".join(rec["path"])
	return None


def witness_for(constraints, idx):
	"""One spelling satisfying every conformance constraint on a name, or None."""
	candidates = None
	for c in constraints:
		if c["kind"] == "superclass":
			return c["rhs"]
		if c["kind"] != "conformance":
			return None
		rhs, precise = c["rhs"], c.get("rhsPrecise", "")
		if precise in idx.by_usr:
			w = module_witness(precise, idx)
			if w is None:
				return None
			this = {w}
		else:
			this = PROTOCOL_WITNESSES.get(rhs.split(".")[-1])
			if this is None:
				w = module_witness(precise, idx)
				if w is None:
					return None
				this = {w}
		candidates = set(this) if candidates is None else (candidates & set(this))
		if not candidates:
			return None
	if candidates is None:
		return "Int"
	for pref in WITNESS_PREFERENCE:
		if pref in candidates:
			return pref
	return sorted(candidates)[0]


def parent_kind(rec, idx):
	"""(record-or-None, kind) for the type a member hangs off, including types from other modules that
	this graph only extends."""
	parent = idx.by_path.get(rec["path"][:-1])
	if parent is not None:
		return parent, parent["kind"]
	ext = rec["sym"].get("swiftExtension") or {}
	return None, ext.get("typeKind")


def protocol_witness_by_name(name):
	w = PROTOCOL_WITNESSES.get(name)
	return sorted(w)[0] if w else None


WHERE_CLAUSE = re.compile(r"\bwhere\b(.*)$", re.S)


def decl_constraints(decl, usrs):
	"""The constraints of a printed `where` clause, shaped like `swiftGenerics.constraints`.

	That key is not always there: a member of a protocol extension arrives with an empty constraint list
	and `where MenuItems : View` only in the print, and a harness that misses it picks a witness that
	conforms to nothing."""
	m = WHERE_CLAUSE.search(decl)
	if not m:
		return []
	out = []
	for part in _split_top(m.group(1), [","]):
		part = part.strip()
		for sep, kind in (("==", "sameType"), (":", "conformance")):
			i = _find_top(part, sep)
			if i < 0:
				continue
			lhs, rhs = part[:i].strip(), part[i + len(sep):].strip()
			if re.match(r"^[\w.]+$", lhs):
				out.append({"kind": kind, "lhs": lhs, "rhs": rhs,
					"rhsPrecise": usrs.get(rhs.split(".")[-1], "")})
			break
	return out


def substitution(rec, idx):
	"""A concrete type for every type parameter except `Self`, or None when one cannot be chosen."""
	sym = rec["sym"]
	gen = sym.get("swiftGenerics") or {}
	names = [g["name"] for g in gen.get("parameters", [])]
	names += [f["spelling"] for f in sym.get("declarationFragments", []) if f["kind"] == "genericParameter"]
	parent, pkind = parent_kind(rec, idx)
	by_name = collections.defaultdict(list)
	# A member repeats its type's parameters but not its type's constraints, so merge them in.
	constraints = list((parent["gen"] if parent else {}).get("constraints", [])) + gen.get("constraints", [])
	constraints += decl_constraints(rec["decl"], frag_usrs(sym.get("declarationFragments", [])))
	for c in constraints:
		lhs = c["lhs"]
		if "." in lhs:
			return None			# a constraint on an associated type: no witness can be guaranteed
		by_name[lhs].append(c)
	subst = {}
	for name in dict.fromkeys(names):
		if name == "Self":
			continue
		cs = by_name.get(name, [])
		same = [c for c in cs if c["kind"] == "sameType"]
		if same:
			subst[name] = same[0]["rhs"]
			continue
		w = witness_for(cs, idx)
		if w is None:
			return None
		subst[name] = w
	for name in by_name:
		if name not in subst and name != "Self":
			return None
	# A sameType witness may itself name a type parameter (`where Value == T?`); resolve, then insist.
	for _ in range(3):
		subst = {k: apply_subst(v, {n: s for n, s in subst.items() if n != k}) for k, v in subst.items()}
	for v in subst.values():
		if any(re.search(rf"\b{re.escape(n)}\b", v) for n in subst):
			return None
	if "Self" in by_name:
		same = [c for c in by_name["Self"] if c["kind"] == "sameType"]
		if same:
			subst["Self"] = apply_subst(same[0]["rhs"], subst)
	return subst


def apply_subst(text, subst):
	if not subst:
		return text
	pattern = re.compile(r"\b(" + "|".join(re.escape(k) for k in sorted(subst, key=len, reverse=True)) + r")\b")
	return pattern.sub(lambda m: subst[m.group(1)], text)


def receiver_spelling(rec, idx, subst):
	parent, pkind = parent_kind(rec, idx)
	if pkind == "swift.protocol":
		if "Self" in subst:
			return subst["Self"]
		w = (module_witness(parent["usr"], idx) if parent else None) \
			or protocol_witness_by_name(rec["path"][-2] if len(rec["path"]) > 1 else "")
		return w
	own = rec["path"][:-1]
	for k in range(1, len(own)):
		anc = idx.by_path.get(own[:k])
		if anc is not None and anc["generic"]:
			return None			# `IntegerFormatStyle<Int>.Percent`: the harness cannot spell it
	name = ".".join(own)
	if parent is not None and parent["generic"]:
		m = re.search(r"\b" + re.escape(parent["path"][-1]) + r"\s*<([^>]*)>", parent["decl"])
		if not m:
			return None
		args = [subst.get(a.strip()) for a in m.group(1).split(",")]
		if any(a is None for a in args):
			return None
		return f"{name}<{', '.join(args)}>"
	if parent is None:
		# An extended type from another module: no declaration here gives its parameters' order.
		depth0 = [g["name"] for g in (rec["sym"].get("swiftGenerics") or {}).get("parameters", [])
			if g.get("depth") == 0]
		if depth0 and pkind != "swift.protocol":
			return None
	return name


def stub_candidate(rec, idx, allow_generic):
	"""A symbol a stub can be written for with no application in hand."""
	if rec["group"] not in (1, 2) or is_unavailable(rec):
		return False
	if any("..." in t for _, t, _ in rec["slots"]):
		return False
	generic = bool(rec["generics"]) or bool(re.search(r"\bSelf\b", rec["decl"]))
	if generic and not allow_generic:
		return False
	path = rec["path"]
	if rec["kind"] == "swift.func":
		return len(path) == 1
	parent = idx.by_path.get(path[:-1])
	if parent is None or parent["usr"] in idx.noncopyable:
		return False
	if parent["kind"] == "swift.protocol":
		return allow_generic
	if parent["kind"] not in ("swift.struct", "swift.class", "swift.enum", "swift.actor"):
		return False
	if parent["generic"] and not allow_generic:
		return False
	if not generic and not parent["generic"]:
		return True
	return allow_generic and substitution(rec, idx) is not None


def labels_of(title):
	"""`value(of:)` -> ["of"]; `init(_:)` -> ["_"]. The graph's parameter `name` is the external label
	for some symbols and the internal one for others, so the title is the only reliable source."""
	m = re.search(r"\((.*)\)$", title)
	if not m:
		return None
	inner = m.group(1)
	if inner == "":
		return []
	return inner.split(":")[:-1]


def emit_stub(rec, idx, index):
	"""A wrapper that reprints the signature and calls the symbol: the generator's own output shape.
	A declaration that type-checks but cannot be called would not be evidence of anything."""
	sym = rec["sym"]
	fs = sym.get("functionSignature") or {}
	params = fs.get("parameters", [])
	labels = labels_of(rec["title"])
	if labels is None or len(labels) != len(params):
		labels = [p.get("name", "_") for p in params]
	decl = rec["decl"]
	path = rec["path"]
	is_static = rec["kind"] == "swift.type.method"
	is_init = rec["kind"] == "swift.init"
	is_free = rec["kind"] == "swift.func"
	mutating = bool(re.search(r"\bmutating\b", decl))
	subst = substitution(rec, idx)
	if subst is None:
		return None
	if is_free:
		receiver = None
	else:
		# `extension P where Self == X` is reachable only on X, whatever type the member's path names.
		receiver = subst.get("Self") or receiver_spelling(rec, idx, subst)
		if receiver is None:
			return None
		subst.setdefault("Self", receiver)

	sig, args = [], []
	if not (is_static or is_init or is_free):
		sig.append(f"_ recv: {'inout ' if mutating else ''}{receiver}")
	for i, (p, label) in enumerate(zip(params, labels)):
		ty = apply_subst((rec["mods"][i] if i < len(rec["mods"]) else "") + frag_text(param_type_frags(p)).strip(), subst)
		inout = ty.startswith("inout ")
		hidden = bool(set(frag_usrs(param_type_frags(p)).values()) & idx.alias_closures)
		# Only a bare function type takes @escaping; inside Optional it is already escaping.
		bare = _find_top(ty, "->") >= 0 or (hidden and not ty.rstrip().endswith(("?", "!")))
		if bare and "@escaping" not in ty and not inout:
			ty = "@escaping " + ty
		sig.append(f"_ a{i}: {ty}")
		arg = f"&a{i}" if inout else f"a{i}"
		args.append(arg if label == "_" else f"{label}: {arg}")

	ret = apply_subst(frag_text(fs.get("returns", [])).strip(), subst)
	if is_init:
		ret = receiver + ("?" if re.search(r"\binit[?!]", decl) else "")
	effects = ""
	if re.search(r"\basync\b", decl):
		effects += " async"
	if rec["throws"] != "none":
		effects += " throws"
	call_prefix = ("try " if rec["throws"] != "none" else "") + ("await " if " async" in effects else "")

	base = rec["title"].split("(")[0]
	if is_init:
		target = f"{receiver}({', '.join(args)})"
	elif is_static:
		target = f"{receiver}.{base}({', '.join(args)})"
	elif is_free:
		target = f"{base}({', '.join(args)})"
	else:
		target = f"recv.{base}({', '.join(args)})"

	body_ret = "" if ret in ("", "()", "Void") else "return "
	iso = f"@{rec['isolation']}\n" if rec["isolation"] else ""
	head = f"func pippin_stub_{index}({', '.join(sig)}){effects}"
	if ret not in ("", "()", "Void"):
		head += f" -> {ret}"
	out = f"{iso}@available(macOS 26.0, *)\n{head} {{\n\t{body_ret}{call_prefix}{target}\n}}\n"
	for name in generic_names(rec):
		if re.search(rf"\b{re.escape(name)}\b", out):
			return None			# a type parameter survived substitution: the stub would not resolve
	return out


def generic_names(rec):
	sym = rec["sym"]
	names = {g["name"] for g in (sym.get("swiftGenerics") or {}).get("parameters", [])}
	names |= {f["spelling"] for f in sym.get("declarationFragments", []) if f["kind"] == "genericParameter"}
	names.add("Self")
	return names


PRELUDE = f"struct {STUB_ERROR}: Error {{}}\n"

WITNESS_COMPLAINT = re.compile(
	r"does not conform to|requires that|is not a member type|be equivalent|requires the types|"
	r"conform to protocol|requires arguments in")


def failure_class(msg, instantiated):
	"""Whose fault the error is: the printed declaration, the harness's witness, or the graph."""
	if "has been renamed" in msg or "is unavailable" in msg or "was deprecated" in msg:
		return "graph names an obsolete spelling"
	if instantiated and WITNESS_COMPLAINT.search(msg):
		return "witness type the harness chose"
	return "the reprinted declaration"


def run_sample(name, module, pool, idx, work, sample_size, import_path, extra_imports, progress):
	if not pool:
		return {"pool": 0, "sampled": 0, "compiled": 0, "failures": [],
			"pool_builder": 0, "sampled_builder": 0, "failed_builder": 0}
	pool = sorted(pool, key=lambda r: r["usr"])
	sample = pool if len(pool) <= sample_size else random.Random(0).sample(pool, sample_size)
	sample.sort(key=lambda r: r["usr"])
	d = os.path.join(work, "stubs", module, name)
	shutil.rmtree(d, ignore_errors=True)
	os.makedirs(d, exist_ok=True)
	imports = "".join(f"import {m}\n" for m in extra_imports)
	files = []
	for i, r in enumerate(sample):
		body = emit_stub(r, idx, i)
		if body is None:
			continue
		src = imports + "\n" + PRELUDE + "\n" + body
		p = os.path.join(d, f"stub_{i:04d}.swift")
		with open(p, "w") as f:
			f.write(src)
		files.append((p, r, src))
	sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()

	def one(job):
		p, r, _src = job
		cmd = ["xcrun", "swiftc", "-typecheck", "-swift-version", "5",
			"-target", HOST_TARGET, "-sdk", sdk]
		if import_path:
			cmd += ["-I", import_path]
		cmd.append(p)
		res = subprocess.run(cmd, capture_output=True, text=True)
		if res.returncode == 0:
			return None
		err = [l for l in res.stderr.splitlines() if ": error:" in l]
		msg = err[0].split(": error: ", 1)[-1] if err else res.stderr.strip()[:200]
		return {"usr": r["usr"], "title": ".".join(r["path"]), "group": r["group"],
			"generic": bool(r["generics"]) or "Self" in r["decl"],
			"builder": bool(r["builder_params"]),
			"class": failure_class(msg, name == "instantiated"), "error": msg}

	progress(f"  swiftc {name}: {len(files)} stubs")
	with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool_exec:
		results = list(pool_exec.map(one, files))
	failures = [x for x in results if x]
	return {"pool": len(pool), "sampled": len(files), "compiled": len(files) - len(failures),
		"pool_builder": sum(1 for r in pool if r["builder_params"]),
		"sampled_builder": sum(1 for _p, r, _s in files if r["builder_params"]),
		"failed_builder": sum(1 for x in failures if x["builder"]),
		"failures": failures}


def verify(module, recs, idx, work, sample_size, import_path, extra_imports, progress):
	concrete, generic = [], []
	for r in recs:
		if not stub_candidate(r, idx, allow_generic=True):
			continue
		if emit_stub(r, idx, 0) is None:
			continue
		(concrete if stub_candidate(r, idx, allow_generic=False) else generic).append(r)
	return {
		"concrete": run_sample("concrete", module, concrete, idx, work, sample_size,
			import_path, extra_imports, progress),
		"instantiated": run_sample("instantiated", module, generic, idx, work, sample_size,
			import_path, extra_imports, progress),
	}


# ---------------------------------------------------------------------------
# Per-module analysis.

def analyze(module, graph_dirs, work, sample_size, import_path, extra_imports, progress, do_verify=True):
	paths = [p for d in graph_dirs for p in graph_files(d)]
	gen, plat = graph_metadata(paths[0])
	progress(f"{module}: {len(paths)} graph file(s)")
	idx = scan_types(paths, progress)

	recs, props, ops = [], [], []
	kinds = collections.Counter()
	seen, dupes = set(), 0
	for p in paths:
		progress(f"  classify {os.path.basename(p)}")
		for sym in iter_symbols(p):
			usr = sym["identifier"]["precise"]
			if usr in seen:
				dupes += 1		# two graphs of one module can print the same symbol; count it once
				continue
			seen.add(usr)
			kind = sym["kind"]["identifier"]
			kinds[kind] += 1
			if any(c.startswith("_") for c in sym["pathComponents"]):
				continue
			if kind in FUNCLIKE_KINDS or kind == "swift.macro":
				recs.append(symbol_record(sym, idx))
			elif kind in OPERATOR_KINDS:
				ops.append(symbol_record(sym, idx))
			elif kind in PROPERTY_KINDS:
				props.append(symbol_record(sym, idx))

	out = {
		"module": module, "generator": gen, "platform": plat,
		"graph_files": [os.path.basename(p) for p in paths],
		"graph_bytes": sum(os.path.getsize(p) for p in paths),
		"duplicate_symbols": dupes,
		"kinds": dict(kinds.most_common()),
		"funclike": summarize(recs, idx),
		"operators": summarize(ops, idx),
		"properties": summarize(props, idx),
	}
	if do_verify:
		out["verify"] = verify(module, recs, idx, work, sample_size, import_path, extra_imports, progress)
	out["sensitivity"] = sensitivity(recs, idx)
	return out, recs, idx


def sensitivity(recs, idx):
	wide = dict(VALUE_USRS)
	wide.update(SENSITIVITY_USRS)
	moved = 0
	for r in recs:
		if r["group"] != 2:
			continue
		if all(classify_type(t, u, wide, idx.alias_closures) in CROSSABLE
				for (_role, t, _b), u in zip(r["slots"], r["slot_usrs"])):
			moved += 1
	return {"value_set_widened_moves_to_group1": moved, "added": sorted(SENSITIVITY_USRS.values())}


def summarize(recs, idx):
	n = len(recs)
	groups = collections.Counter(r["group"] for r in recs)
	causes = collections.Counter()
	for r in recs:
		if r["group"] == 3:
			causes[r["cause"]] += 1
	causes_any = collections.Counter()
	for r in recs:
		for c in r["causes"]:
			causes_any[c] += 1
	slots = collections.Counter(b for r in recs for _, _, b in r["slots"])
	hidden_closures = sum(1 for r in recs for (_role, t, b), u in zip(r["slots"], r["slot_usrs"])
		if b == BUCKET_CLOSURE and _find_top(t, "->") < 0)
	handle_types = collections.Counter(t.strip() for r in recs for t in r["handle_slots"])
	return {
		"total": n,
		"group1": groups[1], "group2": groups[2], "group3": groups[3],
		"group2_generic_only": sum(1 for r in recs if r["group"] == 2 and r["generic_only_reason"]),
		"causes": causes.most_common(),
		"causes_any": causes_any.most_common(),
		"slots": slots.most_common(),
		"hidden_closures": hidden_closures,
		"builder_symbols": sum(1 for r in recs if r["builder_params"]),
		"builder_param_slots": sum(r["builder_params"] for r in recs),
		"wrapper_slot_symbols": sum(1 for r in recs if r["wrapper_slots"]),
		"wrapper_slots": sum(r["wrapper_slots"] for r in recs),
		"top_handle_types": handle_types.most_common(15),
		"throws": collections.Counter(r["throws"] for r in recs).most_common(),
		"isolated": sum(1 for r in recs if r["isolation"]),
		"isolation_kinds": collections.Counter(r["isolation"] for r in recs if r["isolation"]).most_common(6),
		"return_only_generic": sum(1 for r in recs if r["return_only_generics"]),
		"generic": sum(1 for r in recs if r["generics"]),
		"struct_param_no_public_init": sum(1 for r in recs if r["struct_param_no_public_init"]),
		"structs_total": len([u for u, t in idx.by_usr.items() if t["kind"] == "swift.struct"]),
		"structs_no_public_init": len(idx.no_public_init),
	}


# ---------------------------------------------------------------------------
# Extraction.

def extract(module, target, sdk, out_dir, import_path, progress, required=True, access=None):
	if os.path.isdir(out_dir) and graph_files(out_dir):
		progress(f"{module}: reusing {out_dir}")
		return out_dir
	os.makedirs(out_dir, exist_ok=True)
	cmd = ["xcrun", "swift-symbolgraph-extract", "-module-name", module,
		"-target", target, "-sdk", sdk, "-output-dir", out_dir]
	if access:
		cmd += ["-minimum-access-level", access]
	if import_path:
		cmd += ["-I", import_path]
	progress(f"{module}: extracting")
	res = subprocess.run(cmd, capture_output=True, text=True)
	if res.returncode != 0 or not graph_files(out_dir):
		if not required:
			progress(f"{module}: extraction failed, skipped")
			return None
		raise SystemExit(f"{module}: symbolgraph-extract failed\n{res.stderr}")
	return out_dir


# ---------------------------------------------------------------------------
# Re-exports: one `import` is not one `.swiftmodule`.

def usr_module(usr):
	"""The module a mangled Swift USR belongs to, or None when the USR is not a Swift one."""
	if not usr.startswith("s:"):
		return None
	m = re.match(r"s:(\d+)", usr)
	if not m:
		return "Swift"			# a stdlib substitution: `s:Sb`, `s:Sa`
	n = int(m.group(1))
	return usr[m.end():m.end() + n]


DECLARED_USR = re.compile(r'"identifier":\{"precise":"([^"]+)"')
REFERENCED_USR = re.compile(r'"preciseIdentifier":"([^"]+)"')
EDGE_USR = re.compile(r'"(?:source|target)":"([^"]+)"')


def own_usrs(module, paths):
	"""(declared, referenced) USRs of `module` itself over these graphs."""
	declared, referenced = set(), collections.Counter()
	for p in paths:
		with open(p, "rb") as f:
			text = f.read().decode("utf-8")
		for m in DECLARED_USR.finditer(text):
			if usr_module(m.group(1)) == module:
				declared.add(m.group(1))
		for rx in (REFERENCED_USR, EDGE_USR):
			for m in rx.finditer(text):
				if usr_module(m.group(1)) == module:
					referenced[m.group(1)] += 1
		del text
	return declared, referenced


def dangling_own(module, paths):
	"""USRs mangled into `module` that none of these graphs declares.

	A framework shipped as two `.swiftmodule`s keeps one ABI name — SwiftUICore is built
	`-module-abi-name SwiftUI` — so the missing half's symbols are mangled as the module's own and show up
	here rather than under a foreign prefix."""
	declared, referenced = own_usrs(module, paths)
	return {u: c for u, c in referenced.items() if u not in declared}


EXPORTED_IMPORT = re.compile(r"^@_exported import (\w+)", re.M)
ABI_NAME = re.compile(r"-module-abi-name (\w+)")


def interface_path(module, sdk, target, import_path):
	"""The `.swiftinterface` of a module, if the toolchain ships one."""
	pats = []
	if import_path:
		pats += [os.path.join(import_path, f"{module}.swiftinterface"),
			os.path.join(import_path, f"{module}.swiftmodule", "*.swiftinterface")]
	for d in ("Frameworks", "SubFrameworks", "PrivateFrameworks"):
		base = os.path.join(sdk, "System", "Library", d, f"{module}.framework")
		pats.append(os.path.join(base, "Modules", f"{module}.swiftmodule", "*.swiftinterface"))
		pats.append(os.path.join(base, "Versions", "*", "Modules", f"{module}.swiftmodule",
			"*.swiftinterface"))
	pats.append(os.path.join(sdk, "usr", "lib", "swift", f"{module}.swiftmodule", "*.swiftinterface"))
	hits = [h for p in pats for h in glob.glob(p)]
	arch, _, os_name = target.partition("-apple-")
	os_name = re.sub(r"[\d.]+$", "", os_name).replace("macosx", "macos")
	same_os = [h for h in hits if f"-apple-{os_name}." in os.path.basename(h)] or hits
	same_os.sort(key=lambda h: (not os.path.basename(h).startswith(arch), h))
	return same_os[0] if same_os else None


def interface_facts(path):
	"""(ABI name, `@_exported import` names) off a `.swiftinterface` header."""
	with open(path, "r", errors="replace") as f:
		head = f.read(1 << 18)
	m = ABI_NAME.search(head)
	return (m.group(1) if m else None), set(EXPORTED_IMPORT.findall(head))


def abi_siblings(module, paths, sdk, target, work, import_path, progress):
	"""The modules whose graphs have to be read together with this one's.

	`-module-name SwiftUI` returns half a framework: `View`, `Text`, `VStack` and `ViewBuilder` live in
	SwiftUICore. Detected rather than listed — the hole is the set of USRs mangled into the module that
	nothing declares, and a candidate fills it only when its own ABI name is the module's, which is the
	mechanism that put its symbols under that name in the first place."""
	before = dangling_own(module, paths)
	if not before:
		return [], 0, 0
	candidates = {os.path.basename(p).split("@", 1)[1].removesuffix(".symbols.json")
		for p in paths if "@" in os.path.basename(p)}
	iface = interface_path(module, sdk, target, import_path)
	if iface:
		candidates |= interface_facts(iface)[1]
	candidates.discard(module)
	found = []
	for cand in sorted(candidates):
		ci = interface_path(cand, sdk, target, import_path)
		if ci is not None:
			if interface_facts(ci)[0] != module:
				continue		# a module in its own right: its surface answers to its own `require-swift`
		elif not (import_path and glob.glob(os.path.join(import_path, f"{cand}.swiftmodule*"))):
			continue			# no interface and not locally built: nothing cheap says it is a sibling
		d = extract(cand, target, sdk, os.path.join(work, "sg", cand), import_path, progress,
			required=False)
		if d is None:
			continue
		if not own_usrs(module, graph_files(d))[0] & set(before):
			continue
		found.append((cand, d))
	after = dangling_own(module, paths + [p for _, d in found for p in graph_files(d)])
	return found, sum(before.values()), sum(after.values())


PACKAGE_TEMPLATE = """// swift-tools-version: 6.0
import PackageDescription

let package = Package(
	name: "PippinReprintProbe",
	platforms: [.macOS(.v15)],
	dependencies: [
{deps}
	],
	targets: [
		.target(name: "Probe", dependencies: [
{products}
		]),
	]
)
"""


def build_spm_probe(specs, work, progress):
	"""Write and build a throwaway package: the §5 case of a dependency built from source, which has
	no .swiftinterface at all."""
	root = os.path.join(work, "spm-probe")
	os.makedirs(os.path.join(root, "Sources", "Probe"), exist_ok=True)
	deps, products, modules = [], [], []
	for url, version, product in specs:
		name = url.rstrip("/").split("/")[-1].removesuffix(".git")
		deps.append(f'\t\t.package(url: "{url}", from: "{version}"),')
		products.append(f'\t\t\t.product(name: "{product}", package: "{name}"),')
		modules.append(product)
	with open(os.path.join(root, "Package.swift"), "w") as f:
		f.write(PACKAGE_TEMPLATE.format(deps="\n".join(deps), products="\n".join(products)))
	with open(os.path.join(root, "Sources", "Probe", "Probe.swift"), "w") as f:
		f.write("public let probe = 1\n")
	progress("spm probe: swift build")
	scratch = os.path.abspath(os.path.join(root, ".build"))
	res = subprocess.run(["swift", "build", "--scratch-path", scratch], cwd=root, capture_output=True, text=True)
	if res.returncode != 0:
		raise SystemExit("spm probe build failed (network?):\n" + res.stderr[-2000:])
	mod_dir = os.path.join(scratch, "debug", "Modules")
	if not os.path.isdir(mod_dir):
		raise SystemExit(f"spm probe: no module dir at {mod_dir}")
	return mod_dir, modules


# ---------------------------------------------------------------------------
# The report.

def pct(a, b):
	return f"{100.0 * a / b:.1f} %" if b else "—"


def render(results, meta):
	L = []
	w = L.append
	w("# Swift public API: what reprints into a compilable stub")
	w("")
	w("Generated by `make swift-reprint` (`scripts/swift-reprint.py`). The experiment design.md §10 step 8")
	w("names as its first deliverable: measured **by declarations, without an application**, so that the risk")
	w("§5 records — \"the long tail of declarations that do not reprint cleanly\" — has a number before the")
	w("generator is built. Nothing here is part of the generator.")
	w("")
	w("**The numbers are meaningless without the toolchain they were measured with.**")
	w("")
	w("| | |")
	w("|---|---|")
	w(f"| swift | `{meta['swift']}` |")
	w(f"| target | `{meta['target']}` |")
	w(f"| SDK | macOS {meta['sdk_version']} (`{meta['sdk']}`) |")
	w(f"| graph generator | `{meta['generator']}` |")
	w(f"| measured | {meta['date']} |")
	w("")
	w("## What is counted")
	w("")
	w("**In scope: methods, type methods, initialisers and free functions** — `swift.method`,")
	w("`swift.type.method`, `swift.init`, `swift.func`, plus `swift.macro`, which is in scope only so that it")
	w("can be counted as a refusal. Symbols with an underscored path component are dropped: they are public")
	w("only in the ABI sense and are not API.")
	w("")
	w("**A module is more than one `.swiftmodule`.** `swift-symbolgraph-extract -module-name SwiftUI`")
	w("returns half a framework: `View`, `Text`, `VStack` and `ViewBuilder` are not in it, because they live")
	w("in `SwiftUICore.framework`, which `SwiftUI` re-exports. The measured surface is therefore the")
	w("module's graph plus the graph of every such sibling, found from the data rather than from a list:")
	w("SwiftUICore is built `-module-abi-name SwiftUI`, so its symbols are mangled as SwiftUI's own and the")
	w("hole shows up as USRs carrying the module's own name that nothing in its graph declares. A candidate")
	w("— a `@_exported import` in the module's `.swiftinterface`, or a module named by one of the")
	w("`Module@Other` extension graphs — is taken only when its own ABI name is the module's. A module with")
	w("an ABI name of its own is left out: its surface answers to its own `require-swift`, not to this one's.")
	w("The count of unresolved own-name USRs before and after is reported per module, so the step can be")
	w("checked rather than believed.")
	w("")
	w("**Operators (`swift.func.op`) are reported separately and not merged into the totals.** They reprint")
	w("like any other free function, but §5's call forms are `(.method x)` and `(f x)` — an operator has no")
	w("Clojure spelling and would need a naming convention that no part of the design has decided. Counting")
	w("them inside the headline would inflate it with surface no call site can currently reach.")
	w("")
	w("**Properties (`swift.property`, `swift.type.property`, `swift.var`) are likewise separate.** §5 does not")
	w("route them through a stub call at all: a property of a host value is `(:field obj)` through `ILookup`")
	w("over the field metadata. Their bucket classification is still informative — it says how much of the")
	w("read surface is values rather than handles — so it is reported, in its own table.")
	w("")
	w("**The four buckets** are §5's, decided by USR on the type's outer constructor: *value* (Bool, String,")
	w("Substring, Character, every integer and floating-point width, `CGFloat`, `Decimal`, `TimeInterval`,")
	w("`Data`, `Date`, `Never`, `Void`), *collection of values* (`Array`/`Set`/`Dictionary`/`ContiguousArray`/")
	w("`ArraySlice` and their sugar, recursively over value elements), *closure* (a type expression with a")
	w("top-level `->`), *opaque handle* (everything else). `Optional` is transparent — §5 maps it to nil.")
	w("A leaf with no USR is a type parameter or `Self`: its bucket is whatever the call site instantiates,")
	w("so it is counted as its own bucket `generic` and lands the symbol in group 2, with the count of")
	w("\"group 2 for no other reason than a type parameter\" reported beside it.")
	w("")
	w("**An initialiser's return is the type it constructs.** The graph records no return for `swift.init`, so")
	w("it is supplied here. It matters: without it every no-argument `init()` would land in group 1 while what")
	w("actually crosses the boundary is an opaque handle, and the data share would be read off a fiction.")
	w("")
	w("**A result-builder attribute is not a refusal.** `@ViewBuilder content: () -> Content` transforms a")
	w("closure *literal written at the call site*; the parameter itself is an ordinary `() -> Content`, and a")
	w("stub passes a closure *value*, which the attribute does not touch. Measured against")
	w("`init(@ViewBuilder content: () -> Content)` on a `Content: View` struct: a stored `() -> Text`, a bare")
	w("function reference, and a hand-built `TupleView<(Text, Text)>` closure all type-check. So the attribute")
	w("is dropped from the reprinted type and the parameter is classified on its own type — a closure slot,")
	w("which is crossable. The symbols that carry one are held out of group 1 by their `some View` return, not")
	w("by the attribute. The honest remainder is narrower than a refusal: a *multi-statement builder body*")
	w("cannot be written from Clojure, while one view value, or several through")
	w("`TupleView`/`ViewBuilder.buildBlock`, can be passed. The count of builder-annotated parameters is")
	w("reported as information, beside the buckets.")
	w("")
	w("**The three groups, and what they are a pass mark for.** A symbol with any refusal cause is group 3.")
	w("Otherwise it is group 1 when every parameter and the return is a value, a collection of values or a")
	w("closure, and group 2 when anything crosses as an opaque handle. **Groups 1 and 2 both mean the symbol")
	w("crosses.** §10's criterion is \"crosses and crosses back\": it is normal and expected that not all Swift")
	w("data becomes Clojure data, and what has to hold is that a handle passed *back* into Swift works. So the")
	w("1/2 split is reported as information — how much of the surface arrives as data rather than as a handle")
	w("— and not as a pass mark; what the criterion measures against is group 3.")
	w("")
	w("## Summary")
	w("")
	w("| module | function-like | crosses (1+2) | 1 as data | 2 as a handle | 3 refused | concrete stubs compiled | instantiated |")
	w("|---|---:|---:|---:|---:|---:|---:|---:|")
	for r in results:
		f = r["funclike"]
		v = r.get("verify") or {}
		c = v.get("concrete", {})
		g = v.get("instantiated", {})
		def frac(a):
			return f"{a.get('compiled', 0)}/{a['sampled']}" if a.get("sampled") else "— (empty pool)"
		w(f"| {r['module']} | {f['total']} | {pct(f['group1'] + f['group2'], f['total'])} | "
			f"{pct(f['group1'], f['total'])} | {pct(f['group2'], f['total'])} | "
			f"{pct(f['group3'], f['total'])} | {frac(c)} | {frac(g)} |")
	w("")
	w("What each module answered:")
	w("")
	for r in results:
		f = r["funclike"]
		w(f"- **{r['module']}** — {r['note']} Crosses {pct(f['group1'] + f['group2'], f['total'])}, "
			f"refused {pct(f['group3'], f['total'])}; of what crosses, "
			f"{pct(f['group1'], max(f['group1'] + f['group2'], 1))} arrives as data rather than as a handle."
			+ SUMMARY_REMARKS.get(r["module"], ""))
	w("")

	for r in results:
		f = r["funclike"]
		w(f"## {r['module']}")
		w("")
		w(f"{r['note']}")
		w("")
		w(f"Graph: {r['graph_bytes'] / 1e6:.0f} MB over {len(r['graph_files'])} file(s) — "
			f"{', '.join('`' + g + '`' for g in r['graph_files'])}. The `Module@Other` files are the extensions")
		w("the module adds to other modules' types; they are part of the surface and are counted.")
		w("")
		if r.get("reexports"):
			w(f"Re-exported siblings merged in: {', '.join('`' + m + '`' for m in r['reexports'])}. "
				f"Unresolved USRs carrying this module's own name fell from {r['dangling_before']} "
				f"references to {r['dangling_after']}.")
		elif r.get("dangling_before"):
			w(f"No re-exported sibling: {r['dangling_before']} references to USRs carrying this module's own "
				"name are unresolved, and no candidate module declares them.")
		else:
			w("No re-exported sibling: every USR carrying this module's own name is declared in its graph.")
		if r.get("duplicate_symbols"):
			w(f"{r['duplicate_symbols']} symbols were printed by more than one graph file and counted once.")
		w("")
		w("| group | count | share |")
		w("|---|---:|---:|")
		w(f"| 1 — crosses as data | {f['group1']} | {pct(f['group1'], f['total'])} |")
		w(f"| 2 — crosses as a handle | {f['group2']} | {pct(f['group2'], f['total'])} |")
		w(f"| 3 — refused | {f['group3']} | {pct(f['group3'], f['total'])} |")
		w(f"| **total function-like** | **{f['total']}** | |")
		w("")
		w(f"Of group 2, {f['group2_generic_only']} ({pct(f['group2_generic_only'], max(f['group2'], 1))} of it) are group 2")
		w("only because a type parameter is in the signature: the call site's instantiation decides their real")
		w("bucket, and without an application there is no call site (§5, generics).")
		w("")
		if f["causes"]:
			w("### Tail by cause")
			w("")
			w("Each group-3 symbol is attributed to one cause (the first that applies, in the order of the")
			w("second column); the third column counts every symbol the cause touches, so it sums to more.")
			w("")
			w("| cause | symbols (attributed) | symbols (touched) |")
			w("|---|---:|---:|")
			any_map = dict(f["causes_any"])
			for cause, cnt in f["causes"]:
				w(f"| {cause} | {cnt} | {any_map.get(cause, cnt)} |")
			w("")
		w("### Slots by bucket")
		w("")
		w("Every parameter and every return of every in-scope symbol, one row per bucket.")
		w("")
		w("| bucket | slots | share |")
		w("|---|---:|---:|")
		tot = sum(c for _, c in f["slots"])
		for b, c in f["slots"]:
			w(f"| {b} | {c} | {pct(c, tot)} |")
		w("")
		w(f"{f['hidden_closures']} of the closure slots are closures only after a typealias declared in this")
		w("module is resolved: the printed type is a nominal name, so a classifier reading the printed type alone")
		w("would call them handles, and a wrapper forwarding one would not know it needs `@escaping`. This is")
		w("undercounted — an ObjC block typedef (`NSComparator`, `NSUserUnixTask`'s completion handler) has a")
		w("`c:@T@…` USR and no `swift.typealias` symbol in the module's own graph, so it stays a handle here.")
		w("")
		w(f"Result builders, for information and not as a refusal: {f['builder_symbols']} function-like symbols "
			f"({pct(f['builder_symbols'], f['total'])}) take {f['builder_param_slots']} builder-annotated")
		w("parameters between them. Each is classified on its own type; what cannot be written from Clojure is")
		w("a multi-statement builder body, not the call.")
		w("")
		w(f"Property wrappers, likewise for information: {f['wrapper_slot_symbols']} symbols "
			f"({pct(f['wrapper_slot_symbols'], f['total'])}) have a wrapper type in {f['wrapper_slots']} of their")
		w("slots. Those are handles, not refusals — the wrapper has a public initialiser. The refusal is the")
		w("declaration form `@State var x`, counted in the tail as members of a wrapper type.")
		w("")
		for cause in INFORMATIONAL_CAUSES:
			n = dict(f["causes_any"]).get(cause, 0)
			w(f"Existentials, likewise for information: {n} symbols ({pct(n, f['total'])}) spell `any P` for a")
			w("protocol with Self requirements in a slot. Not a refusal — `any P` type-checks (SE-0309) and a")
			w("handle to one crosses back, which is the criterion; reading it still needs accessors.")
			w("")
		w("### Decided, not refused")
		w("")
		w("§5 decided each of these is handled; they are counted so the cost is visible, and they are **not**")
		w("failures.")
		w("")
		w("| case | count | share of function-like |")
		w("|---|---:|---:|")
		w(f"| generic parameter only in the return (needs an explicit annotation at the call) | {f['return_only_generic']} | {pct(f['return_only_generic'], f['total'])} |")
		w(f"| generic in any position | {f['generic']} | {pct(f['generic'], f['total'])} |")
		w(f"| takes a struct with no public initialiser | {f['struct_param_no_public_init']} | {pct(f['struct_param_no_public_init'], f['total'])} |")
		w(f"| isolated to a global actor | {f['isolated']} | {pct(f['isolated'], f['total'])} |")
		w("")
		w(f"Public structs in the module: {f['structs_total']}, of which {f['structs_no_public_init']} "
			f"({pct(f['structs_no_public_init'], max(f['structs_total'], 1))}) declare no public initialiser — "
			"they cross outward as a map and cannot be built inward (§5, \"Структуры несимметричны\").")
		if f["isolation_kinds"]:
			w("")
			w("Isolation: " + ", ".join(f"`@{k}` {v}" for k, v in f["isolation_kinds"]) + ".")
		w("")
		w("| `throws` form | count |")
		w("|---|---:|")
		for k, v in f["throws"]:
			w(f"| {k} | {v} |")
		w("")
		w("### Operators and properties")
		w("")
		o, p = r["operators"], r["properties"]
		w("| | total | group 1 | group 2 | group 3 |")
		w("|---|---:|---:|---:|---:|")
		w(f"| operators | {o['total']} | {o['group1']} | {o['group2']} | {o['group3']} |")
		w(f"| properties | {p['total']} | {p['group1']} | {p['group2']} | {p['group3']} |")
		w("")
		if f["top_handle_types"]:
			w("### What the handles are")
			w("")
			w("The most frequent type expressions that land in the opaque-handle bucket.")
			w("")
			w("| type | slots |")
			w("|---|---:|")
			for t, c in f["top_handle_types"]:
				w(f"| `{t}` | {c} |")
			w("")
		v = r.get("verify")
		if v:
			g12 = f["group1"] + f["group2"]
			c, g = v["concrete"], v["instantiated"]
			w("### swiftc verification")
			w("")
			w("Two pools, because they answer different questions. **Concrete**: no type parameter anywhere, no")
			w("`Self`, a concrete non-generic receiver — the stub is what the generator would print, with nothing")
			w("supplied by the harness. **Instantiated**: generic symbols, with each type parameter and `Self`")
			w("replaced by a witness the harness picks (`Int`, `String`, `Double`, `Array<Int>`, a local `Error`")
			w("type, or a concrete conformer taken from the graph's `conformsTo` edges). A call site would have")
			w("supplied that instantiation (§5); without an application there is none, so a failure in this pool")
			w("may be the witness's fault and not the reprint's. Both samples are")
			w("`random.Random(0).sample(pool, n)` over the pool sorted by USR: seeded, reproducible, drawn before")
			w("any stub was looked at.")
			w("")
			w("| pool | size | share of groups 1+2 | sampled | compiled | |")
			w("|---|---:|---:|---:|---:|---:|")
			w(f"| concrete | {c['pool']} | {pct(c['pool'], g12)} | {c['sampled']} | {c['compiled']} | "
				f"**{pct(c['compiled'], c['sampled'])}** |")
			w(f"| instantiated | {g['pool']} | {pct(g['pool'], g12)} | {g['sampled']} | {g['compiled']} | "
				f"**{pct(g['compiled'], g['sampled'])}** |")
			w("")
			if c["pool"] == 0:
				w("The concrete pool is empty: this module has no public function-like symbol that is free of")
				w("type parameters and hangs off a non-generic type. Everything it exports is generic.")
				w("")
			if g["pool"] == 0:
				w("The instantiated pool is empty: every generic symbol here is either refused outright (group 3)")
				w("or has a constraint the harness cannot pick a witness for.")
				w("")
			w("`swiftc -typecheck -swift-version 5`, one file per stub.")
			w("")
			if c["pool_builder"] or g["pool_builder"]:
				w(f"Builder-annotated symbols in the pools: {c['pool_builder']} concrete, "
					f"{g['pool_builder']} instantiated; {c['sampled_builder'] + g['sampled_builder']} of them "
					f"were sampled and {c['failed_builder'] + g['failed_builder']} failed. That is the check on")
				w("the paragraph above: the attribute is dropped, the closure type is reprinted, and swiftc")
				w("accepts the call.")
				w("")
			for label, arm in (("concrete", c), ("instantiated", g)):
				if not arm["failures"]:
					continue
				by_class = collections.Counter(fl["class"] for fl in arm["failures"])
				w(f"Failures, {label} pool ({len(arm['failures'])}), by whose fault they are:")
				w("")
				w("| fault | count |")
				w("|---|---:|")
				for k, v2 in by_class.most_common():
					w(f"| {k} | {v2} |")
				w("")
				w("| symbol | group | error |")
				w("|---|---:|---|")
				for fl in arm["failures"][:20]:
					err = fl["error"].replace("|", "\\|")[:150]
					w(f"| `{fl['title']}` | {fl['group']} | {err} |")
				w("")
		s = r["sensitivity"]
		w(f"Sensitivity of the value set: adding {', '.join('`' + a + '`' for a in s['added'])} to it would move")
		moved = s["value_set_widened_moves_to_group1"]
		w(f"{moved} symbol{'' if moved == 1 else 's'} from group 2 to group 1 "
			f"({pct(s['value_set_widened_moves_to_group1'], max(f['group2'], 1))} of group 2).")
		w("")

	w("## Method notes")
	w("")
	w("- The stub emitted for verification is a wrapper that both reprints the signature and calls the symbol:")
	w("  a declaration that type-checks but cannot be called would not be evidence of anything. Instance")
	w("  methods take the receiver as a leading parameter, `mutating` ones take it `inout` (§5: `mutating` *is*")
	w("  `inout self`), initialisers return the owning type, and closure parameters are forwarded `@escaping`.")
	w("- `-swift-version 5`, not 6: §5 already records that `Sendable` is bypassed by construction and that")
	w("  actor isolation becomes a runtime problem on our boundary, so Swift 6's strict-concurrency errors")
	w("  would be measuring a rule the design has already declined. Under Swift 5 they are warnings.")
	w("- Every stub carries `@available(macOS 26.0, *)`, which satisfies any `introduced` bound in the SDK;")
	w("  symbols marked unavailable or obsoleted on macOS are dropped from the pool instead.")
	w("- `self-requirement-protocol` is detected by a proxy: a protocol that declares an associated type, or")
	w("  whose members mention `Self`, and whose own declaration names no primary associated type. Swift's own")
	w("  answer is the swiftc column, which is why the sample exists. It fires only where a parameter or")
	w("  return spells `any P`. A constraint `T: P` is not a refusal — the call site supplies `T` (§5,")
	w("  generics) — and `some P` is an opaque type swiftc resolves; counting either would refuse most of a")
	w("  declarative framework for mentioning its own protocol. It is **reported and not counted as a refusal**:")
	w("  `any P` has type-checked since Swift 5.7 (SE-0309), and the criterion is that a value crosses back,")
	w("  which a handle to an existential does — so §5 moved it out of the refusal list and this follows.")
	w("- `property-wrapper-as-api` fires on members of a property-wrapper type, which is where the wrapper is")
	w("  the declaration form (`@State var x`) and nothing is called. A wrapper *in a slot* is not counted: it")
	w("  is a handle that can be built — `Binding(get:set:)` and `State(initialValue:)` are public, and a stub")
	w("  taking `Binding<Bool>` compiles and calls. Wrapper-typed slots are reported per module as information.")
	w("- Witnesses for the instantiated pool come from `swiftGenerics.constraints` **and** from the printed")
	w("  `where` clause, because the key is not always populated: a member of a protocol extension arrives")
	w("  with `where MenuItems : View` in its declaration and an empty constraint list, and a harness that")
	w("  reads only the structured field picks a witness conforming to nothing. That is the same graph gap as")
	w("  the ownership modifiers `functionSignature` drops — the print carries what the structure does not.")
	w("- Cost of the measurement itself, for whoever reruns it: the graphs total "
		f"{sum(r['graph_bytes'] for r in results) / 1e6:.0f} MB, extraction is minutes and analysis is seconds;")
	w("  peak resident memory is about 4 GB, set by SwiftUI's single 450 MB JSON document, which is why the")
	w("  graph is streamed element by element rather than parsed whole. With every graph already extracted the")
	w("  whole run — four modules classified and 325 stubs type-checked — is under two minutes; the")
	w("  classification alone is 34 s, which is what makes an index of the whole declared surface cheap.")
	w("")
	w("## Reading")
	w("")
	w("Which numbers are decisive is a judgement, so it is fenced off here and the decision is not taken.")
	w("")
	arms = [r["verify"][k] for r in results if r.get("verify") for k in ("concrete", "instantiated")]
	sampled = sum(a["sampled"] for a in arms)
	ok = sum(a["compiled"] for a in arms)
	decl_faults = sum(1 for a in arms for fl in a["failures"] if fl["class"] == "the reprinted declaration")
	w(f"- The swiftc columns say the **classification is not lying**: {ok} of {sampled} stubs compiled over all")
	w(f"  the modules, and the reprinted declaration's own fault accounts for {decl_faults} of the "
		f"{sampled - ok} failures —")
	w("  the rest are the harness's witness choice, or a graph that still names a renamed ObjC spelling. So the")
	w("  group shares can be read as they stand, in the worst-case module as well as the easy one.")
	w("- **The number the criterion asks for is 1 + 2, because the criterion is \"crosses and crosses back\".**")
	w("  Not all Swift data becoming Clojure data is the normal mode, not a partial failure; what has to hold")
	w("  is that a handle passed back into Swift works. Group 2 is therefore read as the shape of the")
	w("  boundary, and the 1/2 split as information about how much arrives as data.")
	w("- **The other half of the criterion is not measured here: a handle has to be a legal value on this")
	w("  side, not only travel back.** Putting one in a set or using it as a map key is something a Clojure")
	w("  programmer does without thinking, and it breaks silently while equality and hash stay pointer")
	w("  identity — for a boxed struct that means \"the same box\", an artifact of how the value was stored.")
	w("  §5 answers it by printing `==` and `hash(into:)` for a box from the type's own `Equatable`/`Hashable`,")
	w("  and by refusing to be a key where the type has neither.")
	w("- **The data share understates, and measuring a whole public surface is why.** The largest handle slots")
	w("  are conformance plumbing no Clojure program reaches for — in Foundation, `inout Hasher`, `any Decoder`,")
	w("  `any Encoder`, `[Self.Element]` — and `Hasher`/`Encoder`/`Decoder` are **our own** implementation path")
	w("  on this boundary: the `Codable` fallback §5 keeps for types the generator has not seen, and the")
	w("  `==`/`hash(into:)` a box prints. §5's premise is that generation follows call sites and")
	w("  not the SDK, so the share that decides anything needs a call list from a real application. The")
	w("  instrument answers that the moment such a list exists; it does not exist here.")
	tails = ", ".join(f"{r['module']} `{r['funclike']['causes'][0][0]}`"
		for r in results if r["funclike"]["causes"])
	w("- **The tail is small everywhere, and the cost is that almost everything is a handle.** The largest")
	w(f"  single cause per module is {tails} — argument-passing forms, plus the one case where a declaration")
	w("  form rather than a call is what does not move (a property wrapper). Nothing in the tail is the size")
	w("  §5 feared, and group 2, large as it is, is not a cost in reach — what it costs is the obligations §5")
	w("  puts on a handle: equality, hash, and an accessor for every field the public shape exposes.")
	w("- Two of the separately-counted \"decided\" lines are read as costs rather than footnotes: structs with")
	w("  no public initialiser, which §5 predicted would fill the tail, and global-actor isolation. The second")
	w("  is not a thunk apiece: the hop is conditional — elided when the caller is already on the main carrier —")
	w("  and one shared helper performs it, so its cost is classification correctness, because a missed")
	w("  isolated symbol is a runtime failure where Swift would have given a compile error. The share itself")
	w("  is that correctness at work: SwiftUI's modifiers are `nonisolated` members of `@MainActor` protocols,")
	w("  and a classifier inheriting the protocol's actor without reading `nonisolated` counts them isolated.")
	w("")
	return "\n".join(L) + "\n"


# ---------------------------------------------------------------------------

def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("modules", nargs="*", help="NAME or NAME=SWIFTMODULE_SEARCH_PATH")
	ap.add_argument("--spm", action="append", default=[], metavar="URL=VERSION=PRODUCT",
		help="add a SwiftPM product; all of them go into one throwaway package built under --work")
	ap.add_argument("--work", default=".build/swift-reprint")
	ap.add_argument("--out", default="docs/swift-reprint.md")
	ap.add_argument("--target", default=HOST_TARGET)
	ap.add_argument("--sample", type=int, default=100)
	ap.add_argument("--no-verify", action="store_true")
	args = ap.parse_args()

	def progress(msg):
		print(msg, file=sys.stderr, flush=True)

	os.makedirs(args.work, exist_ok=True)
	sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
	sdk_version = subprocess.run(["xcrun", "--show-sdk-version"], capture_output=True, text=True).stdout.strip()
	swift_ver = subprocess.run(["swift", "--version"], capture_output=True, text=True).stdout.strip().splitlines()
	swift_ver = next((l for l in swift_ver if "Apple Swift version" in l), swift_ver[0] if swift_ver else "?")

	specs = []			# (module, import_path, extra_imports, note)
	for m in args.modules:
		name, _, ipath = m.partition("=")
		specs.append((name, ipath or None, EXTRA_IMPORTS.get(name, [name]), ""))
	if args.spm:
		parsed = []
		for s in args.spm:
			url, version, product = s.split("=")
			parsed.append((url, version, product))
		mod_dir, products = build_spm_probe(parsed, args.work, progress)
		for p in products:
			specs.append((p, mod_dir, [p], ""))

	results = []
	generator = "?"
	for module, ipath, imports, _note in specs:
		gdir = extract(module, args.target, sdk, os.path.join(args.work, "sg", module), ipath, progress)
		sibs, dangling_before, dangling_after = abi_siblings(module, graph_files(gdir), sdk, args.target,
			args.work, ipath, progress)
		if sibs:
			progress(f"{module}: re-exported siblings {', '.join(n for n, _ in sibs)}")
		res, _recs, _idx = analyze(module, [gdir] + [d for _, d in sibs], args.work, args.sample, ipath,
			imports, progress, do_verify=not args.no_verify)
		res["reexports"] = [n for n, _ in sibs]
		res["dangling_before"] = dangling_before
		res["dangling_after"] = dangling_after
		generator = res["generator"]
		res["note"] = MODULE_NOTES.get(module, "")
		results.append(res)
		with open(os.path.join(args.work, f"{module}.json"), "w") as f:
			json.dump(res, f, indent=1)

	meta = {"swift": swift_ver, "target": args.target, "sdk": sdk, "sdk_version": sdk_version,
		"generator": generator,
		"date": subprocess.run(["date", "+%Y-%m-%d"], capture_output=True, text=True).stdout.strip()}
	text = render(results, meta)
	with open(args.out, "w") as f:
		f.write(text)
	progress(f"wrote {args.out}")


# A module's graph names types it only conforms to; the stub has to import those modules as well.
EXTRA_IMPORTS = {
	"Foundation": ["Foundation", "Combine"],
	"SwiftUI": ["SwiftUI", "Foundation", "Combine", "CoreGraphics", "UniformTypeIdentifiers",
		"Symbols", "Spatial", "Observation", "CoreData", "OSLog"],
}

# Appended to a module's summary bullet where its number needs a word to be read honestly.
SUMMARY_REMARKS = {
	"ArgumentParser": " The refusals are `@Argument`/`@Option`/`@Flag` — the declaration form that *is* this "
		"module's API, which is why its refused share is the largest of the four.",
}

MODULE_NOTES = {
	"Foundation": "The large, half-imported-from-ObjC case: what the boundary looks like where most of the "
		"surface arrived through the Clang importer.",
	"SwiftUI": "The worst case, chosen for it: opaque return types, heavy generics, a surface built out of "
		"result builders — and a framework that ships as two modules, so the graph of one is not the surface "
		"of one `import`.",
	"ArgumentParser": "Someone else's code, built from source — the §5 case with no `.swiftinterface` at all "
		"(checked: the build directory has `.swiftmodule` and nothing else).",
	"OrderedCollections": "A second dependency built from source: a collection library, where the surface is "
		"generic by construction rather than by taste.",
}


if __name__ == "__main__":
	main()
