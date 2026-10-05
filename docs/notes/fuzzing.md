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
  (`:no-bigint-marker`), and sequentials print alike. Which error was thrown is not compared: a JVM class and
  an `ex-type` are not one alphabet, and a mapping between them is work for the day a wrong-error bug is
  suspected. A runner that dies takes its transcript's tail with it, and the missing indices are the finding.

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

- **`hash` is out of the comparison**, §10 leaving open whether to match the JVM's values (`:no-hash`). Deciding
  it would cost the JVM's murmur3 over our own collection layouts and a decision about `hash` of a record,
  which `docs/jvm-differences.md` already answers differently; the fuzzer must not answer it by failing.

- [~] **What the pass covers.** Numbers of all ranks but decimals, strings over ASCII, vectors, lists, lazy
  seqs, hash and sorted maps and sets, ratios, `let`, `loop`/`recur`, `fn`, destructuring, `if`/`cond`/`try`,
  `->>`, `apply`, and the seq, string and set function families. Not covered, each with its reason, in
  `:uncovered` of `fuzz/exclusions.edn`: records and protocols, anything whose outcome depends on a schedule,
  vars and namespaces, host interop, regexes, `format`, metadata, transients, and non-ASCII text. The trigger
  for widening is the next pass finding nothing over many seeds.

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

- **Measured.** The gate is 8000 forms over eight seeds in 13 s end to end, the pass itself 9 s, 850 forms a
  second against the oracle; the oracle is the whole cost, our runner evaluating the same file in well under a
  second. Three runners (oracle, interpreter, no-reuse) make 700 forms a second, and the hand pass of 80000
  forms over eighty seeds takes 88 s.

- **The compiled backend is the slow runner**: `CLJ_EVAL=compiled` pays a clang run per top-level form, about
  one form a second, and `--group N` wrapping N expressions in one `do` does not help, the cost being per form
  and not per clang. That is why the gate is the interpreter against the oracle and `make fuzz-long` carries the
  compiled and the `-DCLJ_NO_REUSE` runners, the way `test-eval-compiled` is kept out of the gates.
