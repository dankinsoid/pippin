## Allocator (Sources/CljCore/alloc.c)

- **An exiting thread's heap goes whole to the next thread that needs one.** The blocking pools retire idle
  threads (NOTES "Scheduler", "Blocking pool"), so a burst of `thread` bodies every few minutes would otherwise
  leave a heap of slabs per thread for ever. A pthread key's destructor (`heap_abandon`) pushes the heap on a
  list under a mutex and clears `tls_heap`; `my_heap` pops one before it callocs. The heap, not its slabs, is
  the unit: a slab's `owner` stays the heap, so `pool_free` sees the adopter's frees as local at once and
  everyone else's as foreign, which the adopter drains as before. The destructor gets the heap as the key's
  value, not from `tls_heap`: Darwin may have torn the thread's TLS down first. `tls_heap` is cleared because a
  later destructor's free must take the foreign path — the heap may already have its next owner. A heap stays
  on the list until a thread starts, so its slabs hold their cells meanwhile (the empty-slab item below).
- [ ] **Empty slabs are never returned to the OS.** Peak memory stays resident. Trigger: first run on a
  device (jetsam). Fix: `madvise(MADV_FREE)`/`munmap` when empty slabs per class exceed a threshold,
  plus a memory-warning hook.
- [ ] **Foreign free list drains only when the local list is empty.** With a producer thread allocating
  from bump cells and a consumer freeing, cells pile up unused until the slab is exhausted. Not a leak,
  a delay. Trigger: multi-threaded benchmark showing extra resident memory in producer/consumer runs.
- [ ] **Linear search for a slab with room** when the current one is full. Trigger: a profile showing it;
  unlikely.

