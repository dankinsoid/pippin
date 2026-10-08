## Builtins (Sources/CljCore/builtins.c)

- **Coverage is the minimum for the evaluator tests and core.clj**: arithmetic and comparison, type
  predicates, `get assoc dissoc disj contains? count conj nth first rest next cons list list* vector
  hash-map hash-set set empty seq lazy-seq* realized? range* list* into second last butlast reverse empty? hash
  resolve deref identical? type instance? satisfies? extends? meta with-meta alter-meta!
  reset-meta! reduce reduce-kv reduced reduced? unreduced ensure-reduced volatile! volatile?
  vreset! fused-reduce* fused-into* fused-count*` (`deref` takes vars, reduced boxes, volatiles and atoms; `alter-meta!`/`reset-meta!`
  take vars and atoms), the bit predicates `seq? seqable? sequential? coll? counted? ifn? associative?
  indexed? list? vector? map? char? integer?`, `symbol keyword name namespace gensym`, `str pr-str
  pr prn print println identity apply`, `macroexpand-1 macroexpand ex-info ex-message ex-data
  ex-cause`, the atom API below, and the numeric tower's own file (builtins_number.c: `+' -' *' inc' dec'
  == int? double? ratio? decimal? rational? NaN? infinite? long int short byte float double num bigint
  biginteger bigdec rationalize numerator denominator parse-long parse-double unchecked-*`).
  No `keys`, `vals`, `sort`, ... Most of the rest belongs in core.clj.
- **`into` is C in both arities**: `(into to from)` conj's through `clj_seq_iter`, `(into to xform
  from)` runs the `fused-into*` driver under `[xform]` (fusion.c `clj_into_xform`), because
  `destructure` calls `into` above the `fn` macro, where a core.clj `into` could not be defined.
  Both hold the accumulator alone and conj in place; the transducers see `nil` as `result`, as under a
  fused pipeline (the fusion entry of the evaluator section).
- [~] **Atoms** (atom.c; design §4 "Атомы"): `atom` (with `:meta`/`:validator`), `deref`/`@`, `reset!`,
  `swap!` (any arity), `swap-vals!`, `reset-vals!`, `compare-and-set!` (`identical?`), `add-watch`/
  `remove-watch`, `set-validator!`/`get-validator`, `atom?`, `meta`/`alter-meta!`/`reset-meta!`; the
  type name `Atom`. One `clj_cmutex` per atom ("Coroutine mutex": a CAS uncontended, a park contended, so
  an `f` that parks holds the atom across the park without freezing a carrier); `f` runs *exactly once*
  under it (no CAS retry loop, so an `f` with side effects runs them once) on the value borrowed from the
  atom — the atom keeps its reference, `f` sees it at +0 — the validator runs under it too, watches run after
  it with `(f key atom old new)` (a watch may deref and swap the atom; a throwing watch propagates after the
  store, the remaining watches are skipped). **`deref` takes no lock**: the load and its retain sit in the
  protocol reader window (proto.c, one seq_cst store each side), and `commit` releases the old value only
  after `clj_proto_wait_readers` saw every window closed — the retain never lands on a freed object, and a UI
  read never waits for another execution's `f` (design §4). Measured: `get @atom :k` 54 → 41 ns, `swap! inc`
  unchanged at 38–42, four carriers on one `swap! inc` 60–110 ns (bench/RESULTS.md, "Coroutines and
  channels"). An atom has no carrier affinity: `:affinity` is an unknown option key and ignored, as on the
  JVM (design §4 «Атомы»). **JVM semantics on a throw:** a throw out of `f`, a validator rejection or the
  nested-op trap leaves the state exactly as it was; `(swap! a (fn [s] (if ok (assoc s …) (throw …))))`
  is a rejection idiom and code relies on it. There is no hand-over of the atom's reference to `f`: the
  uniqueness trick (the atom at nil while `f` ran, `assoc` in place through a consuming native or a frame
  owning param 0, `clj_call_invoke_owning`) was tried and removed, because a throw after an in-place step
  cannot restore a version that no longer exists, and an undo journal was judged too complex for the gain.
  The price is one root-to-leaf path copy per `swap! assoc` on a shared map: 91 → 288 ns at 16 keys, 101 →
  613 at 1000, 131 → 904 at 100000 (bench/RESULTS.md, "Atoms", the dated subsection); `swap! inc`, `deref`,
  the watched and the 4-thread rows did not move. `deref` of the same atom from inside `f`, a validator or
  an `alter-meta!` fn returns the current (old) value, as on the JVM: the lock holder reads its own atom
  without taking the lock again (`held_by_me`, the owner execution in the atom). The lock is not recursive,
  so the trap stays for every op that would take it — a nested `swap!`, `swap-vals!`, `reset!`,
  `reset-vals!`, `compare-and-set!`, `add-watch`, `set-validator!`, `alter-meta!`, `meta`, ... on the
  *same* atom from inside `f`, a validator or an `alter-meta!` fn throws "<op> on an atom this thread is
  already swapping (nested swap! trap)" (the JVM retries forever). **Publication:** an atom is born shared, so
  everything stored into it — value, meta, validator, watches — is published by the store itself
  (`clj_slot_store`, NOTES "RC", "Slots"): the atom is a publication point, so a value read on another thread
  is on the atomic path from its first store. Triggers: the uniqueness trick returns when a profile shows `swap!` on a large
  map hot (the 100000-key row is a 4-level trie, not a typical atom), only for atoms without watches or a
  validator (both need `old` intact), and only for an `f` that writes last — nothing that may throw after
  its first in-place write, on any path — starting with the five consuming natives; the condition, the
  order of work and the `deref` wait over the hand-over window are in design §4 «Атомы»; `IRef`/`IAtom` as interfaces (a
  deftype implementing `deref`); `agent`/`ref` (no); `swap!` returning a `reduced`-style early exit (no).
- [~] **Volatiles are single-thread cells by contract** (`volatile!`, `vreset!`, `vswap!`; box.c): a
  read returns the value retained, a write retains the new value, shares it when the cell is
  shared (so a volatile published through a var keeps the RC invariant) and releases the old one
  with no ordering — a concurrent reader may retain a freed value, the `def`/`deref` race of the
  evaluator section. Transducer state (`take`, `partition-all`, `dedupe`, ...) lives in volatiles,
  so one transducer application (one `(xf rf)`) belongs to one thread. Trigger: a transducer
  shared across threads; Clojure's contract is the same.
- [ ] **`seq` on a map is an eager list of `[k v]` vectors** (no O(1) view, no first/next fast path):
  `(first m)` builds the whole entry list. Trigger: `first`/`some` over big maps in a profile. Fix:
  a map-seq cursor over the CHAMP trie and a map-entry type instead of 2-vectors.
- [~] **`range` handles fixnums in C** (`range*`, an O(1) view); doubles and step 0 go through
  `take-while`/`iterate`/`repeat` in core.clj. A bound that is an integer but not a fixnum — a boxed long
  or a bigint — reaches `range*` and throws "Argument must be an integer" rather than falling to that path,
  because `range`'s guard is `integer?` and there is no fixnum predicate to guard with
  (docs/jvm-differences.md, deferred). The same holds for the fixnum index of `nth`, `assoc` on a vector,
  `subvec` and `subs`.
- **Printing realizes lazy seqs and can throw**: `clj_pr_str` returns CLJ_THROWN, which `str`,
  `pr-str`, `print*` and the error-message callers propagate; `Value.description` on the Swift side
  substitutes the exception text.
- [ ] **Output hook is process-wide** (`clj_set_output`), not per thread or per runtime. Trigger: two
  hosts printing concurrently.
- **Error messages are Clojure-like, not Clojure-identical**: type names are the runtime's
  (`string cannot be cast to a number`, not `java.lang.String ... java.lang.Number`).

- **`jvm-hash` is JVM Clojure 1.12.6's `hash`, computed over our layouts** (jvm_hash.c; design §10, the fuzzing
  item): `hash` stays ours, and a key shared with a JVM process (a shard, a cache key) asks for this one. It is
  `Util.hasheq` case by case: Murmur3 `hashLong` for a long and for a bigint in the long range, `Double.hashCode`
  with -0.0 as 0.0 and the canonical NaN, `BigInteger.hashCode` over the limbs for a larger bigint and for a
  ratio's two parts, `BigDecimal.hashCode` after `stripTrailingZeros` (zero is 0), `String.hashCode` then
  `hashInt` over the UTF-16 units our UTF-8 decodes to, `Symbol.hasheq` (its name by `hashUnencodedChars`, its
  namespace by `String.hashCode`) and a keyword's golden-ratio offset, `Character.hashCode`, the booleans' 1231
  and 1237, `UUID.hashCode` and `Date.hashCode` (which `hash` already answers), `hashOrdered` over a sequential
  value, `hashUnordered` over a map's entries or a set, and a record's map hash xor'd with the hash of its class
  name, the symbol `ns.Name` with the namespace's dashes as `namespace-munge` writes them. Every number in
  `JvmHashTests` and in the compiler fixture is the JVM's, and the fuzzer compares `jvm-hash` of every outcome
  with the oracle's `hash` (NOTES "Fuzzing"). A value the JVM hashes by identity (a deftype instance, an atom, a
  fn, a regex, a var) or has no counterpart for is refused with its type named, never answered with a number no
  JVM would give: a key that cannot agree across processes is the bug to report. So is a char past the BMP,
  which no JVM char holds, and a URI, which `java.net.URI` hashes after folding case and escapes that our
  textual equality keeps apart. The walk stops at `clj_stack_limit` with "Stack overflow" rather than faulting.
- [ ] **`jvm-hash` of a tagged literal is refused**, where the JVM answers `TaggedLiteral.hashCode`, which is
  built from Java's `hashCode` of the tag and the form (`List.hashCode`, `Long.hashCode`, not `hasheq`), a second
  hash family over every value. Trigger: a key that holds a tagged literal.
- **A URI is a value of the core** (uri.c; design §3 «"Наш хост" — это C-ядро»): `parse-uri` splits an RFC 3986
  reference by its appendix B (scheme, `//authority`, path, `?query`, `#fragment`) and validates each component
  against its character set, a percent escape and, as `java.net.URI`'s "other" class, any non-ASCII byte; a
  relative reference whose first segment holds a colon, a port past `Integer.MAX_VALUE` or an unclosed IP literal
  is nil. The value keeps the text and spans into it, so the components (`:scheme :user-info :host :port :path
  :query :fragment`) are keyword lookups that cut a string on demand; an empty host is nil, as `java.net.URI`
  answers for `file:///x`. `=` and `hash` are the text's, `str` is the text, and `pr` is `#object[URI "…"]`,
  since the JVM's default readers have no tag for one to read back.
- **The bridges convert a URI, a uuid and an inst; the core values stay as they are.** Level 1 carries them as
  `NSURL`, `NSUUID` and `NSDate` by the argument's type and the return's class (NOTES "ObjC bridge"), level 2 as
  `URL`, `UUID` and `Date` by the slot's type (NOTES "Host bridge"); neither defines what the values are.
