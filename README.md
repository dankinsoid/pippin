# Pippin

Pippin is Clojure for Apple platforms: a runtime with its own C core, an interpreter and a compiler to C,
and bridges to Objective-C, Swift and C. It is not a transpiler to Swift and not a port of the JVM
implementation. The language is JVM Clojure's; every known difference is listed in
[docs/jvm-differences.md](docs/jvm-differences.md).

Next to it, as research rather than a plan, is **Merry** — a static sibling language meant to share the
runtime (see [Merry](#merry) below).

## The idea

Swift is a poor host for a dynamic language when taken head-on: no `eval` and no JIT on iOS, no dynamic
calls into Swift-only API, atomic ARC on every value, a slow `swiftc`. Good Clojure hosts — the JVM, JS,
the CLR — load code at run time, collect garbage and reflect; Swift does none of these. So pippin does what
Clojure does on the JVM: it compiles *below* the host language, into its own runtime, and treats the platform
as an interop surface.

| | Clojure JVM | ClojureScript | ClojureDart | jank | **pippin** |
|---|---|---|---|---|---|
| Compiles to | bytecode | JS | Dart | LLVM IR | **C** |
| Runtime written in | Java | ClojureScript | Dart | C++ | **C core + Swift wrapper** |
| Interop with | Java | JS | Dart | C++ | **ObjC + Swift + C** |

The user sees only the last row. Clojure is the brain — state, logic, data, network; Swift is the hands —
rendering, hot paths, Metal.

## What makes it different

**It is Clojure, not a dialect.** The invariant: a valid Clojure program is valid here with the same result;
analysis changes only cost and diagnostics, annotations are metadata the runtime ignores, and an unknown fact
never stops compilation. Where JVM-ness forces a divergence, ClojureScript is the measure of how far one may
go: a name cljs kept is kept, a name cljs dropped needs a reason of our own. `make api-diff` compares the
public API against JVM Clojure and cljs and fails on any missing name without a recorded verdict
([docs/api-parity.md](docs/api-parity.md)). Clojure's own test suite and third-party `.cljc` libraries run
as the acceptance corpus ([docs/corpus.md](docs/corpus.md)).

**One semantics, two backends.** The reader, macroexpander, analyzer and optimizer are shared; the backend
is either a closure-compiling interpreter or C through clang.
- *Dev*: an app built with a compiled base, plus the interpreter for whatever arrives from the REPL. Calls go
  through vars, so a redefinition reaches compiled code too. This is the only way to run new code on an
  iPhone, where JIT is forbidden.
- *Prod*: a closed world compiled to C — direct linking, tree shaking, unboxed arithmetic, `#line` for traces.

The interpreter is the source of truth; the suite and the corpora run through both and are compared line
by line.

**Facts instead of types.** The analyzer runs abstract interpretation over a lattice of facts — kind,
nullability, shape, escape, effects — Julia-style: inferred from use, no annotations required. ⊤ ("don't
know") boxes and stays quiet, so the worst case is plain Clojure; only a proven contradiction (⊥) is an error,
reported where the program would fail at run time anyway. Function summaries carry facts across calls; over
the corpus's library code they raise the share of value nodes with a known type from 48.5 to 68.1 %
([docs/facts-coverage.md](docs/facts-coverage.md)).

**Explicit twins of implicit optimizations.** Every optimization the runtime does on its own — shapes,
in-place reuse, unboxing, fusion — has an explicit form that guarantees it or fails to compile: `defrecord`,
`^long`, transducers. Planned on top: cost assertions as metadata, not new syntax — `^:in-place`,
`^:fused`, `^:no-alloc`, `:strict` on a namespace — where a failed one names the cause and the twin that
fixes it ("copy: `v` is captured on line N; `transient` gives in-place"), so a refactoring that turns
in-place into a copy breaks the build instead of slowing it down silently.

**Memory without a tracing GC.** Values are tagged words over a size-class slab allocator. Reference counts
are non-atomic until a value crosses threads; Perceus-style last-use analysis turns `assoc` on a unique
map into a mutation. Cycles are collected by trial deletion, locally on the owning thread or concurrently in
the background, with no pauses on the main thread. Keyword maps take a struct-like *shape* layout the way V8
hidden classes do, and vectors carry element kinds.

**Colorless concurrency.** `go` is a stackful coroutine in both backends — no IOC transform, no `async`
rippling up the call stack. Any function may park: `(map #(<! (fetch %)) urls)` works, as do `try/finally`,
`binding` and stack traces across a park. Channels, `alts!`, timers, `future`, agents and refs (two-phase
locking by id) are built on the same scheduler; the main carrier is UIKit's own run loop.

**Interop in three levels, plus C.**
1. The Objective-C runtime — everything `@objc`, from the REPL, with no rebuild.
2. Swift-only API through stubs generated from the symbol graph at build time (`:require-swift`).
3. Data across the boundary — Clojure describes, Swift renders.

C sits under all three: `:require-c` reads declarations and constants from headers with clang and calls
any symbol dynamically. A call keeps the selector's labels in order, as keywords:

```clojure
(ns pippin.screen
  (:require-c [UIKit :refer [UIButtonTypeSystem UIControlEventTouchUpInside UIControlStateNormal]]))

(def taps (atom 0))

(let [button (.button-with-type (objc-class "UIButton") UIButtonTypeSystem)
      target (objc-reify {} (["tap:" "v@:@"] [self sender] (swap! taps inc)))]
  (.set-title button "tap me" :for-state UIControlStateNormal)
  ;; [button addTarget:target action:@selector(tap:) forControlEvents:...]
  (.add-target button target :action "tap:" :for-control-events UIControlEventTouchUpInside))
```

The whole screen is [scripts/ios-app/screen.clj](scripts/ios-app/screen.clj): sixty lines of Clojure, thirty
selectors, not one Swift symbol. From Swift, the runtime is one type and one value:

```swift
import Pippin

let rt = Runtime()
rt.define("greet", arity: 1...1) { args in Value("hello, \(args[0].string ?? "?")") }
let inc = try rt.eval("(fn [x] (inc x))")
let n = try await inc.callAsync(41)   // runs on a coroutine that may park
```

**Diagnostics are written for a reader who cannot ask back** — a human or a coding agent. The fix has to
follow from the text: the position of the subexpression as a range, the source excerpt, the arities, the
nearest name. A machine-readable mode, so that an agent reads structure rather than regexes over stderr,
is planned.

**Correctness is found by machines.** Generated inputs against oracles: a differential fuzzer against JVM
Clojure, random operation sequences over every collection against a Swift model, the corpora through both
backends, libFuzzer over the C parsers. An internal error is loud and names the frame; `CLJ_NO_OPT` turns
the optimizer off to tell a pippin bug from a program bug.

**Tooling through protocols, not an ecosystem.** An nREPL server lets CIDER, Calva and Conjure connect to a
process on the Mac, the simulator or a device as to any Clojure. A language server of our own over the
analyzer is planned to replace clojure-lsp.

## Status

Working: the C core and its collections, the interpreter, the compiler to C (closed world and dev mode),
`clojure.core` at API parity with JVM Clojure apart from JVM-only names, core.async, agents and refs, the
ObjC bridge, the C bridge, generated Swift stubs, nREPL, an iOS app bundle whose screen is Clojure. On iOS
the runtime is 1.1 MB interpreted and 3.2 MB with the compiled core, before tree shaking
([docs/notes/ios.md](docs/notes/ios.md)).

Not yet: interprocedural borrow inference and regions, the UI framework (a reconciler over hiccup, deferred
until the language is done), the language server, other platforms. Development is Apple-only until the last
step; every place that depends on the platform is a row in [docs/portability.md](docs/portability.md), and
`make port-audit` fails on one that is not. The full plan with a status per step is design §10; everything
open is collected in [docs/open.md](docs/open.md).

## Merry

Merry is a sketch of a static language of the Clojure family over the same runtime — Kotlin next to Java,
Gleam next to Elixir. The name: Merry is Pippin's cousin, the careful one who plans ahead; Pippin is curious
and acts first.

Two reasons to want it. Pippin's speed has a ceiling the facts cannot break: uniform tagged values,
megamorphic sites, lazy seqs, number promotion, vars. And a soft compiler guarantees nothing — one unknown
fact spreads silently down the call chain. In Merry the unknown is either a compile error or an explicit
`Dyn`.

The goals: Rust-level safety and speed with the cost visible in code (copy, allocation, boxing, dynamic
dispatch); compilation speed as a constraint on everything else; OCaml-style inference with optional
annotations; malli-like types as data; reflection, a REPL and hot reload where they are affordable.
Compatibility with Clojure is a non-goal. A call is described by a *contract* along independent axes — shape
(HM with row polymorphism), effects (Koka-style rows), modes (ownership and lifetime), cost, refinements,
meta — each with its own checker and its own price.

The roles: pippin for exploration, UI glue and the Clojure ecosystem; Merry for hot modules and libraries
with contracts. The allocator, RC with reuse, coroutines, channels, interop and the stub generator, the
reader, nREPL and the gates carry over almost as they are. Whether Merry is worth building depends on
a measurement not yet taken: pippin, Swift and hand-written "what Merry would emit" C on the same kernels.

Nothing here is decided beyond the goals. The ideas are in [docs/merry.md](docs/merry.md) (Russian), and
[static.clj](static.clj) is a checkout service written in the sketched syntax with the inferred contracts
in comments.

## Repository

- `Sources/CljCore` — the portable runtime: reader, analyzer, evaluator, persistent collections,
  allocator, scheduler, `clojure.core` itself (`boot/`).
- `Sources/Pippin` — the Swift module a host app imports: `Value`, `Runtime`, host errors, the async bridge,
  Swift stubs.
- `Sources/CljCompiler` and `clj-compile` — the C generator over the analyzer's trees; `make boot` regenerates
  the embedded core and its compiled form ([NOTES "Compiler"](docs/notes/compiler.md)).
- `Sources/CljNREPL` and `clj-nrepl` — the nREPL server: `swift run clj-nrepl`
  ([NOTES "nREPL"](docs/notes/nrepl.md)).
- `corpus/` — the acceptance corpus: Clojure's test suite, medley, math.combinatorics, core.async's tests.
- `bench/` — benchmarks and their history ([bench/RESULTS.md](bench/RESULTS.md)).

Documentation: [docs/design.md](docs/design.md) is the design of record (Russian), an index of one file per
section; [NOTES.md](NOTES.md) holds the engineering notes, one file per subsystem — known simplifications,
the mechanism behind each, and the event that makes it worth fixing.

## Building

Requires Swift 6, macOS 12 / iOS 15.

```sh
make gates           # required before pushing; ordered and timed
make gates-full      # gates plus suite isolation and compiled-core ASan
make build           # swift build
make test            # ASan run with the system allocator
make test-all        # every sanitizer and allocator mode
make test-compiled   # the suite on the compiled core.clj (-DCLJ_COMPILED_CORE)
make corpus-compiled # the corpora through compiled user code, compared with the interpreter line by line
make bench
```

`make gates` runs `test`, `test-compiled`, `corpus-compiled`, `facts-report`, `port-audit` and `api-diff`, in
that order. Run `make gates-full` weekly and after allocator, boot, compiler or suite-lifetime changes; see
[NOTES, Gates](docs/notes/gates.md) for coverage and measured times. `api-diff` needs JVM Clojure on `PATH`;
the tests need GNU `timeout`.
