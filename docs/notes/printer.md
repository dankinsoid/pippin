## Printer (Sources/CljCore/printer.c)

- **Map entries are collected into a temporary array per map** because `clj_map_each` is callback-only.
  Trigger: printing huge maps in a profile. Fix: a resumable map iterator.
- **An error message quotes a value through `clj_pr_str_max`** (`CLJ_ERROR_PRINT_MAX` bytes, then `...` and
  the closers of what is still open), so a message about an unbounded lazy seq does not realize it. Every
  error path that prints arbitrary runtime data uses it — "cannot be invoked", the arity error, "No value
  supplied for key", "Duplicate key", the protocol and node-data messages; `pr-str` itself is unbounded, as
  Clojure's is with `*print-length*` nil.
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

