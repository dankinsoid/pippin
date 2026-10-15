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
- **Bytes are borrowed, never read through the object** (design §4 «Строки», the first step of small strings). The
  layout is string.c's: string.h defines `clj_string` for its own inline accessors, then poisons the name
  (`#define clj_string clj_string_layout_is_private_to_string_c`) and undefines `CLJ_STRING_ASCII` unless the
  includer defined `CLJ_STRING_LAYOUT`, which only string.c does, so a field read anywhere else does not compile —
  SwiftPM's module builds included, which export the macro with the header. On the value, no buffer:
  `clj_string_len`, `clj_string_is_ascii`, `clj_string_count`/`offset`/`index_at`. The bytes: `clj_string_borrow(s,
  &buf)` returns `clj_str {bytes, len}` with `bytes[len]` NUL, valid while `s` lives and the caller's `clj_str_buf`
  (8 bytes, where a word string will unpack) is in scope; `CLJ_STR(s)` borrows into a compound literal that lives
  to the end of the enclosing block, and is the form of ~290 of the 324 sites. A pointer kept past its block takes
  a named buffer (`gensym`'s prefix, `c-fn*`'s encodings one per argument, clj-facts' `what`), or a macro that keeps
  the literal in the caller's block (compiler.c `VAR_NAME_BYTES`, the parser harness's `fz_cstr`). Swift reads
  through `Value.withStringBytes`/`Value.decode`, the buffer living inside the closure. The view carries no ASCII
  bit: four callers want it, and `clj_string_is_ascii` reads the value.
- **Bytes that outlive a borrow are materialized**: `clj_string_materialize` (+1, a heap string; today the string
  retained) and `clj_string_heap_bytes`. `slurp`, `spit` and `file-seq` hand their path and text to a pool thread
  while the caller parks (the parker's frame is nobody else's, design §4), and a `deftype`'s `t.name` points into
  its `name` slot for the type's life. objc.c's `'*'` (a `char *` argument, struct field, return or `objc-write!`)
  reads `clj_string_heap_bytes` of the value itself, with no owner: the pointer lives as long as the value, as
  before.
- **Neutral by construction**: every accessor inlines to the field load it replaced. At -O2 the assembly of
  printer.c, reader.c, seq.c, coll.c, compare.c, jvm_hash.c, reuse.c and builtins_format.c is identical to the
  direct reads; string.c, builtins_string.c, regex.c and builtins.c differ in register allocation and block order,
  and per function the instruction count moves by at most 2 (`clj_string_char_at` −1, `b_atom`'s cold error tail
  +2). The generated C reads no
  string bytes: constants go through `clj_string_new`, checks through `&clj_string_type`.
- [ ] **Small strings in the value (tag `100`)** are design §4's next step. Over the borrow API it adds one tag
  branch per borrow (unpack ≤ 7 bytes and a NUL into the buffer) and per `clj_string_len`/`is_ascii` (from the
  word), a heap copy in `clj_string_materialize`, and an assertion of a heap string in `clj_string_heap_bytes`.
  Undecided there: an owner for objc.c's `'*'` copy (the call's for an argument; for a return, a struct field or
  `objc-write!` none exists, an autorelease would be one); whether a materialized heap string of ≤ 7 bytes may meet
  a word one in `=`/`hash` (compare bytes) or must never escape as a value; and the other half of the inventory,
  code that takes `clj_is_string` to imply `clj_is_ptr` (`clj_header_of`, RC, `clj_is_unique` reuse), which this
  step did not touch. Trigger: the census share of strings of at most 7 bytes (8–11 for the packed encodings).
- [ ] **The ASCII scan reads every new string's bytes once more**: `clj_string_new` is 5 % of the strings workload
  (bench/RESULTS.md, "Strings"). A builder that knows its parts' bits (`str`, `join`, `subs` of an ASCII string)
  could pass the answer in. Trigger: `clj_string_new` above the copy itself in a profile.
- [ ] **A loop of `nth` over a long non-ASCII string pays the crumb scan on every read.** Each index decodes up to 63
  code points from the nearest crumb, so a reader walking a string by index, as clojure.data.json's `StringPBR` does
  with `charAt` (`nth` here), spends ~32 decodes a character where the JVM's `charAt` is one load. In the json
  workload (run 38728053580) `clj_string_offset_slow` is 21.3 % of the closed build's samples and 18.1 % of dev's,
  the largest single item, and the port's `read-quoted-string`, which indexes its 64-char window in place where
  upstream copies it into a `char[]`, is 42 % of an iteration inclusive (bench/RESULTS.md "JSON"). What would close
  it: a last-read `(index, byte offset)` hint beside the crumbs, so the next index scans from there (one 64-bit
  store, racy but self-validating as the crumbs are), or a string cursor the reader keeps. Trigger: a decision to
  make json's parse a target, or a second workload whose profile it leads.
- [ ] **No string builder.** `StringBuilder`, `StringWriter` and `goog.string.StringBuffer` have no counterpart: a
  ported library collects parts in `(vswap! v conj x)`, which grows the vector in place, and joins them at the end
  (corpus/data-json's `string-builder`, core's own `StringWriter`). The join walks the vector through a seq, one
  `vector-seq` cell per part: 471.6k of the json workload's 901.8k allocations an iteration are `join`'s, and the
  port's `builder-str` is 10 % of the closed iteration inclusive (run 38728053580, bench/RESULTS.md "JSON"). A C builder (an appendable byte buffer with an ASCII bit, `.append` reached as an
  instance send or as functions) is the fix; ClojureScript has one, so it is inside the §3 bound. Trigger: a
  library's appends or its join in a profile's top items, or the cheaper half first: `str-join*` over a vector by
  index.
