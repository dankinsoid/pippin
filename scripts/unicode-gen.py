#!/usr/bin/env python3
# @ai-generated(solo)
"""Generate Sources/CljCore/unicode_data.inc from the Unicode Character Database (NOTES "Strings", "Unicode data").

    python3 scripts/unicode-gen.py [ucd-dir]

The version is the one the oracle JVM carries (JDK 21: Unicode 15.0), so that the runtime and the differential
fuzzer's oracle answer from the same data; a JVM on another version moves both. The files are downloaded into
.build/ucd-<version> unless a directory holding them is given, and each must match its recorded SHA-256. The
output is deterministic.

What is generated, all of it read by unicode.c alone:
  - a three-stage table from a code point to a record: the general category in java.lang.Character's numbering,
    the simple upper and lower case deltas, and the binary properties java.util.regex asks about;
  - SpecialCasing's unconditional upper-case expansions, which String.toUpperCase applies; the lower-case side has
    only U+0130 and the final sigma, which unicode.c carries by hand;
  - the scripts of Scripts.txt as sorted runs, with the names Character.UnicodeScript.forName accepts;
  - the blocks of Blocks.txt, with what Character.UnicodeBlock.forName needs beyond the canonical names.
"""
import hashlib
import os
import sys
import urllib.request

VERSION = "15.0.0"
BASE = "https://www.unicode.org/Public/%s/ucd/" % VERSION
FILES = {
    "UnicodeData.txt": "806e9aed65037197f1ec85e12be6e8cd870fc5608b4de0fffd990f689f376a73",
    "SpecialCasing.txt": "78b29c64b5840d25c11a9f31b665ee551b8a499eca6c70d770fcad7dd710f494",
    "PropList.txt": "e05c0a2811d113dae4abd832884199a3ea8d187ee1b872d8240a788a96540bfd",
    "DerivedCoreProperties.txt": "d367290bc0867e6b484c68370530bdd1a08b6b32404601b8c7accaf83e05628d",
    "Scripts.txt": "cca85d830f46aece2e7c1459ef1249993dca8f2e46d51e869255be140d7ea4b0",
    "PropertyValueAliases.txt": "13a7666843abea5c6b7eb8c057c57ab9bb2ba96cfc936e204224dd67d71cafad",
    "Blocks.txt": "529dc5d0f6386d52f2f56e004bbfab48ce2d587eea9d38ba546c4052491bd820",
    "emoji/emoji-data.txt": "29071dba22c72c27783a73016afb8ffaeb025866740791f9c2d0b55cc45a3470",
}

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "Sources/CljCore/unicode_data.inc")
N = 0x110000

# java.lang.Character's general category numbers, which CharPredicates masks with directly.
GC = {"Cn": 0, "Lu": 1, "Ll": 2, "Lt": 3, "Lm": 4, "Lo": 5, "Mn": 6, "Me": 7, "Mc": 8, "Nd": 9, "Nl": 10, "No": 11,
      "Zs": 12, "Zl": 13, "Zp": 14, "Cc": 15, "Cf": 16, "Co": 18, "Cs": 19, "Pd": 20, "Ps": 21, "Pe": 22, "Pc": 23,
      "Po": 24, "Sm": 25, "Sc": 26, "Sk": 27, "So": 28, "Pi": 29, "Pf": 30}

# The record's flag bits, in unicode_internal.h's order.
FLAGS = ["ALPHABETIC", "IDEOGRAPHIC", "LOWERCASE", "UPPERCASE", "MIRRORED", "ID_START", "ID_CONTINUE", "EMOJI",
         "EMOJI_PRESENTATION", "EMOJI_MODIFIER", "EMOJI_MODIFIER_BASE", "EMOJI_COMPONENT", "EXTENDED_PICTOGRAPHIC",
         "SPECIAL_UPPER"]
BIT = {name: 1 << i for i, name in enumerate(FLAGS)}

# Character.UnicodeBlock identifiers and old names that the canonical block name does not give.
BLOCK_IDS = {
    "Greek and Coptic": "GREEK",
    "Cyrillic Supplement": "CYRILLIC_SUPPLEMENTARY",
    "Combining Diacritical Marks for Symbols": "COMBINING_MARKS_FOR_SYMBOLS",
}
BLOCK_ALIASES = {
    "Greek and Coptic": ["Greek"],
    "Cyrillic Supplement": ["Cyrillic Supplementary"],
    "Combining Diacritical Marks for Symbols": ["Combining Marks For Symbols"],
}
# A block constant the JDK keeps with no code points: its name compiles and matches nothing.
EMPTY_BLOCKS = ["SURROGATES_AREA"]
# A script value Character.UnicodeScript does not have.
NOT_SCRIPTS = ["Katakana_Or_Hiragana"]


def fetch(ucd):
    os.makedirs(ucd, exist_ok=True)
    for name, digest in FILES.items():
        path = os.path.join(ucd, os.path.basename(name))
        if not os.path.exists(path):
            with urllib.request.urlopen(BASE + name) as r, open(path, "wb") as f:
                f.write(r.read())
        with open(path, "rb") as f:
            got = hashlib.sha256(f.read()).hexdigest()
        if got != digest:
            sys.exit("%s: SHA-256 %s, expected %s" % (path, got, digest))


def lines(ucd, name):
    with open(os.path.join(ucd, os.path.basename(name)), encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                yield [field.strip() for field in line.split(";")]


def cp_range(field):
    lo, _, hi = field.partition("..")
    return range(int(lo, 16), int(hi or lo, 16) + 1)


def load(ucd):
    """Per code point: category, simple upper and lower deltas, flags; and the upper-case expansions."""
    gc = [GC["Cn"]] * N
    upper = [0] * N
    lower = [0] * N
    flags = [0] * N
    first = None
    for f in lines(ucd, "UnicodeData.txt"):
        cp, name = int(f[0], 16), f[1]
        if name.endswith(", First>"):
            first = cp
            continue
        for c in range(first if name.endswith(", Last>") else cp, cp + 1):
            gc[c] = GC[f[2]]
            if f[9] == "Y":
                flags[c] |= BIT["MIRRORED"]
        if f[12]:
            upper[cp] = int(f[12], 16) - cp
        if f[13]:
            lower[cp] = int(f[13], 16) - cp
    props = {"Alphabetic": "ALPHABETIC", "Lowercase": "LOWERCASE", "Uppercase": "UPPERCASE",
             "ID_Start": "ID_START", "ID_Continue": "ID_CONTINUE", "Ideographic": "IDEOGRAPHIC",
             "Emoji": "EMOJI", "Emoji_Presentation": "EMOJI_PRESENTATION", "Emoji_Modifier": "EMOJI_MODIFIER",
             "Emoji_Modifier_Base": "EMOJI_MODIFIER_BASE", "Emoji_Component": "EMOJI_COMPONENT",
             "Extended_Pictographic": "EXTENDED_PICTOGRAPHIC"}
    for file in ("DerivedCoreProperties.txt", "PropList.txt", "emoji/emoji-data.txt"):
        for f in lines(ucd, file):
            if f[1] in props:
                for c in cp_range(f[0]):
                    flags[c] |= BIT[props[f[1]]]
    special = {}
    for f in lines(ucd, "SpecialCasing.txt"):
        if len(f) > 4 and f[4]:
            continue  # conditional or language-sensitive: unicode.c's final sigma, or no locale's at all
        cp = int(f[0], 16)
        low = [int(x, 16) for x in f[1].split()]
        up = [int(x, 16) for x in f[3].split()]
        if low != [cp + lower[cp]] and cp != 0x0130:
            sys.exit("U+%04X: an unconditional lower-case expansion unicode.c does not know" % cp)
        if up != [cp + upper[cp]]:
            if len(up) > 3 or max(up + [cp]) > 0xFFFF:
                sys.exit("U+%04X: an upper-case expansion past three BMP code points" % cp)
            special[cp] = up
            flags[cp] |= BIT["SPECIAL_UPPER"]
    return gc, upper, lower, flags, special


def load_scripts(ucd):
    """Script names sorted, their ISO 15924 codes, and the script index of every code point."""
    codes = {}
    for f in lines(ucd, "PropertyValueAliases.txt"):
        if f[0] == "sc" and f[2] not in NOT_SCRIPTS:
            codes[f[2]] = f[1]
    order = sorted(codes)
    index = {name: i for i, name in enumerate(order)}
    script = [index["Unknown"]] * N
    for f in lines(ucd, "Scripts.txt"):
        for c in cp_range(f[0]):
            script[c] = index[f[1]]
    return order, codes, script


def load_blocks(ucd):
    return [(cp_range(f[0]).start, cp_range(f[0]).stop - 1, f[1]) for f in lines(ucd, "Blocks.txt")]


def block_ident(canonical):
    return BLOCK_IDS.get(canonical, canonical.replace(" ", "_").replace("-", "_").upper())


def block_names(canonical):
    """The keys Character.UnicodeBlock's map holds for one block, upper-cased as forName compares them."""
    names = [block_ident(canonical), canonical.upper(), canonical.replace(" ", "").upper()]
    for alias in BLOCK_ALIASES.get(canonical, []):
        names += [alias.upper(), alias.replace(" ", "").upper()]
    return list(dict.fromkeys(names))


def width(count):
    return 1 if count <= 0x100 else 2


def trie(values):
    """The smallest three-stage split of byte values: (bytes, leaf bits, middle bits, top, middle, leaves).

    The top holds middle block numbers and the middle leaf block numbers, so each is a byte while it can be."""
    best = None
    for lb in range(2, 10):
        for mb in range(2, 10):
            leaves, leaf_at, mids, mid_at, top = [], {}, [], {}, []
            for hi in range(0, N, 1 << (lb + mb)):
                mid = []
                for lo in range(hi, hi + (1 << (lb + mb)), 1 << lb):
                    block = tuple(values[lo:lo + (1 << lb)])
                    if block not in leaf_at:
                        leaf_at[block] = len(leaves) >> lb
                        leaves.extend(block)
                    mid.append(leaf_at[block])
                mid = tuple(mid)
                if mid not in mid_at:
                    mid_at[mid] = len(mids) >> mb
                    mids.extend(mid)
                top.append(mid_at[mid])
            nleaf, nmid = len(leaves) >> lb, len(mids) >> mb
            if nleaf > 0x10000 or nmid > 0x10000:
                continue
            size = width(nmid) * len(top) + width(nleaf) * len(mids) + len(leaves)
            if best is None or size < best[0]:
                best = (size, lb, mb, top, mids, leaves)
    return best


def array(out, ctype, name, items, per_line=20):
    out.append("static const %s %s[%d] = {" % (ctype, name, len(items)))
    for i in range(0, len(items), per_line):
        out.append("\t" + ", ".join(str(x) for x in items[i:i + per_line]) + ",")
    out.append("};")


def strings(out, name, items):
    """Names joined by NULs into one array, as character constants: C99 caps a string literal at 4095 bytes."""
    text = "".join(s + "\0" for s in items)
    out.append("static const char %s[%d] = {" % (name, len(text)))
    for s in items:
        out.append("\t" + " ".join("'%s'," % ("\\'" if ch == "'" else ch) for ch in s) + " 0,")
    out.append("};")
    return len(text)


def main():
    ucd = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build", "ucd-" + VERSION)
    if len(sys.argv) <= 1:
        fetch(ucd)
    gc, upper, lower, flags, special = load(ucd)
    order, codes, script = load_scripts(ucd)
    blocks = load_blocks(ucd)

    records, record_at, info = [], {}, []
    for c in range(N):
        r = (upper[c], lower[c], flags[c], gc[c])
        if r not in record_at:
            record_at[r] = len(records)
            records.append(r)
        info.append(record_at[r])
    if len(records) > 0x100:
        sys.exit("%d records do not fit a byte" % len(records))

    out = ["// Generated by scripts/unicode-gen.py from the Unicode Character Database %s; do not edit." % VERSION,
           "// clang-format off", "", '#define UC_VERSION "%s"' % VERSION, ""]
    sizes = {}

    size, lb, mb, top, mids, leaves = trie(info)
    ctype = {1: "uint8_t", 2: "uint16_t"}
    out.append("#define UC_LEAF_BITS %d" % lb)
    out.append("#define UC_MID_BITS %d" % mb)
    array(out, ctype[width(len(mids) >> mb)], "uc_top", top)
    array(out, ctype[width(len(leaves) >> lb)], "uc_mid", mids)
    array(out, "uint8_t", "uc_leaf", leaves)
    out.append("static const clj_uc_record uc_records[%d] = {" % len(records))
    for r in records:
        out.append("\t{%d, %d, 0x%04X, %d}," % r)
    out.append("};")
    sizes["case and category"] = size + 12 * len(records)
    out.append("")

    out.append("static const struct {")
    out.append("\tuint16_t cp, up[3];")
    out.append("} uc_special_upper[%d] = {" % len(special))
    for cp in sorted(special):
        up = special[cp] + [0] * (3 - len(special[cp]))
        out.append("\t{0x%04X, {0x%04X, 0x%04X, 0x%04X}}," % (cp, up[0], up[1], up[2]))
    out.append("};")
    sizes["upper-case expansions"] = 8 * len(special)
    out.append("")

    runs = [c << 8 | script[c] for c in range(N) if c == 0 or script[c] != script[c - 1]]
    out.append("#define UC_SCRIPTS %d" % len(order))
    out.append("#define UC_SCRIPT_UNKNOWN %d" % order.index("Unknown"))
    out.append("// (first code point << 8) | script, one per run of a script.")
    array(out, "uint32_t", "uc_script_runs", runs, 8)
    out.append("// Each script's name and its ISO 15924 code, upper-cased as UnicodeScript.forName compares them.")
    names = []
    for long in order:
        names += [long.upper(), codes[long].upper()]
    sizes["scripts"] = 4 * len(runs) + strings(out, "uc_script_names", names)
    out.append("")

    out.append("#define UC_BLOCKS %d" % len(blocks))
    out.append("static const uint32_t uc_block_ranges[%d][2] = {" % len(blocks))
    for lo, hi, _ in blocks:
        out.append("\t{0x%04X, 0x%04X}," % (lo, hi))
    out.append("};")
    sizes["blocks"] = 8 * len(blocks) + strings(out, "uc_block_names", [b[2] for b in blocks])
    # The keys of UnicodeBlock's map that the canonical name, its spaceless form and its identifier do not give.
    derived = {}
    for i, (_, _, canonical) in enumerate(blocks):
        for n in (canonical.upper(), canonical.replace(" ", "").upper(),
                  canonical.replace(" ", "_").replace("-", "_").upper()):
            derived.setdefault(n, i)
    extra, refused = [], []
    for i, (_, _, canonical) in enumerate(blocks):
        want = block_names(canonical)
        for n in want:
            if derived.get(n) != i:
                extra.append((n, i))
        for n in (canonical.replace(" ", "_").replace("-", "_").upper(),):
            if n not in want:
                refused.append(n)
    # unicode.c resolves a name by the extras, then the refused identifiers, then the derived forms in block order.
    want = {n: i for i, b in enumerate(blocks) for n in block_names(b[2])}
    resolved = dict(extra)
    for n, i in derived.items():
        if n not in resolved and n not in refused:
            resolved[n] = i
    if resolved != want:
        sys.exit("block names: unicode.c's resolution would differ from UnicodeBlock's map")
    extra += [(n, -1) for n in EMPTY_BLOCKS]
    out.append("static const struct {")
    out.append("\tconst char *name;")
    out.append("\tint16_t     block; // -1: a JDK constant with no code points")
    out.append("} uc_block_extra[%d] = {" % len(extra))
    for n, i in extra:
        out.append('\t{"%s", %d},' % (n, i))
    out.append("};")
    out.append("// Identifiers the canonical names suggest but the JDK spells otherwise.")
    out.append("static const char *const uc_block_refused[%d] = {%s};"
               % (len(refused), ", ".join('"%s"' % n for n in refused)))
    out.append("")
    out.append("// Tables: %s." % ", ".join("%s %d bytes" % kv for kv in sizes.items()))
    out.append("")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print("%s: %d records (%d-bit leaves, %d-bit middles), %d scripts in %d runs, %d blocks, %d upper expansions; %s"
          % (os.path.relpath(OUT, ROOT), len(records), lb, mb, len(order), len(runs), len(blocks), len(special),
             ", ".join("%s %d" % kv for kv in sizes.items())))


if __name__ == "__main__":
    main()
