#!/usr/bin/env python3
# @ai-generated(solo)
"""Fails when a heap object gets a clj_value written around the slot primitives (design §4, «Запись в слот»).

Two checks over the C sources. A struct that is a heap object -- led by a clj_header, or by another heap struct
-- declares no plain clj_value field, atomic or not: an edge is a clj_slot or a clj_atomic_slot, which a bare
assignment does not compile into, and a value the object keeps for its own execution only is a clj_private_value.
And only object.h, where the primitives live, writes the word inside a slot: an assignment to `.v` or its address
taken anywhere else is a store that skips the publication.
"""
import os
import re
import sys

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), ".."))
SCOPE = ["Sources/CljCore", "Sources/CljCompiler", "Sources/clj-load", "Sources/clj-facts"]
PRIMITIVES = "Sources/CljCore/include/clj/object.h"
GENERATED = "Sources/CljCore/boot/"

STRUCT = re.compile(r"\bstruct\s*(\w*)\s*\{")
TYPEDEF_NAME = re.compile(r"^\s*(\w+)\s*;")
FIELD_VALUE = re.compile(r"^\s*(?:const\s+)?(?:_Atomic\s*\(\s*clj_value\s*\)|(?:_Atomic\s+)?clj_value)\b(?!\s*\()")
SLOT_WRITE = re.compile(r"\.v\s*(?:[-+*/|&^]?=)(?!=)|(?:[(,=?:]|\breturn)\s*&(?!&)\s*\w+(?:(?:->|\.)\w+|\[[^\]]*\])*\.v\b|\.v\s*(?:\+\+|--)")


def sources():
	for top in SCOPE:
		for d, _, files in os.walk(os.path.join(ROOT, top)):
			for f in sorted(files):
				rel = os.path.relpath(os.path.join(d, f), ROOT)
				if rel.startswith(GENERATED) or not f.endswith((".c", ".h")):
					continue
				yield rel


def strip_comments(text):
	text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
	text = re.sub(r"//[^\n]*", "", text)
	return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)


def structs(text):
	"""(name or typedef name, first member type, body text, body offset) per struct definition."""
	for m in STRUCT.finditer(text):
		depth, i = 1, m.end()
		while depth and i < len(text):
			depth += {"{": 1, "}": -1}.get(text[i], 0)
			i += 1
		body = text[m.end():i - 1]
		name = m.group(1)
		after = TYPEDEF_NAME.match(text[i:])
		if after and text[max(0, m.start() - 8):m.start()].strip().endswith("typedef"):
			name = after.group(1)
		first = re.match(r"\s*(?:const\s+)?(?:struct\s+)?(\w+)\s+\w+\s*[;\[]", body)
		yield name, first.group(1) if first else "", body, m.end()


def main():
	texts = {rel: strip_comments(open(os.path.join(ROOT, rel), encoding="utf-8").read()) for rel in sources()}
	found = [(rel, s) for rel, t in texts.items() for s in structs(t)]
	heap = {"clj_header"}
	changed = True
	while changed:
		changed = False
		for _, (name, first, _, _) in found:
			if name and name not in heap and first in heap:
				heap.add(name)
				changed = True
	errors = []
	for rel, (name, first, body, at) in found:
		if first not in heap:
			continue
		text = texts[rel]
		for line in body.split(";"):
			if FIELD_VALUE.match(line.split("{")[-1]):
				lineno = text.count("\n", 0, at + body.find(line.strip())) + 1
				errors.append(f"{rel}:{lineno}: {name or 'a struct'} declares a clj_value field: make it a clj_slot "
				              f"(an edge), or a clj_private_value (its own execution's, never visited)")
	for rel, text in texts.items():
		if rel == PRIMITIVES:
			continue
		for m in SLOT_WRITE.finditer(text):
			lineno = text.count("\n", 0, m.start()) + 1
			errors.append(f"{rel}:{lineno}: writes a slot's word directly; go through clj_slot_store/clj_slot_init")
	if errors:
		print("\n".join(errors))
		print(f"slot-audit: {len(errors)} store(s) around the slot primitives (design §4, «Запись в слот»)")
		return 1
	print(f"slot-audit: {len(heap) - 1} heap object types, every slot written through the primitives")
	return 0


sys.exit(main())
