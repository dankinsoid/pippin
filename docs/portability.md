# Portability ledger

Other platforms are the last goal of the project, after everything else (design §10). Until then the
runtime is built and tested on Apple platforms only, and this page is the one place that records every
spot where the code assumes them, so that the port is a walk down a list rather than an archaeology.

Two rules keep the list complete:

- **Behaviour is one; only the mechanism is per platform.** A platform-specific branch may choose how
  to find a stack bound, a section, a register or a random byte; it may not change what a program
  observes. A branch whose other side is "not supported" is a listed gap, not a design.
- **`make port-audit` fails on an unlisted spot.** The script greps the sources for the platform markers
  below and compares the files it finds against this ledger. A new Apple-only API in a file that is not
  listed here fails the audit; add the row. Agents: every task that adds such a spot adds its row.

## Spots

| file | what is Apple-specific | fallback today | port note |
|---|---|---|---|
| `include/clj/lock.h` | `os_unfair_lock` | `pthread_mutex_t` on other platforms | Semantics identical; the mutex is 2× slower (bench). |
| `shadow.c`, `eval.c` (stack limit) | `pthread_get_stackaddr_np` for the thread's stack bounds | a fixed 512 KB assumption from the first call | `pthread_getattr_np` on Linux gives the same. |
| `guard.c`, `guard_internal.h` | guard-page overflow recovery: `sigaltstack`, `ucontext` register layout for arm64/x86_64 macOS; `CLJ_CRASH_EXIT` skips the crash reporter's corpse path (ReportCrash answering `EXC_CRASH`) | none — a compiled overflow is a plain crash elsewhere | Same design on Linux with its `ucontext` field names; the interpreter's own check is portable; the `_exit` fallback is plain POSIX and matters only where a crash reporter can wedge the death. |
| `trace.c` | frame and site tables found through `getsectiondata` in Mach-O sections `__TEXT,__cljframe`/`__cljsite`; PAC stripping on arm64e | none — traces carry no compiled frames elsewhere | ELF: same sections with `__start_`/`__stop_` symbols; PAC is arm64e-only anyway. |
| `compiled_internal.h` | the frame attributes (`section(...)`) and the site-marker emission the compiler relies on | none | Section names follow the object format; the attribute spelling differs between Mach-O and ELF. |
| `profile.c`, `profile_internal.h`, `eval.c` (signposts) | `os_signpost` intervals under `--instrument` | compiled out | Instruments-only; a port keeps the profiler and drops signposts. |
| `uuid.c` | `arc4random_buf` for `random-uuid` | none | `getrandom(2)` on Linux; a CSPRNG is required (design: not the SplitMix rng). |
| `CljCompiler/jit.c`, `include/cljc/compiler.h` | `xcrun` to find clang and the SDK; `dlopen` of a per-form dylib (diagnostic mode only) | none | Diagnostic path, not a product feature (design §9). |
| `Makefile`, `Package.swift` | `swift test --sanitize`, `xcrun clang` for the corpus in compiled mode | none | Toolchain, not runtime. |
| `clj-bench/main.swift` | `os_unfair_lock` rows as the Swift comparison | none | Bench only. |
| `coro.c` | the context switch in asm for arm64 and x86_64 (Darwin symbol prefix `_`); `mmap`/`mprotect` for the stack reserve and its guard page; `MADV_FREE_REUSABLE` for a parked stack's tail; `task_info(TASK_VM_INFO)` for the physical footprint; the ASan fiber hooks | none — no other architecture | Linux: the same asm without the underscore, `MADV_DONTNEED`, `/proc/self/statm`; the fiber hooks are the sanitizer's, not the OS's. |
| `sched.c` | `pthread_set_qos_class_self_np` on the carriers; the main carrier is a version-0 `CFRunLoopSource` signalled with `CFRunLoopWakeUp` | the main carrier queue alone (a host can pump it by hand) | Linux: an `eventfd` in the host's `epoll`/GLib loop performs the same pump; QoS has no equivalent. |
| `cmutex.c`, `chan.c`, `runtime.c` (the writer thread) | pthreads and C atomics only | — | Portable; listed so the audit knows the scheduler files were read. |

## Not platform-specific, worth knowing

- The interpreter, the analyzer, the facts pass, the compiler's generator and every collection are plain
  C17 and Swift with no platform calls; `boot/core.c` is generated and carries whatever
  `compiled_internal.h` carries.
- The deadline, the shadow stack, the atoms, the pool allocator use only pthreads and C atomics.
- The coroutine mutex, the parking lot, the channels and the output writer are pthreads and C atomics; only the
  switch, the stack mapping and the main carrier's run-loop source are platform code (rows above).
- A coroutine may resume on another thread, so no `_Thread_local` is read through an address cached across a
  call that can park (NOTES.md "Coroutines", TLS); that rule is the same on every platform.
