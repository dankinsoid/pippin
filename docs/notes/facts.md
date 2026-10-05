## Facts (Sources/CljCore/facts.c, summary.c, include/clj/facts.h, summary.h)

- **A `CLJ_DEAD_REFINED` ⊥ is explained, and the report counts it apart.** `clj-facts` credited only
  `CLJ_DEAD_LITERAL` as a dead branch and called the other cause unexplained, which its watchdog fails on;
  the first one the corpus produced was Clojure's own `(sequence nil)` and `(sequence [])` as the only two
  recorded callers of `sequence`'s 1-arity, whose join of nil and vector the `(seq? coll)` branch excludes
  with neither side pinned (docs/facts-coverage.md, "Dead branches"). An unexplained ⊥ is now only a
  `CLJ_DEAD_NONE` one, which is what "the lattice is wrong" meant.
- **The coverage report is committed, what a run costs is not.** `clj-facts` writes `docs/facts-coverage.md`
  and, given a third argument, a cost report beside it (`make facts-report` names `$(BUILD_ROOT)/facts-cost.md`,
  which git does not see). The split is not tidiness: `make facts-report` is a gate, so a committed file holding
  wall-clock dirtied the tree on every gate run, and the numbers moved two- to threefold with machine load —
  which hid a real count move inside noise nobody read. What stays in the committed report is what is the same
  on every machine: forms, nodes, table bytes, the summary and round counts. A cost worth keeping goes to
  bench/RESULTS.md from a deliberate run, as every other measurement does.
- **A side table, built on request, never on the way through.** `clj_facts_of(root)` walks the analyzer's
  optimized tree and returns `clj_facts`, `nnodes` entries indexed by node id exactly as `clj_exec` is.
  Nothing calls it: `clj_analyze`, `clj_exec_new` and the compiler are untouched, so the pass costs zero
  until a consumer asks. The tree stays `const` — no fact is written into a `clj_node` — and the table is
  plain `malloc`, not a pool object, so building facts never perturbs the allocator a test is counting.
  The root is retained, because a singleton fact borrows the constant it names.
- **Pure by construction.** `clj_facts_of` reads no var root, no epoch, no protocol table and no thread state,
  so the same tree always yields the same table (`pureAndSideTableOnly`). That is what makes a fact cacheable
  beside a serialized tree later, and it is also the one rule that decides every "⊤ or not" question below.
  `clj_facts_of_with(root, store)` is the same walk with a summary store consulted at call sites and var reads
  (the bullets from "Summaries" on); such a table is runtime state, records every var it rested on with its
  epoch, and `clj_facts_valid` answers false once any of them was rebound.
- **The lattice.** A type fact is a set of 27 kinds — nil, bool, fixnum, boxed long, bigint, ratio, decimal,
  double, char, string, keyword, symbol, seq, vector, map, set, sorted-map, sorted-set, record, array, fn,
  var, atom, uuid, inst, regex, host — plus an optional singleton (the constant itself), an array element
  kind and a record/host descriptor. ⊤ is every bit, ⊥ none; `join` is `|`, `meet` is `&`. Sorted maps and
  sets are separate bits so that the false branch of `(map? x)` may subtract exactly what `map?` answers
  true for. A set of more than four members widens to ⊤ (design §3, "полиморфизм ≤4 shape'ов"), the six
  numeric kinds counting as one member — the design's ladder is int/double/number/⊤, so `(+ a b)` must be
  able to say "a number" rather than collapse.
- **Nullability is its own two-bit lattice** (`never`, `always`, `maybe` = ⊤, ⊥), joined and met alongside
  the type. It is not derived from the nil bit because the union cap destroys that bit: `(if x …)` leaves
  the true branch with ⊤ minus nil, which is 26 kinds and widens straight back to ⊤, while the separate
  nullability survives as `never`. Over library code the type is known for 50 % of value nodes and
  nullability for 51 %, and the second number is the one that holds up where the first does not.
- [~] **Escaping is per local slot, per frame**, three values joined by max: `local`, `captured`, `escapes`.
  A frame is the top level, one fn arity, one direct fn arity or a fused node's argument frame; the table
  records each with its body's id range, so `clj_facts_frame_of(id)` finds the innermost. A slot escapes when
  it is an argument of any call the signature table does not mark as storing nothing, an item of a vector,
  map or set literal, a `def` init, a thrown value, a fused argument, or the value of its frame's body
  (returned). A closure capture marks it `captured`, and so does a read through the static link (an `OUTER`
  node in a direct fn body, or a closure made there capturing an outer slot): the slot is read by address,
  which is what bars its promotion to a C variable, so the definer's frame is charged, `depth` links up.
  `(let [b a] …)` records an alias edge and the escape of `b` flows back to `a` at the end of the frame.
  Unknown is `escapes`. The three levels are joined by max, so a consumer cannot tell "the value leaves"
  from "the slot is read by address" once both happened; a bitset would, and the trigger for it is the
  compiler needing the distinction (it does not today: `escapes` bars promotion as well).
- **Sources of a type fact.** Constants (a singleton of their kind, by pointer identity, so two literals the
  reader made separately stay two singletons, exactly as the codec keeps them); vector/map/set literals and
  fn literals; the signature table below, for intrinsics and for named C builtins; `let` bindings; `loop`
  variables (the widened join of the init and every recur argument); `try` (the join of the body and every
  handler); a variadic rest parameter (a seq or nil); a closure capture (the fact of the captured slot at
  the moment the closure is made, which is sound because a capture happens once); the self slot of a
  named fn (a fn). Everything else is ⊤.
- **Refinement is on predicates, not on `if`** (design §3, occurrence typing). A predicate call over a local
  yields a `refinement` — the target slot, the narrowed fact and whether the predicate is true for *exactly*
  that set, which is what lets the false branch subtract rather than only the true branch meet. `nil?`,
  `some?`, `string?`, `map?`, `set?`, `vector?`, `number?`, `integer?`, `fn?`, `record?` and the rest carry
  one; `not` flips it; `=` against a constant pins the singleton; `instance?` refines against the canonical
  builtin type names of proto.c. A bare local as the test refines on truthiness alone (only nil and false
  are falsy). `when`, `cond`, `and`, `or`, `if-let`, `when-let` are macros over `if` and `let*`, so they need
  no rule of their own — with one catch that cost a bug: `(and (map? x) …)` expands to `(let [t (map? x)]
  (if t …))`, so the test of the `if` is the temporary. A `let` binding therefore records the refinement its
  init carries, and a test that reads such a slot applies it; rebinding a slot retires every refinement taken
  over its old value.
- **Loops: fixpoint, then widening after N = 3 rounds.** A loop variable starts at its init and joins every
  recur argument; the body is re-run with recording off until nothing changes. N is 3 because the height a
  variable can climb without widening is singleton → one kind → two → three → four → ⊤, and the loops that
  matter settle in one: `(loop [i 0] … (recur (inc i)))` reaches fixnum|long on the first round and stops on
  the second. Three rounds leave one round of headroom for a join that alternates, and bound the work at
  three passes over a body — the same order as the liveness fixpoint next door in optimizer.c. Anything still
  moving after the third round goes straight to ⊤, which terminates by construction. Over the whole corpus
  the rule fires on nothing; the test that exercises it rotates four values of four kinds through one loop.
- **What pass 1 alone leaves at ⊤, and why.** Every `INVOKE` whose head is not a core var the signature
  table names, every `DIRECT_CALL`, every var read, every `OUTER` read, every fn parameter — pass 1 has no
  function summaries and may not read a var's root. The summaries below take the calls, the var reads and the
  direct calls (over library code known types go 50 → 69 % of value nodes, 30 → 57 % of computed ones,
  docs/facts-coverage.md). What stays ⊤ after them: a **parameter's own fact** — a summary constrains a
  parameter by requirement (what the body needs), never by what callers pass, so `(inc n)` on a parameter is
  "a number", not a fixnum; the caller join below is what closes that from the other side. `OUTER` reads,
  captured slots of a callee (⊤ inside its body), a `(:k m)` on a parameter, and everything behind `deref`.
- **⊥ means one of two things, and they are told apart.** A meet that contradicts bumps `clj_facts_conflicts`
  and the branch below it is marked `unreachable` on every node with the cause (`clj_dead`): `CLJ_DEAD_LITERAL`
  when the test decides on a pinned value (`literal_test`: the slot it reads or refines holds a singleton or is
  exactly nil — a let-bound literal, an `and`/`or` temporary, a var whose root is nil —, or the test is `(= x
  <const>)`), `CLJ_DEAD_REFINED` when two refinements alone exclude each other; a node is legitimately ⊥ when a
  `throw` or a `recur` is the only way out of it. Anything else — a ⊥ value node that is neither in a literal's
  dead branch nor behind an exit — is a wrong signature in this file: `make facts-report` counts it per library
  ("Dead branches" in docs/facts-coverage.md, the unexplained column computed by the pass's own class, not by a
  list of known sites) and fails on any, as does `FactsTests.noContradictionOverCore`. That counter is what
  found the two bugs this pass shipped with: the complement of a `maybe` nullability subtracted everything, and
  a loop's fixpoint rounds counted conflicts against variables not yet widened. Over the corpus the 64 conflicts
  are all `dead-branch` (`(and nil true)`, `(when-let [x [0 1 2]] …)`, `(ratio? x)` after `(= 1 x)`, `(or
  *assertion-pos* …)` with the root nil).
- [~] **Signatures live in facts.c, not in the intrinsics table.** `clj_intrinsic` is the C-call contract the
  compiler emits against and the differential test crosses; hanging a lattice column on it would tie the ABI
  to the fact kinds and force every future fact into that struct. More decisively, half of what is worth
  annotating — `str`, `keys`, `vec`, `re-pattern`, `int-array` — has no intrinsics entry at all, so only a
  table of its own can hold both. It is keyed by the unqualified name in `clojure.core` plus the arity, with
  rules for arithmetic (`fixnum + fixnum` is fixnum|long, since an overflow throws rather than promoting;
  a ratio operation may normalize back to an integer; `/` on integers may answer a ratio), for `conj`/`assoc`
  (the collection's own kind, `nil` included), and for the identity-on-type calls (`into`, `with-meta`,
  `empty`). Matching on the *var* rather than its root is a speculation: `(def str …)` in `clojure.core` would
  make `(str x)` no longer a string, where an intrinsic node has a runtime guard and this has none. Nothing
  reads the table yet, so the speculation costs nothing today; trigger for fixing it: the first consumer that
  changes generated code on a builtin signature, which then needs the same boot-root guard `eval_intrinsic`
  makes.
- **The table's shape and cost.** 24 bytes per node (type set, nullability, array element kind, unreachable
  flag, singleton, descriptor) plus one byte per frame slot and one `clj_fact` per loop variable: 337 KB for
  all 264 forms of core.clj, 13 KB for its largest single form, 262 KB for the largest form in the corpus
  (a `deftest` with hundreds of assertions). Building it costs 0.31× the analysis of the same forms for
  core.clj and 0.10× for medley (bench/RESULTS.md, "Facts pass"); the ratio falls as forms grow because
  analysis pays for macroexpansion and the facts pass does not.
- **Summaries (summary.c): pass 1 bottom-up over var roots.** A `clj_summary` is one arity of one function:
  what the body *requires* of each parameter, the result fact, the effect set, and where each requirement
  came from (the line and column of the use, for the two-position message). It is computed by walking the
  arity's body once with the parameters at ⊤ (`clj_facts_walk_arity`, the same walk with recording off, so
  nested closures are not entered and nothing is stored) and is keyed by the var for a var-bound fn — the root
  is read, and a closure root carries its fn node — and by the arity's node identity for a direct fn (a
  let-bound fn only ever called). A native without an annotation has no summary; a compiled closure
  (`-DCLJ_COMPILED_CORE`) is a native too, so under the compiled core every core fn's summary is its annotation
  alone. Summaries are borrowed from the store and valid until the next call into it; direct-fn entries are
  dropped at the end of every table (`clj_summaries_forget_arities`), because a pointer into a tree must not
  outlive the table built over that tree.
- **Requirements: the meet of the uses along a path, the join across branches.** The environment carries a
  `req` per slot beside the fact. A call whose callee has a requirement for the position meets it into the
  argument's slot; `(if t a b)` joins the two branches' requirements, so `(if flag (inc x) (name x))` requires
  "a number or an ident or a string" of x and `(defn f [x] (if (string? x) (subs x 1) (inc x)))` requires
  nothing that the refinement did not already prove — the join, not the meet, is what keeps a use inside one
  branch from becoming a false conflict for the other. A `try` body requires nothing past the try (a throw
  skips the rest), a loop body's requirements on the outer slots hold (it runs at least once), a rebound slot
  forgets. A requirement keeps every kind it names — `clj_fact_meet_wide`, no union cap — because "seqable"
  is ten kinds and would widen to ⊤ as a fact; it is never stored on a node, only met against one, and the
  meet's result is capped as usual. So `(count 1)` is a proven conflict although no node can hold "seqable".
- **The fixpoint and its bound.** A call of a var whose entry is being computed answers the entry's optimistic
  value (result ⊥, no requirement, no effects) and marks it recursive; the root of the cycle then re-walks
  until the summary stops changing, at most N = 3 rounds, then widens (parameters ⊤, result ⊤, effects
  everything). N is the loop rule's N for the loop rule's reason: the height a fact can climb is singleton →
  one kind → two → three → four → ⊤, so one round settles the common recursion (`(fact (dec n))`: the result
  is a number on round one and stays), and three bound the work at three walks of a body. Entries computed
  *above* a fixpoint in flight (mutual recursion: `odd?` inside `even?`'s round) rest on an optimistic value
  and are transient — not cached, recomputed by the root's next round — or `odd?` would be cached as
  "always false" from the round that saw `even?` at ⊥. A finished summary never answers ⊥ (a body that never
  returns normally is ⊤ at call sites, so the ⊥ watchdog keeps meaning "the lattice is wrong") and never a
  singleton (it would borrow a constant of the callee's tree, which only the callee's root keeps alive).
- **Budget: the walk depth, not only the nesting.** A nested summary walk shares the C stack with the walk that
  asked, and a sanitizer build's frames are large (UBSan overflowed at ~45 nested nodes over the 512 KB of a
  test thread). So: every var a tree calls is summarized *before* its walk, at the top of the stack
  (`warm_summaries`), at most 6 summary computations nest, and a summary walk stops descending past 40 nodes
  of total depth (`CLJ_FACTS_MAX_WALK_DEPTH`) and answers ⊤ for the subtree — the recording walk is never cut, and
  neither are its loop-fixpoint rounds nor the self walk of the caller join (`pass.bounded` marks the store's walks
  alone; the first version keyed the cut on `!record`, which cut a recording walk's own rounds at depth 40).
  A summary can therefore depend on how deep it was first asked for; the warm phase makes the first ask
  shallow for everything a tree names directly.
- **Pass 2: at every call site the caller's fact meets the callee's requirement.** The meet is the argument
  node's fact from then on (and the slot's, when the argument is a local), which is how a receiver, a
  `(keys m)` and a `(zero? n)` narrow the parameter behind them for the rest of the body. A meet down to ⊥ is
  a *proven conflict*: recorded on the table as a `clj_diagnostic` with both positions — the argument at the
  call site and the use inside the callee that imposed the requirement (or "requires", when it came from a
  declaration or a protocol table) — rendered by `clj_diagnostic_message` as "user/sum-need uses argument 0
  as nil|map|sorted-map|record at 3:14, vector is passed at 1:8". The argument keeps the caller's fact:
  storing ⊥ would trip the watchdog, and the watchdog is worth more.
- [~] **Diagnostics: the ladder of design §3 "Строгость", by strength of knowledge.** One struct, two severities.
  *Errors:* a call-site ⊥ (`CLJ_DIAG_CALL_CONFLICT`) and a `:=>` declaration the body contradicts
  (`CLJ_DIAG_DECL_CONFLICT`) — a runtime failure shown early, in every mode. One refinement of the rule: a
  site ⊥ inside a `try` body with a handler is the failure the code expects, not one it suffers (`(is
  (thrown? (keys 1)))` is the corpus's whole population, 35 sites), so it is reported at warning severity with
  `caught` set. *Warnings, on by default, off per namespace:* ⊤ meeting a *declared* requirement
  (`CLJ_DIAG_TOP_INTO_DECL`) and a body answering ⊤ where its declaration promises something
  (`CLJ_DIAG_TOP_RESULT`) — a declaration is an explicit ask to be told this. ⊤ against an *inferred*
  requirement is silent (`:strict` would turn it on; not built). The switch is the namespace's meta:
  `{:facts/warnings false}`, which the `ns` form's attr-map sets (`(ns app.core {:facts/warnings false} …)`)
  and `alter-meta!`/`reset-meta!` on a namespace object change; namespaces got a meta slot for it, as they
  have in Clojure. Over the corpus the declarations on 17 core vars raise 82 such warnings — `(inc x)` on a
  parameter is "nothing is known about what is passed" — which is the cost the design names of declaring a
  builtin: the ask is the core author's, the warning lands on the caller; the per-ns switch is the answer it
  gives. Nothing halts on any of this: `make facts-report` exits non-zero on an error (the corpus gate) and
  that is all; wiring an error to stop a load or a compile is a later trigger, once the gate has been green
  long enough to trust the lattice. The table's `clj_facts_nerrors` and the store's `clj_summaries_nerrors`
  are what a consumer would gate on.
- **The signature table must be sound, not merely precise: a false ⊥ is now a false error.** Audited with
  that eye, these entries were weakened: `list*` answers seq or nil (`(list* nil)` is nil); `map`, `mapcat`
  and `partition-all` are seqs at two arguments or more only (one argument is a transducer, a fn); `into`
  follows the conj rule (`(into nil xs)` is a list, not nil); `dissoc` on a record answers record or map (a
  basis key drops the record); `empty` answers nil for anything that is not a collection, a string included;
  `assoc-in` answers a map or a vector; `int?` is exact on fixnum and long only (`(int? 1N)` is false, so the
  false branch must not subtract bigint); `sequential?` and `coll?` are no longer exact (a queue is both and
  is kind seq). Everything else stood: the numeric rules already promote through ratio and decimal, `/` on
  integers may answer a ratio, and the kind of a deftype that implements `IFn` is host, not fn, so `fn?`'s
  exact refinement holds. The corpus gate (`make facts-report`, `noContradictionOverCoreWithSummaries`) is
  what keeps this true from here on: zero errors over core.clj, the embedded libs and both corpora.
- **Var reads and roots, guarded by a var epoch.** `clj_var` counts its root binds (`clj_var_epoch`, 0 while
  never bound, bumped by `clj_var_bind_root`); the summary layer reads roots and metas freely and records
  every var it read, with the epoch it saw, on the entry being computed and on every entry in flight below it
  (a caller's summary rests on what its callees read), and on the facts table. `clj_summary_of_var` recomputes
  an entry when any of its recorded vars moved; `clj_facts_valid` answers false for a table in the same case.
  A var read in a walk with a store answers the *kind* of its root and its descriptor, never the singleton —
  the root may be rebound and only the epoch guards it, and a consumer that constant-folds a root is a bigger
  speculation than the design's inline-cache-with-epoch. What invalidation does today, exactly: a `def` bumps
  the var's epoch and the process epoch, nothing else; no store is told, no table is told. The next lookup of
  that var (or of a summary that read it) recomputes that entry alone; a table built before is still readable
  and `clj_facts_valid` is what a consumer must check before trusting it. The interpreter therefore stays
  incremental (a `def` costs one increment), and there is no whole-program recompute anywhere yet — the
  compiler's closed world would run the same store over the whole set once. Past 24 recorded vars an entry
  falls back to "valid while the process epoch stands", which every `def` moves; a protocol method's entry
  rests on the process epoch by construction.
- **Protocol receivers: the requirement is the join of the implementors' kinds.** A protocol method's summary
  requires of its receiver the join of the kinds of every type in the protocol's tables: the immortal ones are
  enumerated from proto.c's side table (`clj_proto_each_immortal`), and user types are two counters on the
  protocol (`user_types`, `user_records`, bumped by every extend of a deftype or a record, defrecord's initial
  extends included; record.c sets the record bit before those extends so they count right) — the live ones
  are also enumerable (`clj_proto_each_user`, the compiler's CHA), but the counters stay the summary's source,
  since a dead type's extends still say what the protocol was written for. One deftype implementor is therefore "host", one kind: known. `Object` or a core interface
  extended, or nothing found (a reify-only protocol: its types are user types too, but the counter is what
  keeps them), makes the requirement ⊤. The corpus's 26 receiver sites all go known this way, 16 of them
  `defmethod` expansions where the receiver is the multimethod's var (a var read: host, MultiFn's descriptor)
  and the rest parameters narrowed by the method's requirement (`(-add-method mf …)` inside a `defmulti` helper).
  `clj_facts_kind_of_type` says host for any deftype whatever interfaces it implements: `(fn? x)` is false on
  a deftype with `IFn`, so it must not be a fn.
- **Records: `(new* T …)` and `(record-map* T m)` on a type's var answer the kind with the descriptor.** The
  constructor bodies `defrecord` and `deftype` expand to; with a store the walk reads the var's root, and when
  it is a user type the result is record (or host) with `desc` set. `->Foo`'s summary is thus "a Foo", and
  `(let [m (->Foo 1)] (:k m))` sits on a known record (`SummaryTests.recordConstructorResult`). The corpus has
  no such lookup — its 69 `(:k m)` sites are 58 on locals (parameters, constrained by requirement only, and
  `(:k m)` requires nothing) and 11 below derefs and other calls — so the report's record row stays 0 → 0 for
  want of a site, not of a mechanism.
- **Effects, as far as they fall out.** Six bits, `alloc`, `throw`, `io`, `atom` (atom-write), `park` and
  `opaque`, joined up the walk: a vector, map, set or fn literal allocates; `throw` throws; a `def` is
  everything (registration); a known core call takes `clj_facts_core_effects` — nothing for a predicate,
  `not`, `identity`, `boolean`, `meta`, `type`; io for `print`/`println`/`slurp`/…; atom for
  `swap!`/`reset!`/`alter-var-root`/…; park for `chan-take*`/`chan-put*`/`chan-alts*`/`chan-deref*`/`sleep*`;
  alloc|throw for the rest of the named list; `CLJ_EFFECT_ANY` for anything unnamed — and a callee with a
  summary takes the summary's. A closure body's effects are its own, not its definer's. `clojure.core.async`'s
  `<!`/`>!`/`<!!`/`>!!`/`alts!`/`alts!!` and `Thread/sleep` are named as well (`is_park_var`), although their
  bodies do reach the builtins: under `-DCLJ_COMPILED_CORE` there is no body to walk, and the whole lint went
  dark in the compiled gate before they were named.
- **`opaque` is what makes the park ladder three-valued.** `CLJ_EFFECT_ANY` is every bit *but* `park`, so an
  unknown callee says "nothing is known", not "waits"; without the separate bit the first unknown call in a
  body would read as a proven park and the ladder of design §4 would collapse to two steps. `deref` is the
  reason the fact cannot be keyed on the symbol: `@atom` never parks and `@future`/`@promise` do, so `deref`
  stays opaque and drops the bit only where the argument is known to be an atom.
- **The `:effects` requirement (design §4).** A parameter whose schema is a `:=>` with a properties map
  declares what the function passed there may do: `(swap! a f)` is `[:=> {:effects #{}} [:cat :any] :any]` at
  argument 1, "no park". Only `park` is checked (`CLJ_EFFECTS_CHECKED`) — the other bits describe a body, no
  site forbids an allocation. `{:effects/severity :error}` is for the places where parking cannot work at
  all (`dosync`, a host stub's sync closure, code under `clj_lock`): there a known park is an error and an
  opaque argument a warning. No var carries it yet — none of those exist; `SummaryTests` is what exercises it. Without it the requirement is a lint: a known park warns and an opaque argument is
  silent, because warning on it costs 76 warnings over the corpus and proves nothing. `swap!`, `swap-vals!`,
  `set-validator!` and `lazy-seq*` carry one — each runs its function while holding the atom's coroutine
  mutex or the seq's forcing claim. Watches do not: `commit` unlocks the atom before `notify`.
- [~] **Declarations: `:=>` meta on the var, the vocabulary of design §3, one mechanism.** A var may carry
  `{:=> [:=> [:cat arg-schema …] ret-schema]}` in its meta — the value is a complete Malli function schema, the
  same data `m/=>` takes, spelled either in a defn's attr-map (`(defn vec {:=> [:=> [:cat :any] :vector]}
  [coll] …)`, three of core.clj's defns carry one) or set by `alter-meta!` for a builtin that has no defn
  (the one table at the end of core.clj, 18 vars). `clj_fact_of_schema` is each tag's abstract
  interpretation: `:int` → {fixnum, long, bigint}, `:number` all six, `:map` map|sorted-map|record, `:set`
  both sets, `:seq` seq, `:boolean`, `:string`, `:keyword`, `:symbol`, `:vector`, `:fn`, `:nil`, `:any` ⊤;
  `[:maybe X]` X ∪ nil, `[:or …]` join, `[:and …]` meet, `[:= x]` and `[:enum …]` singletons (kept only for
  an immediate or a keyword, which no tree owns), `[:vector …]`/`[:tuple …]`/`[:map …]`/`[:set …]` their kind
  (the children describe elements no fact holds yet), `[:fn pred]` ⊤, `[:=> …]` fn; a properties map after the
  tag is skipped; `[:* X]` in the `:cat` ends the fixed arguments. An unknown tag is ⊤ and never an error,
  so a schema the vocabulary outgrows degrades, it does not break. The declaration meets the inferred
  summary; the inferred one is still computed and, for a visible body, decides — for an opaque one (a native,
  a compiled closure) the declaration is trusted. `clj_fact_to_schema` is the total embedding back (a kind
  set as `[:or …]` of the widest tags that fit, nullability as `[:maybe …]`, a singleton as `[:= x]`), and
  `signatureTableRoundTrips` projects every entry of the signature table through it and back: 154 of the
  201 entries round-trip, 16 are transfer functions (`+ - * / quot rem inc dec conj assoc into with-meta vary-meta
  dissoc disj empty`: the result is computed from the arguments, which is a function in the result position
  of `:=>` — out of scope here, the trigger is the comptime evaluator of design §3), and the rest name the
  **vocabulary gaps**, kinds without a tag: *array* (the twelve array constructors, `aclone`, `to-array`,
  and the reason `count`, `nth`, `next`, `rest`, `seq`, `vec` declare `:any` where they mean "seqable" — a
  narrower declaration would be a false error on `(count (int-array 3))`); *fixnum alone* (`count`, `hash`,
  `compare`, `alength`: `:int` reads back as three kinds, so the unboxing prize cannot be declared, only
  inferred); *a plain map or set alone* (`hash-map`, `zipmap`, `frequencies`, `group-by`, `hash-set`, `set`:
  `:map` and `:set` read back with the sorted kinds and records); *sorted-map*, *sorted-set*, *atom*, *char*,
  *regex*, *uuid* — 31 entries. Each is a tag the vocabulary would need, not a second mechanism. Measured
  (docs/facts-coverage.md, "declarations alone"): they move no known-type percentage, they take pass 2's
  narrowed arguments from 9 to 99 and its proven throws from 0 to 35, because inference alone has no
  requirements at the leaves.
- **The reverse index and the caller join (callers.c): the closed-world direction as a dev-mode fact under
  an epoch** (design §3 "Проход 2"). The recording walk lists every call site of a var with the facts of what
  it passes (`clj_facts_site`, before the callee's requirement narrows them) and every read of a var as a
  value (`clj_facts_value_read`: an argument, a capture, `#'f`, the fn of `apply`), each flagged `in_fn` when
  it sits in a fn body. A consumer puts them into the process-wide index under an *owner* — the exec of the
  tree for the interpreter, a token per form for the report tool — and the owner's death takes them out
  (`clj_callers_forget`, from the exec's finalizer), so the index is what the *live program* passes: a tree
  that ran once and died is gone with its sites, a redefined fn's old body with its. The interpreter records
  only `in_fn` sites: a call at the top level of a form runs once, like a call from the host, and a fact
  about the program must not flip with every REPL line (the first version recorded everything, and reading
  `f` as a value at the REPL de-specialized `f` for good). Every change bumps the var's **callers epoch**
  (`clj_var_callers_epoch`), the second epoch beside the root epoch: redefining `f` bumps its root epoch,
  which invalidates every summary and table that read `f`'s root; adding or losing a caller of `f` bumps its
  callers epoch, which invalidates only the join `f`'s own body was derived under. A table built with a store
  that has `clj_summaries_use_callers` on enters the parameters of a `(def f (fn …))` at the **join** over
  the recorded sites that resolve to that arity (`clj_callers_join`, the evaluator's own arity choice: a
  site with n arguments feeds the fixed arity n, else the variadic one, whose rest parameter takes nothing),
  kinds and nullability only — no singleton, which would borrow a caller's constant, and no descriptor,
  which may die — and records the join with the epoch it was read under (`clj_facts_join_at`, checked by
  `clj_facts_valid` beside the var deps). **A fn's own site is no site of the index: it enters the entry's own
  join.** `(defn fact [n] (if (<= n 1) 1 (* n (fact (dec n)))))` recorded as any other site would put `n` at ⊤ into
  its own join before any caller exists — "a number" — and every later join would rest on that, which is exactly the
  poison the first version had (`fact` never got a worker). So a call of the def'd var from the arity's own frame,
  resolving to that same arity (`self_site`: same var, `in_fn` at the frame's depth, `clj_facts_arity_for`; nested
  fns, direct fns and fused programs are other frames and stay recorded), is dropped from the table's site list in
  every mode, and `enter_join` closes the join over it: `callers` is what the index answered over the external
  sites, `params` starts there and, when some position is narrower than ⊤ and the body has such a site
  (`scan_self_sites`), takes in what the self-sites pass under it — the body walked again with the parameters at the
  join, recording nothing, entering no nested fn (`self_sites_join`, a `pass` with `self_join` set) — until nothing
  moves; a position still moving after `WIDEN_ROUNDS` = 3 goes to ⊤ and every round past that widens what moved, so
  the loop ends within `nparams` more rounds (`self_fixpoint`; `self_rounds`, `self_widened` on the join). `fact`:
  the external join is a fixnum, the self-site under it passes int64, under int64 it passes int64 — two walks. A
  self-site passing ⊤ (`(f @box)`) makes the position ⊤ and the reason `CLJ_JOIN_TOP_ARG`, as a recorded ⊤ site
  would; a self-site that climbs kind by kind (`(f (str x))`, `(f (keyword x))`, …) widens. An arity calling the
  fn's *other* arity (`(defn sum ([n] (sum n 0)) ([n acc] …))`) is a caller like any other: recorded with the facts
  the one-parameter arity's own join gave it, so the two-parameter arity reaches int64 through the re-derivation
  the new site queues (specialize.c), not inside one table. The consumers compare the index against `callers`
  (`enqueue_if_stale_in`, `clj_exec_derivation_valid`) and enter and emit against `params`. The self walk is the
  recording walk's, so the depth cut of a summary walk does not apply to it (`pass.bounded`, the budget bullet
  above). The **⊤ rules**, each a reason the join reports: *no site* recorded (a fn nobody calls yet: unknown,
  not ⊥, or every use in its body would be a false error); a site
  passing *⊤ at that position* (the other positions keep their join); the var *read as a value* anywhere
  live (it may be called from a place the index cannot see); `^:dynamic` (a binding may put anything
  behind the var). The host boundary is a fifth, unrecorded caller, which is why no consumer trusts the join
  without a runtime check (the tag checks of the two consumers below); the join's job is to decide *where*
  a fast path is worth emitting. Over the corpus (`make facts-report`, which records every site of every
  library first and then re-derives every table for 3 rounds, so a parameter narrowed by its callers narrows
  the sites in its own body for the next round) 865 arities asked, 140 came back narrower than ⊤ at some
  parameter, 147 of 1102 parameters narrowed and every one to a single kind; the reasons over every ask:
  576 no site, 444 a ⊤ site, 1221 first-class (`map`, `partial`, `comp`, `concat`, `min`, `max` and the
  `deftest` vars are the population: a fn passed around is called from anywhere), 0 dynamic, 354 clean.
  Arithmetic over library code: 18.8 → 22.4 % of sites with every argument fixnum and 44.7 → 49.4 % with
  every argument int64 (medley 6.7 → 26.7 %). The gap between the two shares was never parameters: a
  fixnum stays exactly a fixnum only until the first `inc`, since the signature table answers fixnum|long
  for arithmetic (an overflow past the 63-bit tag boxes), so every loop variable is int64 and not fixnum,
  and the 22 sites between the shares are loop variables and vars holding a boxed long. What the join
  moves are the parameters, which sat at ⊤ or "a number" in neither share; what it leaves (43 of 85 sites):
  18 with an argument that is "a number" and no narrower — a parameter whose callers pass mixed numerics or
  are unrecorded, a captured parameter (⊤ inside a closure body: no join reaches a capture), the result of
  `quot`/`rem`/`/` or of a fn whose summary says number —, 14 with a parameter the ⊤ rules left at ⊤ (a
  helper used first-class, no live caller), 2 comparisons against a double. **A conflict inside a joined
  frame is a warning**, `CLJ_DIAG_CALLERS_CONFLICT`, never an error: `(name s)` with every recorded caller
  passing a fixnum says no recorded call takes that path, not that a call throws — the site itself already
  carries the site diagnostic, and a join-derived error would be a false one for a caller the index does
  not see. So the join adds no ⊥ to any table (the entry meet is skipped when it would be ⊥ and the site
  diagnostics stand) and the corpus gate stays at zero errors.
- **Summaries specialized to a call's arguments: domains.** `clj_summary_of_var_at(store, var, nargs, domains)` walks
  the arity with each parameter entered at its domain — int64 (fixnum|long), double, or ⊤ — instead of ⊤, and caches
  the entry beside the generic one (the key is the argument count under the domains in base 3, at most 8 arguments),
  under the same validity, fixpoint and budget rules; a native, a protocol method and an annotation-only var answer
  NULL. Pass 2 asks it at every call whose argument has a domain (`specialized_of`, `clj_domain_of` on the argument's
  fact) and takes its *result* for the site node; the requirements and the effects stay the generic entry's, so no
  diagnostic moves — the result of `(sq i)` with `i` int64 is int64, and the loop accumulating it stays typed where the
  generic summary said "a number". This is the transfer function of design §3 for a body the analyzer sees, sound on
  the site's own argument facts alone (no join is involved): the parameters enter at exactly what the site passes. A
  declared result meets the specialized one; the declaration's own diagnostics are the generic entry's. It is what the
  compiler's primitive entry reads on both sides of a call (the Compiler entry). A recursive call inside the
  specialized walk asks the specialized entry again (`specialized_of` keys on the site's own argument facts, which
  rest on the domain parameters), finds it running and takes its optimistic ⊥ — the rule below — so `fact` at int64
  is fixnum after the first round and int64 after the second, `fib` int64 in two, and `(defn halve-n [x n] (if (zero?
  n) x (halve-n (/ x 2.0) (dec n))))` at (double, int64) double; the generic entry, computed inside the first round
  with the parameters at ⊤, stays "a number". Over the corpus the store holds 987 entries instead of 743 and no
  coverage percentage moves: the corpus has few numeric helpers called from typed loops.
- **⊥ through a call in a summary walk.** A summary walk (`pass.summary`) answers ⊥ for a call whose argument is ⊥:
  the optimistic value of a fixpoint in flight, or a branch a refinement killed — the call is not reached, so its
  result joins nothing, and `(defn fact [n] (if (zero? n) 1 (* n (fact (dec n)))))` at an int64 argument climbs from
  fixnum to fixnum|long instead of stopping at "a number" (`arith_result` reads a ⊥ operand as any number, which is
  right for a stored fact and wrong for an optimistic one). Recording walks are untouched: a stored ⊥ would trip the
  watchdog, and the dead-branch classes are theirs.
- **`quot` and `rem` are arithmetic transfer rules** (`RA`, the `+` rule): int64 × int64 is fixnum|long (a quotient
  never grows, and the one overflow, `INT64_MIN` by −1, throws), a double operand makes a double, ratio and decimal as
  for `+`. They joined the intrinsics table too (`clj_quot`, `clj_rem`, the builtins' own bodies), so the compiler sees
  an INTRINSIC node; the round trip counts 16 transfer functions now. Over the corpus the refinement conflicts rise
  64 → 174, every one a dead branch: `(let [r (quot 10 3)] (and (int? r) …))` in the quot, rem and mod tests now has a
  known `r`, so the false branch of `int?` is dead by the literal; ⊥ value nodes, errors and the unexplained count
  stand.
- [ ] **Deliberately not here, each with its trigger.** No shape facts (the design's key sets) — trigger: a
  record fact reaching a consumer, which the constructor summaries now make possible. No ownership, thread
  affinity or the rest of the design's fact kinds — each is a field and a transfer rule on the shared walk;
  trigger: a consumer. No refinement on `CAPTURED` or `OUTER` reads, only on frame slots — trigger: a profile
  where a closure body re-tests what its definer already knew. No `case`/`condp` refinement beyond what their
  expansion into `if` gives. A `DIRECT_FN` node is recorded as a fn although its slot holds nil at run time:
  nothing reads that slot as a value, and the useful fact is that the name denotes a function. No host-call
  effect bit: design §5 keeps that one a fact and a diagnostic, never a prohibition, because a prohibition
  needs the call chain and `opaque` is the admission that the chain is not always there — trigger: the stub
  generator's report reaching the LSP. No backward flow of an expected type into a body either: the walk runs
  arguments → result, so a Swift generic's type is known at the call and does not propagate upward, and a
  malli schema reaching such a call is a diagnostic — a schema is a predicate over values, a type parameter
  selects code (design §5, "Дженерики").

