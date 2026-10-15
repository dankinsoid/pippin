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
- **ASCII is a header bit, `CLJ_STRING_ASCII` (bit 10)**, set by the constructor's 8-bytes-at-a-time scan and
  cleared only by an in-place append of a non-ASCII byte (the string builder, below): an index is the byte offset, `count` is `len`, `subs`/`nth`/`index-of` are O(1) plus the copy. A header
  bit rather than a body field: the body has no spare word (`hash` and `len` fill it to 24 bytes), and one more
  would grow every string for a bit. A string is a leaf object (no `each_child`, no
  drop), so the bit never has to survive into rc.c's dead link, which keeps only `LARGE`, `META` and `SHAPE`.
- **A non-ASCII string past 64 bytes carries a tail** after its NUL, 8-aligned: the index hint (below), its code point count and the byte
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
- **A walk by index over a long non-ASCII string is one code point a step**: the tail carries an index hint, one
  64-bit word `(index << 32) | byte offset` of the code point read last, beside the count and the crumbs. An index
  within a crumb's distance of the hint scans from it, forward or back (UTF-8 finds a lead byte going backwards as
  well), and stores itself as the next hint; anything farther starts from its crumb as before. So a reader walking a
  string with `nth`, as clojure.data.json's `StringPBR` does with `charAt`, decodes one code point a character where
  it decoded ~32. One word, loaded and stored relaxed, is what makes it safe on a shared string — the json payload is
  a var's, so shared, and so is any string out of an atom, a channel or a literal: a reader on another thread sees
  one store or another, never one's index with another's offset, and every value ever stored is true of the
  immutable bytes, so a stale hint is a valid start like a crumb. Two threads walking one string overwrite each
  other's hint and fall back to the crumbs. The tail grew by the word and is 8-aligned; ASCII strings and non-ASCII
  ones of at most 64 bytes carry none and take no branch for it. `CLJ_NO_OPT=hint` scans from the crumb. Measured
  (bench/RESULTS.md "JSON's string gap"): json closed 0.80× its base, dev 0.83×, `clj_string_offset_slow` from
  21.3 % of the closed profile to 4.1 %; `clj_string_char_at` over every index in order 67 → 7.5 ns.
- **The string builder is the string, appended in place** (builtins.c `clj_str_owned`, string.c
  `clj_string_append`). `(str s x)` of two arguments is an intrinsic whose consuming form takes `s` at +1, as `conj`
  takes its vector; a unique unshared `s` gets `x`'s text onto its end in its own cell, which grows by half again
  when full (`clj_cell_capacity`: the pool cell's size, or `malloc_size` of a large block, docs/portability.md), so
  n appends copy O(n) bytes rather than O(n²). Every site that owns its accumulator takes it: a last-use local,
  compiled or interpreted, an owned parameter (`(fn [s i] (str s i))` under a reduce), `(reduce str "" xs)` through
  the reducer's consuming call, and `(vswap! v str x)`, which the macro sends to `vswap-take*` as it does `conj`:
  the string leaves the cell for the append and goes back, for an `x` whose text runs none of the caller's code (a
  string, nil, a char, an integer); any other `x` derefs and resets. The append clears the ASCII bit when its bytes
  are not ASCII, zeroes the hash cache and resets the tail's count and hint, so the string reads as a fresh one. No
  `StringBuilder`: Java's API is not the target (design §8), and the Clojure idiom is the builder (design §4
  «Строки»). The first append to a literal or a shared string copies it, as `str` always did. clojure.data.json's
  port builds its output and its slow strings this way (`(vswap! sb str x)`). `CLJ_NO_OPT=grow` copies every time.
  Measured (bench/RESULTS.md "JSON's string gap"), against the vector of parts joined at the end: json closed 0.87×,
  its allocations 882k → 376k an iteration; strings 0.95× (its `(reduce (fn [s i] (str s i)) ...)`);
  `(reduce str "" (range 5000))` 666 → 43 ns an append.
- [ ] **No bulk copy between a string and a char array.** Upstream clojure.data.json reads each string through a
  64-char `char[]` window that `String.getChars` fills with one memcpy and `String(char[], off, len)` turns back into
  a string. Here the window can only be filled a char at a time (`nth` and `aset`, 64 of each whatever the string's
  length, which in json is mostly a short key), so the port's restored window cost json closed 1.93×, dev 1.80×,
  interp 1.61× against reading the string in place (run 38737821230; compiled generic calls 2.42M → 9.78M an
  iteration), and the port reads in place. What would close it is a native copy of a code point range into a char
  array and a string of a char array's range, which clojure.core has no name for and Java's names are not the
  target (design §8). Trigger: a port whose algorithm needs the window, or the Foundation port's buffers.
