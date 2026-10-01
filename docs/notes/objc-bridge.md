## ObjC bridge (Sources/CljCore/objc.c, include/clj/objc.h; design §5 level 1)

Level 1 of the bridge: calling anything `@objc` from Clojure with the signature known only at run time.
It lives in C rather than the Swift target although `Package.swift` says host-specific things go through
Swift, because a Swift dispatcher would pay `clj_host_invoke` on every call (~64 ns over a C builtin,
`bench/RESULTS.md` "Host-defined fns") and level 1 is meant to carry most of a real application.
`sched.c` is the precedent; the row is in `docs/portability.md`.

- **A fixed set of `objc_msgSend` prototypes covers every shape we accept, and the rest are refused.**
  The symbol must be called through the prototype the method's type encoding describes: on arm64 the integer and
  floating-point argument registers are separate files, a `float` occupies a v register's low half where
  a `double` occupies all of it, and calling through the wrong prototype is silent corruption, not a
  crash. Two ABI facts collapse the combinatorics. Arguments a method does not declare are harmless —
  they sit in registers the callee never reads — so one prototype with every slot filled serves every
  argument count. And each register class is allocated independently, in the order of that class's own
  arguments, so every integer-class argument (pointer, `SEL`, `BOOL`, `char` through `long`, each passed
  as a full 64-bit slot we extend ourselves) becomes one kind. What is left is whether the
  floating-point slots are `float` or `double`, times the return: `long long`, `double`, `float`, `void`
  and one per struct-return shape (below). Six integer slots and eight floating-point ones keep every
  call register-only on arm64, so no stack argument area is involved.
  **Not covered**, and answered with an error rather than a call through the wrong shape: a union or
  array argument or return, `long double`, a struct over 128 bytes, more than 6 integer-class or 8
  floating-point arguments, and `float` and `double` mixed in one selector.
- **A struct is passed by its AAPCS64 class, not by its size.** The members are flattened and the class
  read off them: up to four members of one floating type are an HFA and travel in `v0`-`v3`, so a
  32-byte `CGRect` is four FP registers and not memory; any other aggregate of at most 16 bytes goes in
  integer registers, and a larger one by a pointer the caller copies to, with `x8` for a return.
  Flattening an HFA into that many `double` slots of the fixed prototype puts the same values in the
  same registers, because each register class is allocated in the order of its own arguments; the
  integer classes are built as the struct's byte image and read out as 8-byte words, which is right
  whatever the member types and padding are. Calling a 32-byte `CGRect` memory because it is over 16
  bytes is the mistake this avoids, and it would be silent. x86_64 classifies by eightbyte instead and
  needs `objc_msgSend_stret`, which arm64 does not have, so there every diverging shape is
  refused (`docs/portability.md`).
- **A struct crosses as a map when we know its field names, as a vector when we do not.** A type
  encoding gives `{CGRect={CGPoint=dd}{CGSize=dd}}`: the struct's name and its member types, never a
  member's name. So the keys come from a built-in table (`CGPoint`, `CGSize`, `CGRect`, `CGVector`,
  `_NSRange`, `CGAffineTransform`, `UIEdgeInsets`, `NSEdgeInsets`, `NSDirectionalEdgeInsets`,
  `UIOffset`), and a struct the table does not name — including an anonymous one such as
  `{?=dddddd}` — crosses as a vector in member order, in both directions. A listed struct also takes a
  vector, and a nested struct nests. Refusing an unnamed struct instead would lock out
  `-[NSAffineTransform transformStruct]`, whose encoding carries no name at all.
- **A variadic selector is refused by name.** No encoding shows variadicity, and a variadic callee reads
  arguments off a stack area the fixed prototypes never build, so `stringWithFormat:` and the dozen
  other known Cocoa variadics are a run-time error rather than a silently wrong call.
- **The autorelease pool lives in the slice between two context switches.** A +0 return is autoreleased
  and needs a pool on the calling thread; our carriers are raw pthreads with none. A pool cannot span a
  park: push and pop are one thread's stack, and a coroutine that parked mid-slice resumes on another
  carrier, where popping its token would corrupt that carrier's. One pool per carrier is wrong for the
  same reason, and one per call throws away the amortization the moment a loop of sends appears. So the
  first send of a slice pushes a pool into `clj_objc_pool_token`, and `clj_coro_switch_out` drains it on
  the way off the carrier — the thread that pushed it, properly nested inside whatever pool the host's
  run loop keeps. Draining at a park loses nothing: `clj_objc_wrap` retains, and `clj_objc_id` is
  borrowed only for as long as its wrapper. Off a coroutine (a host thread calling in) nothing will
  switch, so such a call pushes and pops its own pool.
- **Ownership is split at the selector, not at the call site.** `clj_objc_wrap` retains a +0 return;
  `clj_objc_wrap_owned` takes the caller's reference from the `alloc`/`new`/`copy`/`mutableCopy`/`init`
  families, read off the selector's first word as ARC reads it. An `init` also takes over the reference
  its receiver was created with, so the bridge retains the receiver before sending one: otherwise the
  wrapper around the `alloc` and the wrapper around the `init` own one reference between them, and
  `[[NSAffineTransform alloc] init]` dies when the second one finalizes (`NSMutableString` hides it,
  because its placeholder `init` answers a different object). A `Class` is immortal, so the wrapper
  records `is_class` and the finalizer skips the release rather than asking the runtime again.
- **Selectors resolve from the kebab spelling, cached per (class, spelling).** A capital run is one word
  and digits join the word before them, so `UTF8String` is `utf8-string` and `centerXAnchor` is
  `center-x-anchor`. Resolution kebabs every selector of the class and its superclasses and takes the
  first match, as dispatch would; the parsed signature is cached with it, so a warm call site reads one
  hash lookup. A miss names every selector of the receiver sharing the base name, which is how a wrong
  or reordered label reads against the right order (design §3).
- **What crosses as a value and what stays a handle.** Every `NSString` and `NSNumber` return becomes our
  string and number, whatever the class behind it; `ns-string`, `ns-mutable-string` and `ns-string->str`
  hand back the object itself where one is needed. `@YES` and `@1` answer the same `-objCType`, so the booleans are
  told apart by identity against the `kCFBoolean` singletons. Everything else is an opaque wrapper whose
  equality and hash are identity: `-isEqual:` would run foreign code under a map's lock. Identity is
  defensible here and only here — an Objective-C object is a reference, so it means "the same object".
  A level-2 handle to a Swift struct is a pointer to a box holding a *copy*, where identity would mean
  "the same box", so design §5 has the generator print `==` and `hash(into:)` from the type's own
  `Equatable`/`Hashable` and refuse to be a map key where the type has neither.
- **The analyzer builds the selector, no backend resolves anything from the call's shape.**
  `(.add-target btn self :action sel :for-control-events e)` becomes one `CLJ_NODE_OBJC_SEND` carrying
  the string `add-target:action:for-control-events:`; labels are the odd items and arguments the even
  ones, evaluated left to right. `objc-send` with the full Objective-C text is the escape hatch for a
  selector that is not a literal.
- **A collection crosses by hand only, and the four conversions say so.** `ns-array` and `ns-dictionary`
  take a Clojure sequence or map to Cocoa, `ns-array->vec` and `ns-dictionary->map` bring one back;
  scalars inside convert themselves, a nested collection converts too (a one-level conversion would put
  wrappers Cocoa cannot read inside the array it was handed), and the losses design §5 names are real:
  nil is `NSNull`, a keyword key travels as its name and comes back a string. A receiver is never
  bridged, so `(.count [1 2])` is still an error. Level 2 converts a collection implicitly instead, because
  a stub knows the element type, and a mixed vector where `[Int]` is expected is refused at the call site
  rather than corrupted (design §5).
- **Calling in: one trampoline per return shape, no `NSInvocation`.** The IMP prototype problem is
  `objc_msgSend`'s inverted, and the same two AAPCS64 facts answer it: the caller filled only the
  registers its own selector declares, and reading the rest is harmless because the signature we
  installed the method with says how many there are. So the eleven return shapes of the send side, plus
  one per size of an `x8` return (below), times float or double slots, cover every argument list; `self`
  and `_cmd` say which instance and which method, and a linear scan of a handful of `SEL`s finds it.
  `forwardInvocation:` would be general and cost microseconds a
  `tableView:cellForRowAtIndexPath:` cannot pay; libffi would be a new dependency.
- **A class per reify shape, never disposed.** `objc_disposeClassPair` refuses while an instance lives,
  and a reify inside a loop must not mint a class per instance, so the class is cached under
  (superclass, protocols, selectors, encodings) — which makes it per site by construction — and lives
  for the process, like an interned keyword. The Clojure fns are per-instance state in the extra bytes
  `class_createInstance` puts behind the instance (an ivar would cost a runtime lookup of its offset per
  callback), retained and `clj_share`d because a callback can arrive on any thread and the `dealloc`
  that releases them can run on any thread too.
- **A method's type encoding comes from the protocol or the superclass, never from a guess.** A selector
  no protocol and no superclass declares needs its encoding written beside it (`["doThing:" "v@:@"]`),
  because guessing `void` and object arguments for a target/action would be exactly the silent wrong
  call the send side refuses. The kebab spelling resolves against the protocols first, so a delegate
  method is written the way a call site writes it.
- **A block is a stack block that is copied, as the compiler's is.** `_Block_copy` runs our copy helper,
  so the heap block owns the fn and the dispose helper gives it back; a host that stores the block only
  bumps a refcount. One descriptor per signature, cached and never freed, because the block points at it
  for as long as any copy lives. The invoke pointer has the IMP problem without the `_cmd`, so the same
  trampoline set serves with the block in `x0`.
- **A cache entry is allocated on its own, because callers hold its address long after the lookup.** All
  three tables of the bridge — the `(class, spelling)` selector cache, the reify class cache and the block
  kind cache — hold pointers to entries, never entries, so growing one rehashes the pointers and frees
  only the table. Who may hold an entry's address, and for how long: a send borrows the cached `objc_sig`
  for the call instead of copying its ~150 bytes; a reified instance keeps its row in `reify_state.rc` for
  its whole life, and every callback reads the row; a heap block keeps `&kind->desc`, which libclosure and
  the runtime read on every call, copy and release of it. Nothing is ever evicted, so an entry lives for
  the process. Entries inside the table, as the first cut had them, meant the ninth distinct block
  signature freed the descriptor of every block made before it, and the ninth reify shape freed the row
  every earlier instance dispatches through (`aBlockSurvivesItsKindCacheGrowing`,
  `aReifiedInstanceSurvivesItsClassCacheGrowing`). The hot path pays one pointer load more per selector
  lookup and one `calloc` per *new* (class, spelling), never per call.
- **A body runs with `host_depth` raised, so a park inside it is an error** (design §5, "делегат не
  паркуется"): UIKit calls a delegate and waits for the value now. An error the body did not catch is
  reported where it happened, because a callback has nowhere to throw to. The callback's autorelease
  pool is its own and nested: it cannot span a switch, and popping it pops any pool a send opened inside
  it, so the slice's token is hidden for the duration and restored after.
- [ ] **Not done, and why it shows.** Design §5 wants the label list checked against the type's selector
  table at analysis; that needs an Objective-C type in the facts lattice, which does not exist (the
  facts pass returns ⊤ and `CLJ_EFFECT_ANY` for the node). So a wrong or out-of-order label is a run-time
  error with the right order in the message, not an analysis error. Also absent: a static check of a
  reify method's signature and the boundary bench (it wants a `CLJEncoder` that does not exist yet); that
  bench is an input to a generator decision and not an end in itself, since the size above which a
  collection of values travels as a handle instead of a copy comes from it (design §5). An
  Objective-C class in `catch` position goes through the Swift resolver like any other host type
  (`So7NSErrorC`), not through this bridge.
- **A pointer argument carries what it points at, because the pointer does not.** `BOOL *stop` arrives
  as a borrowed handle, and the width of a write through it is the pointee's encoding, not the
  pointer's, so the wrapper records that encoding and `objc-write!` reads it. Only what the bridge lays
  out by value is written — a number or a boolean: an object or a C string written into an out-parameter
  would die with the callback's pool that autoreleased it, and `^v` has no width at all, so both are
  refused by naming the encoding rather than guessed at. It is not a cell: nothing reads it back, it
  lives until the callback returns, and what it is for is the caller reading the flag — which is the
  test, `-[NSArray enumerateObjectsUsingBlock:]` stopping where the block wrote YES.
- **A struct returned through `x8` is a shape per size, because that is all an indirect return is.** The
  caller passes a buffer sized for the real struct, so a prototype returning a fixed 128 bytes writes
  past it; but an indirect return's ABI reads nothing about the members, only that the aggregate is
  MEMORY class, so `struct { unsigned char b[n]; }` for the real `n` puts the same bytes in the same
  place. A struct's size is a multiple of its widest member's alignment, so the table steps by 4 from 20
  to 128 and the only shapes outside it are aggregates of chars or shorts over 16 bytes, refused with
  their size in the message. The send side needs none of this: our own return buffer is the widest the
  bridge accepts, so a callee writing its real size writes inside it.
- **Calling a block is the send side with the block in place of `(self, _cmd)`.** Its `invoke` takes the
  block in `x0` and the first real argument in `x1`, where a method's `_cmd` sits, so the same prototypes
  serve by passing the first integer argument through the `SEL` slot — which is why the marshalled
  integer words start one slot in. The signature comes from the block itself: `Block_descriptor_3`'s
  pointer sits behind the size word and, only when `BLOCK_HAS_COPY_DISPOSE` says so, the copy/dispose
  pair. No `BLOCK_HAS_SIGNATURE`, no call: there is nothing to derive the prototype from, and every block
  a compiler emits has one. `isKindOfClass: NSBlock` tells a block from any other object, since every
  block class descends from it. A signature the bridge cannot *implement* can still be one it can
  *call* — the trampoline table is the callee's problem — so the kind cache keeps a NULL invoke and only
  `objc-block` refuses it.
- **Mutability cannot decide what converts, so nothing asks it.** `__NSCFString` is a subclass of
  `NSMutableString` whether or not it is actually mutable, so `isKindOfClass:` splits strings by length
  (tagged pointer or not) and not by what the caller can do with them: the first cut converted short
  strings and handed back long ones as wrappers, and nothing in the call text said which. There is no
  honest run-time test — the Clang importer decides this from the declared header type, which
  `method_getTypeEncoding` does not carry (the same gap that blocks the static label check), and where
  Swift is unsure it copies rather than tests. So every `NSString` converts, and the object is asked for
  by name. The cost: an `NSMutableString` a method returns arrives as a snapshot, and a later mutation
  of the original is not seen.
- **An allocation is not an object of its class yet.** `+[NSMutableString alloc]` answers a placeholder
  whose `-length` raises, so the `alloc` family alone returns a handle where every other `@` return is
  read; the `-init` that follows answers a real object and converts like any other.

