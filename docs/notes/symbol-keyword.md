## Symbol / keyword (Sources/CljCore/symbol.c, keyword.c)

- **A symbol's meta survives `with-meta` copies only**: `symbol`/`name`/`namespace` and the analyzer
  work on the fields, and `def` interns a bare copy of a meta-carrying name so the mapping key never
  holds the var's meta twice.
- **Interning a keyword is permanent and shows in `clj_debug_live_objects`** (keyword, symbol,
  string, intern-table nodes); tests intern the keywords they use before taking a baseline. The runtime's own
  first-use sets (the reader's, analyzer's, node codec's, printer's, error's, trace's, profiler's and var's
  `intern_keywords`, each behind a `pthread_once` so the module also works unbooted) are interned by `clj_init`
  through their `clj_<module>_intern_keywords` entries, and every suite under `CoreTests` runs behind the
  `.booted` trait (`EvalSupport.swift`, a recursive `TestScoping` calling `clj_init`), so no baseline lands
  before the boot or before a lazily made set, whichever suite runs first. `make test-isolated` runs every suite
  alone to keep that true; it is periodic, not a gate. A baseline that still depends on order is one taken
  before test data the test itself interns (a `:k` read from its own source, a method name an error names).
- [ ] **Keyword intern table is one global map under one mutex**, and interning allocates a temporary
  symbol for the lookup even on a hit. Trigger: keyword literals resolved at runtime in a hot path
  (the reader/analyzer resolves them once, so unlikely). Fix: sharded tables or a lock-free read path.
- **A string is limited to 4 GiB** (`uint32_t len`); `clj_string_new` aborts beyond that.

