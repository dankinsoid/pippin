## List (Sources/CljCore/list.c, cons.c)

- **PersistentList and Cons are two descriptors over one 32-byte cell** (`clj_list_type`,
  `clj_cons_type`), differing only in name, the `CLJ_CORE_LIST` bit and `conj`. `list?` reads the bit, so
  `(list? (cons 1 '()))` is false while `seq?` stays true, and `(peek (cons 1 '()))` throws as on the JVM.
  Reader lists, `list`, `list*`'s tail, `reverse`, `rest` of a list and `(cons x nil)` (RT.cons's own rule)
  are PersistentLists; `cons` onto a seq, a lazy seq and `conj` on any other seq give a Cons.
  `conj` on a list or on `()` carries the collection's meta onto the new head, as `PersistentList.cons`
  does, and `pop` of the last cell hands the empty list that meta; `ASeq.cons` (a Cons, a lazy seq) does
  not. A Cons constant is not foldable: the codec reads it back as a list, and a fold must not change a type.
- [ ] **A cons chain has no count slot** (`count` walks it) and no hash cache, so hashing a list walks it
  every time. Trigger: lists as map keys or `count` on long lists in a profile. Fix: a count and a hash
  cache on the list cell, as Clojure's PersistentList has.
- [ ] **Hash and equality recurse on nesting depth** (`clj_hash` → element hash). Reading and printing are
  iterative, so a 200k-deep literal reads and prints but crashes when hashed. Trigger: untrusted input
  used as a map key. Fix: an explicit stack in `clj_seq_hash`/`clj_seq_equals`, or a depth cap.

