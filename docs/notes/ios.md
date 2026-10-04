## iOS (scripts/ios-probe.c, scripts/ios-sizes.sh)

- **The runtime is cross-compiled by SwiftPM, not by xcodebuild.** `swift build --target CljCore` (and
  `Pippin`, `CljCompiler`, `CljNREPL`) with `-Xcc -target <triple> -Xcc -isysroot <sdk>` and the same pair as
  `-Xswiftc -target/-sdk` builds every target for `arm64-apple-ios15.0-simulator` and `arm64-apple-ios15.0`.
  `xcodebuild -scheme Pippin -destination 'generic/platform=iOS Simulator'` builds the package too, but only
  with `GCC_WARN_64_TO_32_BIT_CONVERSION=NO`: it adds its own default warning set on top of `Package.swift`'s
  `-Werror`, and `-Wshorten-64-to-32` then fails `summary.c`'s `e->world = clj_epoch()` — a flag the project
  does not use, on a truncation that only ever makes a summary recompute, since the comparison that reads it
  widens both sides. SwiftPM keeps the package's own flags and needs no such override.
- **The executable link is by hand, because SwiftPM has no iOS executable product.** `scripts/ios-sizes.sh`
  gathers `CljCore.build/*.o` from the SwiftPM build and links `scripts/ios-probe.c` against them with
  `clang -target <triple>`, `-framework CoreFoundation -framework Foundation -lobjc`. A bare Mach-O built for
  the simulator runs under `xcrun simctl spawn <udid> <path>`: no bundle, no signing, no Xcode project.
- **The whole `__APPLE__` side of the core is iOS-valid.** Every platform spot of `docs/portability.md` is
  gated on `__APPLE__`, which iOS defines, and all of them compile and run there: `sigaltstack` and the arm64
  `ucontext` layout (`guard.c`), `mmap`/`mprotect` with the guard page, `MADV_FREE_REUSABLE`/`REUSE`,
  `task_info(TASK_VM_INFO)` and `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE` (`coro.c`), the version-0 `CFRunLoopSource`
  main carrier and `pthread_set_qos_class_self_np` (`sched.c`), `getsectiondata` on `__TEXT,__cljframe` and
  `__cljsite` (`trace.c`). One API is missing and only one: Foundation's `Process`, which iOS does not export,
  so level 2's stub generator (`SwiftStubs.generate`) is `#if os(macOS) || os(Linux)` and refuses elsewhere.
- **What the probe proves on the simulator.** `clj_init`, arithmetic, `mapv`, an atom under `dotimes`, a lazy
  `filter` over `range`, `assoc`, `re-seq`, `random-uuid`, `require` of two embedded libs, a `go` block taking
  from a `timeout` channel, `@(future …)`, unbounded recursion caught as `"Stack overflow"`, and `clojure.string/split`
  — all twelve forms answer the same values interpreted and with `-DCLJ_COMPILED_CORE`. The `go` block is the
  one that matters most: it is the `CFRunLoopSource` main carrier and a `mmap`ed coroutine stack together.
- **A compiled core's trace works on iOS.** `(mapv inc [1 nil 3])` comes back with `clojure.core/mapv` at
  core.clj's line 1314, which is the frame table found by `getsectiondata` in `__TEXT,__cljframe`; the
  interpreted build names the same frame from the shadow stack, with line 1 instead.
- **Binary size, the baseline of design §10.** Release, arm64, `CljCore` plus `scripts/ios-probe.c`, and the
  same objects again under the Swift host (`scripts/ios-probe.swift`, `import Pippin`); Swift's own runtime
  ships with the OS from iOS 12.2, so none of it is in the binary. Each slice is linked plain and with
  `-Wl,-dead_strip`, because a shipping app links with dead stripping on, and the dead-stripped figure is the
  baseline tree shaking is measured against. Interpreted: 1,190,312 / 1,086,896 / 1,493,832 bytes for the
  simulator slice (plain, dead-stripped, Swift host) and 1,196,880 / 1,110,640 / 1,514,352 for the device one.
  With `-DCLJ_COMPILED_CORE`: 3,474,848 / 3,192,288 / 3,599,768 simulator and 3,480,056 / 3,199,648 /
  3,603,896 device. So the un-shaken runtime is **1.1 MB interpreted and 3.2 MB with the compiled core**, 1.5
  and 3.6 MB with the Swift bridge — the bridge is a flat ~405 KB, of which 136 KB is `__text` and 213 KB is
  `__LINKEDIT` (Swift's symbol and reflection tables). Both are "units of MB"; the compiled core is what tree
  shaking has to work on. The two slices differ by under 1%, so the simulator number stands in for the device
  one. Device and simulator are both arm64 here: the x86_64 simulator slice is not measured.
- **Where the bytes are.** Interpreted, dead-stripped simulator slice: `__text` 551,736, `__TEXT,__const`
  175,144, `__DATA_CONST,__const` 35,248, `__cstring` 29,707, `__bss` 12,744 — nothing else over 6 KB. With
  the compiled core: `__TEXT,__cljframe` **1,402,048**, which is 44% of the binary and its largest section by
  far (the compiled frame bodies), then `__text` 909,924, `__cstring` 156,838, `__TEXT,__const` 67,464,
  `__bss` 232,528, `__DATA_CONST,__const` 50,896. `__TEXT,__cljsite` is 88,232 in both links, kept by the
  section's `no_dead_strip` attribute (NOTES "Compiler"), which the dead-stripped figures above predate: the
  device slice re-linked on a later SDK is 3,283,392 dead-stripped and 3,687,648 under the Swift host, about
  +84 KB each.
- **core.clj's share is `__TEXT,__const` and nothing else.** The embedded sources are `static const` byte
  arrays (`core_clj.inc`, `libs_clj.inc`): core.clj is 107,668 bytes and the seven embedded libs 65,854, which
  is 173,522 of that section's 175,144. Dead stripping removes core.clj's copy from a compiled build
  (175,224 → 67,464) because nothing calls `clj_core_source` there, and keeps it in an interpreted one, where
  the embedded source is 16% of the whole binary. The libs stay in both: `require` reads their source even
  with the compiled units registered, so a tree-shaken build has to drop the libs an app does not load.
- **Boot and peak footprint (`phys_footprint`, simulator, 10 MB of which is the simulator's own baseline).**
  `clj_init` costs 6.0 MB interpreted and 1.1–1.3 MB with the compiled core — the analyzer trees of core.clj
  against the compiled bodies. The twelve forms then take the process to 23–29 MB and it does not come back
  down (NOTES "Allocator", empty slabs; `coro.c`'s pooled stacks).
- [~] **No device run.** Blocked by signing, not postponed: a bare Mach-O cannot be launched on a device — there is no
  `simctl spawn` for one, and `devicectl device process launch` takes an installed bundle id, so the probe
  would need an `.app` signed with a development provisioning profile for a new bundle id and the target
  device. An "Apple Development" identity and a paired iPad are on this machine; the profile is not, and
  registering an app id is outside this work. Xcode 26.3's `DeviceSupport` also stops at 16.4, below the
  paired iPhone's 26.6.1. The arm64 simulator is closer to a device than it looks: `getpagesize()` is 16384
  there, the device's value, not the 4 KB of an x86_64 host (`docs/portability.md`, "The page size"), and a
  third-party app is arm64, not arm64e, so `trace.c`'s PAC stripping is identity in both. What is left for
  the device is jetsam under real pressure and the real clock. Trigger: the §10 app, which needs a bundle anyway.
