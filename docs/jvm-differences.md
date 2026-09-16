# Differences from JVM Clojure

The language contract is JVM Clojure's, checked by the corpus (`docs/corpus.md`). Every known
difference is listed here in one of three classes, so that a deliberate choice is never mistaken for
an unfinished one. NOTES.md carries the mechanism behind each entry; this page carries the decision.
A closed difference is deleted, not kept, so the page is the open list. No **Fix** row is open.

- **Fix** — visible to portable core-only code; the gap is against the contract and closes when its
  trigger fires or sooner.
- **Deliberate** — an artifact of the Java type system or of Java semantics that Clojure inherited and
  nobody relies on on purpose. Not replicated, as ClojureScript does not replicate it.
- **Deferred** — a missing feature or a slower algorithm with the same result; waits for its trigger.

## Numbers

| Difference | Class | Decision |
|---|---|---|
| No `Float`: `(float x)` range-checks through `Float` but returns a double, so `(= (float 0.1) 0.1)` is true where the JVM says false | Deliberate | A separate 32-bit float value type serves nobody in Clojure code; `Float32` is a boundary concern (Metal, Accelerate, Core ML) and the bridge converts by the Swift parameter type. |
| `(long ##NaN)` throws; the JVM returns 0 | Deliberate | Java cast semantics; Swift traps on the same conversion. Failing loudly wins. |
| One bigint type where the JVM has `BigInt` and `BigInteger` | Deliberate | The second type exists only because of `java.math`. |
| `with-precision`, `*math-context*`, rounding modes; decimal `/` succeeds only when the quotient terminates | Deferred | Trigger: a library that uses them. Money code on mobile rarely needs a context. |
| Bigint division is shift-subtract, gcd is Euclid | Deferred | Same results; trigger: a profile with thousand-bit values. Fix is Knuth D and binary gcd. |
| An index or bound outside the 63-bit fixnum is refused: `(range 0 Long/MAX_VALUE)`, `(nth v Long/MAX_VALUE)`, `(subs s 0 Long/MAX_VALUE)` throw where the JVM answers or reports an index error | Deferred | `range`'s O(1) view and the index arguments take a fixnum; the same values threw as bigints before the boxed long existed. Trigger: a corpus test or a profile that needs a 64-bit index; then int64 bounds on `clj_range` and on the index paths. |

## Collections

| Difference | Class | Decision |
|---|---|---|
| `transient`/`persistent!`/`conj!`… are the persistent operations; no use-after-`persistent!` error | Deliberate | The in-place path on a unique value is the transient (design §6b); a transient-shaped code path works unchanged. |
| `seq` of a map, set or sorted collection is an eager list | Deferred | Trigger: `first` on a big map in a profile. |
| Sorted `dissoc` walks the tree twice | Deferred | LLRB deletion needs a present key; trigger: a delete-heavy profile. |
| `compare` returns −1/0/1 only and orders strings by code point | Deliberate | The JVM's char or length difference is an implementation leak, and UTF-16 unit order differs from code point order only between an astral char and U+E000–U+FFFF. |

## Arrays

| Difference | Class | Decision |
|---|---|---|
| An array prints its elements, `#array[:int 1 2 3]` | Deliberate | The JVM prints `#object["[I" 0x… "[I@…"]`: a class name and an address, neither of which a reader or a test can use. Neither form reads back, so nothing is lost. |
| `(type (int-array 1))` is `array`, not `[I`; kinds are keywords (`:int`, `:i32`), not `Integer/TYPE` | Deliberate | A class object is interop (design §5); one descriptor with an element kind is the representation, and the kind keyword is the only name it needs. |
| `char` elements are 4-byte Unicode scalars, not UTF-16 units | Deliberate | Same choice as the char value itself: there are no surrogates in this runtime. |
| `(vec array)` copies; the JVM aliases the array, so a later `aset` shows through the vector | Deliberate | Aliasing a mutable buffer from a persistent vector is a JVM leak; the corpus's own test calls it out for three other runtimes. |
| `aset-int` and its siblings are aliases of `aset`; the array's kind decides the cast | Deliberate | The typed variants exist on the JVM to pick a bytecode; here the kind is on the object. |
| `vector-of` returns an ordinary vector of cast elements, not unboxed storage | Deferred | The elements go through the kind's cast, so values and range errors match; only the memory does not. Trigger: a `vector-of` in a profile (NOTES.md, "Arrays"). |
| No multi-dimensional arrays: `make-array` takes one dimension, `aget`/`aset` one index | Deferred | Trigger: a library indexing `(aget m i j)`. |

## Multimethods and hierarchies

| Difference | Class | Decision |
|---|---|---|
| `isa?` knows tags only, no host superclass or protocol conformance | Deferred | Part of interop (design §4 "Мультиметоды"); trigger: an `instance?`-shaped dispatch in a corpus library. |
| `prefers` walks the multimethod's own hierarchy; Clojure's walks the global one regardless of `:hierarchy` | Deliberate | A JVM quirk, not a documented rule. |
| `case` is `cond` over `=`, no jump table | Deferred | Trigger: a `case` in a profile. |

## Namespaces, vars, errors

| Difference | Class | Decision |
|---|---|---|
| No hoisting: a forward reference in a file is an error | Deferred | Design §4 pre-pass; no corpus library needed it. |
| `def` is eager | Deferred | Design §4 lazy `def`; trigger: load-time cost of a namespace. |
| `catch` knows five class names (`:default`, `Throwable`, `Exception`, …) and no class hierarchy | Deliberate | There is no Java class hierarchy; `ex-info` and host errors are the two kinds. |
| Error messages are Clojure-like, not identical; type names are the runtime's | Deliberate | Tests that match on message text are the corpus's problem, not the runtime's. |
| `^:private` is a resolve-time rule only; `#'ns/x` and `resolve` still reach the var | Deliberate | Same as JVM Clojure in practice. |
| `letfn` leaves a reference cycle per call | Deferred | Closed by design §7 trial deletion. |

## Strings and the reader

| Difference | Class | Decision |
|---|---|---|
| No regex engine: regex literals are read errors, `clojure.string` takes literal strings | Deferred | Trigger fired by the corpus; the engine is its own task. |
| `upper-case`/`lower-case`/`capitalize` map ASCII letters only | Deferred | Trigger: non-ASCII case in a corpus library; then Unicode case tables. |
| No tagged literals (`#inst`, `#uuid`), no `#:ns{}` maps, no `#=` | Deferred | Trigger fired by the corpus for `#uuid`. |
| `thrown-with-msg?` takes a substring, not a regex | Deferred | Follows from the missing regex engine. |

## Concurrency

| Difference | Class | Decision |
|---|---|---|
| `swap!` inside its own `f` on the same atom traps; `deref` inside `f` returns the old value | Deliberate | A nested `swap!` on the same atom is a bug on the JVM too (it spins or double-applies); trapping is the loud version. |
| No `ref`/`dosync`, `agent`, `future`, `pmap`, `promise` | Deferred | Design §4: `agent` as a library over a serial executor, `ref` as two-phase locking; `future` waits for core.async's carriers. |
