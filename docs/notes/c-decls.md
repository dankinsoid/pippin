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
- **A mutable global is a reference, `@Name` reads it now.** `NSFoundationVersionNumber` and
  `kCFCoreFoundationVersionNumber` are declared *without* `const`, and a value snapshotted into a var at load is not
  that global's value later. Constness is read where the ABI puts it — at the pointer for a pointer global, at the
  value for a scalar — so AppKit's `NSWindowDidResizeNotification`, declared without `const`, is a reference while
  UIKit's and Foundation's notification names, `NSString *const`, are values. The reference is a `reify` of `IDeref`
  in the generated file (`c-global-ref`), whose deref is one more `c-global*`: a `dlsym` per read, which is what a
  global the program may reassign costs; the load still reads it once, so a miss is a load-time report line. An
  object global read this way races its writer exactly as any C reader of it does.
- **An object global is a value, which is what settled its ownership.** A `const NSString *` is read once by
  `dlsym` at load and crosses as a level-1 `@` return does: an `NSString` or `NSNumber` as ours, any other object
  as a handle whose +1 is never given back — right, because an immortal global has nobody to give it back to. So
  the handle rule of NOTES "ObjC bridge" ("a reference, the caller owns it") needed no exception and no new kind of
  value: `NSBundleDidLoadNotification` reaches `-[NSNotificationCenter addObserverForName:…]` as the string the
  framework holds. A raw pointer global (`CFRunLoopMode`, a `const struct __CFString *const`) is refused instead:
  what it points at has no owner the bridge can name, and `CFRelease` on it would be a guess.
- **A C function is `dlsym` plus the level-1 dispatcher, and the header's types travel as its encodings.**
  `(def UIApplicationMain (c-fn* "UIApplicationMain" "i" ["i" "^*" "@" "@"]))`: the parse maps clang's
  *desugared* spelling of each parameter onto the Objective-C type encodings that `signature_shape` and
  `classify_struct` already read, so there is one classifier and one prototype table, not two. `id`, `Class` and
  `SEL` are keyed on the alias instead, because `SEL` desugars to `SEL *`; a pointer whose desugared pointee is
  still a bare identifier is an Objective-C object, since a typedef chain ends at a builtin, a tag or a class and
  only the last is left. `c-fn*` resolves the symbol and builds the signature once, at load, and the var holds a
  plain fn afterwards, so a compiled unit calls it like any other fn (as with a global: a compiled unit runs its
  own `ns` form and finds the module parsed).
- **A return type is desugared by declaring a variable of it, because a function type is not.** clang's JSON gives
  `desugaredQualType` per `ParmVarDecl` but only one `qualType` for the whole `FunctionDecl` (`int (int, char **)`),
  and `@encode`'s text is nowhere in the AST — the `ObjCEncodeExpr` node carries the encoded *type* and the string's
  length as `char[7]`, never its bytes. So the return spelling is split off that signature and fed back to clang as
  `static <ret> __clj_v_r0;`, one probe for every function of the module at once; a spelling the split got wrong
  fails to compile there and is refused with clang's own message rather than guessed at.
- **An enum parameter is its underlying integer type, and clang is asked which rather than guessed.** An
  `NS_ENUM(NSUInteger, …)` parameter desugars to `enum NSSearchPathDirectory` and an anonymous
  `typedef enum {…} E` desugars to nothing at all, so neither is in the encoding tables. The value probe answers
  both: `sizeof(T)` and `((T)-1 < (T)0)` are two more expressions for the same `enum : long long` fold, and the
  pair gives the encoding char. A spelling for which neither folds — a struct, a union — is left to be refused,
  because `sizeof` folds for those too and the signedness cast does not.
- [ ] **A struct type is not itself a value.** A struct a function takes or returns by value crosses as a map
  (below), but naming the type in `:refer` is still a report line: `deflayout` (design §4) is not built. Trigger:
  `deflayout`.
- **Swift's import of the same header renames and retypes it, and the test is the evidence.** Reading the fixture's
  own global from Swift does not compile as `NSAppKitVersionNumber` — swiftc answers "has been renamed to
  `NSAppKitVersion.current`" and the value is a `RawRepresentable` struct over the double. The header gives the C
  name and a number (design §5 «Декларации — из заголовка, clang'ом»).
- **Build environment: nothing new on a cold machine.** The generator needs `clang` on `PATH` and python3, both of
  which the build already requires; it uses no clang Python bindings and no libclang, only `-Xclang -ast-dump=json`
  of the clang in the toolchain. On a machine without an SDK the fixture test fails where any other SDK-dependent
  test does.
- **Refused by name at parse time, each with its reason, so nothing throws at a call site.** A variadic function
  (arm64 Apple puts a variadic argument on the stack and a fixed prototype does not place one); a `static` or
  `inline` one, whose body is in the header and in no binary — `static` is what makes the symbol missing, so
  `inline` alone is not the test; a declaration with no prototype; a shape past the eight integer or eight
  floating-point slots, or mixing `float` and `double`; and a functional macro, which the value probe refuses with
  clang's diagnostic as it always did.
- **A struct by value is a map of the header's member names** (design §5 «C — уровень 0», third slice). The
  JSON AST carries no `@encode` text, so the parse builds the encoding itself: one `-ast-dump-filter <tag>` dump
  per struct reached by value, the `RecordDecl` with `completeDefinition` its fields, recursively for a member
  struct, the members' enums through the same scalar probe as a parameter's. The encoding names each member as an
  ivar's does, `{_NSRange="location"Q"length"Q}`, so objc.c's one classifier and marshaller read it; a named
  encoding wins over level 1's built-in table, which is the fallback for runtime encodings that never carry names.
  Refused by the parse, with the struct named: a pointer or object member (no owner the header names; and an
  `@"Class"` suffix would read as the next member's name), a bit-field, a union, an array member, a struct with no
  definition the header shows. The filter matches by substring, so the dump of `CGPoint` prints `CGPointMake` and
  the rest too; the definition is picked out by name.
- **A `dlsym` miss or an ABI refusal at load is one name's report line.** Whether a symbol is linked is a fact about
  the program, not about the header, and whether a struct shape passes is a fact about the architecture
  (`CGRect` by value on x86_64). The generated file binds each global and function through its private
  `c-bind!`, which interns the value or writes `(ex-message e)` into the namespace's `:pippin/c-refused`, so
  `require-c` names that one symbol "Unable to resolve" with the reason and the module's other names load. The
  fixture's `clj_fixture.h` declares two symbols nothing links.
- **The compiler calls through the same dispatcher, and the design's "direct call" is a later slice.** §5 wanted
  `#include` of the header and an unboxed call out of the generated C. The headers of an Apple framework are
  Objective-C and a unit is compiled as C17 (`-std=c17 -Wall -Wextra -Wpedantic -Werror`, `CljCompiler/jit.c`), so
  `@interface` does not compile there at any sysroot. The cheap shape, when it is wanted, is a unit-local
  `static int (*p_UIApplicationMain)(int, char **, const void *, const void *)` resolved once at unit init:
  unboxed arguments, types from the header, plain C17, no SDK flags in the driver. Trigger: a measured boundary
  cost that the dispatcher's marshalling dominates.
- **No `:host-call` fact is emitted, and the design says not to.** §5 lists one beside the signature, but the facts
  pass deliberately has no host-call effect bit (NOTES "Facts", "Deliberately not here") — a prohibition needs the
  call chain and `opaque` is the admission it is not always there. A `c-fn*` var holds a native fn, which the walk
  already treats as opaque, so a C call reads exactly like a Swift one.
