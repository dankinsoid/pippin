// @ai-generated(guided)
#ifndef CLJ_LOCK_H
#define CLJ_LOCK_H

// Not recursive; no rwlock in the core (design §4: readers contend on the count like writers).

#ifdef __APPLE__
#include <os/lock.h>

typedef os_unfair_lock clj_lock;
#define CLJ_LOCK_INIT {0}

static inline void clj_lock_init(clj_lock *l) { *l = (clj_lock){0}; }
static inline void clj_lock_lock(clj_lock *l) { os_unfair_lock_lock(l); }
static inline void clj_lock_unlock(clj_lock *l) { os_unfair_lock_unlock(l); }
static inline void clj_lock_destroy(clj_lock *l) { (void)l; }
#else
#include <pthread.h>

typedef pthread_mutex_t clj_lock;
#define CLJ_LOCK_INIT PTHREAD_MUTEX_INITIALIZER

static inline void clj_lock_init(clj_lock *l) { pthread_mutex_init(l, NULL); }
static inline void clj_lock_lock(clj_lock *l) { pthread_mutex_lock(l); }
static inline void clj_lock_unlock(clj_lock *l) { pthread_mutex_unlock(l); }
static inline void clj_lock_destroy(clj_lock *l) { pthread_mutex_destroy(l); }
#endif

#endif
