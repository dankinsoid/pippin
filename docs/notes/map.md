## Map (Sources/CljCore/map.c)

- [ ] **`clj_debug_hash_override` is checked on every `clj_hash`** even in release (one global load +
  branch). Trigger: it shows in a profile.

- **`clj_map_of` is private to map.c**: the trie layout is one of two behind the same functions (the "Shapes"
  section), so core.c reads the cached hash through `clj_debug_map_cached_hash` and MapTests the root through
  `clj_debug_map_root`. `clj_map_assoc`/`clj_map_dissoc` dispatch on the layout; `clj_hash_map_assoc`/`dissoc`
  are the trie alone, for the set's impl and the keyword table, and `clj_map_from_items` builds from a whole
  key/value array (the reader, `hash-map`, the analyzer's constants, `clj_c_map_literal`).

- **The flat layout is the map's third** (design §4 «Плоское представление по линейности»): what `transient` makes
  of a map, an insertion-ordered open-addressing table — entries `(key, value)` appended with their hashes beside,
  an index of entry + 1 probed linearly at a load of at most one half, byte cells up to 128 entries and word cells
  beyond, a `dissoc` leaving a hole (`CLJ_UNBOUND` as the key) that the next rebuild drops. One type still: every
  `clj_map_*` entry and slot dispatches, `map?`/`=`/`hash` (the trie's entry mix and its cache rule, so a flat map
  and a trie of the same entries are equal and hash alike, collision keys included), printing, the codec and the
  bridge go through them. The layout is told apart by a nil word where the trie keeps its root (never nil): rc.c's
  dead-link word has no fourth bit, and death leaves the body whole, as with the tuple. No meta word: `with-meta`
  turns it into a trie, one way.
- **A transient edits in place at any count**, by the transient contract that only the returned value is used
  again: a builder's accumulator is a borrowed parameter of the reduce fn, so it reaches `assoc!` at a count of 2
  and the trie copied its path every step (the reuse counter's medley/group-freq/pipelines copies). Growth makes a
  new object when another reference holds the old one (realloc would move it under them), doubling, so O(1)
  amortized. Copies instead: a published transient (the collector reads immutable objects lock-free) and an entry
  that is the map itself (a cycle no collector sees). A walk of a transient (`reduce-kv`) retains each entry across
  its fn, which may `dissoc!` it from the map being walked. Stale references see stale contents, as on the JVM.
  *A transient used after `persistent!`* does not throw as on the JVM: `persistent!` clears the flag on the object it
  may return as the value, so the stale handle and the value are one persistent map and an `assoc!`/`dissoc!`/`conj!`
  through either copies while the other holds it — the value never changes (a compiler fixture in `seqs.clj`, both
  backends). Throwing would need the value to be a different object than the transient, an O(n) copy per
  `persistent!`; a trie that is its own transient was persistent all along.
- **`transient` and `persistent!` are `transient*`/`persistent!*`** (`clj_map_transient`/`clj_map_persistent`);
  any other collection's transient is the collection itself, as before. `(transient m)` is flat for a shape map, a
  flat one and a trie of at most the threshold without meta (a bounded copy); a larger trie or one with meta is its
  own transient. `persistent!`: empty → `{}`; keyword keys alone, at most 32 → replayed through the shape
  transitions, dictionary rule included, so a keyword map is a shape map however it was built; at most the
  threshold → the same object, O(1); above it → the trie, O(n) once against the n insertions. A persistent flat map
  edits at rc 1 and copies otherwise (bounded by the threshold); a new key past the threshold turns it into the trie.
  `frequencies` and `group-by` build through transients, as on the JVM; `zipmap`'s loop already updates its
  accumulator at rc 1 and stays persistent.
- **The threshold is 16** (`FLAT_MAX_DEFAULT`, `clj_debug_flat_max`), measured (bench/RESULTS.md, "Flat transient
  maps"): an assoc on a shared flat map (a copy) costs what the trie's path copy does up to 16 entries and up to 2×
  more from 32 on, and persistent! keeping it flat saves 30–40 ns an entry over converting.
- **Order is insertion order**, process-independent like the shape's sorted keys and unlike the trie's hash order
  (`(group-by odd? (range 6))` prints `{false …, true …}`, as the JVM's array map does); past the threshold or after
  with-meta it is the trie's.
- [ ] **Not done, with triggers.** *A trie transient* (a map above the threshold) still copies its path at a count
  of 2: the JVM's edit-token transient over the trie; trigger: a builder over a large existing map in a profile.
  *Sets* (`transient #{}`) stay on the trie: a flat set is the same table without values; trigger: set builders in
  the reuse counter. *A flat map's lookup* is ~0.5 ns over the trie's at ≤ 16 entries (hash, index, entry); a
  linear scan of the hashes would drop the index there; trigger: a flat map's get in a profile.

