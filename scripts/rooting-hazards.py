#!/usr/bin/env python3
# @ai-generated(solo)
"""The rooting hazard analysis of the runtime's C (design §7, «Фаза 3», «Код рантайма»; docs/notes/rc.md).

A parked coroutine's references are read by the collector only where something names them. This fails the build on
a reference a function holds across a call that may park and that nothing names:

- may park: clj_park (CLJ_MAY_PARK), every function that calls one that may park, and every call through a function
  pointer, unless the field or typedef it is read through is CLJ_NO_PARK; a function stored into a CLJ_NO_PARK
  field or passed as a CLJ_NO_PARK typedef must not park itself. A function with no body here (libc, the host)
  does not park: parking under host_depth > 0 is an error (design §5 «Вызов внутрь»).
- held: a local of clj_value, an array or struct of them, a clj_value * or a heap object pointer, live after the
  call (read on some path before it is assigned again) or whose address the call gets. A parameter is held only
  when CLJ_OWNED or released by the function itself: an unmarked one is the caller's +0.
- named: registered by a CLJ_ROOTS/CLJ_ROOTS_SPAN in effect at the call (the whole variable, or the field path a
  struct's owned field sits at), or declared CLJ_BORROWED. A struct field is owned unless CLJ_BORROWED.
- a heap stack: a struct field pointing at an array of structs that hold references. Each entry's owned field is
  held while the field is, and CLJ_ROOTS_FIELD names one field of every entry (CLJ_ROOTS_WALK, a walker of the
  whole variable, what no span describes: a linked list); a local pointer into one, or an array of such pointers, is
  a view.
- a temporary: a call's owned result that is an operand of a call or an initializer is held by no variable while
  another operand runs; one that may park is a hazard, unless the initializer's field is CLJ_BORROWED or CLJ_NO_CYCLE.

Also: no write to a slot's word outside object.h, and no plain clj_value field in a heap object (design §4
«Запись в слот»).

Assumed, not checked: a callee does not keep a local's address past the call it got it in (a pointer stored
elsewhere makes the local held to the end of its scope); the debug configuration's code is the release one's.
The runtime oracle (cc.c, cc/frame-missed) checks what this cannot.
"""
import argparse
import multiprocessing
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import libclang as C  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SCOPE = ["Sources/CljCore", "Sources/CljCompiler"]
# Hosts: their parks are errors (design §5 «Вызов внутрь»), so only the slot rule applies.
HOSTS = ["Sources/clj-load", "Sources/clj-facts"]
INCLUDES = ["Sources/CljCore", "Sources/CljCore/include", "Sources/CljCompiler/include"]
PRIMITIVES = "Sources/CljCore/include/clj/object.h"
FIXTURES = "scripts/rooting-fixtures"
VALUE_TYPEDEFS = {"clj_value"}
SLOT_TYPEDEFS = {"clj_slot", "clj_atomic_slot"}
FIELD_REG = re.compile(r"\bCLJ_ROOTS_FIELD\([^,]+,[^,]+,\s*([\w.\s]+)\)")
MARK = re.compile(r"\bCLJ_(BORROWED|OWNED|NO_PARK|MAY_PARK|NO_CYCLE|RING)\b")
NORETURN = {"abort", "exit", "_exit", "longjmp", "siglongjmp", "_longjmp", "__builtin_unreachable", "__builtin_trap",
            "__assert_rtn", "pthread_exit", "__builtin_abort"}
RELEASE = {"clj_release"}

LAND, LOR, ASSIGN, COMMA = 20, 21, 22, 33
ADDR_OF, PRE_INC, PRE_DEC, POST_INC, POST_DEC = 5, 3, 4, 1, 2


def rel(path):
	return os.path.relpath(path, ROOT) if path else path


_texts = {}


def source(path):
	t = _texts.get(path)
	if t is None:
		with open(path, "rb") as f:
			t = _texts[path] = f.read().decode("latin-1")  # libclang offsets are bytes
	return t


def head_text(c, stop="=;{,)"):
	"""The declaration's text around its name: from its line's start or the delimiter before it on that line, to
	the delimiter after it at paren depth 0. A mark goes first in the declaration, so a comment above never counts."""
	path, _, _, off = C.location(c)
	if not path:
		return ""
	text = source(path)
	b, _ = C.extent(c)
	i = min(b, off)
	while i > 0 and text[i - 1] not in ";{}(,\n":
		i -= 1
	j, depth = off, 0
	while j < len(text):
		ch = text[j]
		if ch in "([":
			depth += 1
		elif ch in ")]":
			if depth == 0:
				break
			depth -= 1
		elif depth == 0 and ch in stop:
			break
		j += 1
	return re.sub(r"//[^\n]*|/\*.*?\*/", " ", text[i:j], flags=re.S)


def marks(c):
	return set(MARK.findall(head_text(c)))


def marks_fn(c):
	return set(MARK.findall(head_text(c, ";{")))


def strip(c):
	while c.kind in (C.UNEXPOSED_EXPR, C.PAREN_EXPR, C.CSTYLE_CAST_EXPR):
		ch = [x for x in C.children(c) if 100 <= x.kind < 200]
		if len(ch) != 1:
			break
		c = ch[0]
	return c


class Types:
	"""Which types hold a reference, per translation unit."""

	def __init__(self):
		self.memo = {}
		self.heap = {}
		self.borrowed_fns = set()

	def typedef_chain(self, t):
		names = []
		for _ in range(16):
			if t.kind == C.TYPE_ELABORATED:
				t = C.lib().clang_Type_getNamedType(t)
				continue
			if t.kind != C.TYPE_TYPEDEF:
				break
			d = C.lib().clang_getTypeDeclaration(t)
			names.append((C.spelling(d), d))
			t = C.lib().clang_getTypedefDeclUnderlyingType(d)
		return names, t

	def typedef_marks(self, t):
		out = set()
		names, _ = self.typedef_chain(t)
		for _, d in names:
			out |= marks(d)
		return out

	def is_heap_record(self, decl):
		key = C.usr(decl)
		if key in self.heap:
			return self.heap[key]
		self.heap[key] = False
		fields = [f for f in C.children(decl) if f.kind == C.FIELD_DECL]
		r = False
		if fields:
			names, t = self.typedef_chain(C.ctype(fields[0]))
			if any(n == "clj_header" for n, _ in names):
				r = True
			elif t.kind == C.TYPE_RECORD:
				r = self.is_heap_record(C.lib().clang_getTypeDeclaration(t))
		self.heap[key] = r
		return r

	def holder(self, t):
		"""None, ("value",), ("array", h), ("struct", [(field, marks, h)]), ("vptr",) or ("objptr",)."""
		key = C.type_spelling(t)
		if key in self.memo:
			return self.memo[key]
		self.memo[key] = None
		names, base = self.typedef_chain(t)
		h = None
		if any(n in VALUE_TYPEDEFS for n, _ in names):
			h = ("value",)
		elif any(n in SLOT_TYPEDEFS for n, _ in names):
			h = ("value",)
		elif base.kind in (C.TYPE_CONSTANT_ARRAY, C.TYPE_INCOMPLETE_ARRAY):
			inner = self.holder(C.lib().clang_getArrayElementType(base))
			# an array of pointers into structs is views (a wake list's queue nodes), not a heap stack
			h = ("array", inner) if inner and inner[0] != "sptr" else None
		elif base.kind == C.TYPE_RECORD:
			decl = C.lib().clang_getTypeDeclaration(base)
			if not self.is_heap_record(decl):
				fields = []
				for f in C.children(decl):
					if f.kind != C.FIELD_DECL:
						continue
					fh = self.holder(C.ctype(f))
					if fh:
						fields.append((C.spelling(f), marks(f), fh))
				h = ("struct", fields) if fields else None
		elif base.kind == C.TYPE_POINTER:
			p = C.lib().clang_getPointeeType(base)
			pn, pb = self.typedef_chain(p)
			if any(n in VALUE_TYPEDEFS for n, _ in pn):
				h = ("vptr",)
			elif pb.kind == C.TYPE_RECORD:
				decl = C.lib().clang_getTypeDeclaration(pb)
				if self.is_heap_record(decl) and "NO_CYCLE" not in marks(C.definition(decl) if not C.is_null(C.definition(decl)) else decl):
					h = ("objptr",)
				elif not self.is_heap_record(decl):
					inner = self.holder(p)
					if inner and inner[0] == "struct" and owned_paths(inner):
						h = ("sptr", inner)
		elif base.kind == C.TYPE_ATOMIC:
			h = self.holder(C.lib().clang_Type_getValueType(base))
		self.memo[key] = h
		return h


def owned_paths(h, prefix=()):
	"""The paths below a holder that hold a reference the holder owns: a CLJ_BORROWED field holds none."""
	if h[0] == "struct":
		out = []
		for name, m, fh in h[1]:
			if m & {"BORROWED", "NO_CYCLE", "RING"}:
				continue
			out += owned_paths(fh, prefix + (name,))
		return out
	# An array of structs: its elements own what the struct's fields do; a name of any element field path covers all.
	if h[0] == "array" and h[1][0] == "struct":
		return owned_paths(h[1], prefix)
	# A heap stack: each entry owns what the struct's fields do, named field by field (CLJ_ROOTS_FIELD).
	if h[0] == "sptr":
		return owned_paths(h[1], prefix + ("*",))
	return [prefix]


class Node:
	__slots__ = ("kind", "var", "site", "succ", "live")

	def __init__(self, kind, var=None, site=None):
		self.kind, self.var, self.site, self.succ, self.live = kind, var, site, [], 0


class Var:
	__slots__ = ("idx", "name", "holder", "marks", "param", "scope", "decl_off", "escape", "line", "owned", "copies", "tname")


class Fn:
	"""One function body: its control flow over uses, assignments and calls of tracked variables."""

	def __init__(self, tu, types, cursor, out):
		self.tu, self.types, self.cursor, self.out = tu, types, cursor, out
		self.name = C.spelling(cursor)
		self.key = C.usr(cursor)
		self.path = C.location(cursor)[0]
		self.vars = {}
		self.var_list = []
		self.regs = []
		self.calls = []
		self.nodes = []
		self.loops = []
		self.switches = []
		self.labels = {}
		self.gotos = []
		self.exit = self.node("exit")
		self.scopes = []

	def node(self, kind, var=None, site=None):
		n = Node(kind, var, site)
		self.nodes.append(n)
		return n

	def link(self, front, n):
		for p in front:
			p.succ.append(n)
		return [n]

	def line(self, c):
		return C.location(c)[1]

	# ---- variables

	def var_of(self, c):
		"""The tracked variable a DeclRefExpr names, or None."""
		if c.kind != C.DECL_REF_EXPR:
			return None
		d = C.referenced(c)
		if C.is_null(d) or d.kind not in (C.VAR_DECL, C.PARM_DECL):
			return None
		return self.vars.get(self.decl_key(d))

	def decl_key(self, d):
		_, line, col, off = C.location(d)
		return (C.spelling(d), off)

	def declare(self, d, param):
		# CX_SC_Extern, CX_SC_Static: no stack slot
		if d.kind == C.VAR_DECL and C.lib().clang_Cursor_getStorageClass(d) in (2, 3):
			return None
		h = self.types.holder(C.ctype(d))
		if not h:
			return None
		v = Var()
		v.idx = len(self.var_list)
		v.name = C.spelling(d)
		v.holder = h
		v.marks = marks(d)
		if "RING" in v.marks and not self.path.endswith("/eval.c"):
			self.out["errors"].append((rel(self.path), C.location(d)[1], f"{self.name}: {v.name} is CLJ_RING outside eval.c: "
			                           "only the interpreter names references through its ring"))
		v.param = param
		v.scope = self.scopes[-1] if self.scopes else (0, 1 << 40)
		v.decl_off = C.location(d)[3]
		v.escape = None
		v.owned = False
		v.copies = set()
		v.tname = C.type_spelling(C.ctype(d))
		v.line = self.line(d)
		self.vars[self.decl_key(d)] = v
		self.var_list.append(v)
		return v

	def path_of(self, c):
		"""(var, field path) of an lvalue made of '.' and '[]' over a tracked local, else None."""
		c = strip(c)
		fields = []
		while True:
			if c.kind == C.DECL_REF_EXPR:
				v = self.var_of(c)
				return (v, tuple(reversed(fields))) if v else None
			if c.kind == C.MEMBER_REF_EXPR:
				ch = C.children(c)
				if not ch:
					return None
				base = strip(ch[0])
				bt = C.lib().clang_getCanonicalType(C.ctype(ch[0]))
				if bt.kind == C.TYPE_POINTER:
					return None
				fields.append(C.spelling(c))
				c = base
				continue
			if c.kind == C.ARRAY_SUBSCRIPT_EXPR:
				ch = C.children(c)
				bt = C.lib().clang_getCanonicalType(C.ctype(strip(ch[0])))
				if bt.kind == C.TYPE_POINTER:
					return None
				c = strip(ch[0])
				continue
			return None

	# ---- ownership: a call's result is owned (object.h, the slot convention), unless the callee is CLJ_BORROWED

	def fn_marks(self, d):
		m = marks_fn(d)
		dd = C.definition(d)
		if not C.is_null(dd):
			m |= marks_fn(dd)
		return m

	def source(self, v, e):
		"""Records what e hands v: a call's result makes it owned, a copy of a tracked variable ties it to that one."""
		if v is None or v.holder[0] == "struct":
			return
		e = strip(e)
		k = e.kind
		if k == C.CALL_EXPR:
			ch = C.children(e)
			f = strip(ch[0]) if ch else None
			name = "a call through a pointer"
			if f is not None and f.kind == C.DECL_REF_EXPR:
				d = C.referenced(f)
				if not C.is_null(d) and d.kind == C.FUNCTION_DECL:
					if C.usr(d) in self.types.borrowed_fns or self.fn_marks(d) & {"BORROWED", "NO_CYCLE"}:
						return
					name = C.spelling(d)
			v.owned = v.owned or f"{name}'s result, line {C.location(e)[1]}"
		elif k == C.DECL_REF_EXPR:
			w = self.var_of(e)
			if w is not None and w is not v:
				v.copies.add(w.idx)
		elif k == C.CONDITIONAL_OPERATOR:
			ch = C.children(e)
			for x in ch[1:]:
				self.source(v, x)
		elif k == C.BINARY_OPERATOR and C.binary_op(e) in (COMMA, ASSIGN):
			self.source(v, C.children(e)[-1])
		elif k == C.INIT_LIST_EXPR:
			for x in C.children(e):
				self.source(v, x)
		elif k == C.STMT_EXPR:
			v.owned = v.owned or f"a statement expression, line {C.location(e)[1]}"

	# ---- expressions

	def use(self, v, front):
		return self.link(front, self.node("use", v))

	def ex(self, c, front):
		k = c.kind
		if k == C.DECL_REF_EXPR:
			v = self.var_of(c)
			return self.use(v, front) if v else front
		if k == C.CALL_EXPR:
			return self.call(c, front)
		if k == C.BINARY_OPERATOR:
			op = C.binary_op(c)
			ch = C.children(c)
			if op in (LAND, LOR) and len(ch) == 2:
				f1 = self.ex(ch[0], front)
				return f1 + self.ex(ch[1], f1)
			if op == ASSIGN and len(ch) == 2:
				f = self.ex(ch[1], front)
				lhs = strip(ch[0])
				v = self.var_of(lhs)
				if v:
					self.source(v, ch[1])
					return self.link(f, self.node("def", v))
				p = self.path_of(lhs)
				if p and not p[1]:
					self.source(p[0], ch[1])
				return self.ex(ch[0], f)
			for x in ch:
				front = self.ex(x, front)
			return front
		if k == C.CONDITIONAL_OPERATOR:
			ch = C.children(c)
			if len(ch) == 3:
				f = self.ex(ch[0], front)
				return self.ex(ch[1], f) + self.ex(ch[2], f)
		if k == C.UNARY_OPERATOR and C.unary_op(c) == ADDR_OF:
			p = self.path_of(C.children(c)[0])
			if p:
				self.escape(p[0], c)
				return self.use(p[0], front)
		if k == C.STMT_EXPR:
			for x in C.children(c):
				front = self.st(x, front)
			return front
		# CXCursor_UnaryExpr is sizeof, alignof and the like: an unevaluated operand reads nothing
		if k in (C.BLOCK_EXPR, 136):
			return front
		if k == C.INIT_LIST_EXPR:
			self.temporaries(c, C.children(c), self.unowned_fields(c))
		for x in C.children(c):
			front = self.ex(x, front) if 100 <= x.kind < 300 else front
		return front

	def escape(self, v, c):
		off = C.location(c)[3]
		if v.escape is None or off < v.escape:
			v.escape = off

	def callee(self, c, ch):
		"""('direct', usr, name, noreturn) or ('indirect', description, no_park)."""
		if not ch:
			return ("indirect", "an unknown callee", False)
		f = strip(ch[0])
		if f.kind == C.DECL_REF_EXPR:
			d = C.referenced(f)
			if not C.is_null(d) and d.kind == C.FUNCTION_DECL:
				name = C.spelling(d)
				nr = name in NORETURN or "noreturn" in head_text(d, ";{") or "_Noreturn" in head_text(d, ";{")
				return ("direct", C.usr(d), name, nr)
		desc, no_park = "a function pointer", False
		m = self.types.typedef_marks(C.ctype(ch[0]))
		if f.kind == C.MEMBER_REF_EXPR:
			d = C.referenced(f)
			desc = f"the {C.spelling(f)} slot"
			if not C.is_null(d):
				m |= marks(d) | self.types.typedef_marks(C.ctype(d))
		elif f.kind == C.DECL_REF_EXPR:
			d = C.referenced(f)
			desc = f"the function pointer {C.spelling(f)}"
			if not C.is_null(d):
				m |= marks(d) | self.types.typedef_marks(C.ctype(d))
				if d.kind == C.PARM_DECL:
					m |= self.parm_marks(d)
		elif f.kind == C.ARRAY_SUBSCRIPT_EXPR:
			base = strip(C.children(f)[0])
			if base.kind in (C.DECL_REF_EXPR, C.MEMBER_REF_EXPR):
				d = C.referenced(base)
				desc = f"the function pointer table {C.spelling(base)}"
				if not C.is_null(d):
					m |= marks(d)
		return ("indirect", desc, "NO_PARK" in m)

	def call(self, c, front):
		ch = C.children(c)
		info = self.callee(c, ch)
		if info[0] == "indirect" and ch:
			front = self.ex(ch[0], front)
		addr, given = [], []
		params = []
		if info[0] == "direct":
			d = C.referenced(strip(ch[0]))
			params = [p for p in C.children(d) if p.kind == C.PARM_DECL]
		for i, a in enumerate(ch[1:]):
			s = strip(a)
			if s.kind == C.UNARY_OPERATOR and C.unary_op(s) == ADDR_OF:
				p = self.path_of(C.children(s)[0])
				if p:
					addr.append(p[0].idx)
					if not p[1]:
						p[0].owned = p[0].owned or f"written through its address by {info[2] if info[0] == 'direct' else info[1]}, line {C.location(s)[1]}"
					front = self.use(p[0], front)
					continue
			v = self.var_of(s)
			if v and info[0] == "direct" and (info[2] in RELEASE and i == 0 or "OWNED" in self.param_marks(strip(ch[0]), i)):
				v.owned = v.owned or f"handed to {info[2]}, line {C.location(s)[1]}"
				given.append(v)
			self.fn_ref(s, lambda: (self.param_marks(strip(ch[0]), i), f"parameter {i + 1} of {info[2]}"))
			front = self.ex(a, front)
		self.temporaries(c, ch[1:])
		site = len(self.calls)
		_, line, col, off = C.location(c)
		self.calls.append({"line": line, "off": off, "callee": info, "addr": addr, "live": 0, "given": [v.idx for v in given],
		                   "stale": 0})
		n = self.node("call", site=site)
		front = self.link(front, n)
		for v in given:
			front = self.link(front, self.node("given", v))
		if info[0] == "direct" and info[3]:
			return []
		return front

	def temp_of(self, e):
		"""The callee's name when e is a call whose result is a reference nothing holds yet, else None."""
		e = strip(e)
		if e.kind != C.CALL_EXPR:
			return None
		ch = C.children(e)
		f = strip(ch[0]) if ch else None
		if f is None or f.kind != C.DECL_REF_EXPR:
			return None
		d = C.referenced(f)
		if C.is_null(d) or d.kind != C.FUNCTION_DECL:
			return None
		if C.usr(d) in self.types.borrowed_fns or self.fn_marks(d) & {"BORROWED", "NO_CYCLE"}:
			return None
		h = self.types.holder(C.lib().clang_getResultType(C.ctype(d)))
		return C.spelling(d) if h and h[0] in ("value", "objptr") else None

	def calls_in(self, e, acc):
		"""The direct callees and the may-park indirect calls evaluated in e."""
		if e.kind == C.CALL_EXPR:
			info = self.callee(e, C.children(e))
			if info[0] == "direct":
				acc.add(info[1])
			elif not info[2]:
				acc.add(("indirect", info[1]))
		if e.kind in (C.BLOCK_EXPR, 136):
			return acc
		for x in C.children(e):
			self.calls_in(x, acc)
		return acc

	def unowned_fields(self, il):
		"""The positions of a struct initializer whose field holds no reference it owns."""
		names, base = self.types.typedef_chain(C.ctype(il))
		if base.kind != C.TYPE_RECORD:
			return set()
		fields = [f for f in C.children(C.lib().clang_getTypeDeclaration(base)) if f.kind == C.FIELD_DECL]
		out = set()
		for i, x in enumerate(C.children(il)):
			ch = C.children(x) if x.kind == C.UNEXPOSED_EXPR else []
			fd = C.referenced(ch[0]) if len(ch) == 2 and ch[0].kind == 47 else fields[i] if i < len(fields) else None
			if fd is not None and not C.is_null(fd) and marks(fd) & {"BORROWED", "NO_CYCLE"}:
				out.add(i)
		return out

	def temporaries(self, c, operands, unowned=frozenset()):
		"""An owned call result among the operands lives unnamed while a sibling operand runs."""
		if len(operands) < 2:
			return
		for i, a in enumerate(operands):
			t = self.temp_of(a) if i not in unowned else None
			if not t:
				continue
			others = set()
			for j, b in enumerate(operands):
				if j != i:
					self.calls_in(b, others)
			if others:
				self.out["temporaries"].append((rel(self.path), C.location(a)[1], self.name, t, sorted(others, key=str)))

	def parm_marks(self, p):
		"""A parameter's marks over every declaration of its function: a prototype may carry them."""
		fn = C.lib().clang_getCursorSemanticParent(p)
		if C.is_null(fn) or fn.kind != C.FUNCTION_DECL:
			return set()
		ps = [x for x in C.children(fn) if x.kind == C.PARM_DECL]
		i = next((j for j, x in enumerate(ps) if C.lib().clang_equalCursors(x, p)), None)
		if i is None:
			return set()
		out = set()
		for x in (fn, C.definition(fn), C.lib().clang_getCanonicalCursor(fn)):
			if C.is_null(x):
				continue
			xs = [y for y in C.children(x) if y.kind == C.PARM_DECL]
			if i < len(xs):
				out |= marks(xs[i])
		return out

	def param_marks(self, f, i):
		"""The marks of parameter i over every declaration of the callee f names."""
		d = C.referenced(f)
		out = set()
		for x in (d, C.definition(d), C.lib().clang_getCanonicalCursor(d)):
			if C.is_null(x):
				continue
			ps = [p for p in C.children(x) if p.kind == C.PARM_DECL]
			if i < len(ps):
				out |= marks(ps[i]) | self.types.typedef_marks(C.ctype(ps[i]))
		return out

	def fn_ref(self, e, sink):
		"""A function named as a value into a sink that may be CLJ_NO_PARK: it must not park then."""
		e = strip(e)
		if e.kind == C.UNARY_OPERATOR and C.unary_op(e) == ADDR_OF:
			e = strip(C.children(e)[0])
		if e.kind != C.DECL_REF_EXPR:
			return
		d = C.referenced(e)
		if C.is_null(d) or d.kind != C.FUNCTION_DECL:
			return
		m, desc = sink()
		if "NO_PARK" in m:
			_, line, _, _ = C.location(e)
			self.out["obligations"].append((C.usr(d), C.spelling(d), desc, rel(self.path), line))

	# ---- statements

	def decl(self, d, front):
		if d.kind != C.VAR_DECL:
			return front
		if C.type_spelling(C.ctype(d)) == "clj_roots_mark":
			return self.registration(d, front)
		v = self.declare(d, False)
		for x in C.children(d):
			if 100 <= x.kind < 200:
				self.source(v, x)
				if x.kind == C.INIT_LIST_EXPR:
					self.init_list_refs(x, C.ctype(d))
				else:
					self.fn_ref(x, lambda: (self.types.typedef_marks(C.ctype(d)) | marks(d), f"{C.spelling(d)}"))
				front = self.ex(x, front)
		if v:
			front = self.link(front, self.node("def" if "=" in self.tu.tokens(d) else "decl", v))
		return front

	def init_list_refs(self, il, t):
		names, base = self.types.typedef_chain(t)
		if base.kind != C.TYPE_RECORD:
			return
		decl = C.lib().clang_getTypeDeclaration(base)
		fields = [f for f in C.children(decl) if f.kind == C.FIELD_DECL]
		for i, x in enumerate(C.children(il)):
			ch = C.children(x) if x.kind == C.UNEXPOSED_EXPR else []
			if len(ch) == 2 and ch[0].kind == 47:
				fd = C.referenced(ch[0])
				val = ch[1]
			elif i < len(fields):
				fd, val = fields[i], x
			else:
				continue
			if C.is_null(fd):
				continue
			if strip(val).kind == C.INIT_LIST_EXPR:
				self.init_list_refs(strip(val), C.ctype(fd))
				continue
			self.fn_ref(val, lambda fd=fd: (marks(fd) | self.types.typedef_marks(C.ctype(fd)), f"the {C.spelling(fd)} slot"))

	def registration(self, d, front):
		paths = []

		def walk(c):
			for x in C.children(c):
				s = strip(x)
				if s.kind == C.UNARY_OPERATOR and C.unary_op(s) == ADDR_OF:
					p = self.path_of(C.children(s)[0])
					if p:
						paths.append((p[0].idx, p[1]))
						continue
				walk(x)

		walk(d)
		path, line, _, _ = C.location(d)
		m = FIELD_REG.search(source(path).split("\n")[line - 1]) if path else None
		if m and paths:
			# CLJ_ROOTS_FIELD(&base, &n, a.b): the entries' field a.b under the base path
			paths = [(paths[0][0], paths[0][1] + ("*",) + tuple(m.group(1).replace(" ", "").split(".")))]
		off = C.location(d)[3]
		self.regs.append((off, self.scopes[-1][1] if self.scopes else 1 << 40, paths, self.line(d)))
		return self.link(front, self.node("reg", site=len(self.regs) - 1))

	def st(self, c, front):
		k = c.kind
		if k == C.COMPOUND_STMT:
			self.scopes.append(C.extent(c))
			for x in C.children(c):
				front = self.st(x, front)
			self.scopes.pop()
			return front
		if k == C.DECL_STMT:
			for d in C.children(c):
				front = self.decl(d, front)
			return front
		if k == C.IF_STMT:
			ch = C.children(c)
			f = self.ex(ch[0], front)
			ft = self.st(ch[1], f) if len(ch) > 1 else f
			fe = self.st(ch[2], f) if len(ch) > 2 else f
			return ft + fe
		if k == C.WHILE_STMT:
			ch = C.children(c)
			head = self.node("nop")
			self.link(front, head)
			f = self.ex(ch[0], [head])
			self.loops.append(([], head))
			fb = self.st(ch[1], f)
			brk, _ = self.loops.pop()
			self.link(fb, head)
			return f + brk
		if k == C.DO_STMT:
			ch = C.children(c)
			head, cond = self.node("nop"), self.node("nop")
			self.link(front, head)
			self.loops.append(([], cond))
			fb = self.st(ch[0], [head])
			brk, _ = self.loops.pop()
			self.link(fb, cond)
			f = self.ex(ch[1], [cond])
			self.link(f, head)
			return f + brk
		if k == C.FOR_STMT:
			return self.for_stmt(c, front)
		if k == C.SWITCH_STMT:
			ch = C.children(c)
			f = self.ex(ch[0], front)
			self.switches.append({"front": f, "default": False})
			self.loops.append(([], None))
			fb = self.st(ch[1], [])
			brk, _ = self.loops.pop()
			sw = self.switches.pop()
			return fb + brk + ([] if sw["default"] else f)
		if k in (C.CASE_STMT, C.DEFAULT_STMT):
			n = self.node("nop")
			self.link(front, n)
			if self.switches:
				self.link(self.switches[-1]["front"], n)
				if k == C.DEFAULT_STMT:
					self.switches[-1]["default"] = True
			ch = C.children(c)
			sub = ch[-1] if ch else None
			return self.st(sub, [n]) if sub is not None and (k == C.DEFAULT_STMT or len(ch) > 1) else [n]
		if k == C.BREAK_STMT:
			if self.loops:
				self.loops[-1][0].extend(front)
			return []
		if k == C.CONTINUE_STMT:
			for brk, cont in reversed(self.loops):
				if cont is not None:
					self.link(front, cont)
					break
			return []
		if k == C.RETURN_STMT:
			for x in C.children(c):
				front = self.ex(x, front)
			self.link(front, self.exit)
			return []
		if k == C.GOTO_STMT:
			self.gotos.append((list(front), C.spelling(C.children(c)[0]) if C.children(c) else ""))
			return []
		if k == C.INDIRECT_GOTO_STMT:
			self.gotos.append((list(front), None))
			return []
		if k == C.LABEL_STMT:
			n = self.node("nop")
			self.link(front, n)
			self.labels[C.spelling(c)] = n
			ch = C.children(c)
			return self.st(ch[0], [n]) if ch else [n]
		if k == C.NULL_STMT:
			return front
		if 100 <= k < 200:
			return self.ex(c, front)
		for x in C.children(c):
			front = self.st(x, front)
		return front

	def for_stmt(self, c, front):
		ch = C.children(c)
		path, _, _, off = C.location(c)
		text = source(path) if path else ""
		b, e = C.extent(c)
		semis = []
		i = text.find("(", b, e)
		depth = 0
		if i >= 0:
			j = i
			while j < e:
				x = text[j]
				if x == "(":
					depth += 1
				elif x == ")":
					depth -= 1
					if depth == 0:
						break
				elif x == ";" and depth == 1:
					semis.append(j)
				j += 1
			rparen = j
		parts = {"init": None, "cond": None, "inc": None, "body": None}
		if len(semis) == 2 and len(set(C.extent(x)[0] for x in ch)) == len(ch):
			for x in ch:
				xb, xe = C.extent(x)
				if xb < semis[0]:
					parts["init"] = x
				elif xb < semis[1]:
					parts["cond"] = x
				elif xb < rparen:
					parts["inc"] = x
				else:
					parts["body"] = x
		else:
			# A for inside a macro: its parts cannot be told apart, so all of them run as the body of a loop.
			head = self.node("nop")
			self.link(front, head)
			self.loops.append(([], head))
			f = [head]
			for x in ch:
				f = self.st(x, f)
			brk, _ = self.loops.pop()
			self.link(f, head)
			return [head] + brk
		self.scopes.append((b, e))
		f = self.st(parts["init"], front) if parts["init"] is not None else front
		head, inc = self.node("nop"), self.node("nop")
		self.link(f, head)
		fc = self.ex(parts["cond"], [head]) if parts["cond"] is not None else [head]
		self.loops.append(([], inc))
		fb = self.st(parts["body"], fc) if parts["body"] is not None else fc
		brk, _ = self.loops.pop()
		self.link(fb, inc)
		fi = self.ex(parts["inc"], [inc]) if parts["inc"] is not None else [inc]
		self.link(fi, head)
		self.scopes.pop()
		return (fc if parts["cond"] is not None else []) + brk

	# ---- the analysis

	def run(self):
		ch = C.children(self.cursor)
		self.scopes.append(C.extent(self.cursor))
		for p in ch:
			if p.kind == C.PARM_DECL:
				self.declare(p, True)
		body = [x for x in ch if x.kind == C.COMPOUND_STMT]
		if not body:
			return
		entry = self.node("nop")
		f = self.st(body[0], [entry])
		self.link(f, self.exit)
		for front, label in self.gotos:
			targets = [self.labels[label]] if label in self.labels else list(self.labels.values())
			for t in targets:
				self.link(front, t)
		self.liveness()
		self.reg_unset = {}
		self.staleness()
		self.infer()
		self.report()

	def liveness(self):
		order = list(reversed(self.nodes))
		changed = True
		while changed:
			changed = False
			for n in order:
				out = 0
				for s in n.succ:
					out |= s.live
				if n.kind == "call":
					self.calls[n.site]["live"] = out
				elif n.kind in ("def", "decl"):
					out &= ~(1 << n.var.idx)
				elif n.kind == "use":
					out |= 1 << n.var.idx
				if out != n.live:
					n.live = out
					changed = True

	def staleness(self):
		"""Forward: the variables that may hold a reference handed over (released or consumed) and not reassigned."""
		preds = {id(n): [] for n in self.nodes}
		for n in self.nodes:
			for s in n.succ:
				preds[id(s)].append(n)
		# Two bits a variable: stale (handed over since its last assignment) and unassigned (declared without a value).
		stale = {id(n): 0 for n in self.nodes}
		unset = {id(n): 0 for n in self.nodes}
		changed = True
		while changed:
			changed = False
			for n in self.nodes:
				v = u = 0
				for p in preds[id(n)]:
					v |= stale[id(p)]
					u |= unset[id(p)]
				if n.kind == "call":
					self.calls[n.site]["stale"] = v
					for a in self.calls[n.site]["addr"]:
						u &= ~(1 << a)
				elif n.kind == "def":
					v &= ~(1 << n.var.idx)
					u &= ~(1 << n.var.idx)
				elif n.kind == "decl":
					v &= ~(1 << n.var.idx)
					u |= 1 << n.var.idx
				elif n.kind == "given":
					v |= 1 << n.var.idx
				elif n.kind == "reg":
					self.reg_unset[n.site] = u
				if v != stale[id(n)] or u != unset[id(n)]:
					stale[id(n)] = v
					unset[id(n)] = u
					changed = True

	def infer(self):
		changed = True
		while changed:
			changed = False
			for v in self.var_list:
				if not v.owned:
					w = next((self.var_list[w] for w in v.copies if self.var_list[w].owned), None)
					if w is not None:
						v.owned = f"a copy of {w.name}"
						changed = True

	def why_owned(self, v, path):
		if "OWNED" in v.marks:
			return "CLJ_OWNED"
		if v.holder[0] == "struct":
			return f"a field of {v.tname} not declared CLJ_BORROWED"
		return v.owned

	def held(self, v):
		if v.marks & {"BORROWED", "NO_CYCLE", "RING"}:
			return False
		if "OWNED" in v.marks or v.holder[0] == "struct" and not v.param:
			return True
		# a pointer into a heap stack is a view of the field that owns the array
		if v.holder[0] == "sptr":
			return False
		return v.owned

	def report(self):
		sites = []
		for i, call in enumerate(self.calls):
			if call["callee"][0] == "indirect" and call["callee"][2]:
				continue
			live = call["live"]
			for a in call["addr"]:
				live |= 1 << a
			cands = []
			for v in self.var_list:
				inside = v.scope[0] <= call["off"] <= v.scope[1]
				escaped = v.escape is not None and v.escape < call["off"] and inside
				if not (live >> v.idx & 1 or escaped) or not self.held(v):
					continue
				regs = [p for off, end, ps, _ in self.regs if off < call["off"] <= end for (x, p) in ps if x == v.idx]
				for p in owned_paths(v.holder):
					if any(p[:len(r)] == r or r[:len(p)] == p for r in regs):
						continue
					cands.append(("unnamed", v.name + "".join("." + f for f in p), v.line, self.why_owned(v, p)))
			named = {x for off, end, ps, _ in self.regs if off < call["off"] <= end for (x, p) in ps if not p}
			for x in named:
				v = self.var_list[x]
				if x in call["given"]:
					cands.append(("given", v.name, v.line, "it is handed to the call itself; clear it first"))
				elif call["stale"] >> x & 1:
					cands.append(("given", v.name, v.line, "it was handed over before and not assigned since"))
			sites.append((call["line"], call["callee"], cands))
		for i_reg, (off, end, ps, line) in enumerate(self.regs):
			for x, p in ps:
				v = self.var_list[x]
				if "BORROWED" in v.marks:
					self.out["errors"].append((rel(self.path), line, f"{self.name}: CLJ_ROOTS names {v.name}, which is "
					                           "declared CLJ_BORROWED: a +0 reference read by the collector overcounts"))
				elif self.reg_unset.get(i_reg, 0) >> x & 1:
					self.out["errors"].append((rel(self.path), line, f"{self.name}: CLJ_ROOTS names {v.name} before it is "
					                           "assigned on every path: the collector would read an uninitialized word"))
				elif not p and v.holder[0] == "value" and not v.owned and "OWNED" not in v.marks:
					self.out["errors"].append((rel(self.path), line, f"{self.name}: CLJ_ROOTS names {v.name}, which only "
					                           "copies +0 references here: the collector would overcount; declare it CLJ_OWNED "
					                           "if a reference moves into it"))
		self.out["functions"][self.key] = {"name": self.name, "file": rel(self.path), "line": self.line(self.cursor),
		                                   "sites": sites}


def scan_slots(tu, c, out, types):
	"""Raw slot writes outside object.h, and plain clj_value fields of heap objects."""
	path = rel(C.location(c)[0])
	for x in C.children(c):
		k = x.kind
		if k in (C.STRUCT_DECL, C.UNION_DECL) and C.lib().clang_isCursorDefinition(x):
			if types.is_heap_record(x):
				for f in C.children(x):
					if f.kind != C.FIELD_DECL:
						continue
					names, _ = types.typedef_chain(C.ctype(f))
					if names and names[0][0] == "clj_value":
						out["fields"].add((path, C.location(f)[1], C.spelling(x) or "a struct", C.spelling(f)))
		if path == PRIMITIVES:
			continue
		if k in (C.BINARY_OPERATOR, C.COMPOUND_ASSIGN_OPERATOR, C.UNARY_OPERATOR):
			ch = C.children(x)
			target = None
			if k == C.UNARY_OPERATOR:
				if C.unary_op(x) in (ADDR_OF, PRE_INC, PRE_DEC, POST_INC, POST_DEC) and ch:
					target = ch[0]
			elif k == C.COMPOUND_ASSIGN_OPERATOR or C.binary_op(x) == ASSIGN:
				target = ch[0] if ch else None
			if target is not None:
				t = strip(target)
				if t.kind == C.MEMBER_REF_EXPR and C.spelling(t) == "v":
					base = C.children(t)
					if base:
						bt = C.ctype(base[0])
						if C.lib().clang_getCanonicalType(bt).kind == C.TYPE_POINTER:
							bt = C.lib().clang_getPointeeType(C.lib().clang_getCanonicalType(bt))
						names, b = types.typedef_chain(bt)
						rec = C.spelling(C.lib().clang_getTypeDeclaration(C.lib().clang_getCanonicalType(bt)))
						if any(n in SLOT_TYPEDEFS for n, _ in names) or rec in ("", ) and False:
							out["slot_writes"].add((path, C.location(x)[1]))
						elif C.type_spelling(C.lib().clang_getCanonicalType(bt)) in ("clj_slot", "clj_atomic_slot"):
							out["slot_writes"].add((path, C.location(x)[1]))
		scan_slots(tu, x, out, types)


def compile_args():
	sdk = subprocess.run(["xcrun", "--show-sdk-path"], check=True, capture_output=True, text=True).stdout.strip()
	args = ["-std=c17", "-DCLJ_DEBUG=1", "-isysroot", sdk, "-fsyntax-only"]
	for i in INCLUDES:
		args += ["-I", os.path.join(ROOT, i)]
	return args


def accessor_calls(fn):
	"""An accessor's body is one return statement that stores nothing and hands out no call's result but those of
	the callees named here: its result is +0 when theirs are. None for anything else."""
	body = [x for x in C.children(fn) if x.kind == C.COMPOUND_STMT]
	if not body:
		return None
	stmts = C.children(body[0])
	if len(stmts) != 1 or stmts[0].kind != C.RETURN_STMT or not C.children(stmts[0]):
		return None
	calls = set()

	def stores(c):
		for x in C.children(c):
			if x.kind in (C.COMPOUND_ASSIGN_OPERATOR, C.CALL_EXPR) or x.kind == C.BINARY_OPERATOR and C.binary_op(x) == ASSIGN:
				if x.kind != C.CALL_EXPR:
					return True
			if x.kind == C.UNARY_OPERATOR and C.unary_op(x) in (PRE_INC, PRE_DEC, POST_INC, POST_DEC):
				return True
			if stores(x):
				return True
		return False

	def source(e):
		e = strip(e)
		k = e.kind
		if k == C.CALL_EXPR:
			f = strip(C.children(e)[0])
			if f.kind != C.DECL_REF_EXPR:
				return False
			d = C.referenced(f)
			if C.is_null(d) or d.kind != C.FUNCTION_DECL:
				return False
			calls.add(C.usr(d))
			return True
		if k == C.CONDITIONAL_OPERATOR:
			return all(source(x) for x in C.children(e)[1:])
		if k == C.BINARY_OPERATOR and C.binary_op(e) == COMMA:
			return source(C.children(e)[-1])
		return k != C.STMT_EXPR

	e = C.children(stmts[0])[0]
	if stores(e) or not source(e):
		return None
	return calls


def analyze(job):
	path, args = job
	host = any(rel(path).startswith(h + "/") for h in HOSTS)
	out = {"functions": {}, "errors": [], "obligations": [], "marks": {}, "slot_writes": set(), "fields": set(),
	       "parse": [], "temporaries": []}
	tu = C.TU(path, args)
	out["parse"] = tu.errors()
	types = Types()
	top = C.children(tu.cursor())
	defs = []
	accessors = {}
	for c in top:
		if c.kind != C.FUNCTION_DECL:
			continue
		file = C.location(c)[0]
		if not file.startswith(ROOT):
			continue
		m = set(MARK.findall(head_text(c, ";{")))
		key = C.usr(c)
		if m:
			out["marks"].setdefault(key, set()).update(m)
		if "BORROWED" in m:
			types.borrowed_fns.add(key)
		if C.lib().clang_isCursorDefinition(c):
			defs.append(c)
			calls = accessor_calls(c)
			if calls is not None:
				accessors[key] = calls
	changed = True
	while changed:
		changed = False
		for key, calls in accessors.items():
			if key not in types.borrowed_fns and calls <= types.borrowed_fns:
				types.borrowed_fns.add(key)
				changed = True
	# a header's static inline is analyzed in every unit that includes it, the same each time
	for c in defs if not host else ():
		Fn(tu, types, c, out).run()
	for c in top:
		if c.kind == C.VAR_DECL and C.location(c)[0] == path:
			for x in C.children(c):
				if x.kind == C.INIT_LIST_EXPR:
					Fn(tu, types, c, out).init_list_refs(x, C.ctype(c))
	scan = {"slot_writes": set(), "fields": set()}
	for c in top:
		f = C.location(c)[0]
		if f == path or (f.startswith(ROOT) and f.endswith(".h")):
			scan_slots(tu, c, scan, types)
	out["slot_writes"] = scan["slot_writes"]
	out["fields"] = scan["fields"]
	return out


def sources(root):
	out = []
	for top in SCOPE + HOSTS:
		d = os.path.join(root, top)
		for f in sorted(os.listdir(d)):
			if f.endswith(".c"):
				out.append(os.path.join(d, f))
	return out


def check(results):
	"""Joins the units' summaries: errors as (file, line, message), and stats."""
	functions, marks_by, obligations, errors, parse, temporaries = {}, {}, [], [], [], []
	slot_writes, fields = set(), set()
	for r in results:
		functions.update(r["functions"])
		for k, m in r["marks"].items():
			marks_by.setdefault(k, set()).update(m)
		obligations += r["obligations"]
		temporaries += r["temporaries"]
		errors += r["errors"]
		parse += r["parse"]
		slot_writes |= r["slot_writes"]
		fields |= r["fields"]
	if parse:
		return [("", 0, "libclang could not parse: " + p) for p in sorted(set(parse))], {}
	# may park: a CLJ_MAY_PARK function, a caller of one, a call through an unmarked pointer
	why = {}
	callers = {}
	for k, f in functions.items():
		if "NO_PARK" in marks_by.get(k, ()):
			continue
		if "MAY_PARK" in marks_by.get(k, ()):
			why[k] = "is CLJ_MAY_PARK"
		for line, callee, _ in f["sites"]:
			if callee[0] == "indirect":
				if not callee[2] and k not in why:
					why[k] = f"calls through {callee[1]} (line {line})"
			else:
				callers.setdefault(callee[1], set()).add(k)
	for k, m in marks_by.items():
		if "MAY_PARK" in m:
			why.setdefault(k, "is CLJ_MAY_PARK")
	work = list(why)
	while work:
		k = work.pop()
		for c in callers.get(k, ()):
			if c not in why and "NO_PARK" not in marks_by.get(c, ()):
				name = functions[k]["name"] if k in functions else k.split("@")[-1]
				why[c] = f"calls {name}"
				work.append(c)
	for name in filter(None, os.environ.get("RH_WHY", "").split(",")):
		k = next((k for k, f in functions.items() if f["name"] == name), None)
		while k in why:
			print("WHY", functions.get(k, {}).get("file"), functions.get(k, {}).get("name", k), why[k])
			nxt = why[k][6:] if why[k].startswith("calls ") and not why[k].startswith("calls through") else None
			k = next((x for x, f in functions.items() if f["name"] == nxt), None) if nxt else None
	out = []
	sites = hazards = 0
	for k, f in functions.items():
		if "NO_PARK" in marks_by.get(k, ()):
			continue
		for line, callee, cands in f["sites"]:
			if callee[0] == "direct" and callee[1] not in why:
				continue
			sites += 1
			via = f"{callee[2]}, which {why[callee[1]]}" if callee[0] == "direct" else callee[1]
			for kind_, name, vline, why_ in cands:
				hazards += 1
				if kind_ == "given":
					out.append((f["file"], line, f"{f['name']}: {name} (line {vline}) is named for the collector across a "
					                             f"call that may park ({via}), but {why_}"))
				else:
					out.append((f["file"], line, f"{f['name']}: {name} (line {vline}) holds a reference ({why_}) across a "
					                             f"call that may park ({via}); register it (CLJ_ROOTS) or declare it CLJ_BORROWED"))
	for file, line, fn, temp, others in temporaries:
		# an indirect call is listed only when its pointer is not CLJ_NO_PARK
		parks = [o for o in others if isinstance(o, tuple) or o in why]
		if parks:
			o = parks[0]
			via = o[1] if isinstance(o, tuple) else functions.get(o, {}).get("name", o.split("@")[-1])
			out.append((file, line, f"{fn}: {temp}'s result is held by no variable while {via} runs beside it, which may park; "
			                        "assign it to a named variable first"))
	for usr, name, desc, file, line in obligations:
		if usr in why:
			out.append((file, line, f"{name} goes into {desc}, which is CLJ_NO_PARK, but it may park: it {why[usr]}"))
	for file, line in slot_writes:
		out.append((file, line, "writes a slot's word directly; go through clj_slot_store/clj_slot_init"))
	for file, line, struct, field in fields:
		out.append((file, line, f"{struct} declares a clj_value field {field}: make it a clj_slot (an edge), or a "
		                        "clj_private_value (its own execution's, never visited)"))
	out += errors
	stats = {"functions": len(functions), "may_park": len(why), "sites": sites, "hazards": hazards}
	return sorted(set(out)), stats


EXPECT = re.compile(r"//\s*expect:\s*(.+?)\s*$")


def fixtures(pool, args):
	"""Each fixture alone: a line with `// expect: text` must draw an error containing text, and no other line any."""
	d = os.path.join(ROOT, FIXTURES)
	files = sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".c"))
	failed = 0
	for f, r in zip(files, pool.map(analyze, [(f, args) for f in files], chunksize=1)):
		errors, _ = check([r])
		lines = source(f).split("\n")
		want = {i + 1: m.group(1) for i, l in enumerate(lines) for m in [EXPECT.search(l)] if m}
		got = {}
		for file, line, msg in errors:
			if file == rel(f) or not file:
				got.setdefault(line, []).append(msg)
		for line, text in sorted(want.items()):
			if not any(text in re.sub(r" \(line \d+\)|, line \d+", "", m) for m in got.get(line, [])):
				print(f"{rel(f)}:{line}: expected an error with \"{text}\", got {got.get(line, 'none')}")
				failed += 1
		for line, msgs in sorted(got.items()):
			if line not in want:
				for m in msgs:
					print(f"{rel(f)}:{line}: unexpected: {m}")
					failed += 1
	if failed:
		print(f"rooting-hazards: {failed} fixture expectation(s) not met ({FIXTURES})")
		return 1
	print(f"rooting-hazards: the {len(files)} fixtures fail where they should, and nowhere else ({FIXTURES})")
	return 0


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("files", nargs="*", help="units to analyze together; default: the runtime")
	ap.add_argument("--fixtures", action="store_true", help=f"check that the analysis catches what {FIXTURES} plants")
	ap.add_argument("-j", type=int, default=os.cpu_count())
	a = ap.parse_args()
	started = time.time()
	args = compile_args()
	with multiprocessing.Pool(a.j) as pool:
		if a.fixtures:
			return fixtures(pool, args)
		files = [os.path.abspath(f) for f in a.files] or sources(ROOT)
		errors, stats = check(pool.map(analyze, [(f, args) for f in files], chunksize=1))
	took = time.time() - started
	if errors:
		print("\n".join(f"{f}:{l}: {m}" for f, l, m in errors))
		print(f"rooting-hazards: {len(errors)} hazard(s) (design §7, «Фаза 3»; scripts/rooting-hazards.py), {took:.1f} s")
		return 1
	print(f"rooting-hazards: {stats['functions']} functions, {stats['may_park']} may park, {stats['sites']} calls that "
	      f"may park: every reference held across one is named ({took:.1f} s)")
	return 0


if __name__ == "__main__":
	sys.exit(main())
