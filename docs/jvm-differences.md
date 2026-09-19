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
| No `#=` read-eval, no `tagged-literal`/`reader-conditional` values | Deferred | `#=` is a code-loading hole nobody wants on a phone; a tag without a reader is an error here as on the JVM unless `*default-data-reader-fn*` says otherwise, and a library that wants the unresolved tag kept as data binds its own fn. Trigger: a corpus library using `tagged-literal`. |
| `str` of a `#inst` is its `#inst` text without the tag; `Date.toString` is `Thu Jan 01 … UTC 1970` in the host zone | Deliberate | The JVM's text is locale- and zone-dependent and reads back as nothing; the printed form is what a log or a test wants. |
| An `#inst` before 1582 is proleptic Gregorian; `GregorianCalendar` switches to the Julian calendar there | Deliberate | Thirty lines of day arithmetic against the whole of java.util.Calendar; no date a mobile app handles falls before the cutover. |
| A PersistentQueue prints `#queue [1 2 3]` | Deliberate | Same reason as arrays: the JVM prints an address, and the items are what a test can read. Nothing reads the form back. |
| Two patterns with the same text are `=` and hash alike, where the JVM compares `Pattern` by identity | Deliberate | A pattern is a value written as a literal, so identity equality only ever surprises; ClojureScript's `RegExp` is no better. The corpus's own `eq` test calls the JVM behaviour out for three other runtimes. |
| A lookbehind body must be fixed-width: `(?<=a+)b` is a compile error | Deferred | Java walks a variable-length body backwards from every candidate length; the fixed width is one subtraction. Trigger: such a pattern in a corpus library. |
| `\p{…}` knows a dozen POSIX names and answers them over ASCII, so `\p{L}` refuses `é`; `\p{InGreek}`, `\p{Sc}` and the block and script names are compile errors | Deferred | Same missing Unicode data as the case functions above. |
| No `\G`, `\R`, `\X`, `\N{…}`, `\b{g}`, `\h`, `\v`, no `(?u)`/`(?U)`/`(?d)` flags and no `CANON_EQ` | Deferred | Nothing in the corpus uses them; each is a `switch` arm in regex.c's parser to fill. |
| A pattern that backtracks catastrophically is stopped by the host's deadline, not refused | Deferred | The cooperative deadline of eval.h is checked every 4096 backtracks (NOTES.md, "Regex"); a memo table would bound the work instead, at a table per match. Trigger: a host that cannot set a deadline. |

## Printing and formatting

| Difference | Class | Decision |
|---|---|---|
| `str` of a collection ignores `*print-length*` and `*print-level*`; on the JVM `toString` runs through `RT.printString` and honours them | Deliberate | `str` builds keys, messages and output that must not change under a debugging binding; only the `pr` and `print` families read the vars. |
| `format` knows `%s %b %c %d %o %x %e %f %g %n %%` with the `- + space 0 ,` flags; `%h`, `%t`, `%a`, the `#` and `(` flags and `%x` of a bigint are errors naming the spec | Deferred | The subset libraries use; each missing conversion is a `switch` arm in builtins_format.c. Trigger: a corpus library formatting a date or a hash. |

## Concurrency

| Difference | Class | Decision |
|---|---|---|
| `swap!` inside its own `f` on the same atom traps; `deref` inside `f` returns the old value | Deliberate | A nested `swap!` on the same atom is a bug on the JVM too (it spins or double-applies); trapping is the loud version. |
| No `ref`/`dosync`, `agent`, `future`, `pmap`, `promise` | Deferred | Design §4: `agent` as a library over a serial executor, `ref` as two-phase locking; `future` waits for core.async's carriers. |

## core.async

| Difference | Class | Decision |
|---|---|---|
| `<!!`, `>!!`, `alts!!`, `alt!!` are the same functions as `<!`, `>!`, `alts!`, `alt!`: a wait from a bare thread blocks that thread, from a coroutine parks it | Deliberate | Design §4: no thread-blocking variant exists (a semaphore in the pool is a priority inversion), and since any function may park the two semantics are one. The symbols stay defined so foreign code loads. |
| `<!`, `>!`, `alts!` are legal in any function, not only inside a `go` body; `(map #(<! (fetch %)) urls)` works | Deliberate | Colorless coroutines (design §4, "Бесцветность"): the JVM's restriction is an artifact of its IOC transform. |
| A park inside a synchronous host call (`Value.apply`) is an error with a trace to the wait, and `Runtime.eval` from a bare thread blocks that thread | Deliberate | Design §5: the host waits for a value now. Trigger for making the bare-thread case an error on the main thread: the async bridge, which gives the host `callAsync`. |
| `cancel!` exists: it cancels the `go` behind a channel at its next park or loop tick, and a cancelled coroutine's park points keep throwing | Deliberate | core.async has no cancellation (design §4, "Отмена"); the JVM idiom of a control channel in `alts!` still works. |
| An uncaught error in a `go` body is reported through `clj_coro_set_uncaught_handler` (stderr by default) and the channel closes; the JVM prints the thread's uncaught-exception report | Deliberate | Same observable shape; the host owns the report. |
| `(chan n xform)` and `(chan n xform ex-handler)` throw "not supported yet" | Deferred | The transducer step would run user code under the channel's `clj_lock` (NOTES "Channels"). Trigger: a library using them. |
| The library layer is missing: `pipe`, `mult`, `tap`, `pub`, `sub`, `mix`, `merge`, `pipeline`, `pipeline-async`, `onto-chan`, `to-chan`, `promise-chan`, `reduce`, `into`, `take`, `map`, `split`, `unique`, `unblocking-buffer?` | Deferred | The next task of design §10 step 5; docs/api-parity.md lists what is built. |
| A `go` inside `with-out-str` prints to the real output; the JVM conveys `*out*` to the block | Deferred | Output captures are per execution and not conveyed (NOTES "Scheduler"). Trigger: a library capturing a go block's output. |
| `set!` of a conveyed dynamic binding from the spawned coroutine is allowed; the JVM throws "Can't set!: from non-binding thread" | Deliberate | The box is shared with the spawner (NOTES "Coroutines"); refusing it needs an owner per frame. Trigger: a library relying on the refusal. |
| The pending-put and pending-take limits are the JVM's 1024 with the JVM's messages | — | Same. |
