## Allocator (Sources/CljCore/alloc.c)

- **An exiting thread's heap goes whole to the next thread that needs one.** The blocking pools retire idle
  threads (NOTES "Scheduler", "Blocking pool"), so a burst of `thread` bodies every few minutes would otherwise
  leave a heap of slabs per thread for ever. The heap is the value of a pthread key, whose destructor
  (`heap_abandon`) pushes it on a list under a mutex; `my_heap` pops one before it callocs. The heap, not its
  slabs, is the unit: a slab's `owner` stays the heap, so `pool_free` sees the adopter's frees as local at once
  and everyone else's as foreign, which the adopter drains as before. The slot is NULL by the time the
  destructor runs (POSIX), so a later destructor's free takes the foreign path — the heap may already have its
  next owner. A heap stays on the list until a thread starts, so its slabs hold their cells meanwhile (the
  empty-slab item below).
- **Thread-local access is a pthread key read off the thread register** (`tsd_internal.h`, `clj_tsd_get`):
  the heap here and the carrier (`coro.c`, whose `current` is the running execution). Darwin's `_Thread_local`
  is a call into dyld's `_tlv_get_addr` on every access; Mach-O has no initial-exec model (clang emits the
  same `TLVPPAGE` call under `-ftls-model=initial-exec` or the `tls_model` attribute), so that call was 5–12.5 %
  of the closed build's samples on the data workloads, from `pool_alloc`, `pool_free` and `clj_coro_current`
  (bench/RESULTS.md "Corpus workloads"). The inline read is the load `pthread_getspecific` itself does,
  `tsd[key]` off `TPIDRRO_EL0`/`%gs`, as Go's runtime does for its own key; the keys are made before main
  (`clj_tsd_key_create`), which fails loudly when the read and `pthread_setspecific` disagree. Writes stay
  `pthread_setspecific`, which also enrolls the key in the thread's destructor pass. The asm is `volatile`, so a
  coroutine resumed on another carrier never reuses the old thread's base (NOTES "Coroutines", TLS across a
  park). What stays `_Thread_local`: the shadow ring's mirror (`clj_shadow_tls`, interpreted calls only) and the
  colder per-thread state (the protocol reader window, compiled inline caches, rand state). Platform spot:
  docs/portability.md.
- **`clj_alloc_uninit` skips the zeroing for a constructor that writes every field** (`object.h`, alloc.c).
  `clj_alloc` clears a reused cell (`memset` in `slab_take`; a bump cell is zero already), the `memset` the corpus
  profile counted at 3–8 % (bench/RESULTS.md, "Corpus workloads"). The hot constructors write each field instead:
  cons and list, double, long, string (its hash cache), map node and collision node, a trie map's three copy
  paths, sorted node, vector-seq, lazy seq, and the compiled and interpreted closures. Debug builds fill the body
  with `0xF0`, which reads as a pointer with a non-canonical address, so a field a constructor misses faults at its
  first read in every debug suite instead of reading as nil. What those constructors left to the zeroing before
  (each a field `clj_alloc`'s contract filled, now written): a string's hash cache, a sorted node's two children, a
  copied map's meta or hash, a lazy seq's state and value, and of a closure its arities, meta and native release.
  Kept on `clj_alloc` because their callers fill a prefix and rely on nil past it: a tuple (spare capacity, hash,
  meta), a vector node (`sizes`, flags, the slots past `len`), a shape map (`clj_shape_map_alloc` hands out empty
  slots), and every less frequent type. `AllocTests.aStringInAReusedCellHashesItsOwnBytes` sees a stale hash cache.
- [ ] **A dying cell is not handed straight to the next allocation of its class.** The free list is already the
  hand-off where it can be one: `pool_free` pushes onto the cell's slab's list and `slab_take` pops it LIFO, so a
  cell of the current slab is the next one handed out, for one load of its link. What a per-class slot in the heap
  would add is the cell of a non-current slab, at the price of a branch and a store on every free and alloc, and
  the TLS read both already make (`tls_heap`, a spot another change owns). The reuse that pays is the compiler's
  reuse token, which builds the new cell in the one it drops (docs/notes/compiler.md, "Drop-guided reuse"), not an
  allocator change. The census bounds what either could reach (bench/RESULTS.md, "Allocation
  census"): a death followed by an allocation of its class as the very next one of its frame is 5.1 % of the data
  workloads' allocations, within four 11.4 % (dependency 24 %, `cons` from `concat`). Trigger: the `reuse_type`
  rows of a corpus-bench run showing a structure whose copies stay high while its input dies at the same site, or
  a profile where `pool_alloc`'s scan of non-current slabs ranks.
- [ ] **Empty slabs are never returned to the OS.** Peak memory stays resident. Trigger: an app whose peak
  working set is a real fraction of the jetsam limit — the §10 app with a UI and data, not the first iOS
  run, which says nothing (NOTES "iOS"): the twelve probe forms leave 11–12 MB of `phys_footprint` behind
  after their values die, part empty slabs and part `coro.c`'s pooled stacks, against a foreground limit of
  hundreds of MB. Fix: `madvise(MADV_FREE)`/`munmap` when empty slabs per class exceed a threshold, plus a
  memory-warning hook.
- [ ] **Foreign free list drains only when the local list is empty.** With a producer thread allocating
  from bump cells and a consumer freeing, cells pile up unused until the slab is exhausted. Not a leak,
  a delay. Trigger: multi-threaded benchmark showing extra resident memory in producer/consumer runs.
- [ ] **Linear search for a slab with room** when the current one is full. Trigger: a profile showing it;
  unlikely.

