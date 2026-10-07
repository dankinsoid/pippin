## Benchmarks (bench/)

- Numbers drift between sessions (thermal, background load). Compare only within one run; use
  `CLJ_SYSTEM_ALLOC=1` on the same binary as the control.
- A cost is held here, not in a gate: the gates run under ASan on shared runners. The context-switch row
  (`clj_bench_switch_ns`, 14 ns on the M3) is the switch's number; `CoroTests.switchCost` only bounds it
  loosely (gates.md, "Timing in tests").
- The "C iterator" number (3.8 ns/element) moves to 4.3 with identical machine code when the linker
  places `clj_seq_iter_next`/`clj_vector_nth` differently; `aligned(64)` on both brings it back.
  Compare that row across builds only with the alignment forced, or read it as ±0.5 ns.
- The same for the call rows of the compiled-core binary, at ±1.5 ns: a change to facts.c alone moved every
  protocol-call row 7.7 → 9.1 with byte-identical compiled-eval C (`CLJ_EVAL_KEEP=1` keeps it for the diff), and
  `-Xcc -falign-functions=64` on both sides took the gap to 0.3 (bench/RESULTS.md, "The self-recursive worker").
  A CljCore change that shows less than that on a call row it does not touch is layout until the aligned build says
  otherwise.
- **`make bench-ab` compares two revisions in one job** (`scripts/bench-ab.sh`): release `clj-bench` of `BASE`
  (default `main`) against the working tree, alternated for `ROUNDS` rounds over the `ONLY` selectors, both sides
  built with this tree's harness; a block fenced `bench-ab: head only { … }` is cut from the base's copy. On CI it
  is a `workflow_dispatch` with `target: bench-ab` and `make_args: BASE=<full sha>` (a short sha does not fetch;
  `arches: arm64` is the M1 runner, 3 cpus, virtualized): two
  separate runs land on two runners, and the runner-to-runner spread is larger than the effects worth measuring.
  Round 1 of the side that runs first reads warm-up on that runner; compare rounds 2 and on.
- [ ] Not yet measured: multi-threaded reads of a shared map, assoc from a shared base across threads,
  cross-thread free, cost of `clj_share` on a large graph, forcing one shared lazy seq from many
  threads (the CAS claim path).

