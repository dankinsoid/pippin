## Strings (Sources/CljCore/string.c, include/clj/string.h; design §4 «Строки»)

- **One constructor.** Every string is made by `clj_string_new` — the reader, `str`, the builders, `subs`, the
  regex slices, the Swift `Value` bridge, ObjC and C declarations all reach it — so whatever a string knows about
  its bytes is decided there, once, while they are copied in.
- **The index unit is the code point**, and every index-taking or index-returning op counts it: `count`, `subs`,
  `nth`/`get`, `contains?`, `seq` and a string seq's `count`, `index-of`/`last-index-of`, `char`, `format`'s width
  and precision, the regex matcher (`re_text`), the reader's `😀` pair as one scalar. The audit of
  2026-10-10 found no op mixing units: the C side had used code points from the start, `capitalize` and
  `trim-newline` go through `subs`/`nth`. One JVM difference unrelated to the unit went with it: `index-of` from
  past the end clamps there, so an empty needle answers the length, as `String.indexOf` does. What differed from the JVM outside the BMP is the row of
  docs/jvm-differences.md; `jvm-hash` walks UTF-16 units (`utf16_next`) and agrees with the JVM's `hash` on
  astral strings, which fuzz/regressions/strings-unicode.clj checks against the oracle.
- **ASCII is a header bit, `CLJ_STRING_ASCII` (bit 10)**, set by the constructor's 8-bytes-at-a-time scan and never
  cleared: an index is the byte offset, `count` is `len`, `subs`/`nth`/`index-of` are O(1) plus the copy. A header
  bit rather than a body field: the body has no spare word (`hash` and `len` fill it to 24 bytes), and one more
  would grow every string for a bit. A string is a leaf object (no `each_child`, no
  drop), so the bit never has to survive into rc.c's dead link, which keeps only `LARGE`, `META` and `SHAPE`.
- **A non-ASCII string past 64 bytes carries a tail** after its NUL, 4-aligned: its code point count and the byte
  offset of every 64th code point (*crumbs*), at most `(len - 1) / 64` of them, ~6 % of the bytes. Both are built
  by one pass on the first indexed access and published by a release store of the count, which an acquiring reader
  takes as "crumbs valid"; two racing builders store the same words, so a shared string needs no lock. Index →
  crumb plus a scan of at most 64 code points; byte → index (`index-of`, `last-index-of`, a string seq's count) a
  binary search over the crumbs plus a scan. Shorter non-ASCII strings scan from the start; ASCII ones need neither.
  The tail is inline rather than a side allocation the string's drop frees: a drop would take every string off
  rc.c's leaf path (`is_leaf`: dealloc at once instead of the dead-object stack), which is the common case.
- **Malformed bytes** (the reader does not validate UTF-8, NOTES "Reader") count by one rule: a code point starts at
  every byte that is not `10xxxxxx`. `clj_utf8_decode` still reads its lead byte's length, so a truncated sequence
  decodes from bytes the next boundary also claims.
- **Integers print through `clj_int64_decimal`** (number.c, two digits per division), not `snprintf`: the printer's
  fixnum and long arms, `str` of an integer (which no longer goes through `pr-str`), and `format`'s `%d`. Doubles
  keep the shortest-round-trip printer (`put_double`), untouched.
- **A literal pattern skips the engine in `split` and `replace`**: `clj_regex_new` keeps the pattern's bytes when
  it holds only plain characters and backslash-quoted ASCII punctuation (`#","`, `#"\."`, `#"::"`, `#"😀"`), and
  `clj_regex_split`/`replace_scan` then search with `clj_bytes_find` (memchr, then memcmp; `memmem` is not C11) and
  slice bytes, with no `re_text` decode. A literal match is never empty, so the zero-width rules do not arise; the
  limit, the leading empty part and the trailing empties follow `Pattern.split` as the engine path does, and
  `$0`, `\$` and the group errors go through the same `expand`. `re-find`, `re-seq` and `re-matches` still run the
  engine. The literal `str-split*`/`str-replace*` (a string or char separator) search the same way.
- [ ] **No UTF-16 view yet.** The crumbs are code point → byte; the Apple boundary's `NSRange` indexes (design §4
  «Строки», "На границе с Apple индексы UTF-16") need code point ↔ UTF-16 on the same crumbs. Trigger: the first
  bridge API that passes an index across.
- [ ] **Small strings in the value (tag `100`) and the borrow-bytes API** are design §4's next steps, in that
  order after this one. Trigger: the census share of 8–11-byte strings, or the borrow API's first caller.
- [ ] **The ASCII scan reads every new string's bytes once more**: `clj_string_new` is 5 % of the strings workload
  (bench/RESULTS.md, "Strings"). A builder that knows its parts' bits (`str`, `join`, `subs` of an ASCII string)
  could pass the answer in. Trigger: `clj_string_new` above the copy itself in a profile.
