#!/usr/bin/env python3
# @ai-generated(solo)
"""Seed inputs for the parser fuzzers, mined from the Clojure sources in the tree: the corpus libraries, the
embedded core and the test fixtures. usage: seeds.py <repo root> <out dir>; writes <out dir>/<target>/<sha1>."""
import hashlib
import os
import re
import sys

ROOT, OUT = sys.argv[1], sys.argv[2]
SOURCES = ["corpus", "Sources/CljCore/boot", "Tests/PippinTests/Fixtures", "lib", "fuzz"]
CHUNK = 2000
CAP = 3000

# Positions in format.c's palette, by the conversion that takes them.
FORMAT_ARGS = {"d": [1, 5, 2, 6], "x": [3, 2], "o": [3], "f": [12, 15, 21, 22], "e": [14, 19], "g": [17, 20],
               "s": [30, 31, 43, 37], "c": [34, 51], "b": [38, 37]}
SUBJECTS = ["abc123 def_456", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!", "foo@bar.example.com", "2020-01-31T12:34:56Z",
            "The quick brown fox", "  spaced\tout\nlines  ", "x,y;;z", "héllo wörld 日本"]


def sources():
    for top in SOURCES:
        for d, _, files in os.walk(os.path.join(ROOT, top)):
            for f in sorted(files):
                if f.endswith((".clj", ".cljc", ".edn")):
                    with open(os.path.join(d, f), "rb") as fh:
                        yield fh.read()


def chunks(text):
    """Top-level forms grouped up to CHUNK bytes: a whole file is past the fuzzers' max_len."""
    out, cur = [], b""
    for part in re.split(rb"\n(?=\()", text):
        if cur and len(cur) + len(part) > CHUNK:
            out.append(cur)
            cur = b""
        cur += part + b"\n"
    if cur.strip():
        out.append(cur)
    return out


def unescape(body):
    """A Clojure string literal's body as the string it reads to; None for an escape the sketch does not know."""
    try:
        return body.decode("utf-8").encode("latin-1", "backslashreplace").decode("unicode_escape").encode("utf-8")
    except (UnicodeDecodeError, UnicodeEncodeError):
        return None


def write(target, items):
    d = os.path.join(OUT, target)
    os.makedirs(d, exist_ok=True)
    unique = {hashlib.sha1(x).hexdigest(): x for x in items if x}
    for h in sorted(unique)[:CAP]:
        with open(os.path.join(d, h), "wb") as fh:
            fh.write(unique[h])
    print(f"seeds: {target}: {min(len(unique), CAP)} of {len(unique)}")


def main():
    reader, regex, number, fmt = [], [], [], []
    for text in sources():
        reader += chunks(text)
        for m in re.finditer(rb'#"((?:[^"\\]|\\.)*)"', text):
            subject = SUBJECTS[len(m.group(1)) % len(SUBJECTS)].encode()
            regex.append(m.group(1) + b"\0" + subject)
        for m in re.finditer(rb'\(re-pattern\s+"((?:[^"\\]|\\.)*)"', text):
            p = unescape(m.group(1))
            if p is not None:
                regex.append(p + b"\0" + SUBJECTS[0].encode())
        number += re.findall(rb"(?<![\w.*+!?<>=/-])[+-]?\d[\w.+/-]*", text)
        for m in re.finditer(rb'\((?:clojure\.core/)?(?:format|printf)\s+"((?:[^"\\]|\\.)*)"', text):
            f = unescape(m.group(1))
            if f is None:
                continue
            convs = re.findall(rb"%(?:\d+\$)?[-+ 0,#(]*\d*(?:\.\d+)?([a-zA-Z%])", f)
            for variant in range(2):
                args = bytes(FORMAT_ARGS.get(c.decode().lower(), [30])[variant % len(FORMAT_ARGS.get(c.decode().lower(), [30]))]
                             for c in convs if c not in (b"%", b"n"))
                fmt.append(f + b"\0" + args)
    write("reader", reader)
    write("regex", regex)
    write("number", number)
    write("format", fmt)


main()
