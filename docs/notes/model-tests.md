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
  entries sorted, every sequential spelled by its kind, so the text is also the code that rebuilds the value), `=`
  both ways and `hash` against that code spelled beside it, the meta, a `reduce` count and a lookup of every
  expected key or index. A handle the generator retains is checked again after the last step. Whether an
  operation meets its source at one reference is the program's liveness: the source's last read is handed over
  (NOTES "Analyzer and evaluator", last-use reuse), so a sequence with aliasing 0 is a chain of in-place updates,
  and one with aliasing 0.25 or 0.5 reads old handles again — as sources, elements, map keys, `merge` and `into`
  sources, self-insertions (`(conj h h)`) — and those updates must copy. A sequence stays under the 64 slots a
  frame marks.

- **What the generator covers.** Constructors: literals (constants, so shared), `vector` (a fresh tuple),
  `(vec (range n))` and `into []` at 5–8, 31–34, 63–65, 95–97, 1023–1025 and 1055–1057 (the tuple's 6→7, the
  tail's 32, the root's 1024, the height past 1056), `conj []`, `subvec`; shape maps by literal and `hash-map`,
  keyword `zipmap`s of 30–33 keys (across the shape's 32), integer tries of up to 40 keys, a map with meta, `into {}`
  with `nil` and string keys, flat maps (NOTES "Map": a `reduce` fn's `assoc!` into `(transient {})`, then
  `persistent!`, at 1, 3, 7–9, 15–17 and 31–33 keys across any threshold, integers, strings, keywords and the
  collision keys mixed or keywords alone, a `dissoc!` hole in some); hash sets of up to 33, sorted maps and sets of up to 40, lists, queues. The first
  one may be an inferred lazy `def`, and the first step a second `def` over that var. Operations: `conj` (one to
  three), `conj` of an entry, `assoc`, `dissoc`, `disj`, `pop`, `update` (`mt-wrap`, `constantly`, `conj`),
  `update-in`, `assoc-in`, `merge`, `into` six ways (the builtin, the fused `(map identity …)` driver, the
  transducer arity, `reduce conj`, `reduce` with a fn of its own, whose accumulator is a borrowed parameter,
  `transduce`), `subvec`, `with-meta`, `vary-meta`, `empty`, `vec` six ways (`vec`, fused, `into [] xform`, `mapv`,
  a `reduce` fn, `transduce`), `select-keys`, `get` and `first` of a nested collection, and `transient` with
  `conj!`/`assoc!`/`dissoc!`/`disj!`/`pop!` on vectors, hash maps and hash sets. Elements: integers, keywords,
  strings, `nil`, booleans, three atoms (the reach bits on their owner), lazy seqs (the lazy bit), literal vectors
  and other handles. Keys include `0`, `-1`, `nil`, `"k15599"` and `"k97211"`, whose `hash` is equal here, so the
  trie meets collision nodes. A step reaches its source one of 20 ways: directly, in an `if`, through a `let`
  alias, a fn parameter, a `let`-bound fn, a rest argument, vector and map destructuring, a `binding`, a `loop`
  once round, a `try`, a closure capture, `mt-pub` (published through an atom, then unique again), a `future`,
  `apply`, `reduce`, `swap!` on an atom, `vswap!` on a volatile, and under a `seq` or an unrealized
  `(map identity …)` taken before the update and checked after it.

- **Every backend.** Each test loads batches of 40 sequences, prelude included, as one file: `interpreted` as
  source, `compiled` and `closed` as one unit (`compileFixtureAsUnit`, a clang run per batch; `closed` refuses
  `eval`, which is why the expected values are spelled and never read back). `make test-compiled` runs all three
  over the compiled core, `make test-noreuse` with every in-place path off. The bounded pass is 8 interpreted
  batches (320 sequences) and one batch of each compiled kind: on the arm64 runner it adds 12.0 s to the ASan
  shard of `make test` (4.9 s interpreted, 3.7 s compiled, 3.3 s closed; run 37937702510), which runs beside the
  other shard; with 6 interpreted batches and no closed one `make test-compiled` took 2.2 s (run 37935456617).

- **`make model-long`** runs 400 interpreted batches and 12 of each compiled kind in each of the ASan,
  compiled-core and no-reuse builds: 48000 interpreted and 2880 compiled sequences. On the arm64 runner the tests
  took 298, 101 and 113 s, the job 15 min with its three builds (run 37937706539, clean).

- **A failure shrinks and replays.** The first failing sequence of a batch is shrunk greedily — dropping a step
  (its readers pointed at its source), a retained check, the defs, a wrapper, an argument of a multi-argument
  operation, a transient sub-step — re-running each candidate until none fails, 400 runs at most (30 for a
  compiled backend, each a clang run). The issue names the batch's seed and the variable that replays it, and
  prints the minimal sequence as the forms to load after the prelude. A crash ends the process before any issue:
  each batch first writes `model: <backend> batch <seed>` to stderr, and its source stays in `.build/model-tests/`.

- **Self-test.** Faults put into the runtime by hand are found within the first batches: vector.c's tuple path
  not clearing the hash cache and its trie path taking a shared 40-element vector in place (12 of 12 batches,
  shrunk to three steps: `[22]`, `conj!` through `transient`, `conj` — a stale hash), map.c's collision node
  always edited in place (two of 15 batches, the debug check "realloc of a non-unique object"), and sorted.c's
  tree node always edited in place (the first batch).

- **Found.** The compiler emitted a constant longer than 4095 bytes as a C string literal (NOTES "Compiler"), at
  the first compiled batch. No aliasing bug: besides the CI passes, 12000 interpreted sequences (debug, ASan,
  no-reuse) and 1000 dev and closed compiled ones ran clean on x86_64 while the generator was built.

- [~] **What the pass does not cover.** Records (shapes with their own in-place `assoc`), `array-map` and
  `vector-of`, transients of sorted collections (the JVM refuses them), nested collections more than one level
  deep as `update-in` paths, values a channel moves between coroutines, and the cycle collector running
  mid-sequence (nothing here makes a cycle). Trigger: an aliasing bug the pass missed, or a new in-place path in
  one of these.
