## Printer (Sources/CljCore/printer.c)

- [ ] **Map entries are collected into a temporary array per map** because `clj_map_each` is callback-only.
  Trigger: printing huge maps in a profile. Fix: a resumable map iterator.
- **An error message quotes a value through `clj_pr_str_max`** (`CLJ_ERROR_PRINT_MAX` bytes, then `...` and
  the closers of what is still open), so a message about an unbounded lazy seq does not realize it. Every
  error path that prints arbitrary runtime data uses it — "cannot be invoked", the arity error, "No value
  supplied for key", "Duplicate key", the protocol and node-data messages; `pr-str` itself is unbounded, as
  Clojure's is with `*print-length*` nil.
- **An integer prints through `clj_int64_decimal`**, as `str` and `format`'s `%d` do (NOTES "Strings"); a double
  through `put_double`'s shortest round trip, which still calls `snprintf`/`strtod`.
- **Control characters print as `\uXXXX`** inside strings and as char literals; Clojure prints them raw.
  Readable by both, but `(pr-str "\u0001")` differs from the JVM byte for byte.
- **`*print-length*` and `*print-level*` are read by `clj_pr_str_dynamic`**, the entry `pr`, `prn`, `print`,
  `println` and `pr-str` use, once per print into a `limits` pair; `clj_pr_str` (`str`, `Value.description`,
  the codec) and `clj_pr_str_max` (error messages) never read them, so an error message stays the same
  under any binding. The rules are print-sequential's: after `length` items a frame writes its separator and
  `...` (`elide`), which for a seq realizes one more element as the JVM's `[x & xs]` does; a collection
  about to open at depth `stack.count >= level` prints as `#` (`is_collection`: every kind that gets a frame,
  the `#error` map excepted). A negative length prints everything and a negative level nothing, as on the
  JVM; a non-integer throws "cannot be cast to a number". The vars are looked up by name on each call until
  core.clj has defined them (vars are immortal, so the cache never goes stale).
- **`*print-readably*`, `*print-meta*` and `*print-namespace-maps*` are read with the limits**, by
  `clj_pr_str_dynamic` only: `pr` prints as `print` under a false `*print-readably*`; under `*print-meta*` (and a
  readable print) a symbol, a var or a collection with non-empty meta prints `^meta ` first, a lone `:tag` as the
  tag (`put_meta`); under `*print-namespace-maps*` a hash or sorted map whose keys are all keywords or symbols of
  one namespace prints `#:ns{…}` with the keys stripped (`lift_ns`, the frame then owning the stripped keys). The
  root is false, as the JVM's is outside its REPL. `*print-dup*` is read by nothing.
- **`print-method` is a multimethod on `type` (or a keyword `:type` in the meta), consulted by `print_hook`** for a
  deftype or record instance and a value with a keyword `:type` in its meta, never for a built-in type: it calls
  core.clj's `print-with-method*`, which answers nil when only `:default` applies (the C printer then prints as
  ever) and otherwise the text the method wrote to a `StringWriter`. A method writes with `(.write w x)` — a
  string, a char or a char's code — reaching `IWriter`'s `-write` through the instance send rule (NOTES "ObjC
  bridge"), and prints a part with `(print-method part w)`, whose `:default` is `pr-str` or `print-str` by
  `*print-readably*`. Only the pr family consults it (`limits.hooks`): an error message never runs user code.
- **`*out*` and `*err*` are two `PrintStream` values**, `clj_output` (runtime.c) checking one thing: `*out*`
  thread-bound to the root of `*err*` sends the bytes to the process's standard error, past every capture.
  Everything else is the capture stack and then the host's output, as before; `with-out-str` binds `*out*` to its
  root while it captures, so `(binding [*out* *err*] (with-out-str …))` captures, as the JVM's StringWriter does.
  `(.write *out* s)` and `(.flush *out*)` work through `PrintStream`'s `IWriter`.
- **`#queue [1 2 3]`** for a PersistentQueue (queue.c): the JVM prints an address. The `F_QUEUE` frame is the seq
  frame with `]` as its closer; nothing reads the form back (docs/jvm-differences.md).
- **`format` is java.util.Formatter's subset** (builtins_format.c): `%s %S %b %B %c %C %d %o %x %X %e %E %f %g
  %G %n %%`, the flags `- + space 0 ,`, width, precision and `n$` positions, with the ordinary argument index
  independent of explicit ones as Formatter's is. `%s` is `str` with `null` for nil (`clj_str_value`,
  builtins.c); `%d` takes a long or a bigint and `%x`/`%o` a long (two's complement, as `Long`); `%f`/`%e`/`%g`
  take a double or a decimal and refuse an integer, as the JVM's `f != java.lang.Long` does. Floats round the
  shortest round-trip digits HALF_UP (`shortest`, `round_to`), because Formatter does — `(format "%.2f" 1.005)` is
  `1.01`, which C's printf would print as `1.00` — and `%g` follows Formatter's rule (decimal within
  `[1e-4, 10^precision)`, trailing zeros kept). Anything else (`%h`, `%t`, `%a`, the `#` and `(` flags, a
  precision on `%d`, a bigint under `%x`) is an error naming the spec.

