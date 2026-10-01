## Arrays (Sources/CljCore/array.c, builtins_array.c)

- [~] **Elements live inline, after the header**, not behind a pointer: `{rc, flags, type, kind, count, data[]}`,
  one allocation, one load to reach an element and a fixed offset from the object to the first byte. An array
  is fixed-length and never `clj_realloc`'d, so that address is stable for its life, which is what a zero-copy
  handoff needs — a `u8` or `f32` array can become a `Data`/`UnsafeBufferPointer` over `clj_array_data`
  without a copy. The cost of inline is that the whole object goes through the size classes, so a big array
  lands in the system allocator (`CLJ_FLAG_LARGE`) and there is no resize and no page-aligned buffer; an
  external buffer would give both and cost an indirection on every `aget`. Trigger for the pointer form:
  a Metal buffer that must be page-aligned, or `MTLBuffer`-backed storage the array only views.
- **Ten element kinds, one type.** `i8 u8 i16 i32 i64 f32 f64 bool char object`; Clojure's constructors name
  seven of them (`byte short int long float double` plus `boolean`/`char`/`object`) and `u8` exists for the
  bridge. A kind is named by keyword in either spelling (`:int` and `:i32`), and `make-array`/`into-array`
  take that keyword where the JVM takes `Integer/TYPE` — a class object is interop and has no representation
  here. `char` elements are 4-byte Unicode scalars, not UTF-16 units, so a `char-array` is twice the JVM's.
- **Reads box, writes range-check.** `aget` returns a fixnum, a double, a bool, a char or the stored value;
  an `i64` past the fixnum range promotes to a bigint, as the rest of the tower does. `aset` casts the way
  `RT.byteCast`/`intCast`/`floatCast`/`booleanCast` do — an integer kind truncates a double toward zero and
  refuses what leaves its range ("Value out of range for byte: 300"), `bool` takes truthiness, `char` takes
  a char or a scalar. `aget`/`aset`/`alength` are intrinsics (intrinsics.h) and impure ones: an array is
  mutable, so the optimizer may not fold them.
- **An array is mutable, so it is outside the reuse analysis.** `clj_is_unique` is never consulted: `aset` is
  a plain write into the object the caller already holds, and `aclone` is the only copy. An `:object` slot
  retains what goes in and releases what it replaces, and a write into a *shared* array shares the new value
  first, keeping the invariant that every child of a shared object is shared. Two threads writing one shared
  array race, as they do on the JVM; nothing in the core makes that safe.
- **Seqable and nothing else.** `core_bits` is `CLJ_CORE_SEQABLE` alone, as a JVM array is no
  `IPersistentCollection`: `(coll? a)`, `(counted? a)`, `(indexed? a)` and `(sequential? a)` are all false,
  while `count`, `lookup` and `reduce` are slots without their bits and `nth`/`contains?` special-case the
  type by hand, the way `RT.get`/`RT.nth`/`RT.contains` special-case `String`. `seq` is an O(1) view
  (`clj_array_seq`, 32 bytes per `next`) that the iterator walks through its slots, not inline. `=` and
  `hash` are identity, as on the JVM, so an array is a map key by address and never equal to its clone.
- **Printing writes the elements**, `#array[:int 1 2 3]`, where the JVM prints `#object["[I" 0x… "[I@…"]`
  (docs/jvm-differences.md). The printer boxes every element into a frame of owned entries, so printing a
  big array allocates the whole row; and an `:object` array that holds itself prints forever, the same
  hazard a self-referential lazy seq already has (no `*print-length*`).
- [ ] **`vector-of` is a normal persistent vector** whose elements went through the kind's cast, so
  `(vector-of :byte 300)` throws and `(vector-of :float 0.1)` holds `0.10000000149011612` as on the JVM, but
  the storage is boxed `clj_value`s and `conj` onto it forgets the kind. An unboxed persistent vector needs
  the trie to carry an element kind and every leaf to be typed, which is the "elements kinds" item of design
  §4; the typed array is the piece that item stands on. Trigger: a `vector-of` in a profile, or the first
  code that wants `(vector-of :f32)` handed to Metal.
- [ ] **No multi-dimensional arrays**: `make-array` takes one dimension and `aget`/`aset` one index, where the
  JVM nests. An array of arrays is written out by hand. Trigger: a library indexing `(aget m i j)`.
- **`aset-int` and its siblings are aliases of `aset`**: the array's kind decides the cast, so `aset-int`
  into a `double-array` stores a double where the JVM would refuse the array type at compile time.

