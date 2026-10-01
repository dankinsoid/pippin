## Set (Sources/CljCore/set.c)

- **A set is a wrapper over a map** (`clj_set.impl`, element → element), as Clojure's PersistentHashSet
  over PersistentHashMap: two objects per set, 16 bytes per element in the trie for a value nobody
  reads. `conj`/`disj` hand the wrapper's own trie reference to `clj_map_assoc`/`dissoc`, so a unique
  set edits its trie in place (the consuming `disj` intrinsic and the reduce drivers reach it as they
  reach `conj`); a present element is kept as it is (`(conj #{[1]} [1])` returns the same set). `seq`
  is an eager list of the elements, printing collects them into an array, `get` returns the stored
  element. Triggers: the ≤8-element linear-array set of the design ("Представление по наблюдению";
  the same trigger as the array map: small literal sets in a profile); a set-shaped trie without the
  value slots (memory of big sets); `clojure.set` as a namespace (the `ns`
  form, see core.clj).

