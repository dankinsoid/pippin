# Engineering notes

Known simplifications in the runtime, each with the event that makes it worth fixing.
Delete an entry when it is done. Architecture-level decisions live in docs/design.md.

The notes live in [`docs/notes/`](docs/notes/), one file per subsystem; the section titles are the ones code
comments cite (`NOTES "Coroutines"`, `NOTES.md, "Facts"`). Read this index, open only the file you need;
`grep -nE '^- (\[.\] )?\*\*' docs/notes/<file>.md` lists a file's entry titles.

An entry that names open work carries a mark: `- [ ]` the entry is open as a whole (a simplification waiting
for its trigger), `- [~]` it describes what exists and names what is left (Not done, Trigger, Deferred). An
unmarked entry describes what exists. Open items of both documents are collected in [`docs/open.md`](docs/open.md),
written by `make open-items` from the marks; `make open-items-audit` fails when it is stale.

## Index

Memory and the object model
- [Allocator](docs/notes/allocator.md) — size-class slabs per thread: abandoned and empty slabs, the foreign free list.
- [Type descriptor](docs/notes/type-descriptor.md) — descriptors, protocol tables and their snapshots, core-interface slots, seq iteration, `reduce` slot, metadata placement.
- [Locks](docs/notes/locks.md) — `clj_lock`, the one mutex type of the core's internals.
- [RC](docs/notes/rc.md) — `-DCLJ_NO_REUSE`, `unlink`, the copy path, the shared-object invariant, live counts.
- [Guard](docs/notes/guard.md) — the SIGSEGV/SIGBUS handler, stack overflow recovery, fatal faults with a trace.

Coroutines and concurrency
- [Coroutines](docs/notes/coroutines.md) — stackful coroutines, stacks and their evacuation, TLS across a park, traces, bindings, cancellation, deadlines, suspension.
- [Scheduler](docs/notes/scheduler.md) — carriers, the wake protocol, park/resume, the main carrier, the blocking pool, timers, output.
- [Channels](docs/notes/channels.md) — the channel, transducers under a cmutex, `alts!`, `go`, the core.async library layer.
- [Futures and scopes](docs/notes/futures-and-scopes.md) — `future`/`promise` as promise-buffered channels, `go-scoped`.
- [Coroutine mutex](docs/notes/coroutine-mutex.md) — the parking-lot mutex, its holders, the lock audit.

Collections and values
- [Map](docs/notes/map.md), [Set](docs/notes/set.md), [Vector](docs/notes/vector.md), [List](docs/notes/list.md), [Queue](docs/notes/queue.md) — the persistent collections' simplifications.
- [Sorted](docs/notes/sorted.md) — the LLRB tree behind sorted maps and sets.
- [Records](docs/notes/records.md) — record types as named shapes, lookup, extmap, `record*`.
- [Shapes](docs/notes/shapes.md) — the shape map as the hash map's second layout, transitions, the keyword-lookup site cache.
- [Arrays](docs/notes/arrays.md) — inline elements, ten element kinds, `vector-of`.
- [Numeric tower](docs/notes/numeric-tower.md) — six kinds, JVM promotion, bigint/ratio/decimal, no float.
- [Symbol / keyword](docs/notes/symbol-keyword.md) — interning and its permanence.

Reading, analysis, evaluation
- [Reader](docs/notes/reader.md) — tagged literals, reader conditionals, syntax-quote, positions.
- [Regex](docs/notes/regex.md) — the in-house java.util.regex subset and its matcher.
- [Analyzer and evaluator](docs/notes/analyzer-and-evaluator.md) — nodes and exec state, macros, serialization, errors and `catch`, host types in `catch`, namespaces and vars, the call path, optimizer passes, intrinsics, specialization, last-use reuse, fusion.
- [Facts](docs/notes/facts.md) — the facts lattice, pass 1, summaries, pass 2, diagnostics, effects and `:park`, declarations, the caller join, domains.
- [Builtins](docs/notes/builtins.md) — the C builtins: `into`, atoms, volatiles, `range`, printing, error messages.
- [core.clj](docs/notes/core-clj.md) — how core.clj is embedded and loaded, its contents, semantics that differ from Clojure.
- [Printer](docs/notes/printer.md) — map printing, `clj_pr_str_max`, print limits, `format`.

Host bridges
- [Host bridge](docs/notes/host-bridge.md) — the Swift side (`Value`, host errors, host fns), with subsections "The async bridge", "Typed closure adapters", "Host-defined vars and primitives", "Swift stubs" (level 2: the generator, the box, `require-swift`).
- [ObjC bridge](docs/notes/objc-bridge.md) — `objc_msgSend` prototypes, AAPCS64 structs, selectors, ownership, calling in (`objc-reify`, `objc-block`).

Compiler and tools
- [Compiler](docs/notes/compiler.md) — `clj_node` → C: the load hook, calling convention, empty prologue, dev vs closed, promoted slots, unboxed arithmetic, worker/wrapper, deviations.
- [nREPL](docs/notes/nrepl.md) — sessions as frames, interrupt, streamed output, `stdin`, `complete`/`info`.

Process
- [Corpus](docs/notes/corpus.md) — the vendored libraries, the harness, the watchdog, `make api-diff`.
- [Benchmarks](docs/notes/benchmarks.md) — how to compare numbers, what is not yet measured.
- [Open decisions](docs/notes/open-decisions.md) — decisions not yet taken.
- [iOS](docs/notes/ios.md) — building and running the runtime for iOS, what iOS lacks, the binary-size baseline.
- [Gates](docs/notes/gates.md) — `make gates` and `gates-full`, build directories, measured gate times.
