## Guard (Sources/CljCore/guard.c, guard_internal.h; the crash handler in shadow.c)

- **The SIGSEGV/SIGBUS handler is installed once at `clj_init`** (`SA_ONSTACK | SA_NODEFER | SA_SIGINFO`, an
  alternate stack per carrier and per bare thread's implicit carrier, `clj_guard_thread_init`). On a fault it
  reads the current execution through the carrier's pthread key — for a bare thread that is the implicit
  coroutine, whose ring's stack bounds are the thread's (`clj_shadow_stack_bounds`) — and asks `is_overflow`:
  the address just under the stack's low end or the stack pointer at it. An overflow lands at the execution's
  innermost recovery point (`land_after_return` rewrites the interrupted context so the handler's *return*
  resumes in `land`, off the signal stack); with no recovery point (a bare thread outside `clj_eval`), a fault
  outside the runtime's code or a `clj_lock` held it is fatal: the trace and `clj_guard_die`.
- **A recursion whose depth is the program's stops at `clj_stack_limit()`, because the guard page under a
  `clj_lock` is fatal.** One margin (`STACK_MARGIN`, 64 KB or a quarter of the thread's stack, from the thread's
  bounds; a coroutine's is `stack_lo + 64 KB`) and three readers: the evaluator per call (`run_body`), the
  analyzer per nesting level (`analyze`, "Stack overflow" with the form's position) and the facts pass per node
  (`infer`, which answers TOP and `CLJ_EFFECT_ANY` for the subtree it gave up on — a form with no facts costs
  optimization, not correctness, design §3 «Инвариант: язык не меняется»). The reason is the third fatal case
  above: a landing rewrites the execution to resume in `land` wherever the fault was, so a fault inside a
  critical section would resume with that lock's table half-written — and `clj_exec_derive` (specialize.c) holds
  its lock across the whole facts pass. That is what killed the ASan shard on `corpus/dependency`: its test
  builds `g3` as a `->` chain of 104 interpreted protocol calls, so the node tree is 104 deep, the recording
  walk descended all of it where the summary walk has cut at `CLJ_FACTS_MAX_WALK_DEPTH` since it was written,
  and under `--sanitize=address` ~9 KB a level ran the 512 KB coroutine stack out between the 55th and 60th
  call — inside the lock, hence `fatal stack overflow (a runtime lock is held)` and a dead process. Releasing
  the lock in `land` was the alternative and is wrong: `derive` faults between `forget_locked` and
  `e->derived = d`, so the dependents table would be left pointing at a freed derivation. Measured on the
  `dependency` shape after the fix, on a 512 KB coroutine stack: ASan takes 140 nesting levels and refuses past
  ~150, the plain build takes 400 and refuses past ~500; on an 8 MB thread the plain build takes 6000. Nothing
  is fatal at any depth.
- [ ] **What is still unbounded is `eval_child`.** `run_body` checks per call, so a recursion of Clojure calls is
  bounded, but the evaluator walks one form's nesting with no check of its own: past the margin it reaches the
  guard page, catchably where no lock is held and fatally if the last straw lands in a critical section of its
  own (a keyword intern, a shape transition, a protocol dispatch). It takes a form nested past ~150 levels under
  ASan to get there, and the analyzer refuses most such forms first, which is why nothing reaches it today.
  Trigger: a check in `eval_child` a benchmark shows free, or an evaluator that does not spend a C frame per
  nesting level (design §6b).
- **A fault that is not an overflow is fatal with a trace** (`fatal_fault`): "clj: fatal SIGSEGV at 0x…", the
  Clojure frames (`clj_trace_write`), then the handler that was installed before ours (ASan's report under
  `--sanitize=address`), then `clj_guard_die`. The handler never *returns* to the faulting instruction: the
  first cut chained to a `SIG_DFL` by resetting the disposition and returning, so the kernel re-executed the
  fault and applied the default action with nothing of ours written. `clj_guard_die` raises the signal with its
  default disposition, or — `CLJ_CRASH_EXIT=1` — `_exit(128 + sig)`. The variable exists because the default
  death is reported through the crash reporter: the kernel makes a corpse and sends `EXC_CRASH` to ReportCrash,
  and a ReportCrash that never answers leaves the process in the kernel in state `UE`, unkillable, with the
  faulting thread's user pc still at the fault (`sample` showed `clj_var_root` at var.c:66, no handler frame).
  That was the wedge of two test helpers and reproduces with a five-line C program on the same machine, so it
  is the host's, not the runtime's; the Makefile exports `CLJ_CRASH_EXIT=1` and `ASAN_OPTIONS=abort_on_error=0`
  so a test that crashes ends with its trace and a nonzero exit either way. Every Makefile `swift test` also
  passes `--disable-xctest`: the suite is swift-testing only, and `swiftpm-xctest-helper` (XCTest discovery) loads
  the ASan-linked bundle without the runtime first, dies in the sanitizer's init and wedges the same way (state
  `UE` helpers from a clean `main` too); the non-ASan passes gain nothing from it either.
  `TraceTests.nonGuardFaultDiesWithATrace` is an exit test: a child faults on `clj_var_root(CLJ_NIL)` and its
  stderr carries the line and the frames (skipped under ASan: the child is spawned without the insert library).
- **The FnRootTests fault itself** was the test harness: `cljEval` ran `(in-ns …)` on the *root* of `*ns*`
  (no thread binding), so a parallel suite's `Runtime.eval`, which binds `*ns*` to the current value, defined
  its vars in that namespace, `clj_ns_resolve(clj_ns_user(), …)` answered nil and `clj_var_root(nil)` read
  address 0x…. The suites whose sources `in-ns` now go through `cljEvalScoped`, which binds `*ns*` around the
  call (`EvalSupport.swift`); `NamespaceTests` still tests the root deliberately and restores `user` at once.
  The opt-in crash handler (`clj_crash_handler_install`, SIGSEGV/SIGBUS/SIGILL/SIGABRT/SIGFPE) dies through
  the same `clj_guard_die`.

