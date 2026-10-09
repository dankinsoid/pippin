# Fuzzing

Generated inputs against an oracle: design §10's differential fuzzer and design §3 «Корректность реализации»
item 2. `fuzz/` holds it; `make fuzz` is the bounded pass, `make fuzz-long` the one run by hand.

- **The shape.** One case file per seed, self-contained: a header defining the normalizer's flags, then
  `fuzz/prelude.clj` verbatim, then one `(fz <index> <expr>)` per generated expression. Every runner is a
  process over that one file, printing `<index><TAB><outcome>` per line, and the driver compares the
  transcripts. A case file runs by hand under any runner with no driver
  (`.build/plain/debug/clj-fuzz .build/fuzz/seed1.clj`), which is what makes a finding reportable.

- **The oracle is the pinned JVM Clojure 1.12.6**, the `JVM_DEPS` of `make api-diff`, and the driver refuses to
  run under any other version. It is a subprocess (`java -cp` with the driver's own classpath, so the same jar),
  bounded by a timeout: a generated form is bounded by construction, but a shrink candidate is not.

- **The outcome of one expression is its value as one canonical text, or that it threw.** `fz-norm` prints a
  value itself instead of calling `pr-str` on the whole of it, so that a map's and a set's order can be sorted
  away (`:map-seq-order`), an integer's digits can be printed without the JVM's `N` type marker
  (`:no-bigint-marker`), and sequentials print alike. A runner that dies takes its transcript's tail with it, and
  the missing indices are the finding.

- **Which error was thrown is compared by family** (`fuzz/errors.edn`). A throw prints raw — `#fz/throw class
  java.lang.ClassCastException "msg"` from the oracle (`type` of the throwable), `#fz/throw nil "msg"` from ours
  (`ex-type`, resolved like `jvm-hash`) — and the driver maps each side to one of six families (`:index`,
  `:arith`, `:arity`, `:state`, `:argument`, `:type`) by the first rule that matches: the JVM's by class and,
  where a class covers two mistakes, message (`IllegalArgumentException` "Don't know how to create ISeq" or "bit
  operation not supported" is `:type`, any other is `:argument`); ours by message, since our runtime errors carry
  no `ex-type` (design §4 «Тип ошибки»: everything else is nil) and an `ex-type` that is not nil is its own family.
  So the same mistake in other words agrees and a different mistake diverges, and a throw no rule matches keeps
  its raw text and diverges from everything — how a new message asks to be classified (the first CI pass asked
  for two: our "Non-terminating decimal expansion" and "Infinite or NaN"). The table was built from the oracle's
  own throws over 16000 forms: 8 classes, every one mapped. An `:errors` exclusion puts its rule ahead of the
  table, which is how a deliberate difference in *which* error is cited: `:int-cast-range-error`, the JVM's
  `(int x)` of a long being the one narrowing cast that throws ArithmeticException (`Math.toIntExact`).
  Regression files written before families keep the bare `#fz/throw`, which still compares as a throw.

- **The exclusion list is `fuzz/exclusions.edn`**, and `fuzz/differential.clj audit` (part of every run) refuses
  an entry whose citation does not resolve: `:row` must be a verbatim slice of a row of
  `docs/jvm-differences.md`, `:design` with `:cite` a slice of a design file. The ids are read by the code — an
  entry dropped from the file turns its exclusion off — so the list is the mechanism and not a comment.
  `:uncovered` beside it is ground the generator does not reach, where nothing is hidden and no row is cited.

- **Self-test.** Dropping `:map-seq-order` from a copy of the list and running one seed finds the row again
  within 200 forms and shrinks it to `(update-in {:b :x} (vector :a) identity)`. A fuzzer that finds nothing and
  a broken one read the same, so this is the check that the list excludes rather than blinds.

- **The generator is typed**, and its types are what keeps the exclusions enforceable: a map or a set never
  reaches an order-exposing position (the bridge is `(sort (map pr-str (keys m)))`), a ratio lives in its own
  chain so it never meets a double, a sorted collection lives in its own chain over integer keys so it never
  meets a hash map under `=`, and `str` only ever sees a scalar, a vector or a seq of scalars. Every generated
  function is wrapped in a `try` answering nil, because whether an element function throws before its value is
  asked for is the JVM's chunked-seq decision.

- **Shrinking is type-directed, so every candidate is well-formed.** The generator records, for each
  subexpression, its path, its type and the locals in scope; a candidate replaces a node with the minimal
  literal of its type, with one of its own descendants of a fitting type and the same scope, or drops one
  element of a collection literal. A round evaluates every candidate as one case file and takes the first that
  still diverges, so a round costs one process per runner and not one per candidate.

- **Every outcome carries `jvm-hash` of its value, and the oracle's carries `hash` of the same value.**
  `fz-out` appends ` #fz/hash <n>`, `fz-hash` being `clojure.core/jvm-hash` where it resolves and `hash` on the
  JVM, which has no such var; so a generated value whose `jvm-hash` drifts from the JVM's diverges like a wrong
  answer, and one `jvm-hash` refuses shows as a throw against the oracle's number. Our own `hash`,
  `hash-ordered-coll` and the rest are still not generated (`:no-hash`): §10 decided `hash` is ours.

- [~] **What the pass covers.** Numbers of all ranks, strings over ASCII, vectors, lists, lazy seqs, hash and
  sorted maps and sets, ratios, `let`, `loop`/`recur`, `fn`, destructuring, `if`/`cond`/`try`, `->>`, `apply`
  (also over `(range n)` with n around `CLJ_FN_MAX_FIXED`, into core fns and a multi-arity variadic `fn`),
  the seq, string and set function families, and four areas taken from `:uncovered` (2026-10-08): an atom or a
  volatile made, changed and read inside one expression (`swap!` with one and two arguments, `swap-vals!`,
  `reset-vals!`, `compare-and-set!` against its own deref, `vswap!`, `vreset!`); metadata on vectors, maps and
  sets read back with `meta` after `conj`, `assoc`, `pop`, `subvec`, `into`, `empty`, `vec`, `update`, `mapv`,
  `merge`, `select-keys`, `disj`, `vary-meta`, `seq` and `rest`; fixed comparators in `sort`, `sort-by` (a
  `mod 3` key, which makes ties, so stability is compared) and `sorted-map-by`/`sorted-set-by` through their
  operations; and decimals in their own chain with + - * /, min, max, comparison, `bigdec` of an integer and
  `double` of a decimal, scale included. The first passes over them found no value divergence. Not covered,
  each with its reason, in `:uncovered`: records and protocols, schedules, vars and namespaces, host interop,
  regexes, `format`, metadata on seqs and sorted collections, transients, decimals meeting doubles or ratios,
  and non-ASCII text. The trigger for widening is the next pass finding nothing over many seeds.

- [~] **-0.0 through arithmetic is still a noise source.** `min` and `max` tie on signed zeros and the JVM
  answers two ways (its row in `docs/jvm-differences.md`), so -0.0 left the literal pool; it is still reachable
  as `(* -1.5 0.0)`. Closing it needs either a static sign-of-zero fact or the JVM settling on one answer.

- **Two values carry most of the deliberate rows: `Long/MIN_VALUE` and a lazy seq whose realization throws.**
  Neither can be excluded by a type, so each is excluded by dropping the operations that *make* one:
  `bit-shift-left`, `bit-flip` and `bit-set`, the only ones that set bit 63 without the overflow check `+`, `-`
  and `*` carry, and the two-argument `reductions`, whose `(f)` with no arguments is the one lazy seq that
  throws on realization. Three rows of `docs/jvm-differences.md` stand behind the first and two behind the
  second, and most of them are the JVM answering one expression two ways, by whether its compiler saw a
  primitive long.

- **The compiled backend is a gate runner, as one unit per case** (`fuzz/compiled.sh`, `clj-fuzz --units`):
  `clj-compile --file` writes the whole case file as one C unit — compiling evaluates it, so that transcript is
  the interpreter's and is dropped — and `clj-fuzz` builds it with one clang run, registers it and loads the
  file, which runs the compiled unit. A 1000-form case is a 5.9 MB C file, 4 s on an Intel Mac end to end. So
  `make fuzz` has three runners (oracle, `interp`, `unit`), and the compiled backend is compared with the oracle
  and with the interpreter on every gate run. `CLJ_EVAL=compiled` is the REPL path, a clang run per top-level
  form (~1 form/s, `--group` not helping: the cost is per form), and stays in `make fuzz-long`.

- **Measured** (arm64 CI). With the oracle and the interpreter alone the pass was 8000 forms over eight seeds in
  9 s, 13 s end to end, the oracle the whole cost. With the unit runner beside them and the seeds one after
  another it took 41.3 s (run 37829828629). The seeds are independent processes, so the driver runs them side
  by side (`pmap`), which took the oracle-only pass on an Intel Mac from 20.5 to 11.2 s but the three-runner pass
  on the 3-core arm64 runner only to 39.5 s: clang and the JVM already fill the cores. The gate is 50 s against
  27 s on main (run 37834676178 against 37780771278), 23 s more for the compiled backend on every gate run; the
  hand pass of 80000 forms over eighty seeds took 88 s before the unit runner joined `make fuzz-long`.

## Parser fuzzing (fuzz/parsers/, design §3 item 5)

- **Parser fuzzing.** Four libFuzzer harnesses over the C parsers: `reader` (raw bytes as a host hands source,
  every form to EOF), `regex` (pattern, NUL, subject: compile, find, matches, a matcher scan, replace, split),
  `number` (one text through the reader, `parse-long`, `parse-double`, `bigint`, `bigdec` and
  `clj_bigint_parse` in four radixes) and `format` (format string, NUL, one byte per argument from a fixed
  palette). `make fuzz-parsers` builds CljCore with coverage under ASan and UBSan (`-fno-sanitize-recover`,
  `-DCLJ_DEBUG=1`) and runs each in fork mode for its time (`FUZZ_PARSERS_TIME`), so one finding does not end
  the run; it then reproduces every finding, groups them by message, minimizes three groups per target and prints
  the minimal inputs. Opt-in, like `test-tsan`: in neither `gates` nor `gates-full`. On CI the grown corpus is a
  cache and the findings an artifact (`fuzz-parsers-<arch>`). Every phase runs under `timeout`: the first run
  sat three hours in minimizing OOM inputs, each attempt of which filled the RSS limit slowly.

- **The toolchain.** Xcode's clang takes `-fsanitize=fuzzer-no-link` but ships no `libclang_rt.fuzzer_osx.a`,
  so `fuzz/parsers/libfuzzer.sh` builds libFuzzer from the compiler-rt 21.1.8 release tarball (sha256 checked)
  with the same clang. A Homebrew LLVM would have been a second compiler for CljCore under `-Werror` and a
  second sanitizer runtime; an in-tree driver would redo fork mode, dictionaries and minimization.

- **Leaks are a live-object count.** Apple's ASan refuses `detect_leaks`, so `fz_run` runs each input twice and
  fails when the second run ends with more live objects than it began with (`clj_debug_live_objects` after
  `clj_cc_collect`); the first run interns the input's keywords, which are permanent. A finding names the types
  that grew.

- **Oracles beside the sanitizers.** A form the reader made prints as text that reads back `=` (NaN aside), and
  from the second read on its print is a fixed point: the first read's metadata turns a shape map into a trie
  (NOTES "Shapes") and so changes its order, which printing does not carry. A number prints and reads back as the
  same kind and value; the reader and `parse-double`/`parse-long` agree on the grammar they share. A regex
  operation ends within its 200 ms deadline plus 3 s; a compiled pattern prints and reads back with its groups,
  unless its text holds a bare `"` or ends in a backslash inside `\Q`, which `RT.print`, verbatim as ours,
  prints unreadably too; a syntax error carries ex-data.

- **Inputs refused** (the harness returns -1): more than five backticks, since each nested syntax-quote
  multiplies the expansion on the JVM too (the number harness's first OOMs were ten of them); invalid UTF-8
  where the input becomes a string, which no host hands the runtime; a `format` width or precision from 100000
  to `INT_MAX`, which is padding asked for. One past `INT_MAX` is still taken.

- **Found by the first run** (37933639934, arm64): `(re-pattern "\\")`, a lone trailing backslash, compiled and
  printed as the unreadable `#"\"`; it is now "Unexpected internal error near index 1", as on the JVM.
  `#:4{t 1}` read as a map keyed by the symbol `4/t`, which reads back as no symbol at all; a namespaced-map
  prefix must now be symbol text, as LispReader's is (`#:nil{}` too). Both have their test and seed
  (`fuzz/parsers/seeds/`).

- **Found by the second run** (37958337609): `format` parsed a width, a precision and an argument index into an
  `int` with no bound, so `%9999999999d` was signed overflow (UBSan, 1243 hits). A count past `INT_MAX` is now
  "width out of range" or "precision out of range", as `Integer.parseInt` refuses it in Formatter, and an index
  past it names no argument. The rest of that run was the harnesses' own: a NUL byte cut the NaN check of the
  reader's `=`, and a pattern ending in a backslash inside `\Q` prints unreadably on the JVM as well.

- **Measured** (arm64, 3 jobs; the second run, 20 min each for the reader and regex, 10 for number and format):
  the reader at ~360 exec/s reached 3454 edges (3323 on replaying its 1065-unit corpus); regex ~30 exec/s, its
  200 ms deadline and backtracking patterns being most of the time, 1899 edges; number ~6900 exec/s, 3432 edges,
  no finding; format 956 edges (its exec/s read 0 because every job ended on the overflow). Each input runs
  twice for the leak check, so these are half the parser's own rate.
