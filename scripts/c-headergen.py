#!/usr/bin/env python3
# @ai-generated(solo)
"""Generate the level-0 declarations of a C header: clang's AST -> constants by value -> one Clojure file.

Design §5 «C — уровень 0»: what an `ns` form declares with `(:require-c [Module :refer [...]])` is parsed here
by clang and lands as `<out>/pippin/c/<Module>.clj`, which `require-c` loads from the load path. Integer
constants (`enum`, `NS_ENUM`/`NS_OPTIONS`, integer `#define`) arrive by value; an `extern const` global arrives
as a `c-global*` read and a function as a `c-fn*` binding, both `dlsym` at load. Everything else is refused by
name with clang's own reason, so an unparsed symbol is an "Unable to resolve" and never a silently wrong call.

Usage:
	c-headergen.py --out DIR [--cache DIR] [--sdk PATH] [--target TRIPLE] [--lang objective-c|c]
		[-I DIR]... [--scan FILE]... [--module NAME [--header TEXT] [--refer A,B]]...

Prints one generated path per module. A cache entry is keyed by the module, the header, the target, the SDK, the
flags, the requested names and clang's version, and validated against the stat of every header clang read, so an
edited SDK misses; a hit copies the entry out and runs no clang.

Two clang runs do the work. The first is a value probe -- one `enum : long long` whose initializers are the
requested names -- because clang's JSON AST carries a folded value on the ConstantExpr of an initializer and
carries nothing at all for an implicitly numbered enum constant or for a macro. A name the probe cannot fold is
dropped by the line of its diagnostic and classified by the second run, which dumps that name's own declaration.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PREFIX = "__clj_v_"


class Failed(Exception):
	pass


def progress(msg):
	print(f"c-headergen: {msg}", file=sys.stderr, flush=True)


# ---- the declared boundary: the (:require-c ...) forms of an ns, read without a Clojure reader


def _forms(text, open_at):
	"""The balanced form starting at text[open_at] == '(' or '[', as a token list; nested forms are lists."""
	stack = [[]]
	i = open_at
	closing = {"(": ")", "[": "]", "{": "}"}
	while i < len(text):
		c = text[i]
		if c == ";":
			i = text.find("\n", i)
			if i < 0:
				break
			continue
		if c in "([{":
			stack.append([])
			i += 1
			continue
		if c in ")]}":
			done = stack.pop()
			if not stack:
				raise Failed(f"unbalanced {c} at {i}")
			stack[-1].append(done)
			if len(stack) == 1:
				return stack[0][0]
			i += 1
			continue
		if c == '"':
			j = i + 1
			while j < len(text) and text[j] != '"':
				j += 2 if text[j] == "\\" else 1
			stack[-1].append(text[i : j + 1])
			i = j + 1
			continue
		if c.isspace() or c == ",":
			i += 1
			continue
		j = i
		while j < len(text) and not text[j].isspace() and text[j] not in '()[]{};,"':
			j += 1
		stack[-1].append(text[i:j])
		i = j
	raise Failed("unterminated form")


def scan(path):
	"""The (:require-c ...) specs of one file: [(module, header or None, [name...])]."""
	with open(path, encoding="utf-8") as f:
		text = f.read()
	out = []
	for m in re.finditer(r"\(:require-c\b", text):
		form = _forms(text, m.start())
		for spec in form[1:]:
			if isinstance(spec, str):
				spec = [spec]
			module = spec[0]
			if module.startswith("'"):
				module = module[1:]
			opts = {}
			rest = spec[1:]
			for i in range(0, len(rest) - 1, 2):
				opts[rest[i]] = rest[i + 1]
			bad = [k for k in opts if k not in (":as", ":refer", ":header")]
			if bad:
				raise Failed(f"{path}: unsupported require-c option {bad[0]}")
			refer = opts.get(":refer", [])
			if isinstance(refer, str):
				raise Failed(f"{path}: {module} :refer takes a vector of names, got {refer}")
			header = opts.get(":header")
			out.append((module, header.strip('"') if header else None, list(refer)))
	return out


# ---- clang


def clang(args, lang, src, extra=()):
	"""Runs clang over source text on stdin; answers (status, stdout, stderr)."""
	cmd = ["clang"] + list(args.flags) + ["-fsyntax-only", "-x", lang, *extra, "-"]
	if args.sdk:
		cmd[1:1] = ["-isysroot", args.sdk]
	if args.target:
		cmd[1:1] = ["-target", args.target]
	p = subprocess.run(cmd, input=src, capture_output=True, text=True)
	return p.returncode, p.stdout, p.stderr


def clang_version(args):
	p = subprocess.run(["clang", "--version"], capture_output=True, text=True)
	if p.returncode != 0:
		raise Failed("clang --version failed; a C header needs a clang on PATH")
	return p.stdout.strip()


def ast_objects(text):
	"""The concatenated top-level JSON objects of an -ast-dump=json run."""
	dec = json.JSONDecoder()
	out, i = [], 0
	while i < len(text):
		while i < len(text) and text[i].isspace():
			i += 1
		if i >= len(text):
			break
		o, i = dec.raw_decode(text, i)
		out.append(o)
	return out


def folded(node):
	"""The value clang folded onto the ConstantExpr under an enum constant's initializer."""
	if node.get("kind") == "ConstantExpr" and "value" in node:
		return node["value"]
	for inner in node.get("inner", []):
		v = folded(inner)
		if v is not None:
			return v
	return None


DIAG = re.compile(r"^<stdin>:(\d+):\d+: (error|fatal error): (.*)$")


def value_probe(args, lang, include, names):
	"""The names clang folded to an integer, and why each of the others did not."""
	values, refused = {}, {}
	todo = list(names)
	while todo:
		lines = [include, "typedef enum __clj_v_probe : long long {"]
		for i, name in enumerate(todo):
			lines.append(f"\t{PREFIX}{i} = ({name}),")
		lines.append("} __clj_v_probe_t;")
		src = "\n".join(lines) + "\n"
		status, out, err = clang(args, lang, src, ["-Xclang", "-ast-dump=json", "-Xclang", "-ast-dump-filter", "-Xclang", PREFIX])
		if status == 0:
			# The filter matches the probe enum, which the dump prints with its constants as its subtree.
			for node in ast_objects(out):
				for const in node.get("inner", []) if node.get("kind") == "EnumDecl" else [node]:
					if const.get("kind") != "EnumConstantDecl":
						continue
					v = folded(const)
					if v is not None:
						values[todo[int(const["name"][len(PREFIX) :])]] = int(v)
			missing = [n for n in todo if n not in values]
			for name in missing:
				refused[name] = "clang folded no value for this name"
			return values, refused
		dropped = {}
		for line in err.splitlines():
			m = DIAG.match(line)
			if not m:
				continue
			row = int(m.group(1)) - 3		# the include and the enum head are lines 1 and 2
			if not 0 <= row < len(todo):
				raise Failed(f"the value probe failed outside its own lines:\n{err}")
			dropped.setdefault(todo[row], m.group(3))
		if not dropped:
			raise Failed(f"the value probe failed with no diagnostic of its own:\n{err}")
		refused.update(dropped)
		todo = [n for n in todo if n not in dropped]
	return values, refused


# The C scalars a c-global* read knows; the key is clang's desugared spelling of the global's type.
GLOBAL_KINDS = {
	"double": ":double", "float": ":float",
	"char": ":char", "signed char": ":char", "unsigned char": ":uchar",
	"short": ":short", "unsigned short": ":ushort",
	"int": ":int", "unsigned int": ":uint",
	"long": ":long", "unsigned long": ":ulong",
	"long long": ":llong", "unsigned long long": ":ullong",
	"_Bool": ":bool",
}

# ---- the call: a header's types in the one vocabulary the level-1 dispatcher reads (design §5 «C — уровень 0»)

# Objective-C type encodings of the canonical scalars. 'l'/'L' are 32-bit to the runtime, so a real long is
# 'q'/'Q'; clang's own encoder does the same.
SCALARS = {
	"void": "v", "_Bool": "B", "bool": "B",
	"char": "c", "signed char": "c", "unsigned char": "C",
	"short": "s", "short int": "s", "unsigned short": "S", "unsigned short int": "S",
	"int": "i", "unsigned int": "I", "unsigned": "I",
	"long": "q", "long int": "q", "unsigned long": "Q", "unsigned long int": "Q",
	"long long": "q", "long long int": "q", "unsigned long long": "Q", "unsigned long long int": "Q",
	"float": "f", "double": "d",
}

# id, Class and SEL are the runtime's own: SEL desugars to "SEL *", so the alias decides, not the desugaring.
OBJC_ALIASES = {"id": "@", "instancetype": "@", "Class": "#", "SEL": ":", "IMP": "^?"}

ATTRS = re.compile(r"\b(_Nullable|_Nonnull|_Null_unspecified|_Nullable_result|__autoreleasing|__strong|__unsafe_unretained|__weak|__kindof|volatile|restrict|_Atomic)\b")
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(\s*<.*>)?$")


def bare_type(text):
	"""A type spelling without the qualifiers and nullability that do not change its ABI."""
	text = ATTRS.sub(" ", text)
	text = re.sub(r"\s+", " ", text).strip()
	while text.startswith("const "):
		text = text[6:].strip()
	return text


class Unencodable(Exception):
	pass


def encode_type(spelled, desugared, is_return):
	"""The type encoding of one parameter or return, or Unencodable with what stopped it."""
	alias = bare_type(spelled)
	if alias in OBJC_ALIASES:
		return OBJC_ALIASES[alias]
	text = bare_type(desugared if desugared else spelled)
	if text in SCALARS:
		if text == "void" and not is_return:
			raise Unencodable("a void parameter")
		return SCALARS[text]
	if "(^" in text:
		return "@?"	 # a block pointer is one encoding, and a block crosses as a level-1 handle
	if "(*" in text:
		return "^?"
	if text.endswith("*"):
		inner = bare_type(text[:-1])
		if inner == "char":
			return "*"
		if inner == "void":
			return "^v"
		if inner in SCALARS or inner.startswith(("struct ", "union ", "enum ")) or inner.endswith("*"):
			try:
				return "^" + encode_type(inner, inner, False)
			except Unencodable:
				return "^v"	 # an opaque pointee crosses as a raw pointer, which is what it is
		if IDENTIFIER.match(inner):
			return "@"	 # a typedef chain ends at a builtin, a tag or an Objective-C class; only the last is left
		raise Unencodable(f"a pointer to {inner}")
	if text.startswith(("struct ", "union ")):
		raise Unencodable(f"a {text} by value")
	if "[" in text:
		raise Unencodable(f"an array ({text})")
	raise Unencodable(f"the type {spelled}")


# A pointer global crosses as a level-1 return of its encoding does, which is a value or an immortal handle.
POINTER_KINDS = {"@": ":id", "#": ":class", ":": ":sel", "*": ":cstring"}

CONST_POINTER = re.compile(r"^(.*\*)\s*const$")


def global_of(spelled, desugared):
	"""The kind keyword c-global* reads this global by, or (None, why).

	Constness is the pointer's own for a pointer and the value's for a scalar: either way a global the
	program may reassign is refused, since a snapshot taken at load stops being its value.
	"""
	d = re.sub(r"\s+", " ", ATTRS.sub(" ", desugared)).strip()
	pointer = CONST_POINTER.match(d)
	if pointer:
		try:
			enc = encode_type(spelled, pointer.group(1), False)
		except Unencodable as e:
			return None, f"a global of type {spelled}, which is {e}"
		if enc not in POINTER_KINDS:
			return None, (f"a raw pointer global ({spelled}): what it points at has no owner the bridge can name, "
			              f"and releasing it would be a guess")
		return POINTER_KINDS[enc], None
	if d.endswith("*"):
		return None, f"a mutable pointer global ({spelled}): a pointer read once at load would not be its value later"
	bare = d.removeprefix("const ").strip()
	if bare not in GLOBAL_KINDS:
		return None, f"a global of type {spelled}: only a scalar and a const pointer cross"
	if not d.startswith("const "):
		return None, f"a mutable global ({spelled}): a value read once at load would not be its value later"
	return GLOBAL_KINDS[bare], None


def split_signature(qual):
	"""('int', 'int, char **') out of 'int (int, char **)'; None when the spelling is not a plain function."""
	if not qual.endswith(")"):
		return None
	depth = 0
	for i in range(len(qual) - 1, -1, -1):
		if qual[i] == ")":
			depth += 1
		elif qual[i] == "(":
			depth -= 1
			if depth == 0:
				return qual[:i].strip(), qual[i + 1 : -1].strip()
	return None


def function_of(node):
	"""(return spelling, [(spelled, desugared)]) of a FunctionDecl, or a refusal string."""
	if node.get("variadic"):
		return "a variadic function: on arm64 Apple a variadic argument rides the stack, which a fixed prototype does not place"
	# `static` is what makes the symbol missing; a C99 `inline` without it may still be exported somewhere.
	if node.get("inline") or node.get("storageClass") == "static":
		how = " ".join(w for w in ("static" if node.get("storageClass") == "static" else "", "inline" if node.get("inline") else "") if w)
		return (f"a {how} function: its body lives in the header and in no binary, so the interpreter needs "
		        "a thin C stub (design §5 «Только в заголовке»)")
	split = split_signature(node.get("type", {}).get("qualType", ""))
	if not split:
		return f"a function whose type the parse cannot read: {node.get('type', {}).get('qualType', '')}"
	ret, params = split
	if "(" in ret:
		return f"a function returning {ret}, which the bridge has no prototype for"
	parms = [c for c in node.get("inner", []) if c.get("kind") == "ParmVarDecl"]
	if not params and not parms:
		return "a declaration with no prototype, so the parse cannot know what it takes"
	types = []
	for p in parms:
		t = p.get("type", {})
		types.append((t.get("qualType", ""), t.get("desugaredQualType")))
	return ret, types


def classify(args, lang, include, name, why):
	"""One name's own declaration: a global read, a function to call, or a refusal naming what it is."""
	src = f"{include}\n"
	status, out, err = clang(args, lang, src, ["-Xclang", "-ast-dump=json", "-Xclang", "-ast-dump-filter", "-Xclang", name])
	if status != 0:
		raise Failed(f"clang failed dumping {name}:\n{err}")
	for node in ast_objects(out):
		if node.get("name") != name:
			continue
		kind = node.get("kind")
		if kind == "FunctionDecl":
			found = function_of(node)
			return (None, found) if isinstance(found, str) else (("fn",) + found, None)
		if kind == "VarDecl":
			t = node.get("type", {})
			spelled = t.get("qualType", "")
			found, why = global_of(spelled, t.get("desugaredQualType", spelled))
			return (("global", found, spelled), None) if found else (None, why)
		if kind == "RecordDecl" or kind == "EnumDecl" or kind == "TypedefDecl":
			return None, f"a type ({kind}): structs are deflayout, which is not in this slice"
		if kind == "ObjCInterfaceDecl":
			return None, "an Objective-C class: level 1 reaches it with (objc-class \"name\")"
	return None, why


def return_kinds(args, lang, include, wanted):
	"""The desugared spelling of each function's return type: a variable of it, dumped (name -> text)."""
	wanted = {n: r for n, r in wanted.items() if bare_type(r) != "void"}
	if not wanted:
		return {}
	order = sorted(wanted)
	lines = [include]
	for i, name in enumerate(order):
		lines.append(f"static {wanted[name]} {PREFIX}r{i};")
	status, out, err = clang(args, lang, "\n".join(lines) + "\n",
	                         ["-Xclang", "-ast-dump=json", "-Xclang", "-ast-dump-filter", "-Xclang", PREFIX + "r"])
	if status != 0:
		raise Failed(f"the return-type probe does not compile:\n{err}")
	out_kinds = {}
	for node in ast_objects(out):
		n = node.get("name", "")
		if node.get("kind") != "VarDecl" or not n.startswith(PREFIX + "r"):
			continue
		t = node.get("type", {})
		out_kinds[order[int(n[len(PREFIX) + 1 :])]] = t.get("desugaredQualType", t.get("qualType", ""))
	return out_kinds


# Eight integer and eight floating-point slots, and the encodings objc.c refuses outright.
MAX_INT_SLOTS = 8
MAX_FP_SLOTS = 8


def encode_function(ret_spelling, ret_desugared, types):
	"""("i", ["i", "@"]) for one function, or a refusal string."""
	try:
		ret = encode_type(ret_spelling, ret_desugared, True)
	except Unencodable as e:
		return f"a function returning {e}, which the bridge cannot carry"
	argv = []
	ints = fps = floats = 0
	for i, (spelled, desugared) in enumerate(types):
		try:
			enc = encode_type(spelled, desugared, False)
		except Unencodable as e:
			return f"a function taking {e} as argument {i + 1}, which the bridge cannot carry"
		argv.append(enc)
		if enc == "f":
			fps += 1
			floats += 1
		elif enc == "d":
			fps += 1
		else:
			ints += 1
	if ints > MAX_INT_SLOTS or fps > MAX_FP_SLOTS:
		return f"a function with {ints} integer and {fps} floating-point arguments, past the {MAX_INT_SLOTS} and {MAX_FP_SLOTS} slots of the dispatcher"
	if floats and floats != fps:
		return "a function mixing float and double arguments, which one prototype cannot place"
	return ret, argv


# ---- the cache (design §3 «Кэш»), keyed by the inputs and validated against the headers clang read


def deps_of(path):
	"""The files a -MD run named, as [[path, size, mtime_ns]]."""
	with open(path, encoding="utf-8") as f:
		text = f.read()
	text = text.replace("\\\n", " ").split(":", 1)[-1]
	out = []
	for p in sorted(set(text.split())):
		try:
			st = os.stat(p)
		except OSError:
			continue
		out.append([p, st.st_size, st.st_mtime_ns])
	return out


def deps_match(deps):
	for path, size, mtime in deps:
		try:
			st = os.stat(path)
		except OSError:
			return False
		if st.st_size != size or st.st_mtime_ns != mtime:
			return False
	return True


def default_cache():
	if os.environ.get("PIPPIN_CACHE"):
		return os.environ["PIPPIN_CACHE"]
	if sys.platform == "darwin":
		return os.path.expanduser("~/Library/Caches/pippin")
	return os.path.join(os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache")), "pippin")


def fingerprint(args, module, header, lang, names, version):
	h = hashlib.sha256()
	with open(os.path.join(HERE, "c-headergen.py"), "rb") as f:
		h.update(f.read())
	for part in [module, header, lang, args.target or "", args.sdk or "", version, *args.flags, *sorted(names)]:
		h.update(part.encode())
		h.update(b"\0")
	return h.hexdigest()[:32]


# ---- the generated file


def emit(module, header, meta, values, globals_, fns, refused):
	lines = [
		f";; Generated by scripts/c-headergen.py from <{header}>; do not edit (design §5 «C — уровень 0»).",
		f"(ns {module}",
		"  {:pippin/c-parse " + render_map(meta) + ",",
		"   :pippin/c-refused (quote " + render_map({k: refused[k] for k in sorted(refused)}) + ")})",
		"",
	]
	for name in sorted(values):
		lines.append(f"(def {name} {values[name]})")
	for name in sorted(globals_):
		kind, spelled = globals_[name]
		lines.append(f";; {spelled}")
		lines.append(f'(def {name} (c-global* "{name}" {kind}))')
	for name in sorted(fns):
		ret, argv, spelled = fns[name]
		lines.append(f";; {spelled}")
		argl = " ".join(json.dumps(a) for a in argv)
		lines.append(f'(def {name} (c-fn* "{name}" "{ret}" [{argl}]))')
	return "\n".join(lines) + "\n"


def render_map(m):
	items = [f"{k} {json.dumps(v, ensure_ascii=False)}" if isinstance(v, str) else f"{k} {v}" for k, v in m.items()]
	return "{" + ", ".join(items) + "}"


def one(args, module, header, names, version):
	lang = args.lang
	include = f"#import <{header}>" if lang.startswith("objective") else f"#include <{header}>"
	key = fingerprint(args, module, header, lang, names, version)
	entry = os.path.join(args.cache, "c", key)
	report = os.path.join(entry, "report.json")
	generated = os.path.join(entry, f"{module}.clj")
	if os.path.exists(report) and os.path.exists(generated):
		with open(report, encoding="utf-8") as f:
			if deps_match(json.load(f).get("deps", [])):
				return install(args, module, generated, entry)
	progress(f"parsing <{header}> for {module}: {len(names)} names")
	tmp = f"{entry}.tmp-{os.getpid()}"
	shutil.rmtree(tmp, ignore_errors=True)
	os.makedirs(tmp, exist_ok=True)
	depfile = os.path.join(tmp, "deps.d")
	status, _, err = clang(args, lang, include + "\n", ["-MD", "-MF", depfile])
	if status != 0:
		raise Failed(f"<{header}> does not compile for this target:\n{err}")
	deps = deps_of(depfile)
	values, refused = value_probe(args, lang, include, names)
	globals_, raw_fns = {}, {}
	for name in sorted(refused):
		found, why = classify(args, lang, include, name, refused[name])
		if not found:
			refused[name] = why
			continue
		del refused[name]
		if found[0] == "global":
			globals_[name] = found[1:]
		else:
			raw_fns[name] = found[1:]
	# One probe for every return type at once: a variable of that type desugars where the function type does not.
	kinds = return_kinds(args, lang, include, {n: r for n, (r, _) in raw_fns.items()})
	fns = {}
	for name, (ret_spelling, types) in raw_fns.items():
		encoded = encode_function(ret_spelling, kinds.get(name), types)
		if isinstance(encoded, str):
			refused[name] = encoded
			continue
		ret, argv = encoded
		params = ", ".join(s for s, _ in types) or "void"
		fns[name] = (ret, argv, re.sub(r"\s+", " ", f"{ret_spelling} {name}({params})"))
	meta = {":header": header, ":target": args.target or "", ":sdk": args.sdk or "", ":clang": version.splitlines()[0],
	        ":fingerprint": key}
	with open(os.path.join(tmp, f"{module}.clj"), "w", encoding="utf-8") as f:
		f.write(emit(module, header, meta, values, globals_, fns, refused))
	with open(os.path.join(tmp, "report.json"), "w", encoding="utf-8") as f:
		json.dump({"module": module, "header": header, "target": args.target, "sdk": args.sdk, "clang": version,
		           "constants": values, "globals": globals_, "functions": fns, "refused": refused, "deps": deps}, f, indent=1, sort_keys=True)
	os.makedirs(os.path.dirname(entry), exist_ok=True)
	shutil.rmtree(entry, ignore_errors=True)
	os.rename(tmp, entry)
	for name in sorted(refused):
		progress(f"  refused {name}: {refused[name]}")
	return install(args, module, generated, entry)


def install(args, module, generated, entry):
	"""The thin catalog of design §3: the project holds a copy of the store's entry, not a parse of its own."""
	if not args.out:
		return generated
	# The declarations alone; report.json stays in the store, since a program ships what it loads.
	out = os.path.join(args.out, "pippin", "c", f"{module}.clj")
	os.makedirs(os.path.dirname(out), exist_ok=True)
	shutil.copyfile(generated, out)
	return out


def main(argv):
	p = argparse.ArgumentParser()
	p.add_argument("--out", help="where pippin/c/<Module>.clj is written; the load path of the program")
	p.add_argument("--cache", default=default_cache())
	p.add_argument("--sdk")
	p.add_argument("--target")
	p.add_argument("--lang", default="objective-c" if sys.platform == "darwin" else "c")
	p.add_argument("-I", dest="flags", action="append", default=[], metavar="DIR")
	p.add_argument("--flag", dest="flags", action="append", metavar="FLAG", help="one more flag for clang")
	p.add_argument("--scan", action="append", default=[], metavar="FILE", help="a .clj whose ns forms declare the headers")
	p.add_argument("--module", action="append", default=[], metavar="NAME")
	p.add_argument("--header", action="append", default=[], metavar="TEXT")
	p.add_argument("--refer", action="append", default=[], metavar="A,B")
	args = p.parse_args(argv)
	args.flags = [f"-I{d}" if not d.startswith("-") else d for d in args.flags]

	specs = []
	for path in args.scan:
		specs += scan(path)
	for i, module in enumerate(args.module):
		header = args.header[i] if i < len(args.header) else None
		refer = args.refer[i].split(",") if i < len(args.refer) else []
		specs.append((module, header, [n for n in refer if n]))
	merged = {}
	for module, header, names in specs:
		header = header or f"{module}/{module}.h"
		have = merged.setdefault((module, header), [])
		have += [n for n in names if n not in have]
	if not merged:
		raise Failed("no (:require-c ...) spec to generate from")
	version = clang_version(args)
	for (module, header), names in merged.items():
		print(one(args, module, header, sorted(names), version))
	return 0


if __name__ == "__main__":
	try:
		sys.exit(main(sys.argv[1:]))
	except Failed as e:
		progress(str(e))
		sys.exit(1)
