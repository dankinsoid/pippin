## C declarations, level 0 (scripts/c-headergen.py, Sources/CljCore/cdecl.c, `require-c` in core.clj)

- **The parse is a build step, and the program loads its product.** `(:require-c [UIKit :refer [...]])` in an `ns`
  expands to `require-c`, which looks for `pippin/c/<Module>.clj` on the load path and loads it; the file is
  written by `scripts/c-headergen.py`, which scans the same `ns` forms. Nothing runs clang at runtime: the device
  has neither clang nor an SDK, and the C-only host that runs there has no subprocess API. The `.app` carries the
  generated file beside `screen.clj` and `main.c` sets the bundle directory as the load path.
- **clang's JSON AST carries no value for a macro or an implicitly numbered enum constant.** A folded value sits
  on the `ConstantExpr` of an *initializer*, so `UIControlEventTouchUpInside = 1 << 6` dumps as `"value": "64"`
  while `UIStackViewAlignmentCenter`, numbered by position, dumps nothing and `#define` is not in the AST at all.
  So the generator makes clang the evaluator: one `typedef enum __clj_v_probe : long long { … = (NAME), … }` whose
  initializers are the requested names, dumped with `-ast-dump-filter __clj_v_`. The fixed underlying type is what
  lets a value past `INT_MAX` through; one past `LLONG_MAX` (`NSUIntegerMax`) is an error, and an error is what we
  want, since a Clojure long could not hold it either.
- **A name that does not fold is dropped by the line of its diagnostic and classified by a second dump.** clang
  reports every bad initializer in one run with `<stdin>:<line>`, which maps back to the name; the remainder get
  one `-ast-dump-filter <name>` dump each, where a `VarDecl` with `desugaredQualType` `const double` is a global to
  read and a `FunctionDecl` is a refusal. That is two or three clang runs per module, against 279 MB of JSON for an
  unfiltered dump of UIKit's umbrella.
- **The parse is directed by the `:refer` names, not by the header.** The declared boundary is the `ns` form
  (design §5 «Объявленная граница»), so the names in it are the generator's input and `:refer :all` is refused —
  there is nothing to enumerate without dumping the whole umbrella. The names are therefore part of the cache key.
- **The cache key is the inputs; the hit is validated against the headers clang read.** `<cache>/c/<sha>/` is keyed
  by the generator's own bytes, the module, the header, the target, the SDK, the flags, the sorted names and
  clang's version; the entry records every file of a `-MD` run with its size and mtime, and a hit re-stats all of
  them (1117 for UIKit). Cold parse 3 s, hit 0.13 s. `PIPPIN_CACHE` moves the store, which defaults to
  `~/Library/Caches/pippin`.
- **`c-global*` is `dlsym(RTLD_DEFAULT, …)` and nothing else.** An exported `const` scalar is read once, when the
  generated file is loaded, and the var holds the number afterwards — so the interpreter and a compiled unit agree
  without the compiler emitting anything (a compiled unit runs its own `ns` form and finds the module parsed). It
  resolves only in a linked image: the test has to touch `NSAppKitVersion.current` from Swift for AppKit to be
  linked into the test binary at all.
- [~] **A mutable global is refused, and that is a correction to the design.** §5 said "`extern const` и глобалы";
  but `NSFoundationVersionNumber` and `kCFCoreFoundationVersionNumber` are declared *without* `const`, and a value
  snapshotted into a var at load is not that global's value later. Reading per access needs a var form that is not
  a constant — a getter fn, or a var kind that re-reads. Not done. Trigger: an API whose mutable global is the API.
- [~] **An object global is refused, and it is the next demand after the constants.** `NSWindowDidResizeNotification`
  and every other `NSNotificationName` is a `const NSString *`, which would cross as a level-1 handle; what is not
  decided is its ownership, since the handle rule is "a reference, the caller owns it" (NOTES "ObjC bridge") and an
  immortal global has no owner. Not done. Trigger: an app that registers for a system notification.
- [~] **A function is a line of the report, not a var.** The design's order puts the call in the second slice, so
  the generator refuses a `FunctionDecl` by name with that reason, and `require-c` raises it as
  "Unable to resolve UIKit/UIApplicationMain: a C function: calling one is the second slice". A var that resolved
  and threw on the call would defer the same failure to runtime and would fix a signature the second slice may have
  to refuse. Structs (`deflayout`) are refused the same way. Trigger: the second slice.
- **Swift's import of the same header renames and retypes it, and the test is the evidence.** Reading the fixture's
  own global from Swift does not compile as `NSAppKitVersionNumber` — swiftc answers "has been renamed to
  `NSAppKitVersion.current`" and the value is a `RawRepresentable` struct over the double. The header gives the C
  name and a number (design §5 «Декларации — из заголовка, clang'ом»).
- **Build environment: nothing new on a cold machine.** The generator needs `clang` on `PATH` and python3, both of
  which the build already requires; it uses no clang Python bindings and no libclang, only `-Xclang -ast-dump=json`
  of the clang in the toolchain. On a machine without an SDK the fixture test fails where any other SDK-dependent
  test does.
