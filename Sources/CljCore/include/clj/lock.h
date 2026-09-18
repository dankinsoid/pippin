// @ai-generated(guided)
#ifndef CLJ_LOCK_H
#define CLJ_LOCK_H

#include <stdint.h>

// Not recursive; no rwlock in the core (design §4: readers contend on the count like writers).

// Locks the calling thread holds: a stack overflow under one cannot be turned into an error (guard.c).
// Not for Swift: a _Thread_local does not import.
#ifndef __swift__
extern _Thread_local uint32_t clj_locks_held;
#endif

#ifdef __APPLE__
#include <os/lock.h>

typedef os_unfair_lock clj_lock;
#define CLJ_LOCK_INIT {0}

static inline void clj_lock_init(clj_lock *l) { *l = (clj_lock){0}; }
#ifndef __swift__
static inline void clj_lock_lock(clj_lock *l) {
	os_unfair_lock_lock(l);
	clj_locks_held++;
}
static inline void clj_lock_unlock(clj_lock *l) {
	clj_locks_held--;
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
	clj_locks_held++;
}
static inline void clj_lock_unlock(clj_lock *l) {
	clj_locks_held--;
	pthread_mutex_unlock(l);
}
#endif
static inline void clj_lock_destroy(clj_lock *l) { pthread_mutex_destroy(l); }
#endif

#endif
