## Host bridge (Sources/Pippin, error.c host-error, fn.c context natives)

- **A host error keeps the Swift `Error` boxed as an opaque payload** and captures
  `String(describing:)` as its message when made; `ex-data` builds `{:host/error e}` on every call
  (storing it would make the value its own child). Catching it by type needs no registry — that is a cast
  (hosttype.c) — but `(:code (ex-data e))`-style access to the error's fields still does, plus a
  `Codable`/reflection walk of the error (design section "Интероп").
- [ ] **Only a host error thrown as is comes back as the Swift error.** Wrapped as a cause (an
  `ex-info` from Clojure code, or the analyzer's positioned rethrow of a macro failure) it surfaces as
  `ClojureError` with `cause.hostError` set. Trigger: a host caller wanting `catch let e as MyError`
  through a macro; then unwrap the cause chain in `takePending` or stop positioning host errors.
- [ ] **A Swift fn extends a protocol only through `extend`** (`(extend T P {:m f})` with `f` a
  `Value(function:)`); there is no Swift API for protocols, types or `satisfies?`. Trigger: a host
  wanting to implement a Clojure protocol for its own type registry (design section "Интероп").
- [ ] **`Value(function:)` bounds arity with a closed range**; a variadic fn with a minimum is `nil`
  (any count) plus a check in the body. Trigger: the first host fn wanting `[a & rest]` semantics.
- **The Swift body of a host fn is not `Sendable`-checked** and runs on whichever thread invokes the
  fn — since coroutines exist, any carrier of the pool (NOTES "Coroutines"). `Value.apply`
  (`clj_host_invoke`) counts as a synchronous host call: a park inside it is an error with a trace to the
  wait, never a block (design §5, `host_depth`). `Runtime.eval` from a bare thread blocks that thread on a
  park, the JVM's `<!!`. The way out is the async bridge below.
- **`ClojureError.trace` is the frames at the throw**, innermost first, and `description` appends
  them Clojure-style (`at user/f (line:col)`); a `ClojureError` rethrown from a host fn hands the
  same frames back to the core, so a non-error value keeps them across the boundary. Compiled frames
  come from the real stack and interpreted ones from the shadow stack, merged by stack order (trace.c):
  the same frames either way, a compiled one at its fn's own position.
- **`Value.apply` is a recovery point** (`clj_host_invoke`): a stack overflow in compiled code called
  from Swift lands there as the "Stack overflow" `ClojureError` (guard.c, "Compiler"), and the call is a
  top-level bracket, so a `def` inside parks the fn roots the caller may still borrow. `Runtime.eval` has
  the same through `clj_eval`. Other host entries that run Clojure code (a lazy seq realized through
  `Value`, a deftype's `equals`) have none: an overflow there is fatal with the trace on stderr.

### The async bridge (Async.swift)

- **`callAsync` spawns, and the fresh coroutine is clean by construction.** `host_depth` is raised only
  by `clj_host_invoke` (eval.c), so a coroutine `clj_coro_spawn` starts has none of the host's frames
  under it and may park anywhere; nothing needs lowering, and `callAsync` is a spawn plus a
  `withCheckedThrowingContinuation` resumed from `on_done` with `clj_coro_result`.
- **`on_done` can run before `clj_coro_spawn` returns**, on another carrier. So the continuation is
  stored before the spawn and the coroutine handle after it, both under the call's lock, and a
  cancellation that lands in that window is replayed once the handle is there. `clj_coro_cancel` on a
  finished coroutine returns at the state check (sched.c), which closes the same race on the C side.
- **A spawn with no `on_done` reports an uncaught throw to stderr** (`finish`, sched.c): the callback is
  how the coroutine knows its throw has a reader. `callBlocking` therefore passes an empty one and reads
  the result after `clj_coro_join_blocking`, which is valid only while the handle is held.
- [ ] **A throw out of a coroutine keeps only the trace on the value.** `clj_coro_entry` stores
  `clj_take_pending()` and the pending trace dies with the coroutine, so `ClojureError.trace` is
  `clj_ex_trace`'s — full for an `ex-info`, empty for `(throw :k)`, where `Value.apply` would still have
  the frames. Trigger: a host reading traces off non-error values; then the coroutine keeps its
  pending trace for the reader.
- **The Swift side of an `async` closure cannot itself wait**, by the same `host_depth` rule: its frame
  is a host call. So the native fn starts a `Task`, hands back `[promise cancel-fn]` and returns, and the
  wrapper of `clojure.core/host-async-fn` — `(deref p)` in a `try`/`catch :cancelled` — does the park, one
  frame later, with the host frame gone. Clojure rather than a C shim because it is four existing calls
  (`apply`, `nth`, `deref`, `throw`) and because `catch :cancelled` is the rule both backends already emit
  rather than a copy of it. The result crosses tagged, `[ok? v]`, because a channel carries values and not
  throws, and the tag is what lets a Swift error arrive as a throw. The native fn may also answer that outcome
  itself, when it made the call in place (a `@MainActor` stub on the main thread, below): a boolean first
  element is an outcome, a promise is a pending call, and the wrapper parks only on the second.
- **The wrapper is a private var of the core, not a string the bridge evaluates.** Swift takes it with
  `clj_ns_resolve(clj_ns_core(), …)` and `clj_var_root`, once. Source evaluated at first use would make the
  host bridge need the reader and the analyzer at run time, and the shipping build is the compiled core with
  no interpreter in it (design §10). `make boot` is what carries a change to it into `core_clj.inc` and
  `boot/*.c`.
- **A cancelled coroutine cannot park again**: the flag is sticky, so the `catch :cancelled` that
  cancels the Task calls a host fn and rethrows, and nothing in that arm may wait. The same trap catches
  tests: a cancellation handler that reports through a channel throws a second cancellation instead.
- **A cancelled taker stays counted in the channel's pending queue** until a hand-off tries it, so
  `clj_debug_chan_pending` says nothing about whether a cancel has landed; the coroutine's own outcome
  does.
- **`callDetached` is `Task.detached`.** An inheriting `Task` would put the wait on the caller's actor,
  so a `@MainActor` caller would hop the main thread for a result it is not waiting on.
- **`callBlocking` refuses only the main thread** (`Thread.isMainThread` or `clj_coro_on_main_carrier`).
  Called from a carrier it is legal and costs the pool that carrier until the call returns — the design's
  explicit opt-in, and the one escape hatch from a synchronous host call that needs a value now.
- **`affinity:` is per call, and `.main` is sticky.** `clj_sched_enqueue` routes by the coroutine's own
  affinity, so a `.main` call resumes on the main carrier after every park too, not only at its first
  step. The spawn throws when no main carrier is installed, which is the whole check — there is no
  fallback to the pool, since a body asking for `:main` asks for the thread and not for speed.
  `callBlocking` takes no affinity: the thread it freezes is the one that would have to turn the run loop.
  A test therefore drives it the way CoroTests does — `clj_debug_sched_main_adopt`, then
  `clj_sched_main_pump` by hand, with no `await` between the two, since a suspension can change the thread
  out from under the adopted carrier.
- **A `@MainActor` stub answers in place on the main thread and hops from anywhere else** (SwiftStubs.swift,
  design §5 «Замыкания через границу»). Its var's fn is `host-async-fn` over a Swift inner fn, which decodes the
  arguments where the caller is and then either makes the call through `MainActor.assumeIsolated` and answers the
  outcome `[true v]` itself — nothing parks, no Task, no promise — or starts it on `MainActor.run` through
  `pendingCall`, the same `[promise cancel]` `Value(asyncFunction:)` answers. The test is `Thread.isMainThread`,
  not `clj_coro_on_main_carrier`: the main actor is the main thread, and a test can adopt any thread as the main
  carrier. Before a hop the stub asks `clj_host_park_allowed`, so under a raised `host_depth` the error, with the
  caller's frames, comes before the Swift function ran and not from the `deref` after it; a cancelled caller gets
  its cancellation. The Swift side is not `callAsync(affinity: .main)`: that one carries Clojure onto the main
  carrier, and here the Swift call is what moves. `SwiftStubs.hops` counts hops for the tests.

- **The bridge suite waits for its own coroutines** (`SettledTrait`, AsyncBridgeTests). None of its tests
  takes a live-object baseline, so one that outlives its test surfaces as a failed `CoroBaseline` in the
  next suite instead — which is how a leftover taker of a cancelled call once failed `AsyncLibTests`.

### Typed closure adapters (Closure.swift)

- **The arity check is the feature, so it happens once.** `closure()`/`closureAsync()` ask
  `clj_fn_accepts` when the wrapper is made and the call path then carries no check of its own. The one
  test left at the call is the result's kind (`ValueTypeMismatch`): a fn returns a value, not a
  signature, so nothing earlier can know it. A keyword, map or vector is invokable but has no arity to
  compare against, so it is refused at creation rather than checked on every call.
- **`Void` can conform to nothing** (a tuple type takes no extension), so the result-dropping adapters are
  their own overloads with no `R`. They are picked by the contextual type: with `-> Void` the decoding
  overload fails its `ValueDecodable` constraint, with any other result the Void one does not match.
- **`onFailure` has no default on purpose.** A non-throwing function type converts to a throwing one, so a
  defaulted policy would make a bare `closure()` ambiguous between the two families. Passing it is also
  what design §5 means by "configurable at the stub": `Value.trap` is the dev policy and
  `Value.report(default:)` the release one, and the choice is written where the stub is.
- **The protocols refine `SendableMetatype`.** The returned closures are `@Sendable` and capture the
  generic parameters' metatypes; without it every adapter warns under Swift 6 concurrency checking.
- **`Bool` decodes by truthiness** — nil and false are false, everything else true — so a Clojure
  predicate answering `nil` is not a decoding failure. `Bool?` is the three-valued reading, as `Optional`
  maps Clojure nil to Swift nil for every wrapped type.

### Host-defined vars and primitives (Runtime.swift `define`, Differential.swift, Primitives.swift)

- **`Runtime.define(name, in:, arity:, doc:, body)`** interns the var (the namespace is created when
  missing) and binds a `Value(function:)` as its root through the calls `eval_def` makes —
  `clj_var_bind_root` (epoch bump, a replaced fn root parked until the thread is idle), then
  `clj_var_set_meta` with `{:ns :name}` plus `:doc`, then the macro and dynamic flags cleared — so a
  `def` and a `define` of the same var take turns freely and a site warmed on the boot fn (an INTRINSIC
  or FUSED node) falls back through its guard (DefineTests, `clojure.core/+`). The fn is named
  `ns/name`, so arity errors read as a `defn`'s. No `:line`/`:column`, no `:arglists`; `(doc x)` prints
  the doc. Nothing new in C: the entries existed. Returns the var, as `def` does.
- **A primitive never exists without its specification and the differential test** (design §6b item 1,
  the intrinsics rule of intrinsics.h from the other side of the bridge): the Swift implementation of
  something Clojure already says how to compute lives next to the Clojure implementation that stays its
  specification, and `Runtime.differential(primitive:spec:samples:messages:)` runs both over the samples
  and returns the divergent ones — a value must be `=`, a throw must meet a throw, the messages agree only
  with `messages:` (a spec in Clojure rarely throws the primitive's text; the sort spec meets a mixed pair
  in another argument order). It is public API, not a test helper: the escape hatch is for host libraries
  (hiccup diff, JSON, sorting) whose own tests cannot import ours, and it returns data, so it binds to no
  test framework.
- [~] **`compare` and `sort` are the first residents** (Primitives.swift), and both bodies are now one C call
  each: `clj_compare` and `clj_sort` (compare.c) do the work, the Swift fns are the vars. They bind into
  `clojure.core` from `clj_host_boot`, a weak C hook `clj_init` calls last, defined by the Swift module with
  `@_cdecl`: a raw `clj_init()` and `Runtime()` boot the same core, tests take baselines after either. Their
  roots are ordinary (not immortal, read owned). `Runtime.sortSpecification` is the top-down merge sort in
  Clojure in the same file, evaluated by PrimitiveTests and the bench; `compare` is the leaf without a
  Clojure spec, since nothing in Clojure here orders two strings or chars (no `int` of a char, no `subs`),
  and it is checked by table. A `(sort ...)` call from Clojure therefore costs one crossing, not one per
  comparison; `sort-by` reaches `clj_sort_by` directly through a builtin and never crosses at all. A C-only
  host has `sort-by` and the sorted collections but not `compare` or `sort` as vars. Trigger for dropping
  the Swift residents entirely: char/int conversion landing, so a Clojure `compare` spec becomes writable
  and the differential can take the C builtin as its subject.
- **Deviations from Clojure's `compare`**: −1/0/1 always (the JVM returns the char or length difference
  for strings); strings order by code point, the JVM by UTF-16 unit (they differ only between an astral
  char and U+E000–U+FFFF); the mixed-type message names the runtime's types (`long cannot be cast to a
  string`) and an unordered type says `cannot be cast to Comparable`. A nil comparator is the default one
  inside the core, so the 2-arity `(sort nil coll)` refuses it by hand, the way invoking nil would.
- **Limits.** Varargs are `arity: nil` plus a check in the body, as with `Value(function:)`. core.clj
  cannot call a primitive at load time and cannot reference one without `(declare ...)`: the hook runs
  after core.clj. A C-only host has neither `compare` nor `sort` as vars, though `sort-by` and the sorted
  collections reach the same C functions. Meta is `:doc` only; `:private`,
  `:dynamic`, `:arglists`, `:tag` need a `def` afterwards. `define` on another thread against a running
  call is the concurrent-`def` race of the evaluator section.
- [~] **Cost** (bench/RESULTS.md, "Host-defined fns"): a host fn call is ~64 ns over a C builtin at the same
  site and ~60 over a closure — the `clj_invoke` path for context natives plus the bridge's `[Value]`
  array, per-argument wrapping and the box retain; the design's "tens of ns" at the upper end. `sort` of
  1k fixnums: 83 ns per element against 4960 through its Clojure spec. Trigger for a cheaper crossing: a
  host fn in a per-element position of a profile; then an argument-buffer body signature.
- [ ] **Triggers.** Many primitives → a registration table and a generated differential suite over it, the
  design's one-table shape for intrinsics; a primitive core.clj needs at boot → a C builtin under the
  intrinsics rule, or a second hook before core.clj; a host wanting a Clojure protocol implemented in
  Swift → the `extend` entry above; `Runtime.define` of a macro → `:macro` meta and `clj_var_set_macro`,
  when a host has a reason.

### Swift stubs (SwiftStubs.swift, scripts/swift-stubgen.py, hostbox.c, hostmodule.c; design §5 level 2)

- **The chain.** `(:require-swift [M :as a :refer [...]])` in `ns` is `require-swift`
  (core.clj), which calls `require-swift*` (hostmodule.c), which calls the loader `clj_host_boot` installs
  (`SwiftStubs.load`); a C-only host has none and refuses by name. `load` finds the module registered, or runs
  `SwiftStubs.generator`: `python3 scripts/swift-stubgen.py`, which extracts the module's symbol graph, classifies
  it with `swift-reprint.py`'s own classifier (imported, so the measurement measures this classifier), prints
  `stubs.swift`, builds `lib<M>PippinStubs.dylib` with swiftc and writes `report.json`, all in a cache directory
  keyed by the module's and the runtime's `.swiftmodule`, both scripts, the module maps, the link arguments and
  the compiler. `load` then `dlopen`s it `RTLD_LOCAL` and calls `pippin_stubs_register_<M>`, which hands
  `SwiftStubs.register` the functions and the refusals. Fixture and test: `Tests/PippinTests/Fixtures/swift/`,
  `SwiftStubTests`.
- **The stub is built against the running `Pippin`.** It imports `Pippin` (the box, the async bridge, the hop)
  and binds to the process's copy of it and of the core with `-undefined dynamic_lookup`, as compiled units do;
  the generator needs the directory of `Pippin.swiftmodule` and CljCore's module map, which the test reads off
  its own bundle's path. Pippin's Swift ABI is not resilient, so a stub belongs to one build of it, and its
  fingerprint is in the cache key. It is compiled in Swift 5 mode: a decoded struct is captured into the
  `@MainActor` call, and the box is `@unchecked Sendable` by design (§5 «Замыкания через границу»).
- **A module is a namespace, a base name a var.** `makePoint(x:y:)` is `PippinFixture/make-point`, kebab-cased by
  `clj_objc_kebab` (one spelling rule for both levels); its fn takes the label keywords where the declaration
  has labels and picks the overload whose labels match, in order — `(moved p :by 3)`. A wrong label is an error
  naming every overload of the base name, at run time, as level 1's is; a label computed at run time works too,
  a superset of the literal labels §5 asks for. A var none of whose overloads parks (isolated or `async`) is a plain
  host fn; one with any gets `host-async-fn`'s wrapper for the whole var.
- **A member is the var `Type.member`** (design §5 «Как пишется вызов»): `Point.init(x:y:)` is `PippinFixture/Point.`,
  `Point.scaled(by:)` is `Point.scaled` with the receiver first and unlabelled, a static member has no receiver, a
  property is a getter `Point.sum` and, where settable (a stored `var`, or `{ get set }`), a setter `Point.set-first`;
  module variables are `greeting`/`set-greeting`. The generator passes the Swift owner and base
  (`SwiftStubs.Function`), the runtime spells the var, so the kebab rule stays `clj_objc_kebab`'s alone. The
  `.`-form on a box refuses with the var to call (objc.c). An instance member of a `@MainActor` class hops as a free
  function does; one marked `nonisolated` does not (the classifier reads `nonisolated` off the declaration's head).
- **Overloads the labels cannot tell apart are all refused.** The generator groups stubs by owner, var base and label
  shape, the receiver included, and refuses every member of a group of two or more with the other declarations in
  the reason (`width(_ n: Int)` and `width(_ s: String)` in the fixture); `register` does the same for shapes only
  the kebab spelling makes equal, adding them to the registration's refusals, which `report.json` then lacks.
- **`inout` and `mutating` answer the new values** (design §5 «`mutating`, `inout`»): the stub decodes into lets,
  copies what the call mutates into a fresh `var` inside the call (a closure that runs on another thread may not
  mutate a capture), and answers `SwiftStubs.outcome`: `inout` values in order with `self` first, then the result
  unless `Void`; one value bare, several a vector. A struct setter is the `mutating` case, answering the new struct;
  a class setter answers nil. `borrowing`, `consuming`, `__owned` and `__shared` are dropped; `isolated` and
  `sending` parameters are refused.
- **A class instance is a box of the object** (`SwiftStubs.box(object:)`, design §5 «Экземпляр класса»). The
  descriptor is per dynamic class, so one object is one box type whatever static type it crossed under, and its
  operations come from the dynamic class at run time: `any Hashable.Type` → its `==` and `hashValue`,
  `any Equatable.Type` → its `==` and no hash (a key refuses, as a struct's does), neither → `===` and
  `ObjectIdentifier`. `unbox(object:as:)` checks the descriptor is a class box's, then casts, so a subclass passes
  for its superclass and a superclass instance where a subclass is expected is a `ValueTypeMismatch`.
- [ ] **Boxes of two dynamic classes are never `=`**, even when a shared superclass's `==` would call them equal: the
  descriptor is the dynamic class's and hostbox.c compares across descriptors as false. Trigger: a module whose
  `Equatable` base class means equality across its subclasses; then a descriptor per root of the conformance.
- **The four `throws` forms share the host-error path** (design §5 «`throws` — четыре формы»): the stub prints
  `try` for `throws`, `throws(E)` and `rethrows` and nothing for `throws(Never)`; `E` goes into the var's `:doc` and
  `report.json`'s `effects`. A thrown Swift error is `Value.throwing`'s host error — `ex-type` its dynamic type,
  caught by `PippinFixture/FixtureError` — and comes back to Swift as itself from `Value.apply` or a closure adapter.
  The classifier reads effects off the text between the parameter list and `->` (`decl_parts`), so a closure
  parameter's `throws` is not the function's.
- **A closure parameter exists for `rethrows`**: a throwing function type of at most three `Int`/`Double`/`Bool`/
  `String` arguments and such a result or `Void`, decoded with `Value.closure()`. A Clojure fn throwing a host error
  hands Swift the Swift error, a Clojure throw arrives as `ClojureError` and comes back out of the stub as the value
  thrown. Anything else in a function type is refused with the reason.
- **An `async` stub parks its caller** (design §5 «`async`-функция модуля»): `Function(async:)` decodes on the
  caller's thread, asks `clj_host_park_allowed` before any Swift code runs (under a synchronous host call the error
  comes first, with the caller's frames), then answers `Value.pendingCall`, a `Task.detached`. The parked caller's
  cancellation reaches `host-async-fn`'s `catch :cancelled`, which cancels the Task; the fixture's
  `wait-for-cancel` counts it, from `future-cancel` and from a cancelled Swift `callAsync`. There is no in-place
  path, and `@MainActor async` hops through its own `await`.
- **Compiled code reaches a stub as a var, nothing more.** A call to `PippinFixture/moved` is an INVOKE of a var
  the compiled set does not define, so both dev and `--closed` emit a `V[]` entry and `clj_c_invoke`; the unit's
  pools intern the var before its `ns` form runs `require-swift`, which binds the root of that same var. The
  fixture runs interpreted, dev-compiled and closed-compiled with one expected output.
- **A boxed struct's `=` and `hash` are the Swift type's** (hostbox.c; design §5 «Равенство и хэш бокса»). The
  generator prints `SwiftStubs.box(v)` and swiftc picks the `Hashable`, `Equatable` or plain overload; the
  descriptor, one per Swift type and immortal, carries those operations. Boxes of two Swift types are never `=`.
  Without `Hashable` the hash slot records a refusal (`clj_refuse`, error.c) and answers 0, and the HAMT's assoc
  throws it — `assoc`, `conj` onto a set, a map or set literal, `hash-set`, `set`, `add-watch`, a record's extmap —
  as do `hash`, `=` and `not=`; a lookup, `contains?` or `dissoc` answers "absent", which is true since storing
  refuses, and drops it. Without `Equatable`, `=` of two distinct boxes refuses the same way; `identical?` and
  `(= b b)` hold. Each storing operation drops a stale record before it hashes, so a refusal is never blamed on
  the wrong key. The cost where nothing refused is one relaxed load of `clj_refusals_held` per HAMT operation.
- [~] **A refusal met outside those operations is dropped.** Swift's `Value ==` and `Value.hash(into:)`, or any C
  caller of `clj_equals`/`clj_hash` itself, get false or 0 and the record waits until the next of those
  operations drops it — the "Left" of NOTES "Type descriptor", `clj_equals`/`clj_hash` cannot throw. A record left on a thread's implicit execution when the thread exits keeps
  `clj_refusals_held` nonzero, so every HAMT operation takes the out-of-line check. Trigger: fallible equals/hash
  slots (the same entry), or `clj_refusal_*_slow` in a profile.
- **The box handed back is the value boxed** (`SwiftStubs.unbox`): the payload is a Swift object holding the
  struct, and unboxing returns its copy of the bits — `is-last-made` in the fixture tells it from an equal value
  made again. A box of another type is a `ValueTypeMismatch`, which crosses as a host error.
- **The report is part of the product.** Every public function, member, operator, property, subscript and enum
  case of the module is either generated or listed with its reason, in `report.json` and in the registration
  (`SwiftStubs.refusals(of:)`); a stub swiftc rejects moves to the report with swiftc's message and the rest is
  built again, so one symbol cannot take a module down. A refused name is no var, so a call to it is the analyzer's
  "Unable to resolve".
- [ ] **What the generator does not generate.** Slots cross as `Int`, `Double`, `Bool`, `String`, `Void`, a module
  struct with no public stored property (a box), a module class (a box of the object), and as a parameter the
  throwing scalar closure above. Refused with a reason: generics (functions and types), optionals (`init?` too),
  collections, tuples, enums and their members and cases, protocols' members, actors, other closures, operators,
  subscripts, isolation other than `@MainActor`, `isolated`/`sending` parameters, types of other modules, structs
  with public stored properties (§5 moves those as a map) and their members. Trigger: the first symbol of that list
  an application needs; generics come with the call-site instantiation list.
- **The classifier the generator shares with the measurement reads a declaration's own head and effects**
  (`decl_parts`): a closure parameter's `throws`, `async` or `@MainActor` is not the function's, `nonisolated` drops
  the owner's actor, the `>` of `->` closes no bracket, and a property's type stops before its `{ get }`.
  `docs/swift-reprint.md` was measured again with them (x86_64, SDK 26.5): the crossing shares held, computed
  properties moved from handles to data, and SwiftUI's isolated share fell from 30.6 % to 1.9 %, its modifiers
  being `nonisolated` members of `@MainActor` protocols (design §5 «Цена изоляции»). The measurement builds for
  the host's architecture now.
- [ ] **Generation is per module, not per call site.** The whole supported surface of a `require-swift`'d module is
  generated (§5 wants the closure of the call sites, which the closed world gives); the cache makes a second load
  free. Trigger: a module whose surface makes the swiftc step slow, or `clj-compile --closed` of an application.
- [ ] **The dev path only.** `dlopen` of a dylib the generator builds; `clj-compile` and the nREPL server configure
  no generator, so a file declaring `require-swift` loads there only when the module is already registered. The
  production path — stubs linked into the app, entries called at start, `generator` nil — is designed (§5
  «Объявленная граница») and not built. Trigger: §10 step 9's application, or the first `clj-compile` of a file
  with `require-swift`.
- [ ] **Generation blocks the calling thread** for the generator and swiftc, seconds cold, under one lock per
  process. Trigger: `require-swift` from the dev client's REPL on the main thread; then the blocking pool.
