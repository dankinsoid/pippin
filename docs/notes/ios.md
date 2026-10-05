## iOS (scripts/ios-probe.c, scripts/ios-sizes.sh, scripts/ios-app.sh, scripts/ios-app/)

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
- **The bundle is a directory and four commands, not an Xcode project** (`scripts/ios-app.sh`,
  `scripts/ios-app/main.c`, `scripts/ios-app/screen.clj`). An `.app` the simulator installs and launches is the
  probe's own link plus `-framework UIKit`, an `Info.plist` written by `cat`, the screen's `.clj` copied in beside
  the executable, and `codesign --force --sign -`. A `.xcodeproj` buys nothing and costs the package's own compile
  flags (the `-Wshorten-64-to-32` story above), so the bundle is built the way the probe is: SwiftPM compiles
  `CljCore` for the triple, clang links, and the product type an Xcode target would add is the twenty lines of
  plist and signing. `make ios-app` builds, installs, launches and measures both modes under the id `dev.pippin.app`.
  The bundle is also the load path: `c-headergen.py` writes the parse of the screen's `(:require-c [UIKit …])` to
  `<app>/pippin/c/UIKit.clj` against that SDK and triple, and `main.c` points `clj_load_path_set` at the
  executable's directory before loading the screen (NOTES "C declarations").
- [~] **The app has no delegate class, because a reified class is not one UIKit can allocate.**
  `(UIApplicationMain 0 nil nil nil)` leaves the process without a principal class and without a delegate,
  and `screen.clj` mounts the screen from a zero-delay `NSTimer` block scheduled on the main run loop before
  `UIApplicationMain` turns it. The reason is `objc-reify`: its Clojure fns live in the extra bytes of
  `class_createInstance(cls, sizeof(reify_state))` (objc.c), while a host handed a class *name* —
  `UIApplicationMain`'s delegate, `NSPrincipalClass`, a storyboard, `NSClassFromString` — sends `+alloc`, which
  allocates `class_getInstanceSize` bytes and leaves `object_getIndexedIvars` reading past the object. Not done:
  a reify shape whose instances the host may allocate (a real ivar for the state, or an `+alloc` of our own).
  Trigger: an API that takes a class where an instance will not do; the window lifecycle is not one.
- **The screen is Clojure and UIKit is level 1** (`scripts/ios-app/screen.clj`, 60 lines). A `UIWindow` over a
  `UIViewController`, a monospaced `UILabel` and a `UIButton` in a centred `UIStackView` held by two
  `NSLayoutConstraint` anchors. The button's target is an `objc-reify` of one method (`"tap:" "v@:@"`) doing
  `(swap! taps inc)`, and the label is an `add-watch` on that atom (design §4 «Подписки»): the text is re-read
  from the atom, not written by the handler. A second timer block taps the button three times through
  `sendActionsForControlEvents:`, so a run with nobody's hand on the simulator still proves target/action — the
  label reads `taps: 3` in both modes, interpreted and `-DCLJ_COMPILED_CORE`. The bridge carried all of it with
  nothing added: `CGRect` as a nested map out of `-[UIScreen bounds]` and back into `initWithFrame:`, a Clojure
  string into `setText:` and an `NSString` return read as one, `false` as a `BOOL`, an `NSArray` of handles
  through `ns-array` into `initWithArrangedSubviews:` and `activateConstraints:`, a selector as the string
  `"tap:"`, and two `objc-block`s for the timers.
- **The screen names its entry point, and the shell hands over rather than driving.** `UIApplicationMain` does
  not return, so calling it at the tail of the screen's own load would take `main.c`'s footprint report and its
  settle timer with it — both of which have to be in place before the run loop turns, because the run loop is
  what delivers them. So `screen.clj` ends in `(defn -main [] (UIApplicationMain 0 nil nil nil))`, the load
  finishes with the two `NSTimer` blocks already scheduled, and the shell evaluates `(pippin.screen/-main)` after
  its own measurement — which is where a Clojure program's entry point lives anyway. `argc` 0 and `argv` nil are
  what UIKit gets; it reads neither (verified in both simulator modes, three taps and the `go-main` body
  unchanged).
- **The main carrier is UIKit's own run loop.** The shell calls `clj_sched_main_install` on the main thread
  before `UIApplicationMain`, and `(a/go-main (a/<! (a/timeout 200)) (swap! taps + 10))` from the screen runs
  when the app's run loop turns: the atom's watch sets the label from inside the coroutine and the screen reads
  `taps: 13`. So the `CFRunLoopSource` carrier of `sched.c` needs no run loop of its own in an app — UIKit's is
  the one it was designed for, and a `go-main` body may touch the view tree because it is on the main thread by
  construction.
- **A UIKit target is unretained, so the Clojure handle is the only owner.**
  `addTarget:action:forControlEvents:` does not retain its target, so the reified object and the timer blocks are
  parked in an atom: releasing the wrapper is the instance's `dealloc`, and the next tap would message a freed
  object. Level 1 is consistent here — a handle is a reference and ownership stays the caller's (NOTES "ObjC
  bridge") — but it is the first thing a UI layer above it has to decide on its user's behalf.
- **§10 step 8's first sample: this screen needs 30 selectors and not one Swift symbol.** The step's first
  delivery is a number, how many Swift-only symbols a real application needs; this is one screen, not an
  application, and it is counted by hand off `screen.clj`.
  *Level 1, works today — 13 classes, 30 selectors, three calling-in objects:* `UIScreen` (`mainScreen`,
  `bounds`), `UIWindow` (`alloc`, `initWithFrame:`, `setRootViewController:`, `makeKeyAndVisible`),
  `UIViewController` (`init`, `view`), `UIView` (`setBackgroundColor:`, `addSubview:`,
  `setTranslatesAutoresizingMaskIntoConstraints:`, `centerXAnchor`, `centerYAnchor`), `UIColor`
  (`systemBackgroundColor`), `UIFont` (`monospacedSystemFontOfSize:weight:`), `UILabel` (`setFont:`,
  `setTextAlignment:`, `setText:`, `text`), `UIButton` (`buttonWithType:`, `setTitle:forState:`), `UIControl`
  (`addTarget:action:forControlEvents:`, `sendActionsForControlEvents:`), `UIStackView`
  (`initWithArrangedSubviews:`, `setAxis:`, `setSpacing:`, `setAlignment:`), `NSLayoutAnchor`
  (`constraintEqualToAnchor:`), `NSLayoutConstraint` (`activateConstraints:`), `NSTimer`
  (`scheduledTimerWithTimeInterval:repeats:block:`); one `objc-reify` and two `objc-block`s.
  *Swift-only and already generated by the stub generator — none.* Every class and method above is `@objc`;
  level 2 was not needed for one symbol of the screen.
  *Swift-only or header-only and refused — eight, and six of them are constants.* `NSTextAlignmentCenter` (1),
  `UIButtonTypeSystem` (1), `UILayoutConstraintAxisVertical` (1), `UIStackViewAlignmentCenter` (3),
  `UIControlEventTouchUpInside` (64) and `UIControlStateNormal` (0) are `NS_ENUM`/`NS_OPTIONS` values that live
  in a header and nowhere else — `UIControlEventTouchUpInside` is not in `UIKit.tbd` and the runtime has no call
  that names one. `UIFontWeightRegular` is an exported `const CGFloat` (it *is* in the .tbd), reachable by
  `dlsym` and by nothing the bridge offered. All seven now come from the header through level 0's
  `(:require-c [UIKit :refer [...]])`, and the screen writes no number of its own (NOTES "C declarations",
  design §5 «C — уровень 0»); the six values the parse answers are the six counted here.
  `UIApplicationMain` is a C function, which level 0's second slice calls: the screen declares it in the same
  `(:require-c [UIKit …])` and `main.c` has neither its declaration nor its call.
  **That list is the demand signal** NOTES "Host bridge" ("What the generator does not generate") waits for: the
  first symbols an application needs and level 1 cannot reach are not functions but constants, and a wrong
  number there is a silently wrong screen, not an error.
- **What level 2 would do with the same screen, asked with the generator's own classifier.**
  `swift-stubgen.py`'s `plans` over a UIKit symbol graph extracted for `arm64-apple-ios15.0-simulator` (the
  generator itself cannot run there: `SwiftStubs.generate` needs Foundation's `Process`, which iOS does not
  export, and `swift-reprint.py` extracts for the host's triple). Of the 34 UIKit declarations behind the screen
  it reprints 11 and refuses 23; the 35th, `init()`, is `NSObject`'s and not in UIKit's graph at all. The
  refusals are optionals (`String?`, `UIView!`, `UIColor?`, `Any?`), `CGRect` and `CGFloat` and the other imported
  types, `[UIView]` and `[NSLayoutConstraint]`, the generic `NSLayoutAnchor.constraint(equalTo:)` and the four
  enum cases. Three of the six constants do reprint — `UIControl.Event.touchUpInside`, `UIControl.State.normal`
  and `UIFont.Weight.regular` are static properties of structs — but each answers a **box** of its Swift type,
  which a level-1 send cannot take where `sendActionsForControlEvents:` wants a number. So a stub does not by
  itself close the gap the constants open: the levels have to meet at the call site, or the constants have to
  arrive another way (design §5 «Три уровня, одновременно»): from the header, as numbers (design §5 «C — уровень 0»),
  which is the way they now arrive.
- **The bundle's size is the probe's binary plus a screen** (release, arm64, dead-stripped, as above).
  Simulator slice: 1,107,664 bytes interpreted and 3,213,056 with `-DCLJ_COMPILED_CORE`, against the bare probe's
  1,086,896 and 3,192,288 — **+20,768 bytes in both modes**, of which the `__text` difference is 648 bytes and the
  rest is `__LINKEDIT` and padding. Device slice: 1,113,312 and 3,202,328 against 1,110,640 and 3,199,648,
  **+2,672 and +2,680**. On disk the `.app` is 1,096 KB interpreted and 3,152 KB compiled (the executable, a
  3 KB `screen.clj`, `Info.plist` and the ad-hoc `_CodeSignature`). So a UIKit application is the same
  "1.1 MB interpreted, 3.2 MB with the compiled core" the baseline named: UIKit itself ships with the OS, and the
  screen's own cost is its source file. Re-measured after level 0 (`cdecl.c`, `require-c` in core.clj and the 1 KB
  `pippin/c/UIKit.clj` the bundle now carries): 1,126,272 and 3,333,232 simulator, 1,115,368 and 3,321,528 device,
  1,116 KB and 3,272 KB on disk. After the second slice (the call, `c-fn*`, `UIApplicationMain` out of `main.c`):
  1,126,784 and 3,333,760 simulator, 1,115,896 and 3,322,064 device, 1,120 KB and 3,272 KB on disk — **+512 to
  +536 bytes**, which is one more `dlsym` path and one more generated declaration. The figures above predate both that and an SDK re-link, so the difference is not
  the slice's alone; what it says is that level 0 did not move the "units of MB" the baseline is about.
- **Footprint with a screen standing (simulator, two runs per mode).** `phys_footprint` at `main`, before
  `UIApplicationMain` and with UIKit only mapped: 11.1–11.6 MB, against the bare probe's ~10 MB. `clj_init` then
  costs **+5.7–6.2 MB interpreted and +1.05–1.3 MB compiled**, the probe's own figures inside a real app.
  Loading `screen.clj`, whose `ns` requires `clojure.core.async`, costs +1.7 MB interpreted and +0.8–0.9 MB
  compiled — the library's source is analyzed either way, since `require` reads it even with the compiled units
  registered. With the window up, three taps delivered and the `go-main` body run, the process is **45.4–45.5 MB
  interpreted and 39.5–39.7 MB compiled**: UIKit's own ~26 MB for a window, a view controller and a text layout
  dwarfs the 6 MB the two core modes differ by. The number §10 asks for is still the boot one; the number that
  matters for jetsam is that a trivial screen is already 39 MB, and almost none of it is ours.
- [~] **No device run, and the only thing still missing is a profile.** The bundle exists now, installs and
  launches on the simulator (above); the device slice of the same `.app` comes out of
  `sh scripts/ios-app.sh <mode> iphoneos` unsigned, and `sign_device` there embeds a profile, takes the
  entitlements from it as Xcode does and signs, so the run is three commands once there is one. On this machine:
  the identity **Apple Development: Danil Voidilov (E4VXARX5DQ)**, team 4733T56UZW, valid to 2027-02-27, and
  three paired devices (`xcrun devicectl list devices`) — an iPhone 16 on 26.6.1, an iPad (A16) on 18.6.2, an
  iPhone 15 Pro on 26.2. Not on it: any profile for `dev.pippin.app`. All 45 local profiles belong to the work
  team 79WNND69Y6, each for a fixed `app.tabby.*` id and none a wildcard, so not one of them can sign this
  bundle. What the user must authorize, in order, and what this work deliberately did not touch:
  1. the App ID `dev.pippin.app` (or a wildcard one) under team 4733T56UZW;
  2. the target device's UDID in that team — iPhone 16 `00008140-000C64620ABA801C`, iPad
     `00008120-0008588001E00032`;
  3. an iOS App Development profile over that App ID, that device and that certificate, downloaded to a file;
  4. Developer Mode on the device (Settings → Privacy & Security), which iOS 16 and later demand before any
     development-signed app runs.
  Then, with nothing further to authorize: `CLJ_APP_PROFILE=<file> CLJ_APP_IDENTITY='Apple Development: Danil
  Voidilov (E4VXARX5DQ)' sh scripts/ios-app.sh interpreted iphoneos`, `xcrun devicectl device install app
  --device <udid> <app>`, `xcrun devicectl device process launch --console --device <udid> dev.pippin.app`.
  Xcode's automatic signing with a personal team does 1–3 by itself, at the price of a seven-day profile and of
  letting Xcode register the id and the device — the same authorization, asked differently.
  The `DeviceSupport` ceiling is probably not in the way: that directory (16.4 under Xcode 26.3) is the
  pre-CoreDevice debugging path, and an iOS 17 or later device is served by the personalized DDI at
  `/Library/Developer/DeveloperDiskImages/iOS_DDI`, which is this Xcode's own (build 17C529). Unconfirmed until
  the run, like everything else here. The arm64 simulator is closer to a device than it looks: `getpagesize()`
  is 16384 there, the device's value, not the 4 KB of an x86_64 host (`docs/portability.md`, "The page size"),
  and a third-party app is arm64, not arm64e, so `trace.c`'s PAC stripping is identity in both. What is left for
  the device is jetsam under real pressure and the real clock. Trigger: an App ID, a registered device and a
  profile for `dev.pippin.app`, which only the owner of the account can make.
