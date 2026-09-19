// @ai-generated(guided)
#ifndef CLJ_LOCK_H
#define CLJ_LOCK_H

#include <stdint.h>

// Not recursive; no rwlock in the core (design §4: readers contend on the count like writers).

// Locks the running execution holds: a stack overflow under one cannot be turned into an error (guard.c), a park
// under one is an error (sched.c). Reached through a call, never a cached TLS address: a coroutine may resume on
// another thread, and clang keeps a _Thread_local's address across calls (NOTES.md "Coroutines", TLS).
uint32_t *clj_locks_held_slot(void);

#ifdef __APPLE__
#include <os/lock.h>

typedef os_unfair_lock clj_lock;
#define CLJ_LOCK_INIT {0}

static inline void clj_lock_init(clj_lock *l) { *l = (clj_lock){0}; }
#ifndef __swift__
static inline void clj_lock_lock(clj_lock *l) {
	os_unfair_lock_lock(l);
	(*clj_locks_held_slot())++;
}
static inline void clj_lock_unlock(clj_lock *l) {
	(*clj_locks_held_slot())--;
	os_unfair_lock_unlock(l);
}
#endif
static inline void clj_lock_destroy(clj_lock *l) { (void)l; }
#else
#include <pthread.h>

typedef pthread_mutex_t clj_lock;
#define CLJ_LOCK_INIT PTHREAD_MUTEX_INITIALIZER

static inline void clj_lock_init(clj_lock *l) { pthread_mutex_init(l, NULL); }
#ifndef __swift__
static inline void clj_lock_lock(clj_lock *l) {
	pthread_mutex_lock(l);
	(*clj_locks_held_slot())++;
}
static inline void clj_lock_unlock(clj_lock *l) {
	(*clj_locks_held_slot())--;
	pthread_mutex_unlock(l);
}
#endif
static inline void clj_lock_destroy(clj_lock *l) { pthread_mutex_destroy(l); }
#endif

#endif
