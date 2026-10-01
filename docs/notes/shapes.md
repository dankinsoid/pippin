## Shapes (Sources/CljCore/shape.c, shape_internal.h; design §4, shapes by observation)

- **A shape map is the hash map's second layout, not a second type.** `clj_map_type` stays the one map type: a
  map whose keys are all keywords, at most 32 of them and without meta, is `{header, shape, values inline}` with
  `CLJ_FLAG_SHAPE` in the header; everything else is the CHAMP trie as before. Every `clj_map_*` entry, every slot
  of the type and `clj_map_each` dispatch on the flag, so `map?`, `instance? PersistentHashMap`, `(type m)`, the
  protocol tables keyed by descriptor, the facts' `CLJ_T_MAP`, printing, the codec and the Swift `Value` bridge see
  one type and never ask. The flag survives the object's death: rc.c keeps it in the dead-link word beside
  `LARGE` and `META`, since `each_child` reads it on a freed object. `clj_map_of` is private to map.c now;
  `clj_debug_map_root`/`clj_debug_map_cached_hash` are what the tests and core.c read (the "Map" trigger fired).
- **A shape is the key tuple plus its transitions**, malloc'd, interned and immortal (it is never a heap object
  and never counts as live), capped at `CLJ_SHAPE_MAX_SHAPES` (65536); past the cap a new key set is a trie. Two
  ways in: a *transition* (`assoc` of a new key, `dissoc`) walks an edge list on the shape — immutable edges pushed
  at the head under one lock, read without it — and a *key set* (`clj_shape_for_keys`: a literal, the reader,
  `hash-map`, the analyzer's constant maps, `clj_c_map_literal`, all through `clj_map_from_items`) goes to the
  key-set table, read lock-free and confirmed under the lock on a miss. Every transition's child is found through
  the same table, so **one shape per key set**: `{:a 1 :b 2}`, `{:b 2 :a 1}` and `(assoc {:b 2} :a 1)` share it.
  Its key order is `compare`'s (namespace, then name), not the order the set was first built in: that order
  would make `seq` and printing depend on what ran earlier in the process — the compiler fixtures and the
  interpreted-against-compiled corpus diff run in different processes and orders and caught it. An edge whose
  key sorts last appends in place (a dissoc always removes in place: sorted stays sorted); any other carries the
  permutation (`edge.perm`) and copies. The root's edges
  live on the keywords themselves (`clj_keyword.shape1`): the root has as many children as the program has first
  keys, and `(assoc {} k v)` must not scan them.
- **What goes generic, one way, invisibly**: a non-keyword or nil key, `with-meta` with a map (the layout has no
  meta word; `(with-meta m nil)` keeps it), the 33rd key, the shape cap, and a **dictionary-like shape**: a shape
  that has taken more than `CLJ_SHAPE_MAX_CHILDREN` (8) distinct assoc transitions is marked and no assoc through
  it makes a shape map again, existing edges included — the design's detector of `{id → user}` maps built in
  varying order. The root is exempt (its children are the program's first keys), and the key-set paths ignore the
  mark: `{:p 0 :k 1}` is a shape map after `{:p}` went dictionary, `(assoc {:p 0} :k 1)` is a trie. A trie never
  becomes a shape map; the empty map does: an empty trie without meta takes a keyword key as a one-key shape map,
  and a dissoc of the last key answers a fresh empty trie (`clj_map_empty_new`), not the singleton, so
  `(identical? {} (dissoc {:a 1} :a))` is false as on the JVM. Sets stay on the trie (`clj_hash_map_assoc`), and
  so does the keyword intern table, by its symbol keys.
- **Semantics are the map's.** `=` and `hash` are structural and the hash is the trie's (the entry mix of map.c,
  as records do): `(= {:a 1} (hash-map ... as a trie))` is true both ways and the two hash alike, so they are
  interchangeable as keys; two maps of one shape compare slot by slot. `seq`, `keys`, `vals`, `reduce-kv` and
  printing follow the shape's order — the keys sorted by `compare` — which is as unspecified as the trie's
  (docs/jvm-differences.md says nothing: the JVM's array map keeps insertion order for ≤ 8 keys, its hash map
  none; the corpus's `nnext` test, allowlisted before for the trie's order, passes now because its keys are in
  sorted order). No hash cache on a shape map (records have none either); trigger: shape maps as
  set members or map keys in a profile. `record?` is false, `(= record shape-map)` false both ways, a `defrecord`
  keeps its own descriptor: merging the two would mean a record descriptor that *is* a shape (one key tuple per
  name, the record's protocol table beside it) and the record's `dissoc`/`=` rules kept by a flag on it —
  not now.
- **`assoc` and `dissoc` reuse the object at rc 1**, as records do: an existing key writes its slot, a new key
  `clj_realloc`s (the address moves when the size class does: 24 + 8n bytes, 32/40/48/56/64/80/80/96 for one
  to eight keys), a permuted edge copies through the permutation and frees the shell without touching the moved
  children; a shared map copies the slot array (≤ 32 words, one retain each). A unique *published* map keeps the
  `SHARED` flag through the copy, so its new children are shared as the invariant asks. `transient` being the
  persistent operations here, `assoc!` is this path.
- **The keyword-lookup site cache** (`clj_kw_entry`, eval.c `kw_lookup`, `compiled_internal.h` `clj_c_kw_get`; the
  design's inline cache). A site is `(:k m)`/`(:k m nf)` with a literal keyword at the head (an INVOKE whose fn
  node is that constant) or `(get m :k)`/`(get m :k nf)` (the get INTRINSIC with a constant keyword second). In
  the interpreter `clj_exec_new` gives such a node the `eval_kw_invoke`/`eval_get_kw` entry and a `kw_ic` in the
  widened `clj_exec_node.ic` (the "exec table" trigger fired: 24 bytes per node now); `clj_exec_count(off)` and
  a re-derivation put the entry back through `clj_eval_site_entry`, and the get entry keeps the intrinsic's
  root guard (`with-redefs [get …]` takes the generic path). An entry is *(shape or record descriptor, slot
  offset)*: a shape map hits on `shape == cached` alone — shapes are immortal —, a record on `type == cached`
  plus a re-read of the live descriptor's `basis[index]`, since a descriptor can die and its address be reused by
  a record of another basis (the epoch would do too, but any `def` would then empty every record entry); a shape
  without the key caches the absence, a record's ext key is never cached (its extmap decides), a trie, nil or
  anything else is the generic `clj_get` with no fill. Monomorphic, then up to `CLJ_KW_IC_ENTRIES` (4) layouts,
  then megamorphic: `n` past 4 and the generic lookup for good. Fills append under one process lock and never
  rewrite an entry, so a reader that loads `n` sees whole entries below it; the compiled site is one
  `static _Thread_local clj_ckw_ic` per site like the protocol cache, filled without a lock. Both backends
  answer exactly `clj_get` — a representation detail with no fact involved (design §6b, the runtime level).
  Measured (bench/RESULTS.md, "Shapes"): compiled `(:k m)` 6.5 ns at any of five positions against 12–13 for
  the trie; the record's middle-of-basis cost is gone the same way (7.0 at field 3); interpreted, the site is
  ~4 ns under the trie's and the loop around it is the rest.
- **A literal of constant keywords builds straight into its shape**: `eval_map_shaped` (the exec caches the shape
  and the slot of each key, resolved at build; `clj_c_map_shaped` does the same per compiled site, once) — one
  allocation, the values evaluated into their slots, no transitions and no duplicate check (the reader made it).
  A literal with a computed key, or one the cap refuses, is the generic path, which throws "Duplicate key" at
  run time as before.
- **Not done, with triggers.** *Unboxed slots by observation* (`:count` always int64): the slot would need a
  representation tag per shape and a check on every write, and the facts have no element fact for a map value to
  consume it; trigger: a fold like the bench's over a numeric field showing the box in a profile. *Tuples and
  elements kinds*: design §4, not this step. *A hash cache on the shape map*: above. *The record merge*: above.
  *Shapes in the facts lattice* (a literal's shape as a static fact, the direct offset without the guard): needs
  the interprocedural pass to carry it; trigger: a monomorphic site whose guard shows.
- **Inspection.** `clj_map_shape`, `clj_shape_nkeys/key/index`, `clj_shape_is_dictionary`,
  `clj_debug_shape_count/children/bytes`, `clj_debug_exec_kw_entries/hits/misses/site_id`,
  `clj_debug_exec_map_shaped`, `clj_shapes_enable` (the bench's control: off, no new shape map at run time; a
  literal site decided at exec build keeps its shape), and the compiler's `--stats` counts keyword-lookup sites.
  The bench makes 61 shapes (8 KB); the corpus run 321 after both libraries, and of its maps 95k are shape maps
  (53k from a key set, 42k from an assoc into `{}`) against 41k tries — all but six by a non-keyword key (the
  namespace tables keyed by symbols, the suites' own data), six by `with-meta`, none by the 33rd key, the
  dictionary rule or the cap (the `CorpusTests` log prints the counters per library).

