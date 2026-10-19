#!/usr/bin/env python3
# @ai-generated(solo)
# Fails on a runtime C function that calls an entry below or a slot a program may fill, with neither CLJ_ROOTS nor
# CLJ_ROOTS_NONE in it (design §7, «Фаза 3», «Код рантайма»). Callers further up are not asked.
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Sources", "CljCore")

ENTRIES = {
    # calls of a fn value
    "clj_invoke", "clj_apply", "clj_call_invoke", "clj_call_invoke_slow", "clj_closure_invoke", "clj_closure_invoke_at",
    "clj_lazy_thunk_run",
    # walks, each of which may realize a lazy seq
    "clj_seq", "clj_first", "clj_next", "clj_rest", "clj_seq_iter_next", "clj_seq_items",
    # reduce drivers
    "clj_reduce", "clj_reduce_kv", "clj_reduce_iter", "clj_reducer_step", "clj_reducer_step_kv", "clj_reducer_step_entry",
    "clj_reducer_finish", "clj_reduce_empty",
    # parks
    "clj_park", "clj_lot_park",
}
# A call through a slot a program's type or fn may fill.
SLOT_CALL = re.compile(r"(->|\.)(invoke|reduce|seq|first|next|rest|owned|split|split_owned)\(|\bnative(_ctx)?\.fn\(")
MARK = re.compile(r"\bCLJ_ROOTS(_NONE|_SPAN)?\b")
CALL = re.compile(r"\b([a-z_][a-z_0-9]*)\s*\(")


def strip(text):
    """Comments, strings and chars blanked, line breaks kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def functions(text):
    """(name, line, body) of each function definition at file scope."""
    depth, head, start, name, body = 0, [], 0, None, []
    line = 1
    for c in text:
        if c == "\n":
            line += 1
        if depth == 0:
            if c == "{":
                h = "".join(head)
                head = []
                if "(" in h and "=" not in h.split("(")[0] and not h.lstrip().startswith("#"):
                    m = re.search(r"([A-Za-z_][A-Za-z_0-9]*)\s*\($", h.split("(")[0] + "(")
                    name, start, body = (m.group(1) if m else None), line, []
                else:
                    name = None
                depth = 1
                continue
            if c in ";}":
                head = []
            elif c == "\n" and "".join(head).lstrip().startswith("#"):
                head = []
            else:
                head.append(c)
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                if name:
                    yield name, start, "".join(body)
                name = None
                continue
        if name:
            body.append(c)


def main():
    missing = []
    audited = 0
    for fn in sorted(os.listdir(ROOT)):
        if not fn.endswith(".c"):
            continue
        text = strip(open(os.path.join(ROOT, fn)).read())
        for name, line, body in functions(text):
            if not (set(CALL.findall(body)) & ENTRIES or SLOT_CALL.search(body)):
                continue
            audited += 1
            if not MARK.search(body):
                missing.append(f"Sources/CljCore/{fn}:{line}: {name}")
    if missing:
        print("roots-audit: a function runs a program's code without CLJ_ROOTS or CLJ_ROOTS_NONE (roots_internal.h):")
        print("\n".join(missing))
        sys.exit(1)
    print(f"roots-audit: {audited} functions of the runtime that run a program's code name what they hold across it")


main()
