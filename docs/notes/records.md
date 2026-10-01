## Records (Sources/CljCore/record.c)

- **A record type is the named half of design §4's shapes** (the other half is the "Shapes" section): a `clj_user_type` descriptor with the basis
  field keywords in a trailing C array, the hash map's `core_bits` (minus `IEditableCollection`) and the
  map slots filled from C. `clj_record_type_new` allocates the longer descriptor and hands it to
  `clj_user_type_init`, the half of `clj_user_type_new` that fills a descriptor the caller allocated, so a
  record gets the deftype name, field vector and `user_protos` without a second copy of that code. A plain
  keyword-keyed map has the same layout with a shared, interned shape in place of the named descriptor, and the
  keyword-lookup site cache serves both; the two descriptors stay separate (the "Shapes" section says what a
  merge would take).
- **An instance is one allocation**: header, the basis values inline, then `extmap` and `meta`. The basis
  comes first on purpose — that prefix *is* a `clj_instance`, so `field*` and the whole `deftype` macro
  body machinery (`method-map`, the groups, the `let` over `field*`) read a record's fields unchanged and
  `defrecord` shares `field-wrap` with `deftype` in core.clj. `clj_is_instance` is therefore true for a
  record; only `new*` has to tell them apart, by the `CLJ_CORE_RECORD` bit, because the sizes differ.
- **Lookup is a linear pointer scan over the interned basis keywords**, which is what `condp identical?`
  compiles to in Clojure's own `defrecord`; `assoc` of a basis key writes the slot in place when
  `clj_is_unique`, like the hash map's consuming path, and of any other key goes to the extmap.
  bench/RESULTS.md, "Records": 1.8 ns for the first field, 2.8 for the third, 4.8 for a unique `assoc`.
  A `(:k r)` site with a literal keyword skips the scan through the keyword-lookup cache ("Shapes"): 7.0 ns
  compiled at any position, against the trie's 12–13.
- **An empty extmap is normalized to nil**, so two records of equal content are equal whatever route built
  them: `(= (dissoc (assoc r :z 1) :z) r)`. Equality needs the same descriptor pointer and compares the
  basis slots and the extmaps; `(= record map)` is false in both directions, which map.c and sorted.c
  enforce by rejecting a record in their own `equals`.
- **`hash` is the map hash of the same content**, not Clojure's `(bit-xor (hash classname) (mapHasheq
  this))`. Equal hashes across representations cost collisions only — `=` still separates them — and the
  entry mix comes from map.c (`clj_map_entry_hash`) so the two cannot drift. There is no hash cache on a
  record yet; trigger: records as map keys in a profile.
- **`dissoc` of a basis key gives up the shape** and answers a plain hash map of everything, with the
  record's meta, as the JVM's generated `without` does; of an ext key it stays a record. `empty` throws
  "Can't create empty", `conj`/`merge`/`reduce-kv`/`into`/`select-keys` behave as for a map, and
  `select-keys` returns a map because it builds onto `{}`.
- **The body may implement protocols only.** A core interface in a `defrecord` form is refused by name:
  every slot behind one — `seq`, `count`, `valAt`, `invoke`, `meta`, `reduce`, `hasheq`, `equiv` — is the
  record's own, and a trampoline over it would break the map contract that `record?` promises. A record
  reaches `IDeref` and every other `defprotocol` exactly as a deftype does, through `extend`. Trigger for
  opening it up: a library that puts `IExceptionInfo` or `IFn` on a record.
- **`record*` is a builtin call in the expansion, so the descriptor is runtime state**, never a node
  constant: a record instance is as unserializable as a deftype instance (node_data.c refuses both), and a
  macro that embeds one fails the same way. `#ns.Name{…}` prints but does not read back
  (docs/jvm-differences.md).
- **`IEditableCollection` is a bit, not interop.** medley's `editable?` is
  `(instance? clojure.lang.IEditableCollection coll)`; `CLJ_CORE_EDITABLE` sits on the hash map, the
  vector and the hash set — exactly the types the JVM answers true for — and the interface is bound under
  both its bare name and the dotted one libraries spell out. Transients are the persistent operations
  here, so the bit answers the predicate and nothing else.

