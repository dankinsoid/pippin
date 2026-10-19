## Unicode data (Sources/CljCore/unicode.c, unicode_data.inc, scripts/unicode-gen.py; design §4 «Строки»)

- **The data is the UCD at one pinned version, the oracle JVM's.** `scripts/unicode-gen.py` downloads
  UnicodeData, SpecialCasing, PropList, DerivedCoreProperties, Scripts, PropertyValueAliases, Blocks and
  emoji-data of Unicode 15.0.0 (JDK 21's, the `make fuzz` oracle on CI) into `.build/ucd-15.0.0`, checks each
  against its recorded SHA-256 and writes `unicode_data.inc`, which only unicode.c includes. The output is
  deterministic, so a regeneration that changes it is a data change. A JVM on another version moves both sides
  at once: regenerate at the CI JVM's version, never ahead of it.
- **What the JDK derives, checked once over every code point.** A dump of JDK 21's `Character` (category,
  `toUpperCase`/`toLowerCase` of the code point and of its one-character string, the 24 `is*` predicates,
  `UnicodeScript.of`, `UnicodeBlock.of`) agreed with the generator's derivation at all 1,112,064 scalars, and
  its `UnicodeBlock` and `UnicodeScript` name maps with the generator's names: the block identifiers the
  canonical name does not give (`GREEK`, `CYRILLIC_SUPPLEMENTARY`, `COMBINING_MARKS_FOR_SYMBOLS`) and their old
  names, `SURROGATES_AREA` with no code points, and no `Katakana_Or_Hiragana` script. The runtime side of the
  same comparison is fuzz/regressions/strings-unicode-case.clj, which `make fuzz` replays against the oracle.
- **One record per code point through a three-stage table.** The record is the category in
  `java.lang.Character`'s numbering (so a `CharPredicates` mask applies as is), the simple upper and lower
  deltas, and fourteen flags: Alphabetic, Ideographic, Lowercase, Uppercase, Bidi_Mirrored, ID_Start,
  ID_Continue, the six emoji properties and "SpecialCasing expands its upper case". 253 distinct records, so a
  leaf is a byte; the generator picks the split (4-bit leaves, 5-bit middles) that minimizes the bytes. Scripts
  are 1657 sorted runs (`first << 8 | script`, a binary search), blocks 327 ranges, both read only by `\p{…}`.
- **Size.** Tables: 25,964 bytes of category and case, 816 of upper-case expansions, 9,004 of scripts, 8,076 of
  blocks (names included), about 44 KB; the iOS device slice grew by 61,960 bytes with the code (NOTES "iOS").
  The scripts as a three-stage table instead of runs were 21.5 KB.
- **Case mapping is String's under the root locale** (docs/jvm-differences.md): the simple mappings, SpecialCasing's
  unconditional upper-case expansions (`ß` → `SS`, the ligatures, the Greek iota-subscript forms), U+0130's
  lower case `i̇`, and the final sigma. `clj_uc_upper_utf8`/`lower_utf8` return NULL for text that maps to itself,
  and keep a byte sequence that is not UTF-8 as it is.
- **The final sigma follows the JDK, not UAX #21.** `ConditionalSpecialCasing.isFinalCased` looks for a cased
  letter before the sigma and none after it within the word that the JDK's word `BreakIterator` draws
  (`BreakIteratorRules`, "WordBreakRules"), and `isCased` is the JDK's own list of categories and ranges. unicode.c
  draws the same word without the iterator: letters and digits join, one mid-word punctuation (`.`, `'`, `"`, a
  dash, a connector, U+2027) between letters and one mid-number punctuation (`.`, `'`, `"`, `,`, U+066B) between
  digits, marks stay with their base, format characters are unseen, kana and the old CJK range stand apart. So
  `ΑΣ1` and `Α.Σ` end in `ς`, `Α..Σ` and `Α$Σ` in `σ`.
- **Patterns resolve `\p{…}` as `Pattern.family` does** (`clj_uc_family`): `In` a block, `Is` a Unicode property,
  then a POSIX name over Unicode, then a `CharPredicates.forProperty` name, then a script; `name=value` for `sc`,
  `script`, `blk`, `block`, `gc`, `general_category`; a bare name a POSIX one only under `(?U)`, else
  `forProperty`'s, compared as written. Names are upper-cased as `String.toUpperCase` does, through the same
  mapping. Under `(?i)` the case properties widen to all three cases, as `CharPredicates` does.
- **`\X` is `jdk.internal.util.regex.Grapheme`**, which derives the cluster break class from the category,
  Extended_Pictographic and a few hand lists rather than from GraphemeBreakProperty, so it falls out of the same
  record (`clj_uc_grapheme_end`).
