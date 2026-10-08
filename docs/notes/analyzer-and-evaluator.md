## Analyzer and evaluator (Sources/CljCore/analyzer.c, eval.c, fn.c, node_data.c)

- [~] **A node is the program, `clj_exec` its execution state.** `clj_node` carries no interpreter field and
  is `const` to eval.c and fn.c; the analyzer numbers a finished tree in pre-order (`id`, `nnodes` = subtree
  size, so a subtree's ids are contiguous). `clj_exec_new` builds `exec_node[nnodes]` in one walk when a
  tree first runs and every child dispatch goes through `frame->exec->nodes[id]`: one extra indirection
  per node, 1–3 % on the seq benchmarks. The table holds the eval pointer and a hit counter (16 bytes per
  node, plus the keyword-lookup cache pointer of a `(:k m)` site — "Shapes" — 24 bytes). Trigger for widening
  it further: the var inline cache from the design.
- **Exec rewrites: `clj_exec_count` is the first.** It swaps every node's eval for a wrapper that bumps
  `exec_node.hits` and calls `clj_node_eval_fn(kind)`, and back; off, nothing in `eval_child` changes.
  Only the mechanism and a test exist: nothing reads the counters yet (the PGO / hot-branch data source
  of the design). A rewrite while the tree runs takes effect at the next child dispatch.
- **Every node carries `line`/`col`** of the innermost enclosing list the reader positioned (0 when none:
  a list a macro rebuilt reports the list the macro call sat in); the codec writes them as a trailing
  `line column` pair and omits them when unknown. Only traces and the profiler read them.
- [ ] **A closure retains its whole top-level tree** through the exec, not only its fn subtree: a fn defined
  inside a large top-level `let` keeps every sibling constant alive, and tests that count live objects
  across a redefinition must repeat the exact defining form. Trigger: memory of a large loaded program;
  then a per-fn exec sliced by the fn's id range.
- **Ownership in the evaluator is a runtime rule, not an analysis** (design, "Конвенция счётчиков"). A
  node evaluates to an owned value, except that `eval_borrowed` reads a local, captured or constant
  node at +0 where the consumer only needs it for a call the frame outlives: the fn position and the
  arguments of an invoke, vector and map literal items, the test of `if`, non-last items of `do`.
  `eval_all` returns a `uint64_t` mask of the owned results (more than 64: all owned) and only those
  are released, on the throw-midway path too. A closure frame borrows its fixed params and the self
  slot from the caller's argument array, alive for the whole call by the +0 convention (`eval_invoke`'s
  buffer, a native's stack array, `clj_apply`'s `all[]`); the variadic rest list is built and owned.
  `clj_frame.owned` has one bit per slot: `slot_set` (let, loop, recur, catch) releases the old value
  only when its bit is set and marks the new owned one, teardown releases owned slots only; a frame
  with more than 64 slots retains every param at entry and treats every slot as owned; the top-level
  frame starts with none. Whatever lands in the heap or is returned is retained as before: `let`/`recur`
  inits, closure capture, a body whose tail is a local. A var read in a borrowed position is +0 when
  its root is immortal (every root bound by boot, next entry) or a fn (next entry but one); a data
  root is retained, since `clj_var_bind_root` releases the old root at once and that +1 is what keeps
  it alive through the call. Measured effect of borrowing locals alone was nil (a non-shared pair is
  five plain instructions); borrowing the core roots removed the atomic pair every call through a
  core var paid, borrowing user fn roots the same pair on every user call (bench/RESULTS.md). A local's
  last use is the one read that hands the frame's reference over instead (the last-use entry below).
- [~] **`clj_node_to_data`/`clj_node_from_data` cover every node kind** (grammar in node_data.c); constants
  are limited to what prints and reads back: nil, booleans, numbers, chars, strings, keywords, symbols and
  vectors/maps/lists/seqs of those (a seq reads back as a list; symbol meta and the reader positions on
  constant lists are dropped, the node's own position is kept). Anything else — a fn or protocol a macro embedded as a constant, a deftype descriptor, a host
  value — makes `to_data` throw "not serializable: <type>". Vars travel as qualified symbols and are
  interned on read; `from_data` checks the shape and slot bounds, not that `recur` sits in a tail
  position. Trigger: a tree cache on disk / AOT; then a binary form and a `recur` placement check.
- **A literal's reader metadata is a `with-meta` call**, as Clojure's `MetaExpr` is: `^:foo [1]` analyzes
  to an invoke of `clojure.core/with-meta` over the vector node and the analyzed metadata map, so
  `^{:a x}` sees the local `x` and the value is rebuilt per evaluation. Folding the metadata into the
  constant would break the codec, which writes a constant through `pr-str` and reads it back: `pr-str`
  does not write metadata. `with-meta` is not a pure intrinsic, so the call is never folded either.
  Vectors, maps, sets and `()` carry it; `^m` on a quote form lands on the quote form, which the analyzer
  consumes, exactly as on the JVM.
- **Every core.clj form and every type-macro expansion must serialize** (`CoreSerializableTests`):
  each top-level form is analyzed in `clojure.core` and round-tripped through `to_data`, `pr-str`,
  read, `from_data`; the test pins the form count so an empty run cannot pass. core.clj defines
  `defprotocol`/`deftype`/`extend-type`/`extend-protocol`/`reify` without using them, so their
  expansions are checked on user forms in the same test. A macro that needs a runtime object must
  emit a var reference or a builtin call that finds it at run time (`reify-type*`), never the object.
- [~] **Macros expand in the analyzer, in `analyze_list`**, not in a separate pass: a list whose head
  resolves to a macro var (and is not a local or a special form) is expanded until it is not, then
  analyzed. `&env` is always nil: locals are slot indices, not a map. Trigger: a macro that inspects
  `&env` (`clojure.tools.macro`-style, `binding`-aware macros). Arity errors count `&form`/`&env`
  (`Wrong number of args (2)` for `(when)`); Clojure subtracts 2.
- **`let`/`loop`/`fn` are core.clj macros over `let*`/`loop*`/`fn*`**, as in Clojure, so
  `macroexpand-1` of `(let ...)` yields `let*` and syntax-quote qualifies them to `clojure.core/let`.
  The analyzer's messages for the starred forms still say `let`/`loop` (`(let* [a] a)` reports
  "let requires an even number of forms"); Clojure says "Bad binding form". Trigger: nobody.
- **A macro is a var whose meta carries `:macro true`**, which is all the JVM keeps too (`Var.isMacro`
  reads that entry): `analyze_def` takes the def node's flag from the def'd symbol's meta, and
  `(def ^{:macro true} m (fn [&form &env x] x))` defines a macro there and here alike. So `defmacro`
  is not a special form but core.clj's own macro over `def`/`defn`, as ClojureScript closes the same
  bootstrap; it expands to a `defn` whose name carries `:macro true` and whose arities grow the implicit
  `&form`/`&env`, which `sigs` elides from `:arglists`.
- **The macros above `defmacro` in core.clj are defs of a bare `fn*`** — `lazy-seq`, `when`, `when-not`,
  `if-not`, `cond`, `let`, `loop`, `fn`, `defn`, each writing out `&form`/`&env` and `:doc`/`:arglists`,
  as Clojure's own core.clj writes its pre-`defmacro` window. Their params go through `fn*`, so they
  cannot destructure: a `[[x y] & body]`-style macro belongs below `defmacro`, which every later macro is.
- [~] **Var meta follows Clojure minus `:file`**, and `:ns` is the namespace's *symbol*, not a Namespace
  object (there is no `ns-name`; `(str (:ns m))` prints the same). `def` evaluates the symbol's meta
  map as a form, so `^{:tag String}` resolves `String` to the descriptor and an unresolvable symbol in
  it is an analysis error, as in Clojure. The C builtins (`first`, `meta`, ...) carry no `:doc` or
  `:arglists`: `(doc first)` prints only the name. Trigger: a doc browser; then a doc column in the
  `entries` table of builtins.c.
- **Privacy is a resolve-time rule only.** `^:private` hides a var from the unqualified fallback into
  clojure.core, refuses a qualified reference from another namespace (through an alias too) and is
  refused by `refer` ("x is not public"); `(var ns/x)`, `#'ns/x`, `resolve` of the qualified symbol and
  a raw `clj_ns_refer` still reach it.
- **Exceptions unwind by return code, not by `longjmp`**: `try` sees `CLJ_THROWN` from its body and
  takes the pending value; every C frame in between releases its own temporaries on the way out.
  `clj_throw` captures the shadow stack (below) as a vector of `{:fn :line :column}` maps, innermost
  first, at most 256 frames: an `ex-info` stores it at its first throw and keeps it through catch and
  rethrow (`ex-trace`, `ClojureError.trace`); any other thrown value (a string, a host error, a deftype
  error) carries it only in the pending state, so a handler that rethrows it records the handler's
  frames. A frame's position is the call site that entered the fn, or the fn's own position when the
  call came from a native or the host (`apply`, `map`, a Swift `callAsFunction`). The trace is built
  at throw time, so a throw from deep in a loop allocates a vector and a map per frame; nothing is
  captured for exceptions that are never thrown. `clj_take_pending` drops the pending trace: a host
  that wants it takes it first (`clj_take_pending_trace`), as `ClojureError.takePending` does.
- **Shadow stack** (shadow.c): a per-thread ring of `{fn node, call site, sp}` pushed and popped around
  every interpreted closure body (natives are leaves; a compiled fn pushes nothing and is found on the real
  stack instead — "Compiler", frames and traces), 8192 frames, calloc'd on the thread's first push (128 KB)
  and freed when the thread exits. Deeper than that, the innermost frames are kept and
  `clj_shadow_stack_dropped` counts the outermost ones overwritten; a test shrinks the capacity with
  `clj_debug_shadow_stack_set_capacity` since Swift Testing's stacks overflow the C stack long before
  8192 (the main thread of a release build can reach it). `clj_shadow_stack_snapshot` is
  async-signal-safe: it reads through a pthread key rather than the `_Thread_local`, because a first
  touch of a `_Thread_local` on a thread that never ran Clojure allocates under dyld. The stack guard's
  limit lives in the same struct, so a call pays one TLS load for both. Cost per call: that load, a
  null check, three stores (`sp` is the frame's address, what orders it among compiled frames), an
  increment and a decrement with a compare, plus one load and branch on the instrumentation byte
  (bench/RESULTS.md: within the run-to-run noise of the closure-call scenario). The C stack is still what
  limits recursion depth; the shadow stack does not replace it. `clj_shadow_stack_trace` is the merged
  trace (trace.c), not the ring alone.
- **Crash handler** (`clj_crash_handler_install`) is opt-in: a host with its own crash reporter
  (Crashlytics, MetricKit) must not have its handlers replaced, and calls `clj_shadow_stack_snapshot`
  from its own instead. Installed, it writes the frames with `write(2)` only (names are borrowed from
  the fn node's symbol, numbers formatted by hand) and re-raises with the default disposition. The
  frames are the merged trace walked from the signal's context (trace.c), on the alternate signal stack
  every thread that ran Clojure has (guard.c); for SIGSEGV and SIGBUS the guard page check runs first, so
  a stack overflow in compiled code is an error, not a report. Tested on SIGUSR1 through a pipe on a
  plain pthread: `raise` on a dispatch worker thread cannot `pthread_kill` itself and delivers the
  signal to whichever thread has it unblocked.
- **Signposts** (`clj_signposts_enable`, `Runtime.signposts`) are Apple-only and process-wide: an
  `os_signpost` interval named `invoke` with the fn name per closure call, off by default; elsewhere
  the call is a no-op. Enabling it costs a signpost id and two `os_signpost` calls per invocation.
- [~] **Fn profiler** (`profile-start!`/`profile-stop!`, the `profile` macro): inclusive wall time and
  call count per fn node, aggregated at pop into one global table under a mutex, reported as
  `{:fns [...]}` sorted by time. It does not measure natives (`+`, `first`, a Swift fn: they are
  leaves without frames), self time, or a call already running when it starts. Macro expansion runs
  closures, so a form analyzed while the profiler is on shows core.clj's macros in the report; the
  `profile` macro expands to a `let`, not a top-level `do`, so its body is analyzed before
  `profile-start!` runs. Entries retain their fn node until the stop. Trigger for a sampling
  profiler: a workload where the mutex per return shows.
- **Debug live counts are per type as well** (`clj_debug_live_objects_of`, `clj_debug_live_report`
  prints `type: count` for non-zero types): a 1024-slot table keyed by descriptor pointer, slots
  never freed, so a dead deftype descriptor keeps its slot and a reused address inherits its count.
- **An unresolved, package-less `catch` name ending in `Exception` or `Error` takes every thrown value**, as
  `Throwable` does (`is_jvm_throwable_name`, analyzer.c). Java names every throwable class that way, which is
  what tells a JVM class with no counterpart here from a typo; the name is tried as a var first, so a type of
  ours wins, and a dotted `java.lang.Exception` is a host type and still refused as one (`TryCatchTests`).
  What it is worth: one `(is (thrown? IllegalArgumentException ...))` clause used to refuse the whole deftest
  around it, and 24 deftests of Clojure's own suite were lost that way, 20 of which pass
  (docs/notes/corpus.md, docs/jvm-differences.md).
- **`catch` takes a fourth kind, `CLJ_CATCH_KEYWORD`, for any keyword but `:default`**: matched at
  unwind by `clj_ex_isa(thrown, c->keyword)` (eval.c), `isa?`'s scalar case reimplemented in C rather
  than called — a catch selector is always a bare keyword literal (design §4, "Селектор держать
  тупым"), never a vector, so the recursive branch of `isa?` never applies and the C copy is exactly
  as capable as calling it. `:default`/`Throwable`/`Exception`/`Object` still take every thrown value
  except one whose `ex-type` `isa?` `:cancelled` — the Python `BaseException`/`Exception` split, done
  as a matching rule instead of a root type (design §4, "Отмена — `:cancelled`"), kept explicit because
  `:default` also catches non-errors (a fixnum, a string) that the type alone cannot decide. The rule is inherited
  through `isa?`, so the two-arity `derive` refuses `:cancelled` as a parent by name (core.clj): the bare keyword
  already failed its `(namespace parent)` assert, but that message reads as "add a namespace" and the namespaced
  retry derives under a keyword nothing matches. The three-arity `derive` is left alone — it writes the caller's own
  hierarchy, and `clj_ex_isa` reads only `global-hierarchy` (error.c). `ExceptionInfo`
  needs no matching carve-out: a cancellation is not an ex-info at all (below), so `clj_is_exception`
  already excludes it structurally. Beside the keyword kind there are two type kinds, `CLJ_CATCH_TYPE`
  and `CLJ_CATCH_HOST` (below); all four share one `selector` field, and only the two type kinds own it. The compiler's
  `emit_try` (compiler.c) mirrors `catch_matches` exactly, including the constant pool for a keyword
  selector (`const_index`, the same one `CLJ_NODE_CONST` uses) rather than interning it per throw; the
  two backends disagreeing here once hung `AsyncLibTests.goScoped` under `-DCLJ_COMPILED_CORE` only —
  `emit_try` had a bare `if (clj_is_exception(ex))` for every non-`:default` kind, so a compiled
  `go-scoped` child's `(catch :cancelled e nil)` swallowed its sibling's real error too, and
  `scope-child-failed!` never ran. `CompilerFixtureTests.compiledCatchSelectivityMatchesTheInterpreter`
  is the regression test: it fails against the old `emit_try` and passes against the fixed one, in
  both compiler modes.
- **A cancellation is not an ex-info** (design §4, "Отмена — не `ex-info`"): `clj_cancellation`
  (error.h/error.c) is its own type, `core_bits` deliberately without `CLJ_CORE_ERROR`, so
  `clj_is_exception` — and therefore `ExceptionInfo`/`CLJ_CATCH_ERROR` — excludes it without a special
  case. `ex-type` still answers `:cancelled`, `ex-message`/`ex-data` still work (`clj_ex_message`/
  `clj_ex_data`, error.c, special-case `clj_is_cancellation` directly since the generic `clj_is_exception`
  dispatch no longer reaches it); `ex-cause` is nil and `ex-trace` is nil like any non-ex-info (the trace
  lives only in the pending slot, same as a thrown string). The printer gives it the same `#error
  {:message :data}` shape as an ex-info, without `:cause`. **An uncaught cancellation is not a failure**:
  `clj_coro_report_uncaught` (sched.c) returns immediately for one, before the installed handler or the
  default stderr print — every call site (`finish`, `go_done`, `future_done`, `step_failed`, callback
  and pipeline paths in chan.c) shares this one function, so the fix is one guard, not several.
  `load.c`'s `wrap_pending` got the same treatment on its own call site: it used to relabel *any*
  exception escaping a loaded form as `"Syntax error compiling at …"`, which would have turned an
  interrupted `load`/`require` into a fake compile error, losing the cancellation's identity entirely.
  Left alone on purpose: `future`'s cached error for `deref` of a cancelled future (design §9, open) and
  `go-scoped`'s own cleanup catches (`scope-spawn`'s `:cancelled` clause, `scoped*`'s, `profile`'s and
  `with-out-str`'s in core.clj) — those don't exist to dodge a matcher carve-out, they exist because
  `finally`-shaped cleanup (child bookkeeping, `profile-stop!`, `out-capture-pop*`) must run before the
  cancellation continues past that specific frame, which a merely-structural exclusion doesn't provide.
- **`isa?`/`derive`/`global-hierarchy` stay Clojure (boot/core.clj); the unwind path reads the
  hierarchy's data instead of calling back into it.** `clj_ex_isa` (error.c) resolves and caches the
  `global-hierarchy` var once after boot (`clj_isa_install`, runtime.c) and, at each keyword catch,
  reads its current root — an ordinary persistent map — and does the membership check
  (`(contains? (:ancestors h) child) parent)`) with `clj_map_get`/`clj_set_contains`, no `clj_invoke`
  and no Clojure evaluation at all. Rejected: (1) invoking the `isa?` var as a closure mid-unwind — the
  obvious bridge, but it means running arbitrary interpreted code, with its own frame and pending-value
  handling, on a path that already has an exception in flight (the ex is out of the pending slot into a
  local by the time `catch_matches` runs, so nothing races, but a re-entrant eval on a hot,
  exception-triggered path is the kind of thing that grows edge cases); it would also have needed
  `isa?` to be resolvable before boot finishes analyzing itself. (2) mirroring the hierarchy as a C
  data structure kept in sync with `derive`/`underive` through a hook — a second source of truth for
  data that already lives in an immutable map one pointer-chase away. Reading that map directly gets
  the same non-reentrant, always-current answer for the one case (`isa?` on two keywords) `catch` can
  ever ask for. `clj_ex_isa` returns false, not a crash, before `clj_isa_install` has run (nothing
  before boot writes a keyword `catch` clause) and checks each level's core bits (map, then set) before
  reading it, so directly `alter-var-root`ing `global-hierarchy` to a non-map cannot crash `catch` — a
  defensive default, not a load-bearing one.
- [~] **`throw` accepts any value** (CLJS semantics): no implicit wrapping of a string or map into an
  ex-info, and no runtime check. `ex-message` of a thrown string is the string itself (CLJS says nil),
  so a `:default` handler reads `(throw "m")` like an ex-info; a string is still no error for
  `ExceptionInfo` or `ex-data`. Trigger: the analyzer's `:strict` mode, which should warn on "throw of
  a non-error value" (JVM/Swift strictness as a lint, not a runtime rule).
- **`ex-type` is total** (design §4, "Тип ошибки"): a keyword answers itself, an `ex-info` answers its
  `type` slot, everything else (a fixnum, `nil`, a string, a record, a host-error box) answers `nil`,
  and it never throws. The slot is filled once, at construction (`ex_info_new`, error.c), from a
  keyword under `:type` in the data map — `clj_ex_info_cause` looks it up and passes it down, so
  `clj_throw_cancelled` can hand `:cancelled` straight to the constructor with no `:type` key in
  `{:cancel/kind ...}` at all. The key stays in `data` either way (`ex-data` is unaffected); a non-keyword
  or absent `:type` leaves the slot `nil`, matching `ex-type`'s "everything else" case. A host error
  answers its host type — a type, not a keyword — which is what makes `ex-type` heterogeneous exactly as
  `clojure.core/type` is (below).
- **A host type is a value of its own** (hosttype.c, include/clj/hosttype.h; design §4, "Хостовая ошибка
  ловится как своя"): `clj_host_type` carries the spelling the source used, the type's **mangled name, which
  is the identity**, and the host's metatype as an opaque pointer. The display name cannot be the identity —
  `String(reflecting:)` of an `NSError` is `NSError` with no module, while the same type is written
  `Foundation/NSError` in source — so values are interned by the mangled name, one entry per type and a
  separate alias list per spelling looked up, and `=` is then identity. Interned values are immortal and
  excluded from the debug live count (`clj_debug_live_objects_exclude`, as error.c does for the cancellation
  singletons), so a suite's baseline does not move when a new Swift type first throws.
- **A host error's `ex-type` is minted where the metatype is, in Swift**: `Value(hostError:)` (Runtime.swift)
  calls `Value.hostType(of: type(of: error))`, which is `_mangledTypeName` plus `clj_host_type_intern`, and
  hands the result to `clj_host_error_new` for the slot beside the message. No synthesized keyword exists
  anywhere: `:Foundation/CocoaError` and `:NSCocoaErrorDomain/-1009` were both removed, because a name the
  user never wrote is a name nothing can be caught by on purpose. A type whose `_mangledTypeName` is nil
  leaves the slot nil, and `ex-type` stays total.
- **Recognition is a cast, not a name** (HostType.swift). `type(of:)` over an `any Error` answers `NSError`
  for every `_BridgedStoredNSError` — `CocoaError`, `URLError`, `POSIXError`, however the value was made —
  while `is CocoaError` is true and `is URLError` false for the same Cocoa-domain `NSError`. So a
  `CLJ_CATCH_HOST` clause resolves its name to a metatype and asks the host for a dynamic `is`:
  `_openExistential` opens the metatype value into a generic context, where `error is T` is an ordinary
  cast and works for a struct, an enum, a class, an Objective-C import, a bridged type and a **protocol**,
  the last being strictly more than the JVM allows in catch position. The name reaches the metatype through
  `_typeByName` over a mechanically built mangle: length-prefixed module (`Swift` is the substitution `s`),
  then a length-prefixed component and a kind letter each. **The kind letter cannot be known from the
  symbol**, so every candidate is tried — `V`, `O`, `C` per component and `P` for a trailing protocol, plus
  `So<len><name>C` for a single-component name, which is how `Foundation/NSError` reaches the Objective-C
  import. Generic arguments (`…Gen<Swift.Int>`), private and local types (`(unknown context at $…)`) and
  deep nesting do not round-trip, and are refused by name rather than guessed at.
- **The resolver is installed, not weak** (`clj_host_type_install`, called from `clj_host_boot`). A weak
  *declaration* is not a weak *reference* on Darwin — that is `weak_import` — so the `if (clj_host_boot)`
  pattern of runtime.h works only because something always defines that symbol. Two function pointers set
  at boot avoid the question entirely, and a C-only host (Sources/clj-load) simply never sets them.
- **A clause that cannot decide throws in place of the exception it was matching, and carries it.**
  `clj_host_type_catches` answers `CLJ_TRUE`/`CLJ_FALSE` or `CLJ_THROWN` ("No host type resolver: …" where
  nothing is installed, "Unable to resolve host type: …" where the name reaches nothing), and
  `clj_catch_instance` does the same for a var that holds no type. `eval_try` and `emit_try` both stop the
  clause chain on that — the compiled chain gets a `!u<k> &&` guard on every condition — because a later
  clause matching would swallow the refusal and turn a broken selector back into a silent mismatch, which is
  the one outcome §4 forbids. The refusal is raised **while another exception is unwinding**, so that
  exception is its `ex-cause` (`clj_throw_msg_cause`): replacing it outright reports a broken selector and
  loses the failure that reached it.
- **The name is resolved at analysis wherever there is anybody to ask.** `clj_init` ends in `clj_host_boot`,
  which installs the resolver, so by the time a form is analyzed a Swift host can answer, and `catch_kind_of`
  refuses an unreachable name there and then: a typo in `Foundation/URLErrror` is a diagnostic, not a clause
  that never fires. This is the one of design §5's three loudness paths for a refused host symbol that is
  built — the generator's per-module report and the LSP diagnostic at the call site are not. There is no nearest-name hint and there cannot be one — a name reaches a type by being
  mangled, and no set of known names exists to be near (design §4 "Диагностика"). A C-only host has nobody
  to ask and stays silent at analysis. The run-time refusal is not a fallback but the other half of the rule:
  `decode_catch` (node_data.c) sets `CLJ_CATCH_HOST` **without validating**, because a unit compiled where the
  type exists may be loaded where it does not; at run time a clause that never fires still asks nothing.
- **Both answers are cached by name, which is finer than §4's "cached per site"**: the core keeps the
  alias list, so a second site naming the same type, and a name that reaches nothing, each cost one
  `strcmp` walk. A *failing* name is remembered too, so a hot loop throwing through a clause naming a
  missing type does not re-run the candidate mangles.
- **`(Name. args)` is the positional factory of a `deftype`/`defrecord`** (`ctor_head_type`, `analyze_ctor`):
  a head symbol with no namespace whose name ends in a dot is rewritten to `(clojure.core/new* Name args)`
  when the stem resolves to a var that is unbound or holds a user type. The second condition is what keeps
  `(Long. 1)`, `(Integer. 1)` and `(java.util.Date.)` reporting the missing constructor §8 refuses rather
  than reaching `new*`: those names are bound here, to a boxed-class namespace or a host type. Unbound
  counts because `deftype` declares the name before it defines it, so a method body may construct its own
  type. Libraries write their own types this way — `com.stuartsierra.dependency` has `MapDependencyGraph.`
  and nothing else to fix — and `->Name` stays the factory the expansion itself uses.
- **A qualified symbol no var answers resolves to a host type outside `catch` too**, which is what makes
  `(derive Foundation/URLError ::network)` the ordinary `derive` it is in §4 — the JVM resolves a classname
  the same way, after the namespace map. `analyze_symbol` tries the resolver only after `clj_ns_resolve`
  returns nil, so every existing "Unable to resolve symbol" message is unchanged, and it emits
  `(clojure.core/host-type "Module/Name")` rather than a constant node: a host type does not print and read
  back, so a compiled unit has to carry the name and resolve at run time. Unqualified stays "Unable to
  resolve classname", which keeps `java.lang.Exception` and the corpus's `IllegalArgumentException` failing
  (their dots are in the *name*; they carry no namespace) and keeps a typo of `ExceptionInfo` from becoming
  a clause that never matches.
- **What `derive` on a host type cannot do.** `isa?` compares the `ex-type` *value*, so grouping fires only
  for types whose `type(of:)` is faithful — every Swift-native error. For a `_BridgedStoredNSError` the
  dynamic type is `NSError`, so `(derive Foundation/URLError ::network)` never fires for a real `URLError`
  even though `(catch Foundation/URLError e …)` catches it: the cast sees what the hierarchy key cannot.
  Not a bug in the hierarchy — the same measured fact that made the cast the identity in the first place.
  `(instance? Foundation/CocoaError e)` makes the same cast the clause does, so the two never disagree.
  Making `isa?` cast against every host type in the hierarchy would fix it and was not done: it puts a scan
  of the hierarchy on the throw path, and `clj_ex_isa` already reads that root **borrowed** while
  `clj_var_bind_root` frees the old root immediately for a non-fn value. For the same reason `derive` stays
  a load-time operation: nothing may write the hierarchy while an exception is crossing.
- **Our own type in `catch` is an unqualified symbol resolving to an ordinary var** (`CLJ_CATCH_TYPE`): the
  catch keeps the var, not the descriptor, so a re-`defrecord` is picked up and `node_data` can encode the
  clause as the var's qualified symbol. Matching is `clj_catch_instance` → `clj_var_deref` → `clj_is_instance_of`,
  the same call `(instance? R x)` makes. A thrown record is a plain value, so `ex-type` of it is still nil:
  the clause decides, not the hierarchy.

- **Namespaces** (ns.c, builtins_ns.c, the tail of core.clj). `*ns*` is a dynamic var in clojure.core whose
  root is `user`; `clj_ns_current`/`clj_ns_set_current` read and write the thread's binding when it has
  one, else the root, so `in-ns` inside a load moves only that load. `Runtime.eval`, `load-file`,
  `load-string` and `require` push `{*ns* (current) *file* path}` around their forms, as Clojure's `load`
  does; `cljEval` in the tests does not, and a test that moves must come back (`inUser`). A namespace
  holds mappings, refers, aliases, imports and an `excludes` set: unqualified resolution is mappings → refers →
  imports → clojure.core minus its private vars and the excludes (so `:refer-clojure :exclude/:only/:rename` are
  the excludes plus refers under the new names, and core stays implicitly visible: a core var defined
  later is visible too, where Clojure's refer snapshot would miss it); a qualified symbol resolves its
  prefix through the aliases first, then the registry, and reads the target's own mappings only (a var
  referred into `b` is no `b/x`). `require` (core.clj `load-libs`) takes symbols, `[lib :as a :refer
  [..] :refer :all :as-alias a]`, prefix lists and the `:reload`/`:reload-all` flags (both reload the
  one lib: no dependency tracking), looks the lib up as `a/b_c.cljc` then `.clj` under the roots of
  `clj_load_path_set` / `Runtime.loadPath` after the embedded libs (`<embedded>/clojure/set.clj` and
  friends, `libs_clj.inc`), records it in `*loaded-libs*` (an atom, not a ref) after a successful load,
  and fails with "namespace 'x' not found after loading" when the file defines no such ns. `ns` handles
  `:refer-clojure`, `:require`, `:use` and `:import`; `:gen-class` names a JVM class and expands to nothing;
  `:load` throws. No ns metadata
  (the docstring and attr-map are dropped), no `ns-unalias`, no `remove-ns`, no `*loaded-libs*` as a
  sorted set, no `load` of a classpath resource by path. A load error is rethrown as
  "Syntax error compiling at (file:line:col). <message>" with `{:file :line :column}` data and the original
  as the cause, like CompilerException. Namespaces are immortal like vars: tests create theirs before
  taking a baseline.
- **An import is a var, so neither backend learns anything new** (`import*`, builtins_ns.c; design §3 «"Наш хост" —
  это C-ядро»). The import table maps a name to the var holding the type: a program's `defrecord`/`deftype`
  brings its own var (`pkg` or `pkg` with `_` as `-`, the JVM's spelling of a record's package), and a bridge's
  answer is interned as `pkg/Name` and bound to the type, so the imported name, the qualified spelling and a
  compiled unit's var reference all reach one value. The bridges are a table tried in order: the host's
  resolver (`clj_host_type_named` of `pkg/Name`, what a qualified symbol and `catch` mean), then
  `clj_objc_class` by the bare name. A JVM package (`java`, `javax`, `jdk`, `sun`, `clojure.lang`) asks no bridge,
  because `java.lang.Object` would otherwise find ObjC's root class `Object`. The import runs when its form runs,
  so a name imported in a `do` is not yet resolvable in that same `do`, as on the JVM, where the class must
  exist at compile time. An import of a name already mapped to another var, interned or referred, is refused
  with the JVM's "already refers to"; one over a clojure.core name is not, since the core's fallback is no
  mapping and the JVM's core has no such var.
- [ ] **An ObjC class imported in a Swift host is the host type, not the class object.** The host's resolver
  answers first and reaches every ObjC class through its `So…C` mangle, so `(import '[UIKit UIView])` binds
  what `UIKit/UIView` means, which `catch` and `instance?` on a host error take but a `.method` send refuses;
  `objc-class` is the send's receiver. Trigger: code importing a class to send to it; then a host type whose
  mangle is `So…C` carries its `Class` and `clj_objc_send` takes it.
- **Var meta carries `:file`** when `*file*` is bound (a load); the host's `eval` and the tests bind none.
- **`set!` is a rewrite**, not a node: `(set! sym v)` becomes `(clojure.core/var-set (var sym) v)` in the
  analyzer, so it serializes as an invoke. A local target is "Cannot assign to non-mutable"; deftype
  fields are not assignable (no mutable fields).
- **Lenient loading** (`clj_load_set_lenient`) is the corpus harness's mode: a top-level form that fails to
  read or evaluate is recorded (`clj_load_take_failures`: `{:file :line :column :name :message}`) and
  skipped, so one missing function does not hide the rest of a library's gaps. Never on for a user.
- **`defmacro` on a failing body still interns the var** (analysis creates it before the fn is
  analyzed), as `def` does: the name resolves afterwards to an unbound var. Same as Clojure.
- [ ] **Declaration-order diagnostics** (design §4 "Порядок объявлений"). A file is analyzed one top-level
  form at a time and a forward reference is "Unable to resolve symbol", as in Clojure. Not done, until the first
  code written for pippin rather than ported: four diagnostics.
  - the hint "defined below at line N, move it or add `(declare foo)`" from a tolerant scan of the rest of
    the source; `clj_load_source` has the bytes, an nREPL eval of a region does not;
  - a reload or a REPL form resolves a reference above a `def` to the var the previous load left (ns.c
    resolves mappings first), so it works until a cold start; a file load should treat a var of this file
    not yet defined in this load (by `def` or `declare`) as unresolved, a REPL form should warn;
  - `clj_ns_intern` checks neither refers nor core: a `def` of a name referred from another ns should be
    JVM's "already refers" error, of a core name a warning naming the references above that meant core;
  - `(declare m)`, a call `(m 1 2)`, then `(defmacro m …)` calls the macro fn without `&form`/`&env`; the
    `defmacro` should warn.
- [ ] **`def` is eager and vars are plain roots.** No lazy thunk state (design §4 "Var и ленивые def").
  Trigger: the first ns whose load-time cost shows.
- [~] **Dynamic vars** (var.c): a per-thread stack of frames, each a persistent map var → box (a volatile)
  merged with the frame below, pushed by `push-thread-bindings` and popped by `pop-thread-bindings`
  (`binding` is the `try`/`finally` pair over them, `with-bindings*`, `bound-fn*` and `with-redefs-fn` are
  core.clj). `clj_var.thread_bound` counts live bindings across all threads, so a deref of a dynamic var
  looks a frame up only while someone binds it; a non-dynamic var's deref is unchanged except for one byte
  load and a predicted branch in `eval_borrowed` (bench: counting loop and closure call within noise,
  bench/RESULTS.md). Binding a non-dynamic var throws "Can't dynamically bind non-dynamic var: ns/x",
  `set!` on a var without a thread binding "Can't change/establish root binding of: ns/x with set". A
  binding's value is stored unshared: the frame belongs to one thread. Deviations: `with-redefs` swaps
  roots process-wide with no lock, as Clojure's does; the bound value is not shared, so a binding handed
  to another thread through `bound-fn` shares it only when that thread's frame stores it (a value
  published this way must be treated as shared by the caller — trigger: `bound-fn` across threads with a
  mutable graph, then `clj_share` in `push_entry`). No `*out*`, `*assert*`, `*flush-on-newline*`
  or the printer vars beyond `*print-length*`/`*print-level*` ("Printer"); `with-out-str` captures the output hook per thread instead.
- **Concurrent `def` against `deref` is unsafe**, and `alter-meta!`/`reset-meta!` against `meta` the
  same way: `clj_var_root`/`clj_var_meta` return a borrowed pointer and a racing writer releases the
  old value, so a reader may retain a freed one (`alter-meta!` is a CAS loop, so its `f` may run
  more than once under contention, as Clojure's); a fn root read at +0 by a call on one thread while
  another thread's `def` replaces it is the same race (the parked-root list is per thread). Redefinition
  is a dev-time operation; evaluate on one thread at a time while defining. Concurrent *calls* of one
  fn from several threads are fine, `extend` against them included (the protocol cache is built for
  it, `concurrentDispatchWhileExtending`). Same for `clj_ns_current` vs `clj_init` ordering: call
  `clj_init` before any evaluation.
- [~] **A side cell of an exec node is a shared mutable cell** (any slot written at run time: an inline
  cache, a cached transducer composition, specialization state, profile counters). It must hold an
  immortal value (filled once via CAS, `CLJ_FLAG_IMMORTAL` set before publishing, the loser freed before
  publishing; the leak is bounded by the number of forms, as with vars), be per-thread, or hold a shared
  value released through the epoch. An ordinary object with an ordinary release there is the concurrent
  `def`/`deref` race again. INTRINSIC keeps a retained (immortal) var and reads the root at evaluation;
  the protocol cache (type descriptor entry above) is the third kind: borrowed impls that only an epoch
  bump retires, a seqlock around the fill. The fusion pass (below) keeps nothing in a side cell: its
  per-form work measured too small to cache. Trigger: the var inline cache of the design.
- [~] **Var lookup is a root load on every evaluation** of a var node (an acquire load; an intrinsic's guard
  is a relaxed one), no inline cache, and no closure cache either: a closure's fn node carries its arity
  table, so the call path reads `fixed[nargs]` off the live closure — one load — where a cache keyed on
  the fn would have to be validated by that same load (and a retained key pins captures or cycles
  through recursion: a site in core.clj's `map` would hold the last `f`, a site in `f` its own exec).
  What a var cache could still save is the acquire load and the immortal/fn check, ~1 ns; trigger: that
  showing in a profile.
- **`apply` walks the trailing seq only as far as the callee can take it** (`clj_apply`, fn.c). A
  variadic callee's rest argument is handed over as the seq itself: `nargs` is `CLJ_NARGS_REST`
  (`SIZE_MAX`) and `args[nparams]` is that seq, so `(apply (fn [& args] 0) (range))` answers 0 instead of
  realizing forever. The sentinel needs no dispatch change — no fixed arity answers `SIZE_MAX`, and
  `n >= nparams` still picks the variadic one — and both backends read it through one helper,
  `clj_rest_args`, which `closure_run` and the emitted `_v` arity both call (compiler.c), so the lazy
  hand-over cannot reach one backend alone. Past `CLJ_FN_MAX_FIXED` (20) no fixed arity exists, so the
  walk stops there and what it took past the rest parameter is consed back in front of the seq; a callee
  with no rest parameter is walked one argument past its own ceiling, which tells a whole spread from an
  arity error, and the error says `(> 20)` rather than a count the reader cannot act on — Clojure's own
  wording, and for the same reason. A variadic *native* still gets a flat array, so `(apply str (range))`
  realizes, as it does on the JVM. Found by `clojure.test-clojure.vars/test-vars-apply-lazily`, whose
  `(future (apply sample (range)))` left a coroutine allocating for the rest of the process: every later
  library's second-run live count became the time the run took (12.5M objects), which is how the harness
  catches a runaway and not only a leak. `list?` of a rest argument depends on how the call was made,
  here and on the JVM both: a spread builds a list, the hand-over a cons chain.
- **The call path** (eval.c, `eval_invoke`, `run_frame`; bench/RESULTS.md, "Call-site caches"): a
  closure with a fixed arity for the call and a frame of at most `SMALL_SLOTS` (16) has its arguments
  evaluated straight into the frame on `eval_invoke`'s stack — no argument buffer, no copy — and the
  owned mask of that evaluation is the frame's, so a `recur` over a param releases what it should and
  teardown releases the rest. The guard reads its limit from the shadow stack (the one thread-local a
  call touches; computed on the thread's first call), then the shadow push, the instrumentation byte,
  the body, the pop (which drains parked fn roots at depth zero). ~7 ns per call including the
  dispatch of a one-intrinsic body; the guard, the shadow frame and the instrumentation byte are each
  within the run-to-run noise now. The generic path (`closure_run`) takes a variadic arity (the rest
  list is built from the buffer), a frame past 16 slots (heap) or 64 (every param retained), a native,
  a protocol method (its cache) and every call from a native or the host (`clj_closure_invoke_at`).
  A native that calls one fn per element (`reduce` and the slots behind it) prepares a `clj_call`
  once — a closure's arity, or a plain native's function pointer after its arity check — and enters
  `closure_run` per element without the type dispatch, the fn-kind switch and the arity search; a
  fn that does not take the count still goes through `clj_invoke`, which reports it (measured
  against `clj_invoke` per element in bench/RESULTS.md, "IReduce").
  A plain native (`CLJ_FN_NATIVE`) at the head — a builtin in a local, a captured slot, a param or a
  user var — is called from the site's borrowed argument buffer after the arity check `fn_invoke`
  would make (`call_native`): no protocol probe first, no `clj_invoke`, no type slot, no kind switch
  (~2 ns of the ~9 such a call cost; bench/RESULTS.md, "Direct native call"). Natives are leaves of
  the shadow stack, so a throw inside reports the same frames either way. A native with a context (a
  host fn, the fusion drivers' reducing fn), a keyword, a map or a vector at the head still go through
  `clj_invoke`; measured from the site too, the context native gained nothing, and an intrinsic-by-value
  variant (a reverse index from the builtin fn object to its table entries, the fixed-arity C function
  called without the `(args, n)` convention) was within noise or worse — the builtins already forward
  in one call. `apply` and every call from a native or the host are unchanged (`clj_invoke`). A let/loop-bound fn
  called only as a head takes none of these paths (next entry).
  Debug builds count per site the calls that took a fast path (`clj_debug_exec_ic_hits`) against the
  generic ones (`..._misses`); release builds count nothing. The site array is indexed by
  `clj_node.site`, the node's ordinal among the tree's INVOKE nodes, assigned with the ids (it fills
  the padding after `col`, so a node grew by nothing; `from_data` renumbers it too).
- [~] **Direct local fns** (optimizer.c `direct_pass`, eval.c `eval_direct_call`; design §6b item 7;
  bench/RESULTS.md, "Direct local fns"): a `let*`/`loop*`-bound `fn*` whose binding is referenced only
  as the head of INVOKE nodes — in the body, in later inits of the same binding vector, inside inner
  *direct* fn bodies (through the static link), and through its own name inside its arities — becomes
  a `DIRECT_FN` node: the let stores nil in its slot, every such INVOKE becomes a `DIRECT_CALL` that
  evaluates its arguments straight into a fresh frame (as the closure fast path does), links the frame
  to the defining one (`clj_frame.outer`, `depth` links up from the caller: 0 from the let body, 1 from
  the fn's own body, deeper from nested direct fns) and runs the body through the same `run_body` as a
  closure — guard, shadow frame (the DIRECT_FN node, so traces and the profiler name it as before),
  instrumentation, recur loop, owned-slot release. The body's free variables were analyzed as
  captures; the rewrite turns each `CAPTURED` read into an `OUTER {depth, slot}` read of the defining
  frame (a captured value of the definer stays `CAPTURED`: the direct frame shares its environment) and
  remaps the capture sources of closures made inside the body the same way (`clj_capture` has a kind
  and a depth). Decided in the optimizer on the analyzed tree, not on forms: macros decide what a use
  is (`(m (f 1))` may expand to `(map f ...)`), and not in the analyzer, which would have to analyze the
  init before seeing the uses. Escapes, each keeping the closure and its behaviour: the binding as an
  argument, a return value, a recur argument, a later init's value, a `loop*` slot any recur of that
  loop rebinds, a capture of an inner closure (including a `lazy-seq` thunk), a call with an argument
  count the fn has no fixed arity for (the runtime arity error stays), a variadic fn. Frame model: a
  frame per activation with a static link, chosen over "params as extra slots of the enclosing frame"
  because that grows the enclosing arity past 16 slots (heap frame, no direct entry for the enclosing
  closure) or 64 (every param retained) and needs a save/restore of the helper's slot range around
  every recursive call; the link costs one pointer per frame and an indirection per free-variable read,
  and recursion gets its own slots for free. An `OUTER` read is owned (retain/release), not borrowed:
  a fifth case in `eval_borrowed` made the switch a jump table in every inlined copy, +3 ns on every
  row. Serialized as `[:direct-fn name [arity+]]` (only as a let/loop init), `[:direct-call [slot
  depth] args*]` resolved by the decoder through a chain of binding frames, `[:outer [depth slot]]`;
  `from_data` checks the link depth and the slot against the chain's frames. Debug builds count direct
  calls (`clj_debug_direct_calls`).
  Who qualifies, over core.clj, the embedded libs and both corpora: of 68 let/loop-bound `fn*` inits,
  20 are direct — 10 in core.clj, 1 in medley, 9 in the suite's tests, none in the libs. At run time
  that is the recursive `up` of `update-in` and of medley's `update-existing-in`, `derive`'s `tf` and
  `with-redefs-fn`'s `root-bind`; the rest run at every expansion of `fn` (`psig`), `for` (`to-groups`,
  `emit`, `do-mod`), `doseq` (`step`), `condp` (`emit`) and `binding` (`var-ize`). Both backends use
  them (the compiled core emits `update-in`'s `up` as a direct fn). `update-in` on a 3-key path is
  6.7 % faster with the pass than without it — no closure per call, three direct calls — and that,
  not the synthetic rows, is why the mechanism stays (design §6b item 7; bench/RESULTS.md, "Direct
  local fns"). `derive`'s two calls are lost in its map work. The 48 that stay closures: 18 call
  themselves from a `lazy-seq` thunk (core's lazy `step`/`walk` helpers, and `for`'s `iter`), 18 are
  captured by an inner closure (`destructure`'s `pvec`/`pmap` among them), 12 are passed as a value
  (its `pb` among them), none is variadic. Expanded code adds nothing: `doseq` and destructuring bind
  no fn, `for`'s `iter` calls itself under `lazy-seq`, and `letfn` binds volatile cells whose fns are
  `vreset!` arguments, never a let init (no `for` occurs in either corpus).
  Triggers: a variadic helper in a profile (build the rest list from a buffer as `closure_run` does);
  a self-referencing `step` under `lazy-seq` in a profile (the thunk would need to reach the frame,
  which it outlives — that is a real closure); a `letfn` whose fns are only called, in a profile —
  then a `letfn*` form with every name in scope before the inits, and a greatest fixpoint that starts
  with every fn direct and demotes one with a non-head use or a reference from a demoted sibling's
  body; the demoted ones keep the cells, the direct ones lose their cell cycle. Not done now: all 7
  `letfn` fns of the corpora would be demoted (2 passed as a value, 2 calling themselves under
  `lazy-seq`, 1 captured by a `defn`, 2 variadic), so the form would buy nothing measured and leave
  `:second-run-live-objects` where it is.
- **The definition epoch** (epoch.h) is one process-wide counter bumped by every root bind (`def`,
  `defmacro`, boot, a host bind), every `extend`, every type creation (`deftype`, a reify site's first
  evaluation) and every `deftype` descriptor's death; `protocol-epoch*` returns it. The protocol call
  cache keys on it, and one bump anywhere invalidates every such cache in the process (they rewarm in
  microseconds, so there is no per-var epoch). Its load and bump are seq_cst so that a read inside a
  dispatch window pairs with a writer's bump-then-wait (Dekker), the same way the window pairs with
  the table publish. Meta changes do not bump it.
- **Immortal core roots.** Once core.clj is loaded, `clj_init` sets `CLJ_FLAG_IMMORTAL` on the root of
  every `clojure.core` var (natives, closures, protocols; type descriptors are skipped because
  `clj_is_user_type` reads the flag as "builtin"). Retain and release on them are no-ops, so
  `eval_borrowed` reads such a var at +0. A later `(def map ...)` in clojure.core "releases" the old
  root as a no-op — a bounded leak per redefinition, accepted — and binds an ordinary root, which reads
  owned. Roots bound after boot (user vars, a core var rebound from the REPL) are never immortalized.
- [~] **A replaced fn root is released once the thread is idle** (`clj_eval_retire_root`, eval.c). A fn
  root is read at +0, so `clj_var_bind_root` cannot release the old fn while a body on this thread
  may still be running it or holding it as a borrowed argument: while a closure frame is up (shadow
  depth) or a `clj_exec_run` is active, the old fn is parked on a per-thread list, drained when the
  last of the two returns to zero (one compare on the pop path; a throw unwinds through the same
  exit). At the top level of a form the def is inside `clj_exec_run`, so `(f (def f ...))` parks too.
  Consequences: the parking is unbounded while the thread stays in flight — `(dotimes [i 1e6] (def f
  (fn [] i)))` inside a closure holds 1e6 fns until it returns; a thread that exits mid-flight leaks
  its list; a `def` on another thread against a running call is the unsafe race the "Concurrent
  `def`" entry describes, unchanged (the list is per thread, there is no reader window on calls).
  Trigger for a bounded variant: a def loop showing in memory; then a drain at every closure return
  whose frame holds no parked root, or the epoch-based reclamation the design names.
- **The optimizer pass** (optimizer.c, `clj_optimize`) runs inside `clj_analyze` after analysis and before
  numbering: it is where "immutable after analysis" begins, so what it produces is what serializes and
  what every evaluator and emitter sees; `clj_node_from_data` does not run it (its input is already
  optimized). Two rewrites: `INVOKE(VAR core-var, args...)` becomes `INTRINSIC {op, var, args}`
  when the head resolved to the var an intrinsics entry names and the arity is listed, and a consumer
  over a nest of lazy stages becomes a FUSED node (the fusion entry below). Both are keyed by the
  var, so a local `(let [+ -] ...)`, a user namespace's own `+` or `(apply + ...)` are untouched; a
  `(clojure.core/+ a b)` anywhere is rewritten. Then constant folding and the last-use marks (the two
  entries after the intrinsics table).
- **Intrinsics table** (intrinsics.h/.c): `{qualified name, arity, kind INTRINSIC_1/2/3, C function, pure,
  consume}` for `+ - * /` (2 args), `inc dec`, `< <= > >= = not= identical?` (2 args), `not nil? zero? pos?
  neg? even? odd?`, the type predicates, `empty? first rest next seq count`, `cons get(2,3) nth(2,3) conj(2)
  assoc(3) dissoc(2) disj(2) with-meta(2) contains? set?`; a side table resolved at boot holds each entry's var and
  native fn. The rule, kept by structure: the builtin bound to the same var calls the same function —
  single-arity builtins forward, variadic ones fold (`b_add` is a loop over `clj_add`), and the five that
  consume their collection at the core (`conj`, `assoc`, `dissoc`, `disj`, `with-meta`) list that core as their
  `consume` form, the table function being the same call after one retain (the builtin fn object points
  back at its consuming entry, `clj_fn.u.native.consuming`, so `clj_call_prepare` finds it in one load: `swap!`
  prepares per call). Consequences: `(+ a b c)` boxes a double at every step where the
  old accumulator did not, and a fixnum fold that overflows mid-way throws where the old one could
  recover (`(+ MAX MAX (- MAX))`); Clojure promotes both. `IntrinsicsTests` crosses every entry with
  sample values of every type against `clj_invoke` and pins that core.clj rebinds none of them. `==`
  is not an entry because no builtin exists; `str`, `hash`, `second`, `meta`, `apply` are left out
  (variadic with no fixed core, throw on unhashable types, composed, or not one function).
- **Intrinsic guard.** `eval_intrinsic` compares the var's root (relaxed load) with the boot fn from the
  side table before every call; on a mismatch it derefs the var and goes through `clj_invoke`, so
  `(def + ...)` in clojure.core or a host rebind is semantically invisible, and binding the boot fn back
  restores the fast path. The epoch would cost the same load and needs a cache to compare against; the
  guard needs none. Cost per intrinsic call: the arg evaluation, two loads and a compare, one indirect
  call — no frame, no arity table, no var deref, plus a load and a branch on the entry's `consume` form.
- [~] **The specialized arithmetic node** (specialize.c, eval.c `eval_fix_*`, `eval_dbl_*`, `eval_fd_*`/`eval_df_*`;
  design §6b item 8, the first self-optimizing node; bench/RESULTS.md, "Specialized arithmetic" and "Specialized
  arithmetic over doubles"). `clj_exec_new` ends by deriving its tree under the process-wide dev store
  (`clj_specialize_store`: summaries with the caller join on, one lock, made on first use) and rewriting the exec
  entry of every INTRINSIC `+ - * / inc dec < <= > >= = zero? pos? neg?` by the kinds of its argument facts. Every
  argument int64 — fixnum or boxed long, never "fixnum" alone, which no loop variable is (the Facts entry above) —
  gives the fixnum entry: the fixnum tag of every argument and the boot-root guard are the whole check, then the
  operation runs inline on the untagged values with `arith2`'s overflow check and `clj_long_new`'s canonical re-tag
  (no `/`: an integer quotient may be a ratio). Every argument double gives the double entry: `clj_is_double` of
  every argument, then IEEE arithmetic into a fresh `clj_double_new` — `(/ 1.0 0.0)` is `##Inf`, every comparison
  with a NaN false, `=` included, exactly `arith2`, `compare2` and `double_equals`. A fixnum beside a double, the
  case the `Numbers` ladder makes a double, gives a mixed entry for `+ - * / < <= > >=`: one check of both tags,
  the fixnum converted as `arith2` converts; `=` between them is false by type and stays generic. Anything else,
  a boxed long included, takes `intrinsic_apply`, the generic path the plain entry runs. Two guards, each for a
  reason: the tag check makes a wrong or stale fact *slower, never wrong* — the host passes a double to a fn whose
  recorded callers pass fixnums, a caller at the REPL does, a `def` moved a join a moment ago — and the root guard
  keeps `(with-redefs [+ -] …)` visible, exactly as the plain entry does. A node whose operator var is rebound at
  derivation time takes the generic entry outright: its guard would fail on every call. The epoch the fact was
  derived under lives on the exec (`clj_exec_derivation_valid`: every var epoch the table read and every
  caller join it took, `clj_facts_valid`'s check) and is what re-derivation is *decided* by, not what a call
  checks: a call pays the tag bit and the root compare, nothing per epoch. The derivation also records the
  tree's fn-body sites in the reverse index; a var whose join that moved has its root closure's exec — or
  the recording exec itself, when it defines the var and the def has not run yet, or calls it recursively —
  re-derived from a worklist, at most 3 times per exec and 64 per trigger (the incremental interprocedural
  fixpoint: `(defn f [n] (f (dec n)))` settles in two, fixnum then int64), and the re-derivation writes
  every arithmetic entry afresh, which is how a stale specialization goes back to the generic entry: the
  fixture is a fn whose callers pass fixnums until a later `def` adds one passing a double, and
  `SpecializeTests` checks the entry and the results either side. **A root rebind pushes the same way**: the
  derivation indexes the exec under every var whose root it read (the dependents index, var → execs, kept with the
  derivation and dropped with it), and `clj_var_bind_root` ends with `clj_exec_root_rebound`, which queues the
  dependents on the same worklist under the same budget — so `(def inc …)` takes the entries of every user of
  `inc` back at the def, the boot fn bound again gives them back, and a redefined callee's callers read its new
  summary at once. `with-redefs` of an operator is two rebinds, so two pushes, each bounded by the 64 (a rebind of
  `+` under an interpreted core has hundreds of dependents; the rest keep their guarded entries). A dynamic var is
  not indexed: a thread binding is invisible to its epoch, so its root is no fact to rest on, and `*ns*` is rebound
  by every `ns` form. Cost of the push: +0.3 ms on the interpreted boot, nothing measurable on the corpus load.
  A rewrite from a re-derivation lands in a running exec as `clj_exec_count`'s does, at the next child dispatch,
  and `clj_exec_count(off)` puts the specialized entries back. Cost: the facts pass per exec, +3–4 ms on the 15 ms
  interpreted boot (the compiled core has no execs and pays nothing), under a second on the 26 s pool test suite;
  `clj_specialize_enable` turns it off (`CLJ_BENCH_NO_SPECIALIZE=1` is the bench's control). Measured, interpreted:
  the counting loop 15.3–16.2 → 12.6 ns per iteration with `(inc i)` alone specialized — `n` comes from the host
  and has no fact — and → 10.3 with the bound known from a def'd caller (`(< i n)` too); the accumulating loop
  24.9–25.2 → 18.0 and → 15.1; `swap! inc` 42 → 38 (the loop's `(inc i)`); a loop accumulating a double 36 → 28,
  what remains over the int64 loop being the `clj_double_new` per iteration a double result always is here; the
  accumulating loop with its bound from `(count v)` 26 → 16 (a count is a fixnum fact). `reduce +` does not move
  (5.6 ns): the reducer calls `clj_add` from C, there is no node. A dot product over two vectors through `nth`
  moves only by its counter: `nth` answers ⊤, the products are generic and their sum "a number" — trigger: an
  element fact for `nth`, which the lattice does not carry. What remains per iteration is the dispatch and the
  frame work the design names; the tag check is ~1 ns of the ~3 an intrinsic call cost.
- [~] **Constant folding** (optimizer.c `fold_intrinsic`/`fold_if`; design §6b item 4): after the intrinsic
  rewrite, children first, an INTRINSIC whose entry is `pure`, whose arguments are all CONST and whose var
  still holds the boot fn is called at analysis and becomes a CONST; an IF whose test is a CONST becomes
  its taken branch (the branch's contents move into the IF node, which the parent already points at; a
  missing else is nil). Every accessor entry is pure (`empty? first rest next seq count cons get nth conj
  assoc contains?`): on the data a fold admits they realize nothing and consume nothing (`conj`/`assoc`
  see a constant at rc ≥ 2 and copy). What folds is bounded by the codec twice over: the arguments must be
  values the codec reads back as the same type (`clj_node_foldable`: nil, booleans, numbers, chars,
  strings, keywords, symbols, vectors, maps and lists of those — a lazy seq, a vector seq, a fn or a var
  embedded by a macro is no input), and so must the result (`(seq [1 2])`, `(rest [1 2])`, `(seq "ab")`
  keep the call: a vector seq or a string seq would read back as a list). A fold that throws (`(/ 1 0)`,
  `(nth [1] 5)`, `(+ 1 "a")`, an overflow) drops the exception and leaves the node, so the program throws
  at run time from the same node with the same message. A var rebound *before* analysis keeps the guarded
  INTRINSIC; a rebind *after* it does not unfold, the same speculation Clojure's `:inline` makes and the
  guard on the unfolded calls does not cover. `with-meta` is not pure (its result carries meta the codec
  drops, and on a unique value it is the value). The var meta of `(def x (+ 1 2))` and the frames of a
  runtime error come from nodes folding never touches (FoldingTests). Trigger for more: a fold rule over
  `str`, `list`, `vector` (variadic builtins are not intrinsics), or `let`-bound constants (needs a
  substitution pass, not a local rewrite).
- [~] **Last-use reuse** (optimizer.c, the liveness pass; eval.c `eval_borrowed`/`eval_local`; design §6b
  item 4, the auto-transient; bench/RESULTS.md, "Last-use reuse" and "Growing a collection per step"): the
  last pass of `clj_optimize` flags a LOCAL read after which its slot is dead on every path
  (`clj_node.u.local.last`, serialized `[:local slot :last]`), and the evaluator then hands the frame's own
  reference to the consumer instead of borrowing it: the value enters the argument array with its owned
  bit set, the slot is niled and its owned bit cleared, so teardown, a recur's rebind and a debug reader
  see nothing there; a slot the frame only borrows (a fixed param, the self slot) reads as before. A
  consuming intrinsic whose collection the site owns — a last-use local, or a nested result such as the
  inner `(conj (conj v 1) 2)` — calls the entry's `consume` form and drops the bit from the mask, so
  `clj_conj`/`clj_assoc_owned`/`clj_dissoc_owned`/`clj_disj_owned`/`clj_with_meta` see rc 1 and update in place (this is
  the first in-place store reachable from interpreted code; NOTES "RC" checks in debug builds that the
  children of a shared object stay shared). Liveness is backward over the evaluation order of one frame (the
  top level, each fn arity, each direct fn arity), on bitsets of the first 64 slots (a higher slot is
  never marked), with these rules: a `loop` body and a fn body with a `recur` are a fixpoint, a recur's
  live-out being the body's live-in minus the slots it rebinds, so a loop var is a last use where nothing
  reads it later on its path — the recur arguments included, `(recur (conj v x) (inc i))` — and a local
  bound outside the loop is live across the recur and dead only on the exit path; `if` branches are
  separate paths (`(if t (conj v 1) v)`: both last); a `let` kills its slot before its init (a shadowing
  `let` is another slot); a closure capture is a read at the closure's creation and its body a frame of
  its own (a captured value is at rc ≥ 2 by the time the definer's last use runs, so the core copies); a
  direct fn body reads the definer's slots through the static link whenever it is called, so every such
  slot is pinned live for the whole defining frame (the definer never hands one over, `(let [v [1] f (fn
  [] (count v))] (let [w (conj v 2)] [(f) w]))`); a `try` body keeps everything its handlers and `finally`
  read live at every point, since any point may throw, and the catch slot is a frame slot like any other;
  a direct local operand of a call or a literal is borrowed until the call completes, so nothing inside a
  later operand may hand that slot over (`(assoc acc i (conj (nth acc i) x))` reads `acc` at +0 in the
  first operand and must not free it in the third: the slot is *held*, not live, so the sets stay exact
  and only the mark is withheld). Only reads whose consumer can own the value are marked: the collection
  of a consuming intrinsic, a let init, a recur argument, a body's value; an `inc` argument, a literal
  item, an `if` test or a call argument is marked nothing, since the hand-over costs the slot write, the
  mask bit and the release loop for a +1 nobody uses (measured on the counting loop: every last use marked
  cost +2.5 ns per iteration, call arguments alone ~2 ns per call). The mark is a flag inside the LOCAL
  case of `eval_borrowed`, force-inlined in optimized builds: a node kind of its own dispatched through
  the exec table cost +3 ns per iteration, and the flag with the inlining left to the compiler +5.5 (it
  stopped inlining and emitted a call per borrowed read). Marking is one final pass per frame with the
  converged sets; each loop's fixpoint re-runs the loops inside it, ~3^depth passes over a body.
  The drivers: `clj_call_prepare` records the consuming entry when the fn is its boot builtin, and
  `clj_reducer_step`/`step_kv` and the fusion bottom hand their own +1 to it and take the result back as
  the new one (`(reduce conj [] xs)`, `(reduce-kv assoc {} m)`, `(reduce conj [] (map f xs))` fused), so
  the accumulator grows in place from the second element on (the init and a seed are shared with whoever
  passed them: one copy). `(into to xform coll)` runs through the `fused-into*` driver under `[xform]`, so
  it matches `(vec (map ...))`. Still copied, each with a reason: a user fn as the reducing fn (its param
  is borrowed from the reducer: `(reduce (fn [a x] (conj a x)) [] xs)` copies every step — a hand-over
  there would need the callee's frame to own the param, the call-argument variant above); `conj` through
  `apply` or any native other than the builtin itself (`clj_apply`'s `all[]` is +0); a collection held by a
  var (`(def v [1])`, `(conj v 2)`: the var's root is read owned at rc ≥ 2); a `(conj v x)` inside a `try`
  whose handler reads `v`; a call argument (`(f v)` then `(conj x 1)` in `f`: the param is borrowed); the
  `to` of a fused `(into to P)` (the driver retains it once: one copy per form, then in place); a
  captured or var-held value, by rc. Triggers: a profile with a collection built through a helper fn per
  element (mark call arguments, ~2 ns per call); `transduce` with a user rf over a collection (the same
  +0 rule); a frame past 64 slots growing a collection (the bitset).
- [~] **The fusion pass** (optimizer.c, fusion.c, `CLJ_NODE_FUSED`; bench/RESULTS.md, "Fusion"): `(reduce
  f [init] P)`, `(into to P)`, `(vec P)` and `(count P)`, where `P` is a nest of `map keep filter
  remove take drop take-while drop-while mapcat map-indexed keep-indexed interpose dedupe distinct` calls — each
  at its lazy arity, `map`/`mapcat` with one coll, every head resolved to the `clojure.core` var — over
  any source, become one FUSED node: the argument expressions (the consumer's, then each stage's own
  from the consumer outwards, then the source) evaluated once in the original order into a frame of
  their own, a guard, and two programs over those locals — the fused one calls a driver native with
  the stages' transducer arities in a vector literal (`(fused-reduce* f coll [(map g) (filter p)])`,
  `fused-into*`, `fused-count*`; `vec` is `fused-into*` onto `[]`), the original one is the consumer
  call as written. Serialized as `[:fused [vars] [args] fused original]`; the two programs' slots are
  bounds-checked against the args, not the enclosing frame. The guard: every core var the two programs
  name (consumer, stages, driver) still holds the root it had when core.clj finished loading, recorded
  by `clj_fusion_install` in a static table the node points into, as an INTRINSIC points into the
  intrinsics table — relaxed loads, no exec-side state, no epoch, and nothing to recompute for an exec
  built after a rebind; `(def map ...)` in clojure.core or a host bind sends the site down the original
  program, binding the boot root back fuses it again. The drivers keep the accumulator in C and hand
  the transducers `nil` as `result` (and `(into to xform coll)` runs the same driver, above), so the seq rules of `reduce` hold exactly: a 2-arity seeds with
  the first *output* and answers `(f)` when there is none (an `eduction` would seed with `(f)` and
  break `(reduce (fn [a x] ...) (map ...))`), a reduced init or first element is data, only `f`'s own
  reduced result stops the walk (a `(reduced nil)` the stack passes up); `fused-into*` conj's an
  accumulator only it holds, so a vector grows in place. Deviations from the lazy form, both in
  `FusionTests`: the driver seqs the source up front, so `(reduce + (take 0 5))` throws where the lazy
  `take` never touched the `5`; and `partition-all` is not a stage, because its transducer emits
  vectors where the lazy arity emits seqs and `conj` on a 2-arity seed tells them apart (trigger: a
  `(map seq)` tail behind it once `counted?` and the type name in error messages may differ). Not
  fused: a multi-coll `map`/`mapcat`, a head that is a local or another namespace's var, a pipeline
  consumed by anything else (`first`, `seq`, `doall`, a value position), a consumer whose coll is not
  a stage call; a pipeline as the source of another is fused on its own. core.clj itself is analyzed
  before the table exists, so nothing inside it is fused (the validator re-analyzes it after boot and
  sees FUSED nodes; they round-trip). No composition cache: the transducer stack is rebuilt per
  evaluation and the whole per-form cost is ~0.3 µs at n = 10, of which building `(map g)` is ~15 ns —
  the `(xf rf)` application must be fresh per run anyway (stateful transducers) — so the design's
  exec-cell cache (CAS fill, immortal winner) has nothing worth its guard. Triggers: a profile with
  fused forms in a hot loop over tiny collections (the per-form cost); consumers `some`/`every?`/
  `run!`/`doseq` (a reduce with early exit; design §6b item 9); a last-use source of `mapv`/`into` (the result
  written into the source's nodes, design §6b item 10); stages inlined into one C loop in compiled code (it calls
  the same driver today, design §6b «Итераторы Rust»); the barriers `sort`/`group-by` (cut a pipeline today);
  multi-coll `map` (a multi-source driver); `partition-all` (above).
- [ ] **C stack per Clojure call is large.** A call is several C frames with slot and argument buffers on
  the stack (the direct path inlines the frame setup into `eval_invoke`, whose 16-slot buffer is the
  callee's frame; the generic path adds `closure_run` with its own 16 slots): on the order of 0.6 KB
  in a debug build, ~2.3 KB under ASan, ~3.7 KB under UBSan, measured before the direct path. On Swift
  Testing's 512 KB threads that is ~600 / ~170 / ~100 nested non-tail calls before the guard throws
  "Stack overflow" (the guard reads the thread's real bounds on Apple platforms; elsewhere it assumes
  512 KB). Fix: frames on the heap and fewer C frames per call (the shadow stack records frames, it
  does not hold them). Tests keep non-tail recursion depth ≤ 50.
- [ ] **The stack guard has no host fallback.** `pthread_get_stackaddr_np` is Apple/BSD; other platforms
  get a fixed 512 KB assumption measured from the first call, and the guard page of compiled code
  (guard.c, `getsectiondata`, the Mach-O `__cljframe` section) is Apple-only outright: elsewhere a
  compiled overflow is a plain crash and traces carry no compiled frames. Trigger: a Linux port.
- **Vars are immortal.** Every `def` of a new name leaks a var, its name symbol and string for the
  life of the process, as do namespaces; tests declare their vars before taking live-object baselines.
- **Analysis error messages are capped at 512 bytes** (`fail` formats into a fixed buffer): a huge
  unresolved form is truncated in the message.
- [ ] **Nodes are pool objects with a 14-arm union**, so a `const` node pays for the fn arity table.
  Trigger: memory of a large loaded program. Fix: per-kind sizes via `clj_alloc(size)`.

