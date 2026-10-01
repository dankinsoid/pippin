## Locks (include/clj/lock.h)

- **Every mutex of the core's internals is a `clj_lock`**: `os_unfair_lock` under `__APPLE__`,
  `pthread_mutex_t` elsewhere, one interface (`clj_lock_init/lock/unlock/destroy`, `CLJ_LOCK_INIT`), the one
  `#ifdef` of its kind (measured on an M3 Pro, a lock+unlock pair: 2.1 vs 4.6 ns, 4 vs 64 bytes; unfair passes
  priority to the owner under contention). Holders: the keyword table, the namespace registry, the protocol
  tables and the reify registry (proto.c), the profiler table, the channels' queues, the parking lot's
  buckets. The invariant of design §4 ("Два лока"): under a `clj_lock` runs neither user code nor IO, and a
  park under one is an error (the audit table under "Coroutine mutex"). What user code holds — an atom's
  `swap!`, `locking`, a lazy seq being forced — is the `clj_cmutex` instead. The count of locks held is per
  execution (`clj_locks_held_slot`, coroutines migrate between threads). Not recursive, so a path that reaches
  the same lock twice deadlocks. No rwlock anywhere, by design §4: a read lock is an RMW on the shared
  count, so readers contend like writers; reads in the core go through immutability and the epoch instead.
  Trigger for `os_unfair_lock_trylock`/a fair variant: a profile showing a starved thread on one lock.

