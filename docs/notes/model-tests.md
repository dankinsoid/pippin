# Model tests

Random operation sequences over the persistent collections against a reference model, with old versions kept
alive: design §3 «Корректность реализации» item 3. `Tests/PippinTests/ModelGen.swift` holds the generator, the model
and the shrinker, `Fixtures/model/prelude.clj` the checks, `ModelTests.swift` the passes; the bounded pass is part
of every suite run (`make test`, `test-compiled`, `test-noreuse`), `make model-long` the one run by hand.

- **The model is plain Swift over texts**, so a bug in the collections cannot reach it. A value is its printed
  scalar, an atom by identity, a lazy seq, or a collection of seven kinds (vector, list, queue, hash and sorted map
  and set) with its meta — a list's per cell, as `PersistentList` keeps it, so `pop` exposes the next cell's.
  Equality is a canonical key in which sequentials compare across kinds and so do hash and sorted maps and sets,
  as `=` does. A key or set element is restricted to values whose printed form two equal values share (scalars,
  atoms, vectors and hash maps and sets of those), since which of two equal keys a collection keeps is not the
  model's to know; sorted collections take integer keys.

- **One sequence is one `let` in a `fn`**: `h0` is a constructor, each `hN` an operation on an earlier handle,
  and every binding goes through `mt-chk`, which compares count, the canonical text (`mt-pr`: a hash collection's
  entries sorted, so the text is also the code that rebuilds the value), `=` both ways and `hash` against that
  code evaluated fresh, the meta, a `reduce` count and a lookup of every expected key or index. A handle the
  generator retains is checked again after the last step. Whether an operation meets its source at one reference
  is the program's liveness: the source's last read is handed over (NOTES "Analyzer and evaluator", last-use
  reuse), so a sequence with aliasing 0 is a chain of in-place updates and one with aliasing 0.25 or 0.5 reads old
  handles again — as sources, as elements, map keys, merge and `into` sources, self-insertion (`(conj h h)`) —
  and those updates must copy. The bindings stay under the 64 slots a frame marks.

- **What the generator covers.** Constructors: literals (constants, so shared), `vector` (a fresh tuple),
  `(vec (range n))` and `into []` at 5–8, 31–34, 63–65, 95–97 and 1023–1025, 1055–1057 (the tuple's 6→7, the tail's
  32, the root's 1024 and the height change past 1056), `conj []`, `subvec`; shape maps by literal and `hash-map`,
  keyword `zipmap`s of 30–33 keys (across the shape's 32), integer tries of up to 40 keys, a map with meta, `into {}`
  with `nil` and string keys; hash sets of up to 33, sorted maps and sets of up to 40, lists, queues; any of them
  through an inferred lazy `def`. Operations: `conj` (one to three), `conj` of an entry, `assoc`, `dissoc`, `disj`,
  `pop`, `update` (`mt-wrap`, `constantly`, `conj`), `update-in`, `assoc-in`, `merge`, `into` four ways (the
  builtin, the fused `(map identity …)` driver, the transducer arity, `reduce conj`), `subvec`, `with-meta`,
  `vary-meta`, `empty`, `vec` four ways (`vec`, fused, `into [] xform`, `mapv`), `select-keys`, `get` and `first`
  of a nested collection, and `transient` with `conj!`/`assoc!`/`dissoc!`/`disj!`/`pop!` on vectors, hash maps and
  hash sets. Elements: integers, keywords, strings, `nil`, booleans, three atoms (reach bits on the owner), lazy
  seqs (the lazy bit), literal vectors and other handles; keys include `0`, `-1`, `nil`, `"k15599"` and `"k97211"`,
  whose `hash` is equal here, so the trie meets collision nodes. Each step reaches its source one of 15 ways:
  directly, in an `if`, through a `let` alias, a fn parameter, a `loop` once round, a `try`, a closure capture,
  `mt-pub` (published through an atom, then unique again), a `future`, `apply`, `reduce`, `swap!` on an atom,
  `vswap!` on a volatile, and under a `seq` or an unrealized `(map identity …)` taken before the update and
  checked after it.

- **Both backends.** The `interpreted` test loads each batch (40 sequences, prelude included) as a file; the
  `compiled` test builds it as one unit (`compileFixtureAsUnit`, a clang run) and loads that. `make
  test-compiled` runs both over the compiled core and `make test-noreuse` with every in-place path off.

- **A failure shrinks and replays.** The first failing sequence of a batch is shrunk greedily — dropping a step
  (its readers pointed at its source), a retained check, the lazy def, a wrapper, an argument of a multi-argument
  operation, a transient sub-step — re-running each candidate until none fails, 400 runs at most (40 compiled,
  each a clang run). The issue names the batch's seed and the variable that replays it, and prints the minimal
  sequence as the forms to load after the prelude. A crash ends the process before any issue: each batch first
  writes `model: <backend> batch <seed>` to stderr, and its source stays in `.build/model-tests/`.

- **Self-test.** Two faults put into vector.c by hand — the tuple's in-place path not clearing the hash cache,
  and the trie's taking a shared 40-element vector in place — were found by 12 of the first 12 batches between
  them and shrunk to three steps (`[22]`, `conj!` through `transient`, `conj`: a stale hash).

- **Found.** The compiler emitted a constant longer than 4095 bytes as a C string literal (NOTES "Compiler"), at
  the first compiled batch. No aliasing bug in 2400 interpreted and 400 compiled sequences run while building it
  (x86_64, debug, reuse on).

- [~] **What the pass does not cover.** Records (shapes with their own in-place `assoc`), `array-map` and
  `vector-of`, transients of sorted collections (the JVM refuses them), `disj`/`dissoc` through `apply` with more
  than one key, nested collections more than one level deep as `update-in` paths, values a reduce driver or a
  channel moves between coroutines, and the cycle collector running mid-sequence (nothing here makes a cycle).
  Trigger: an aliasing bug the pass missed, or a new in-place path in one of these.
