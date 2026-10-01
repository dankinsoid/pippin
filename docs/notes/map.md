## Map (Sources/CljCore/map.c)

- [ ] **`clj_debug_hash_override` is checked on every `clj_hash`** even in release (one global load +
  branch). Trigger: it shows in a profile.

- **`clj_map_of` is private to map.c**: the trie layout is one of two behind the same functions (the "Shapes"
  section), so core.c reads the cached hash through `clj_debug_map_cached_hash` and MapTests the root through
  `clj_debug_map_root`. `clj_map_assoc`/`clj_map_dissoc` dispatch on the layout; `clj_hash_map_assoc`/`dissoc`
  are the trie alone, for the set's impl and the keyword table, and `clj_map_from_items` builds from a whole
  key/value array (the reader, `hash-map`, the analyzer's constants, `clj_c_map_literal`).

