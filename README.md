# Pippin

Pippin is Clojure for Apple platforms: a runtime with its own C core and a Swift host bridge,
not a transpiler to Swift or a port of the JVM implementation. The language is JVM Clojure's;
every known difference is listed in [docs/jvm-differences.md](docs/jvm-differences.md).

- `Sources/CljCore` — the portable runtime: reader, analyzer, evaluator, persistent collections,
  allocator, `clojure.core` itself (`boot/`).
- `Sources/Pippin` — the Swift module a host app imports: `Value`, `Runtime`, host errors.
- `corpus/` — the acceptance corpus (the Clojure test suite and medley), reported in
  [docs/corpus.md](docs/corpus.md); `clojure.core` coverage is in [docs/api-parity.md](docs/api-parity.md).

Design decisions live in [docs/design.md](docs/design.md) (Russian), known simplifications and the
mechanism behind each in [NOTES.md](NOTES.md).

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

`Sources/CljCompiler` is the C generator over the analyzer's trees and `clj-compile` its tool; `make boot`
regenerates the embedded core and its compiled form (NOTES.md, "Compiler").

Before pushing, agents run `make gates`: `test`, `test-compiled`, `corpus-compiled`, `facts-report`,
`port-audit`, `api-diff`, in that order. Run `make gates-full` weekly and after allocator, boot,
compiler, or suite-lifetime changes. See [NOTES.md, Gates](NOTES.md#gates) for coverage and measurements.
`api-diff` needs JVM Clojure on PATH (`/opt/homebrew/bin` with Homebrew); tests need GNU `timeout`.

Requires Swift 6 and macOS 12 / iOS 15.
