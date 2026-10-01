## Reader (Sources/CljCore/reader.c)

- **Tagged literals run their tag fn at read time, inside `clj_read`** (`F_TAG`, `read_tagged`): the tag symbol
  and then the form arrive through `push_value`, and the frame applies `clj_reader.read_tag` to them, so the
  value a literal reads as is a constant to the analyzer like any other. The hook is the runtime's
  (`clj_reader_read_tag`, runtime.c): a dotted tag is a record constructor (`#ns.Name{…}` through
  `clj_record_from_map`, `#ns.Name[…]` positional, LispReader's CtorReader order), then `*data-readers*`, then
  the built-in `inst` and `uuid` (`clj_default_data_reader`, which a reader with no hook also gets), then
  `*default-data-reader-fn*`, else "No reader function for tag". A throw out of the fn is a reader error with
  the exception's message at the literal's position. `default-data-readers` is a plain map of the two native
  fns (`read-inst*`, `read-uuid*`), so a library can merge its own into `*data-readers*`. Not here:
  `tagged-literal`/`reader-conditional` values and `#=` (docs/jvm-differences.md).
- **`#uuid` and `#inst` are value types** (uuid.c, inst.c): a boxed pair of signed longs with `UUID.hashCode`,
  `fromString`'s lenient grouping for `parse-uuid` and `arc4random_buf` for `random-uuid`; a boxed
  millisecond count named `Date` with `Date.hashCode`, read by clojure.instant's grammar (every field
  range-checked, the fraction taken as nanoseconds) through proleptic-Gregorian day arithmetic
  (`days_from_civil`) and printed `yyyy-MM-ddTHH:mm:ss.SSS-00:00` in UTC as the JVM does. Both compare, so
  they sort, and both are node constants: the codec reads them back from their printed form. `str` of a Date
  is that text, not `Date.toString` (docs/jvm-differences.md).
- **Namespaced maps** `#:ns{…}`, `#::{…}` and `#::alias{…}` (`F_NS_MAP`, `read_ns_map_prefix`): the prefix
  token is read, a `{` must follow after optional whitespace, and the map's keys are rewritten as it closes
  (a keyword or symbol with no namespace takes `ns`, one in `_` loses its own, the rest stay); `#::` resolves
  through `resolve_ns` like `::kw`. A collision after the rewrite is "Duplicate key".
- **`#"..."` keeps its text verbatim** (`read_regex`): the string escapes are *not* applied, so a backslash
  reaches the pattern as written and only `\"` fails to close the literal, as LispReader's RegexReader does.
  The pattern compiles at read time, so a syntax error is a reader error at the literal's position.
- **`#(...)` rewrites its body after the list is read** (`fn_literal`): `%`, `%N` (1–20), `%&` become
  `p1__N#`/`rest__N#` params of a `fn*`, with one recursive walk over the literal's own nesting (the
  reader is otherwise iterative). Nested `#(` is refused, as LispReader does.
- **An unselected `#?` branch reads as data whatever it contains** (`in_unselected_branch`, `F_SUPPRESSED`):
  a tagged literal there reads as nil without running its tag fn (the tag's form is read and dropped), a
  `#:ns{}` map as a plain one and a `#=` as nil, instead of ending the file, as Clojure's suppressed read
  does; in the selected branch a tag runs and `#=` is the error it is outside one. A `#"..."` there reads as the plain string of its text and is never compiled, so a pattern
  meant for another runtime's engine cannot fail the read. Which branch is selected is known while reading: the body list's items
  so far are on the value stack, features at the even indexes. Suppression follows the enclosing conditionals,
  so a selected inner branch inside an unselected outer one is suppressed too. Numbers need no suppression:
  every literal the reader takes now reads in either branch (numeric tower).
- **Reader conditionals** `#?`/`#?@` select the first branch whose feature is in `clj_reader.features`
  (a set of keywords copied from the process-wide `clj_reader_set_features` at init; `:default` always
  matches; nil means `:default` alone). No branch → the form reads as nothing (an EOF at top level, a
  missing item inside a collection); `#?@` splices only into an enclosing collection. Which key names
  this runtime is still open (Open decisions); the corpus harness sets `#{:clj}` per library.
- **A keyword's parts may start with a digit** (`:0`, `:1/2`), a symbol's may not: Clojure reads and prints
  both keywords, and the suite's `(keyword "0")` round-trip needs it.
- **`::kw` and `::alias/kw`** resolve through `clj_reader.resolve_ns` (`clj_reader_resolve_ns`: the
  current namespace or one of its aliases); with the hook NULL they are reader errors, an unknown alias
  is "Invalid token". Hex, octal and `NrDDD` radix integers read into longs, or into bigints past 64 bits
  (numeric tower).
- [ ] **Every non-empty list read costs a `{:line :column}` map** (map wrapper plus one node) on its head
  cons, as Clojure attaches positions to lists only; `'x`, `@x`, `#'x` and the syntax-quote output
  are built by the reader without one. Syntax-quote drops the meta of the forms it rebuilds where
  LispReader keeps everything but the position keys. Trigger: `^:once`-style meta inside a
  syntax-quoted template. Fix: `sq_pop` wrapping the rebuilt collection in `with-meta` when the source
  had non-position keys.
- **Syntax-quote resolves through `clj_syntax_quote_resolve` in the thread's current namespace** (`*ns*`),
  not the `clj_env.ns` the host later analyzes in; `resolve_ctx` is unused. The two agree because every
  loader evaluates with `env.ns` nil (the current one) and reads one form before evaluating it, so an
  `in-ns` governs the forms after it; a host that reads a whole file first (`Value.readAll`) resolves
  everything in the namespace current at read time. `alias/x` is rewritten to the aliased namespace,
  a qualified symbol whose prefix is no alias stays as written; no Java class heuristic (`foo.Bar` is
  treated as a namespace prefix and left alone).
- **`~`/`~@` outside syntax-quote are reader errors**, where Clojure reads `(clojure.core/unquote x)`
  and fails later. A literal `(clojure.core/unquote x)` inside a syntax-quote is still an unquote.
- **Only lists carry positions**, so an error on a bare symbol or vector reports the innermost
  enclosing list, or `form_line`/`form_col` of the top-level form for a top-level symbol; a form
  built by a macro reports the list the macro call sat in. Clojure does the same.
- [ ] **Input is not validated as UTF-8** except inside a character literal; malformed bytes pass through
  into strings and symbols, and a column counts every non-continuation byte. Trigger: a non-Swift host
  feeding raw bytes.
- **`strtod`/`snprintf` in reader and printer follow the C locale**, which the runtime never changes;
  a host calling `setlocale` with a comma decimal point would break doubles.

