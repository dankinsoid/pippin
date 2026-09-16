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
| `(abs Long/MIN_VALUE)` throws where `Math/abs` returns `Long/MIN_VALUE` | Deliberate | `abs` is `(if (neg? a) (- a) a)`, and `-` is the checked negation; the JVM's answer is the two's-complement wrap of a value it cannot represent. Failing loudly wins, as for `(long ##NaN)`. |

## Collections

| Difference | Class | Decision |
|---|---|---|
| `transient`/`persistent!`/`conj!`… are the persistent operations; no use-after-`persistent!` error | Deliberate | The in-place path on a unique value is the transient (design §6b); a transient-shaped code path works unchanged. `(instance? clojure.lang.IEditableCollection x)` still answers as on the JVM — a core bit on the hash map, the vector and the hash set — because libraries branch on it. |
| `seq` of a map, set or sorted collection is an eager list | Deferred | Trigger: `first` on a big map in a profile. |
| Sorted `dissoc` walks the tree twice | Deferred | LLRB deletion needs a present key; trigger: a delete-heavy profile. |
| `compare` returns −1/0/1 only and orders strings by code point | Deliberate | The JVM's char or length difference is an implementation leak, and UTF-16 unit order differs from code point order only between an astral char and U+E000–U+FFFF. |
| `(hash record)` is the map hash of its content, not xor'd with the type name | Deliberate | `=` already separates a record from a map and from another record type, so sharing a hash costs collisions and never an answer; one entry mix (map.c) serves both representations. |
| A `defrecord` body implements protocols only; a core interface in it is refused | Deferred | Every slot behind a core interface is the record's own, and a trampoline over it would break the map contract `record?` promises. Trigger: a library putting `IFn` or `IExceptionInfo` on a record. |
| No `Name/create`, no `->Name`/`map->Name` overload for a partial basis | Deferred | `map->Name` covers the map-shaped constructor; trigger: a library that calls `Name/create`. |

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
| `upper-case`/`lower-case`/`capitalize` and a pattern's `(?i)`, `\w`, `\p{L}` map ASCII letters only | Deferred | One missing table serves all of them: trigger is non-ASCII case or a non-ASCII class in a corpus library, and the fix is the Unicode case and category data, not a range table. |
| No tagged literals (`#inst`, `#uuid`), no `#:ns{}` maps, no `#=` | Deferred | Trigger fired by the corpus for `#uuid`. |
| A record prints `#ns.Name{…}` and does not read back | Deferred | The printed form matches the JVM's; reading one needs the reader to resolve a type name, which is the tagged-literal machinery. Trigger: EDN with record literals. |
| Two patterns with the same text are `=` and hash alike, where the JVM compares `Pattern` by identity | Deliberate | A pattern is a value written as a literal, so identity equality only ever surprises; ClojureScript's `RegExp` is no better. The corpus's own `eq` test calls the JVM behaviour out for three other runtimes. |
| A lookbehind body must be fixed-width: `(?<=a+)b` is a compile error | Deferred | Java walks a variable-length body backwards from every candidate length; the fixed width is one subtraction. Trigger: such a pattern in a corpus library. |
| `\p{…}` knows a dozen POSIX names and answers them over ASCII, so `\p{L}` refuses `é`; `\p{InGreek}`, `\p{Sc}` and the block and script names are compile errors | Deferred | Same missing Unicode data as the case functions above. |
| No `\G`, `\R`, `\X`, `\N{…}`, `\b{g}`, `\h`, `\v`, no `(?u)`/`(?U)`/`(?d)` flags and no `CANON_EQ` | Deferred | Nothing in the corpus uses them; each is a `switch` arm in regex.c's parser to fill. |
| A pattern that backtracks catastrophically is stopped by the host's deadline, not refused | Deferred | The cooperative deadline of eval.h is checked every 4096 backtracks (NOTES.md, "Regex"); a memo table would bound the work instead, at a table per match. Trigger: a host that cannot set a deadline. |

## Concurrency

| Difference | Class | Decision |
|---|---|---|
| `swap!` inside its own `f` on the same atom traps; `deref` inside `f` returns the old value | Deliberate | A nested `swap!` on the same atom is a bug on the JVM too (it spins or double-applies); trapping is the loud version. |
| No `ref`/`dosync`, `agent`, `future`, `pmap`, `promise` | Deferred | Design §4: `agent` as a library over a serial executor, `ref` as two-phase locking; `future` waits for core.async's carriers. |
