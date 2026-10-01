## Regex (Sources/CljCore/regex.c)

- **One file, no library, a java.util.regex subset.** `clj_regex_new` parses the pattern into an
  instruction array once (`re_prog`) and `re_run` walks it with an explicit backtrack stack, so nesting
  a quantifier costs heap, not C stack. Only a lookaround or an atomic group recurses into `re_run`, and
  the pattern's own nesting bounds that depth.
- **Jumps are relative to the instruction's own index**, which is what makes the compiler simple: a
  quantifier inserts its `SPLIT` *before* the block it repeats (`prog_insert`) and a counted repeat copies
  the block with `memcpy` (`repeat_block`), and neither has to fix up a target. `{n,m}` expands to `n`
  copies plus `m - n` optional ones, capped at `RE_MAX_REP` and `RE_MAX_PROG` so a pattern cannot ask for
  an unbounded program.
- **The matcher works on code points, not bytes** (`re_text`: the input decoded once into a code point
  array plus a byte offset per index), as `subs` and `index-of` do. A matcher object keeps its `re_text`,
  so `re-seq` over one input is linear in it rather than quadratic; `split` and `replace` keep one
  `re_ctx` across the matches of a scan, so only the first match allocates (bench/RESULTS.md).
- **A group's bounds live in an int32 slot array with an undo log.** Every `SAVE` records the old value,
  and a backtrack point holds the log's height, so restoring a group is popping the log. The same array
  holds the loop marks an unbounded quantifier needs: `MARK`/`PROGRESS` refuse an iteration that consumed
  nothing, which is what keeps `(a*)*` from spinning. Mark slots sit past the group bounds, whose count is
  only known once the pattern is parsed, so the compiler patches their indexes at the end.
- **Case-insensitivity is folded inside the range test**, not applied to the class's answer: `(?i)[^a]`
  must still refuse `A`, which an OR over both cases would accept. ASCII only, as the rest of the runtime's
  case handling is (docs/jvm-differences.md).
- **A negated predefined class is a nested class, a positive one is merged**: `\d` adds its ranges to the
  enclosing class, `\D` hangs off it as a sub-class with `negate`, and `&&` makes the rest of the class
  body the intersection operand, so `[a-z&&[^aeiou]]` is one recursive `class_member` call.
- **Catastrophic backtracking is the host's deadline, not a memo table**: `re_run` reads the clock once per
  4096 backtracks through `clj_deadline_expired` and unwinds with "Execution timed out". Without a deadline
  set, `#"(a+)+b"` against a long string of `a`s runs until the host gives up. A memo table would bound the
  work instead, at the cost of a table per match; trigger is a host that cannot set a deadline.
- **`=` and `hash` read the pattern text**, so `#"ab"` equals `#"ab"` and the two are one set element,
  where the JVM compares `Pattern` by identity (docs/jvm-differences.md). That also makes a pattern a
  serializable node constant: it prints as `#"..."` and reads back equal, so `node_data.c` needs no arm
  of its own for it.
- **`re-find` scans every start position**, since the program carries no first-character filter; the
  leftmost-first rule and `re-matches`' whole-input rule are the same `re_run` with one flag. Trigger for a
  first-set bitmap: a `re-find` in a profile's inner loop (bench/RESULTS.md).
- **`nth` on a matcher is its group**, as `RT.nth` special-cases `Matcher`; the type carries `lookup` for
  the same reason and no core interface bit, so it is not a collection.
- **The `$`-expansion follows `appendReplacement`**: the first digit is always the group and the following
  digits extend it while the group exists, `${name}` names one, and a backslash quotes the next character.
  A group the pattern does not have is an error, not an empty string.
- **The syntax the parser takes**: literals and the escapes `\\ \t \n \r \f \a \e \0nnn \xhh \x{...}
  \uXXXX` and `\Q...\E`, `.`, classes with ranges, negation, nesting and `&&`, `\d \D \w \W \s \S \b \B`,
  `\A \z \Z`, `^ $`, groups (capturing, `(?:)`, `(?<name>)`, `(?=) (?!) (?<=) (?<!)`, atomic `(?>)`),
  alternation, `* + ? {n} {n,} {n,m}` greedy, lazy `?` and possessive `+`, backreferences `\1` and
  `\k<name>`, a dozen `\p{...}` names, and the flags `i s m x` inline and scoped. Everything else is a
  compile error naming the offset, in an `ex-info` whose data carries `:pattern` and `:offset`
  (docs/jvm-differences.md lists what is missing).
- **`re-seq` is a lazy seq over one matcher**, as Clojure's is, so two consumers of the same seq share its
  position; `clojure.string/split` and `replace` scan in C instead (`re-split*`, `re-replace*`), which keeps
  the literal-separator fast path of builtins_string.c untouched for a string or char separator.

