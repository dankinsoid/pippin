# Internal errors

Design §3 «Корректность реализации» items 1 and 6: an invariant of the implementation that fails reports itself as a
pippin bug, with the pass, the backend and the position in the user's source, and the process dies; a verifier
checks the tree and the facts between passes in debug builds; one option turns every optimization off.
`Sources/CljCore/ice.c` and `include/clj/ice.h` (the report, the frames, the switch), `verify.c` and
`include/clj/verify.h` (the tree verifier), `Tests/PippinTests/VerifierTests.swift`.

- **One mechanism: `clj_ice(kind, node, fmt, …)`.** It writes the report to stderr and dies the way `clj_fatal`
  does (`abort`, or `_exit(134)` under `CLJ_CRASH_EXIT`). The kind is stable, `<area>/<invariant>`
  (`verify/read-after-last-use`, `facts/slot-outside-frame`, `emit/promoted-slot-by-address`, `rc/owner`,
  `boot/core-load`), and `assert` for a `CLJ_ASSERT`, whose C position stands in for its own name; the message is
  free text. The fuzzer prints the first line of a runner that died with one (`report-runner-exit`), and
  `fuzz/parsers/run.sh` groups findings by it, so a finding is keyed by the kind, not by wording.

  ```
  clj: internal error [verify/read-after-last-use]: slot 0 is read after a last use handed its value over; the tree as optimize/liveness left it
  clj: this is a bug in pippin, not in the program it ran
  clj:   pass: optimize/liveness
  clj:   backend: interpreter
  clj:   form: app/core.clj:12:1
  clj:   within: optimize/liveness macroexpand
  clj:   node: local at 12:24, id 5
  clj:   optimizations: all on; with CLJ_NO_OPT=1 they are off, and an answer that differs there is a pippin bug too
  clj:   trace, Clojure frames (innermost first):
    at app.core/f (3:5)
  ```
- **`clj_fatal` is kept for what is not a bug of pippin**: out of memory, a thread, mapping or key the system
  refused, a `CLJ_SCHED_SEED` or `CLJ_NO_OPT` that does not parse, a capacity (a string past 4 GiB, the debug type
  tables), a host calling the scheduler from the wrong thread. It prints `clj: fatal: …` and the same context lines,
  without the claim.
- **What became an internal error**: every `CLJ_ASSERT` (debug builds); every "unknown node kind" default (eval,
  facts, optimizer, codec, compiler, reuse planner) and the optimizer's and codec's unknown capture kinds; the
  compiler's self-checks (a temp released twice, a borrowed slot rebound, a typed slot given a boxed value, a
  promoted slot read through the frame, a recur without a target, a worker signature mismatch, a direct call
  before its fn); the runtime's (a non-number in the numeric tower's internals, a vector index or pop past its
  count, a bigint division by zero, a range of step 0, a type without hash or equals used as a key, a finished
  coroutine resumed, an Objective-C trampoline reached by a stranger, an unknown regex instruction, a compiled
  constant or intrinsic a unit names and the runtime lacks, a compiled frame outside its section, a dropped def
  reached in a `--closed` program); the debug RC checks (an unshared object touched by another execution, a shared
  parent over an unshared child, a store without its owner's lock); boot (core.clj failing to load, the intrinsics
  and fusion tables naming no var).
- **The context is a chain of frames on the execution's ring** (`clj_ice_frame`, `clj_shadow_stack.ice`):
  `eval_form` pushes one per top-level form (`macroexpand`, then `load hook`, then `run`, backend interpreter);
  `clj_analyze` one per analysis (`analyze`, `optimize/direct`, `optimize/rewrite`, `optimize/liveness`, `number`,
  `analyze/lazy-def`); `clj_facts_of` and `clj_facts_of_with` (`facts`); a specialize derivation (`specialize`);
  `emit_top` (`emit`, a dev or closed unit by the compiler's options); a compiled eval's unit init (`run`). The
  pass is the innermost frame's, the backend the innermost that names one, the form the innermost with a position
  (its file the innermost with a `*file*` string, borrowed: the load's binding outlives its forms). With no frame
  naming a backend the report asks the trace walk whether compiled code is on the stack. The chain lives on the
  ring, not in a `_Thread_local`, so it follows a coroutine across a park (NOTES "Coroutines", TLS across a park),
  and the guard reads it through the pthread key: a fatal signal (`fatal_fault`, guard.c) prints the same lines
  after its trace. A landing (guard.h) unwinds C frames without leaving theirs, so `clj_recovery` saves the chain
  and `clj_recovery_throw` restores it. A report that faults prints only its first line.
- **⊤ that hid a bug, and ⊤ that is widening** (facts.c). A local or a captured slot past the frame `max_slot`
  sized, a capture index past the captured facts, a recur with no target slot, a node id past the table (facts over
  a subtree) used to answer ⊤ or skip the write; each is `facts/*` now, in every build, since each replaced a
  branch that was already there. What stays ⊤ by design: the walk's depth and stack cut, loop widening after three
  rounds, the union cap, a walk without captured facts (a summary, the top level), an `OUTER` past the frames a
  summary walk holds, a call nothing describes.
- **The kill switch: `CLJ_NO_OPT=1`** (read at `clj_init`; `clj_no_opt_set` before it; `--no-opt` on `clj-load`,
  `clj-compile` and `clj-fuzz`). `=reuse,passes,…` names a subset for bisecting a finding. What it turns off:

  | name | what is off |
  |---|---|
  | `reuse` | `clj_is_unique` and `clj_rc_token_ok` answer "shared": every in-place update copies, interpreted and compiled |
  | `passes` | `clj_optimize`: direct fns, intrinsics, fusion, constant folding, last-use marks |
  | `specialize` | the specialized arithmetic entries of an exec |
  | `lazy-seq` | the interpreted lazy-seq cell without a closure |
  | `layouts` | tuples, shape maps, flat transients |
  | `compiled` | the compiled core (a `-DCLJ_COMPILED_CORE` build boots core.clj from its embedded source), registered units, `CLJ_EVAL=compiled` |
  | `emit` | facts in the C emitter: promoted and typed slots, unboxing, protocol arms, workers, reuse tokens |
  | `inline` | the inlining pass of `clj_optimize` (NOTES "Compiler", inlining on the AST), so `beta` and `path` with it; in the emitter the intrinsics' fast arms at the site and the monomorphized fused stages; at run time the fused drivers' own walk of a vector |
  | `beta` | inlining a callee that calls a parameter, and with it a literal fn argument copied where the callee calls it |
  | `path` | `get-in`, `assoc-in` and `update-in` unrolled over a literal path (NOTES "Compiler", known paths) |
  | `owned` | owned parameters and the late take (NOTES "Compiler", owned parameters): every parameter +0, every call the +0 entry; a lazy-seq thunk keeps its captures to the publish |
  | `lower` | `reduce`, `reduce-kv`, `swap!` and `swap-vals!` over a literal fn run without its closure (NOTES "Compiler", lowered natives); `inline` turns it off too |
  | `drop` | a compiled frame's dead slot released where it dies, not at its rebind or the frame's exit (NOTES "Compiler", dead slots dropped); decided at emission, so the boot units keep theirs |
  | `fuse` | the fusion pass over `(sequence XF src)`, a map over two or three colls, `mapv`, `(into to XF P)` and `transduce` (NOTES "Analyzer and evaluator", the fusion pass), and the compiled step's mapcat, dedupe, distinct and partition-all stages and its stage fns that are a core var or a keyword (NOTES "Compiler", fused stages) |

  Not turned off, each because it is no optimization: lazy defs (semantics of design §4, `CLJ_LAZY_DEFS=eager` is
  their control), the analyzer's folding of a literal collection into a constant, the evaluator's borrowing
  convention, `--closed`'s direct binding of compiled vars (the flag asks for it) and tree shaking (the tripwire
  makes a wrong drop fatal, never wrong). Reuse is a runtime test now: `-DCLJ_NO_REUSE` sets the bit for the
  build (`make test-noreuse`, `model-long`, `fuzz-long`'s noreuse runner) and the bit costs `clj_is_unique` one
  relaxed load. `make fuzz` runs `noopt` beside `interp` and `unit`, so the switch is compared with the oracle and
  with the optimized interpreter on every gate run. Under the switch `clj-compile` is the emitter without the
  optimizer and without facts, which splits a wrong answer of a compiled program between the two.

## Tree verifier

- **Where it runs.** Debug builds only, every check behind `#if CLJ_DEBUG` (`CLJ_VERIFY_TREE` and friends expand to
  nothing in release); `CLJ_VERIFY=0` turns it off in a debug build for measuring. A violation is `clj_ice` with
  `verify/<invariant>`, the pass that left the tree so named in the message and as the frame's pass.

  | after | what |
  |---|---|
  | `analyze` | the tree, and no node kind only the optimizer makes, no last-use mark |
  | `optimize/direct`, `optimize/rewrite`, `optimize/inline` | the tree |
  | `optimize/liveness` | the tree and its marks |
  | `number`, `codec` (`clj_node_from_data`), `emit input` | the tree, its marks and its numbering |
  | `facts` | the table; the tree unchanged across the pass |
  | `specialize` (`clj_exec_new`, every derivation) | the exec's entries; the tree unchanged |
  | `emit` | the tree unchanged; the slot plan of every frame (`emit/slots`) |
  | `run` | a form the compiler pended is unchanged when it is emitted, after the interpreter ran it |

- **Cost: below what the CI runners can resolve.** `make test` (ASan, two shards) on one commit, the verifier on (in
  `gates`) against `CLJ_VERIFY=0` (a `test` dispatch beside it), shard seconds: 152/150 against 124/127 (runs
  38164621545, 38164630018) and 177/176 against 160/159 (runs 38165088626, 38165094451). The same runs' `swift
  build` took 128 against 93 s and 156 against 134 s, so the runners differed by as much as the pairs do; scaled by
  the build, the verifier's side is the faster one in both pairs. Nothing is sampled: every tree of every pass is
  checked. Trigger for sampling: a pair on one runner (both `make test` passes in one job) that shows the verifier
  past a tenth of the suite.

- **The invariants**, each with what reads it.
  1. Structure: every child present and a node of a known kind; a `do` of at least one item (`eval_do` reads item
     n−1); a map literal of even items; a def with its meta node; a catch selector of its kind's type; a send's
     selector a string.
  2. Numbering: ids in pre-order from the root, `nnodes` the subtree size, an invoke's `site` its ordinal. The exec
     table, the facts table, the codec and the emitter's `inside` index by them; a node shared by two parents or
     moved after numbering breaks it.
  3. Frames and scope: a local reads a slot of its frame bound by a binding in scope (the top-level frame sized as
     `clj_exec_new` sizes it, an arity's params, rest and self slot bound at entry); a let, loop or catch binds a
     slot not bound in scope, since the evaluator keeps one value per slot; a captured read is inside the
     environment; an outer read has depth ≥ 1 within the static chain of direct-fn frames, to a slot in scope there;
     a direct arity's self slot is never read (`eval_direct_call` leaves it nil).
  4. `recur`: in a tail position of the innermost loop or arity, no `try` between (the evaluator returns
     `CLJ_RECUR` up to its target, the emitter jumps), to exactly the target's slots.
  5. Direct fns: `DIRECT_FN` only as a let or loop init, without a rest arity or captures; a `DIRECT_CALL`'s fn is
     the `DIRECT_FN` bound in scope at its slot `depth` links up, and its arity that fn's fixed arity for its
     argument count; a direct fn's slot is never read as a value, captured or rebound by a recur.
  6. Guards: an `INTRINSIC`'s var is the one the table names for its op at its arity — the guard compares that
     var's root with the op's builtin, so another var would run the op where the program meant something else; a
     `FUSED` node's guards are entries of the fusion table, its two programs read only its arguments, hold only
     local reads, calls, constants, vars and vector literals, and name only guarded vars; a `fused*` program takes a
     constant mode below `CLJ_FUSED_MODES`, the lead vector that mode takes, a vector of stages and one to three colls
     read from the arguments (`verify/fused-run`).
  7. Last-use marks, which both backends act on (a hand-over in `eval_borrowed`, `taken` in `emit_local`): none
     before the liveness pass; only on a read that consumes (a let or loop init, a recur argument, a body's value,
     a def's init or meta, a thrown value, a consuming intrinsic's collection); never past slot 64 or in a fused
     program; never on a slot an earlier operand of the same call or literal borrows, or one a direct fn body reads
     through the static link; and no read of a slot after its mark on any path until it is bound again. The last
     is checked forward, apart from optimizer.c's backward pass: a fixpoint over each loop and each arity's recur, a
     handler starting from any point of its body, `finally` from any point of the body and the handlers.
  8. Immutability: a digest of every field a backend reads (kind, ids, positions, slots, marks, captures, arities,
     constants and vars by identity) is equal across facts, specialize and emit, and across the interpreter's run
     of a form the compiler holds until `cljc_end`.
  9. Exec entries: each one its node's kind and shape admit — the generic entry, the counting wrapper, a keyword or
     lazy-seq site's, a shaped map literal's, the op's own specialized entries; a site below `nsites`.
  10. Facts: a table over the whole tree (root id 0, the tree's `nnodes`); every fact normal — types within ⊤, the
      union cap, nullability consistent with the types, a singleton, descriptor or element kind only beside one kind
      and the singleton of that kind; frames on `FN`, `DIRECT_FN` or `FUSED` nodes and their bodies' extents;
      loops on loops of their variable count; a loop variable at least its init, the join only climbing.
  11. The compiled slot plan (`promote_slots`): borrowed and typed slots promoted, typed not borrowed, no slot both
      int and double; no promoted slot read by address (a capture, a static-link read, re-derived by
      `clj_verify_frame_slots` apart from `pin_walk`) or past 64; a borrowed slot is a closure's param or self slot
      no recur rebinds; an owned parameter is a closure's param, neither borrowed nor typed — owned slots are
      released by the teardown at both exits and borrowed ones never, so this is what keeps "released exactly
      once" true. Beside it, always on: a caller asking for another owned mask than the callee's entry took
      (`emit/owned-mask`, both read one inference), a hand-over or late take at an operand that does not read the
      slot (`emit/hand-slot`, `emit/take-slot`), and in debug units `CLJC_TAKE_CHECK`: the slot still holds what
      its borrowed read saw when the call takes it. The frame's drops (`clj_verify_drops`, apart from reuse.c's
      backward placement): forward over the frame with invariant 7's fixpoints, no read of a slot after a drop
      until a binding rebinds it (`verify/read-after-drop`), and no drop of a slot an earlier operand of a pending
      call borrows or a direct fn body reads (`verify/drop-of-held-slot`); a drop of a slot the frame does not own
      as a C variable stops the emitter (`emit/drop-slot`). A drop leaves nil or, for a value it keeps (not a core
      collection or seq), the value itself, so the rebind or the teardown releases each reference once.
  12. Inlined calls (`walk_inlined`, `walk_nested_inline`): a `ROOT_IS` only as the test of an `if` whose then branch
      is an `INLINE` (behind a let of fresh slots or bare) and whose else branch is the call of that very var; the
      guard holds a closure without an environment whose fn node is the `INLINE`'s. A guard without core vars is a
      `COPY`'s, of a fixed arity for the call's count; a guard with them is a known path's: entries of the fusion
      table, the var's own among them at that closure as its boot root, a var of `get-in`, `assoc-in` or `update-in`,
      a `PATH` body of an arity the count reaches. A `COPY` or `PATH` only there; a `LEVEL` only inside a `PATH` body,
      at most `CLJ_INLINE_PATH_MAX` inside one another; a `LITERAL` only in the then branch of a guard whose call, or
      an enclosing guard's in the same frame, passes its fn node — never in the call itself, never in another frame;
      at most `CLJ_INLINE_DEPTH_MAX` guarded bodies inside one another in a frame; no recur crosses into a body (it is
      walked with no target). The guard compares the root with that closure, so a body under another fn's guard, a
      copy of a fn nothing passes, or a guard with no call beside it, would run code nothing checks.
  13. Lowered calls (`walk_lowered`, design §10 step 7a′): a `LOWERED` only as the then branch of an `if` whose test
      is a `ROOT_IS` with no core vars, holding the builtin `clj_lowered_root` names for the native, and whose else
      branch is a call of that var at a count the native has a lowering for; the `LOWERED`'s fn is that call's literal
      `fn` argument, the node itself, with the fixed arity the native calls, and its operands are the call's other
      arguments in order. Last-use marks: no operand hands over a slot the fn captures, since `LOWERED` reads the
      captures after its operands. A `LOWERED` anywhere else, or over another node than its call's argument, would run
      a builtin no guard checks or read an environment no closure would have had.
  14. A lazy-seq site's shape is `clj_lazy_seq_site` itself, which both backends ask at build time; what the
      verifier adds is the arity and capture checks the site's fn gets as any fn.
- **What is not checked, and why.** An `if`'s fact as the join of its branches', a `let`'s as its body's: `require_arg`
  narrows the fact of a node that is a call's argument and not the facts of the nodes it is made of, so the relation
  is not an invariant of the table. Stack: past `clj_stack_limit()` a walk gives up instead of failing, since a fault under the specialize lock
  is fatal (NOTES "Guard").
- [ ] **The codec round trip is not part of the verifier.** `to_data` then `from_data` of every tree, compared by
  digest, would check serializability; `CoreSerializableTests` checks it for core.clj and the type macros. Trigger: a
  tree cache on disk or AOT (the codec entry of NOTES "Analyzer and evaluator"), when a tree read back is executed.
- **The seed** (`VerifierTests`): a last-use mark set by hand on `(conj v 2)` in `(let [v [1]] (let [w (conj v 2)]
  [w v]))`, where `v` is read again, is `verify/read-after-last-use`; the exit test runs the verifier as
  `optimize/liveness` on it and finds the kind, the pass and "the tree as optimize/liveness left it" on the child's
  stderr. A slot past the frame, an id out of pre-order, a mark before liveness and a recur moved into an `if`'s test
  have their kinds, and so do a literal copy of a fn no guarded call passes, a path guard that lost its own var and a
  path body under a copy's guard (`inlinedShapesHoldTheirGuards`); the digest sees a mark flip; the report carries kind, claim, pass, backend, form and node; and
  `CLJ_NO_OPT=1` leaves `(+ 1 2)` an invoke, reuse, tuples, shapes and specialization off, and the answer 3.
