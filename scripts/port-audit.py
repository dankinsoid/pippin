#!/usr/bin/env python3
# @ai-generated(solo)
"""Fails when a platform- or architecture-specific spot is missing from docs/portability.md.

Two checks. A file using an Apple-only API must be a row of the "Spots" table. A spot that depends on the
architecture -- an arch condition, assembly, a section placement, pointer authentication, a register context --
must be a row of the "Per-architecture spots" table, named by its file and the function or macro holding it, so
that one more such spot in an already listed file still fails. A recorded spot that no longer exists fails too.
"""
import os
import re
import sys

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), ".."))
LEDGER = os.path.join(ROOT, "docs/portability.md")

API_MARKERS = re.compile(
    r"__APPLE__|getsectiondata|pthread_get_stackaddr_np|arc4random|os_unfair_lock|mach_absolute_time|os_signpost"
    r"|_dyld_|sigaltstack|ucontext_t|xcrun|dlopen\(|TARGET_OS_|__MACH__")
API_SCOPE = ["Sources", "Makefile", "Package.swift"]

ARCH_MARKERS = re.compile(
    r"__aarch64__|__arm64e?__|__x86_64__|__i386__|__arm__\b|__riscv|__powerpc"  # arch conditions
    r"|\b(?:__asm__|asm)\b(?:\s+(?:volatile|__volatile__|goto))*\s*\(\s*\"(?!\")"  # asm with a non-empty template
    r"|\bsection\s*\(\s*\"|\.(?:push)?section\b"  # section placement
    r"|ptrauth|uc_mcontext|_THREAD_STATE64")  # return-address signing, register contexts
SWIFT_ARCH = re.compile(r"#(?:else)?if\b.*\barch\s*\(")
ARCH_SCOPE = ["Sources", "Tests"]
GENERATED = "Sources/CljCore/boot/"


def walk(scope, exts):
    for top in scope:
        path = os.path.join(ROOT, top)
        if os.path.isfile(path):
            yield top
            continue
        for d, _, files in os.walk(path):
            for f in files:
                rel = os.path.relpath(os.path.join(d, f), ROOT)
                if rel.startswith(GENERATED) or (exts and not f.endswith(exts)):
                    continue
                yield rel


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def table(section):
    """Rows of the first table after a '## section' heading, as lists of cells."""
    rows, inside = [], False
    for line in read("docs/portability.md").splitlines():
        if line.startswith("## "):
            if inside:
                break
            inside = line[3:].strip() == section
        elif inside and line.startswith("|") and not re.match(r"\|\s*-", line):
            rows.append([c.strip() for c in line.strip().strip("|").split("|")])
    if not rows:
        sys.exit(f"port-audit: docs/portability.md has no '## {section}' table")
    return rows[1:]


def strip_c(text):
    """Comments blanked; returns (code with strings, code with strings blanked), line structure kept."""
    code, bare, i, n = [], [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            code.append(" " * (j - i)), bare.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            blank = re.sub(r"[^\n]", " ", text[i:j])
            code.append(blank), bare.append(blank)
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            code.append(text[i:j]), bare.append(c + " " * (j - i - 2) + c if j - i >= 2 else text[i:j])
            i = j
        else:
            code.append(c), bare.append(c)
            i += 1
    return "".join(code).split("\n"), "".join(bare).split("\n")


DEF_NAME = re.compile(r"(\w+)\s*\([^;{}]*\)\s*(?:__attribute__\s*\(\(.*\)\)\s*)*$")
DATA_NAME = re.compile(r"(\w+)\s*(?:\[[^\]]*\])?\s*=\s*$")
GLOBL = re.compile(r"\.globl\s+_?(\w+)")


def c_spots(rel):
    """(line, anchor) of every arch-specific construct of a C file; anchor None when nothing names it."""
    code, bare = strip_c(read(rel))
    depth, macro, func, stmt = 0, None, None, ""
    owner = []  # per line: the macro or function holding it, or the file-scope definition it starts
    for k, (line, b) in enumerate(zip(code, bare)):
        pre = b.lstrip().startswith("#")
        m = re.match(r"\s*#\s*define\s+(\w+)", b)
        if m:
            macro = m.group(1)
        here = macro or (func if depth > 0 else None)
        if not pre and depth == 0 and not macro:
            if re.match(r"\s*(?:__asm__|asm)\s*\(", b):
                rest = "\n".join(code[k:k + 40])
                g = GLOBL.search(rest[: rest.find(");") + 2 if ");" in rest else len(rest)])
                here = g.group(1) if g else None
        if not pre and not macro:
            for ch in b:
                if ch == "{":
                    if depth == 0:
                        d = DEF_NAME.search(stmt.strip()) or DATA_NAME.search(stmt.strip())
                        func = d.group(1) if d else None
                        if here is None:
                            here = func
                    depth += 1
                elif ch == "}":
                    depth = max(depth - 1, 0)
                    if depth == 0:
                        stmt = ""
                elif ch == ";" and depth == 0:
                    stmt = ""
                elif depth == 0:
                    stmt += ch
            if depth == 0:
                stmt += " "
        owner.append(here)
        if macro and not b.rstrip().endswith("\\"):
            macro = None
    # A file-scope line is named by the first definition of the innermost conditional group around it.
    group, stack, groups = [None] * len(bare), [], {}
    for k, b in enumerate(bare):
        d = re.match(r"\s*#\s*(if|ifdef|ifndef|endif)\b", b)
        if d and d.group(1) != "endif":
            stack.append(k)
        group[k] = stack[-1] if stack else None
        if d and d.group(1) == "endif" and stack:
            start = stack.pop()
            groups[start] = next((owner[j] for j in range(start + 1, k) if owner[j]), None)
    return [(k + 1, owner[k] or groups.get(group[k])) for k in range(len(code)) if ARCH_MARKERS.search(code[k])]


def swift_spots(rel):
    spots, func = [], None
    for k, line in enumerate(read(rel).split("\n")):
        f = re.search(r"\bfunc\s+(\w+)", line)
        if f:
            func = f.group(1)
        if SWIFT_ARCH.search(line):
            spots.append((k + 1, func))
    return spots


def main():
    failed = False

    listed_files = " ".join(r[0] for r in table("Spots"))
    api_files = sorted(rel for rel in walk(API_SCOPE, None) if API_MARKERS.search(read(rel)))
    for rel in api_files:
        if os.path.basename(rel) not in listed_files:
            print(f"port-audit: {rel} uses a platform-specific API but is not in the Spots table of docs/portability.md")
            failed = True

    recorded = set()
    for r in table("Per-architecture spots"):
        f, s = re.findall(r"`([^`]+)`", r[0]), re.findall(r"`([^`]+)`", r[1])
        if len(f) != 1 or not s:
            sys.exit(f"port-audit: a Per-architecture spots row needs one `file` and its `spot`s: | {' | '.join(r[:2])} |")
        recorded.update((f[0], x) for x in s)
    found = {}
    for rel in walk(ARCH_SCOPE, (".c", ".h", ".m", ".swift")):
        for line, anchor in (swift_spots if rel.endswith(".swift") else c_spots)(rel):
            if anchor is None:
                print(f"port-audit: {rel}:{line} is architecture-specific outside any function or macro; give it one to be named by")
                failed = True
            else:
                found.setdefault((os.path.basename(rel), anchor), f"{rel}:{line}")
    for key, where in sorted(found.items()):
        if key not in recorded:
            print(f"port-audit: {where} is architecture-specific ({key[1]}) but has no row in Per-architecture spots")
            failed = True
    for key in sorted(recorded - set(found)):
        print(f"port-audit: Per-architecture spots lists {key[1]} in {key[0]}, which has no architecture-specific code")
        failed = True

    if failed:
        sys.exit(1)
    print(f"port-audit: every platform-specific file ({len(api_files)}) and per-architecture spot ({len(found)}) is listed")


main()
