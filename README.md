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
make build      # swift build
make test       # ASan run with the system allocator
make test-all   # every sanitizer and allocator mode
make bench
```

Requires Swift 6 and macOS 12 / iOS 15.
