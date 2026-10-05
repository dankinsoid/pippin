# Differences from JVM Clojure

The language contract is JVM Clojure's, checked by the corpus (`docs/corpus.md`). Every known
difference is listed here in one of three classes, so that a deliberate choice is never mistaken for
an unfinished one. NOTES.md carries the mechanism behind each entry; this page carries the decision.
A closed difference is deleted, not kept, so the page is the open list. No **Fix** row is open: every difference Clojure's own test suite found is closed (docs/notes/corpus.md).

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
| `(/ 1.0 0.0)` is `##Inf`; the JVM answers `##Inf` or throws "Divide by zero", by the overload its compiler picked — `(/ (first [1.0]) (first [0.0]))` and the one-argument `(/ 0.0)` throw where `(/ 1.0 0.0)` does not | Deliberate | `Numbers.divide(double,double)` divides and `Numbers.divide(Object,Object)` refuses a zero divisor first, so one expression has no single answer there; IEEE is what the primitive overload answers and what ClojureScript answers (design §3, «Предел расхождения — ClojureScript»). Integer division by zero throws here as there. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| `(+ nil)` and `(* nil)` throw; the JVM's one-argument `+` and `*` are `(cast Number x)`, which answers nil for nil | Deliberate | Failing loudly wins, as for `(long ##NaN)` above; `Class.cast(null)` answering null is a Java rule, and the JVM's `(- nil)` throws anyway. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| One bigint type where the JVM has `BigInt` and `BigInteger` | Deliberate | The second type exists only because of `java.math`. |
| `(/ Long/MIN_VALUE 1)` is `-9223372036854775808`, where the JVM answers a `BigInt` printing as `-9223372036854775808N` | Deliberate | `Numbers.divide(long,long)` reduces by a `gcd` whose `Math.abs` cannot represent `Long/MIN_VALUE`, so that one numerator takes the BigInteger path and the quotient keeps the bigint type however small it is. Same decision as the row above and as `numerator`'s: an integer that fits is the one integer type. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| `(double 1/63)` is the exactly rounded quotient `0.015873015873015872`; the JVM's `Ratio.doubleValue` goes through `BigDecimal` with `MathContext.DECIMAL64` and answers `0.01587301587301587` | Deliberate | Sixteen significant decimal digits before the conversion is a java.math artifact that costs a correctly rounded bit, and it reaches every mixed ratio-and-double operation (`(* 1/63 2.0)`, `max`). Reproducing it needs the decimal context of the `with-precision` row, which exists for nobody's benefit here. Pure ratio arithmetic, `=`, `compare` and printing all agree. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| `numerator` and `denominator` answer the canonical integer, where the JVM answers a `BigInteger` that the first arithmetic turns into a `BigInt`: `(+ 0 (numerator 13717421/4))` prints `13717421` here and `13717421N` there | Deliberate | A ratio's parts are integers of the one integer tower, the row above, and no representation matches both: the JVM prints a bare `BigInteger` without the `N` and a `BigInt` with it, so answering `13717421N` to `(numerator 13717421/4)` would break the direct case to fix the derived one. `=`, `hash` and every arithmetic agree. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| `with-precision`, `*math-context*`, rounding modes; decimal `/` succeeds only when the quotient terminates | Deferred | Trigger: a library that uses them. Money code on mobile rarely needs a context. |
| Bigint division is shift-subtract, gcd is Euclid | Deferred | Same results; trigger: a profile with thousand-bit values. Fix is Knuth D and binary gcd. |
| `(min -0.0 0.0)` is `0.0`; the JVM answers `-0.0` or `0.0`, by the overload its compiler picked | Deliberate | `min` and `max` are `Numbers.min(Object,Object)` to the letter: the NaN checks and then `lt`, which ties on signed zeros and answers the second argument. `Numbers.min(double,double)` is `Math.min`, which orders -0.0 below 0.0, so one expression has two answers there and neither is the contract. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| `(- x Long/MIN_VALUE)` answers the difference wherever it fits; the JVM answers it only when its compiler saw two primitive longs, its boxed `minus` being `add(x, (negate y))` and `negate` of `Long/MIN_VALUE` throwing | Deliberate | `(- (first [-998]) (first [Long/MIN_VALUE]))` throws there and `(- -998 Long/MIN_VALUE)` does not, so one expression has two answers again; a subtraction that is representable is the one worth having. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| `(abs Long/MIN_VALUE)` throws where `Math/abs` returns `Long/MIN_VALUE` | Deliberate | `abs` is `(if (neg? a) (- a) a)`, and `-` is the checked negation; the JVM's answer is the two's-complement wrap of a value it cannot represent. Failing loudly wins, as for `(long ##NaN)`. `(quot Long/MIN_VALUE -1)` is the same case, where the JVM's `x / y` wraps; `(/ Long/MIN_VALUE -1)` is not, the exact quotient being a bigint there and here. |
| Of the boxed classes only `Long`, `Integer`, `Short`, `Byte` and `Double` resolve a static, and only the names demand asked for: `Long/MAX_VALUE`, `Long/MIN_VALUE`, `Long/valueOf`, `Integer/MAX_VALUE`, `Integer/MIN_VALUE`, `Short/MAX_VALUE`, `Byte/MAX_VALUE`, `Double/MAX_VALUE`, `Double/NaN`, `Double/POSITIVE_INFINITY`, `Double/isNaN` | Deliberate | A class is not a value here (`class`, `cast`, design §8), so a static resolves only as a var in a namespace of the class's name — the shape `Thread/sleep` has. The set grows by demand, as the level-0 header constants do (NOTES "Corpus"): one name, one line, no `java.lang` surface guessed. `Long/valueOf` is the identity on an integer, there being one integer type, and refuses anything else rather than answering for `Long/parseLong`. |
| No `Float/MAX_VALUE`, `Float/MIN_VALUE`, `Float/NaN`, `Float/POSITIVE_INFINITY`, `Float/isNaN` | Deliberate | There is no `Float` (the row above), so `Float` is not a class here at all: `Float.` and `Float/isNaN` have nothing to be, and a lone constant as a double would resolve one half of `(Float/isNaN Float/NaN)` and refuse the other. Refusing the class whole is one rule instead of two. |

## Collections

| Difference | Class | Decision |
|---|---|---|
| `transient`/`persistent!`/`conj!`… are the persistent operations; no use-after-`persistent!` error | Deliberate | The in-place path on a unique value is the transient (design §6b); a transient-shaped code path works unchanged. `(instance? clojure.lang.IEditableCollection x)` still answers as on the JVM — a core bit on the hash map, the vector and the hash set — because libraries branch on it. |
| `seq` of a map, set or sorted collection is an eager list | Deferred | Trigger: `first` on a big map in a profile. |
| No chunked seqs: a lazy seq over a vector realizes one element at a time, so `(take 1 (map inc [1 :a]))` answers `(2)` where the JVM throws, having forced the whole 32-element chunk | Deferred | Chunking is a throughput feature, and for a total element function the result is the same: only the moment an error is raised moves, and it moves later, which is the safe direction. Trigger: per-element seq overhead in a profile, or a library leaning on chunk-level eagerness (`chunked-seq?` and `chunk-first` are among the names docs/api-parity.md keeps). Found by the differential fuzzer (docs/notes/fuzzing.md). |
| A map literal or small `hash-map` seqs in this runtime's own order, not the JVM's insertion order: `(seq {1 1, 2 2})` is `([2 2] [1 1])` | Deliberate | A hash map's seq order is unspecified in Clojure; the JVM's comes from `PersistentArrayMap` keeping the literal's order up to eight keys, which a shape map does not have (NOTES.md, "Shapes"). Code that depends on it is relying on a representation. |
| `(= {:a 1} (sorted-map 1 2))` is false; the JVM throws, its `=` looking every key of the first map up in the second through the sorted map's comparator — with the arguments swapped it answers false there too | Deliberate | `=` is a predicate and has to be total; an argument order that decides between false and a ClassCastException is a JVM leak. A lookup still throws here as there (`(get (sorted-map 1 2) :a)`, `assoc`, `disj`): that key is one the collection cannot hold. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| `(mapcat f x)` is lazy to the end: with an `x` that is not seqable it answers a seq here and throws on the JVM, whose `apply concat` realizes the outer `(map f x)` to build an arglist | Deliberate | `apply` does not realize a variadic tail here, which is what makes `(apply f (range))` return instead of never (NOTES.md, "Analyzer and evaluator"), so `mapcat` has nothing left to force at the call. `map`, `filter` and `concat` raise at the same moment as the JVM. Only the moment an error is raised moves, and it moves later. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| `=` and `hash` of a lazy seq realize it only as far as the answer needs: `(= (reductions f []) nil)` with a two-argument `f` answers false here and throws on the JVM, whose `equiv` realizes the seq before it reaches the nil | Deliberate | No seq is ever `=` to nil, so the answer needs no element of it; the realization that raises is `pcequiv` taking the collection path first. Same family as the `mapcat` and chunked-seq rows: only the moment an error is raised moves. A lazy seq still hashes and compares as its realized form — `(contains? #{(map inc [1 2])} '(2 3))` is true here as there. Found by the differential fuzzer (docs/notes/fuzzing.md). |
| Sorted `dissoc` walks the tree twice | Deferred | LLRB deletion needs a present key; trigger: a delete-heavy profile. |
| `(sort [1.0 ##NaN 0.5])` is `(0.5 1.0 ##NaN)`, the JVM's `(1.0 ##NaN 0.5)` | — | `compare` answers 0 for a NaN against anything, so the comparator is not a total order and neither order is specified: each answer is its own sort algorithm's, a stable merge here and TimSort there. |
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
| `def` is eager | Deferred | Design §4 lazy `def`; trigger: load-time cost of a namespace. |
| `catch` takes `:default`, `Throwable`, `Exception`, `Object`, `ExceptionInfo`, a keyword, a type of ours, a host type, and any unresolved, package-less name ending in `Exception` or `Error`; the last takes every thrown value, and there is no class hierarchy | Deliberate | There is no Java class hierarchy; `ex-info` and host errors are the two kinds. Java's naming convention is what tells a JVM throwable class from a typo, and a type of that name still wins, being resolved first. Without it one `(is (thrown? IllegalArgumentException …))` clause refused the whole deftest around it: 24 of Clojure's own deftests, 20 of which pass (docs/notes/corpus.md). |
| Error messages are Clojure-like, not identical; type names are the runtime's | Deliberate | Tests that match on message text are the corpus's problem, not the runtime's. |
| `^:private` is a resolve-time rule only; `#'ns/x` and `resolve` still reach the var | Deliberate | Same as JVM Clojure in practice. |
| `letfn` leaves a reference cycle per call | Deferred | Closed by design §7 trial deletion. |
| A quoted list's reader position (`:line`, `:column`, `:file`) is nil in the compiled backend and set in the interpreted one; any other metadata of a quoted form is kept by both | Deliberate | Wrapping every quoted list in a `with-meta` over a position map would cost the constant pool a map per list for information a compiled unit has no use for (NOTES.md, "Compiler": pools). |

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
| `str` of a lazy seq prints its items: `(str (map inc [1 2]))` is `"(2 3)"` where the JVM answers `"clojure.lang.LazySeq@402"` | Deliberate | `clojure.lang.LazySeq` is the one seq class with no `toString` of its own, so it falls back to `Object`'s class-and-hash text; every other seq, `pr-str`, `print` and a lazy seq nested inside a printed collection answer with the items on the JVM too. The items are what a log or a message wants, and ClojureScript prints them (`IPrintWithWriter` on its `LazySeq`). Found by the differential fuzzer (docs/notes/fuzzing.md). |
| `str` of a collection ignores `*print-length*` and `*print-level*`; on the JVM `toString` runs through `RT.printString` and honours them | Deliberate | `str` builds keys, messages and output that must not change under a debugging binding; only the `pr` and `print` families read the vars. |
| `format` knows `%s %b %c %d %o %x %e %f %g %n %%` with the `- + space 0 ,` flags; `%h`, `%t`, `%a`, the `#` and `(` flags and `%x` of a bigint are errors naming the spec | Deferred | The subset libraries use; each missing conversion is a `switch` arm in builtins_format.c. Trigger: a corpus library formatting a date or a hash. |

## Concurrency

| Difference | Class | Decision |
|---|---|---|
| `swap!` inside its own `f` on the same atom traps; `deref` inside `f` returns the old value | Deliberate | A nested `swap!` on the same atom is a bug on the JVM too (it spins or double-applies); trapping is the loud version. |
| No `ref`/`dosync`, `agent` | Deferred | Design §4: `agent` as a library over a serial executor, `ref` as two-phase locking. |
| `future`, `promise` are promise-buffered channels of the coroutine runtime: `@f` parks the coroutine (blocks only a bare thread), both are `alts!` ports, `future-cancel` is `cancel!` and a cancelled future is done at once; a future's exception is rethrown as itself, not wrapped in an `ExecutionException` | Deliberate | Design §4, "`future`/`promise` на корутинах" (NOTES.md, "Futures and scopes"). The JVM wraps because its future is a `FutureTask`; the cause is what code catches. |
| `pmap` keeps `(+ 2 available-processors)` futures ahead, where the count is the carrier pool's size | — | The JVM's shape with its thread count read from the pool. |
| `Thread/sleep` parks the coroutine on the timer thread; it is a var `sleep` in a namespace `Thread`, so only that static call resolves | Deliberate | Library code sleeps (NOTES.md, "Channels"); a carrier never blocks. Beside the boxed-number statics of the Numbers row, every other `Class/method` call stays unresolved. |

## core.async

| Difference | Class | Decision |
|---|---|---|
| `<!!`, `>!!`, `alts!!`, `alt!!` are the same functions as `<!`, `>!`, `alts!`, `alt!`: a wait from a bare thread blocks that thread, from a coroutine parks it | Deliberate | Design §4: no thread-blocking variant exists (a semaphore in the pool is a priority inversion), and since any function may park the two semantics are one. The symbols stay defined so foreign code loads. |
| `<!`, `>!`, `alts!` are legal in any function, not only inside a `go` body; `(map #(<! (fetch %)) urls)` works | Deliberate | Colorless coroutines (design §4, "Бесцветность"): the JVM's restriction is an artifact of its IOC transform. |
| A park inside a synchronous host call (`Value.apply`) is an error with a trace to the wait, and `Runtime.eval` from a bare thread blocks that thread | Deliberate | Design §5: the host waits for a value now. Trigger for making the bare-thread case an error on the main thread: the async bridge, which gives the host `callAsync`. |
| `cancel!` exists: it cancels the `go`, `future` or `thread` body behind a channel at its next park or loop tick, and a cancelled coroutine's park points keep throwing | Deliberate | core.async has no cancellation (design §4, "Отмена"); the JVM idiom of a control channel in `alts!` still works. |
| `go-scoped` and `plet` exist: every `go` in the dynamic extent (through called functions and spawned coroutines) is a child joined on exit, a child's error cancels the siblings and the body and is rethrown, cancelling the scope's coroutine cancels the children; `plet` is `async let` over it | Deliberate | Design §4, "Контекст go": structured spawn over the unstructured `go`. Not in core.async (NOTES.md, "Futures and scopes"). |
| An uncaught error in a `go` body is reported through `clj_coro_set_uncaught_handler` (stderr by default) and the channel closes; the JVM prints the thread's uncaught-exception report | Deliberate | Same observable shape; the host owns the report. |
| A channel's transducer step runs under a coroutine mutex and may park; a step touching its own channel is an error ("… from inside its own transducer step") where the JVM deadlocks on its lock | Deliberate | Design §4, "Два лока": user code never runs under a `clj_lock` (NOTES.md, "Channels"). |
| `pipeline`'s default ex-handler is the uncaught report rather than the thread's uncaught-exception handler; `map>`, `filter>`, `remove>`, `mapcat>` return a front channel piped into the target instead of a write-port wrapper of it | Deliberate | Same observable shape; the JVM's `WritePort` reify has no counterpart (NOTES.md, "Channels"). |
| A `go`, `thread` or `future` inside `with-out-str` writes into the capture, as the JVM's conveyed `*out*` does; the string is what was written when `with-out-str` returns | — | Same (NOTES.md, "Scheduler"). |
| `set!` of a conveyed dynamic binding from the spawned coroutine throws "Can't set!: ns/name from non-binding thread" | — | Same as the JVM: a binding frame knows the execution that pushed it (NOTES.md, "Coroutines"). |
| The pending-put and pending-take limits are the JVM's 1024 with the JVM's messages | — | Same. |
| A spawned coroutine's first step may run before the spawner's next form: `(mult (to-chan! [1 2 3]))` can drain its source before three `tap` calls register, and `mult` drops what arrives with no taps | Deliberate | Design §8, the spawn-locality row: the JVM promises no more. Measured on core.async 1.6.681, a `go` block saw its spawner's counter at 1, 70, 76, 106 and 1000 of 1000 increments (ours at 0–15), so the ordering the ASYNC-127 block of `async_test.clj` relies on is the dispatch queue's handoff latency, tens of microseconds, against our ~1 µs wake. Starting a spawn only at the spawner's next park would starve a spawner that never parks (NOTES.md, "Scheduler"). |
| `Thread/currentThread` does not resolve, so `put!`/`take!`'s `on-caller?` has no observable thread identity. `on-caller?` false does spawn the callback as the JVM dispatches it, but a callback the *other* side completes runs on that completing execution, where the JVM hands it to its dispatch pool | Deliberate | Design §8: a coroutine changes carrier at every park, so a thread identity is a key that stops being one at the next `<!`, and `Thread` here has no methods. `Thread/sleep` is a verb with a whole meaning (NOTES.md, "Channels"); `currentThread` is an identity, and the two `deftest`s whose subject it is stay unloaded rather than being answered with a carrier's address. |
