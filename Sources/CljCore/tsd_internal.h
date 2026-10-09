// @ai-generated(solo)
#ifndef CLJ_TSD_INTERNAL_H
#define CLJ_TSD_INTERNAL_H

#include <pthread.h>
#include <stdint.h>

// Darwin's _Thread_local calls dyld's _tlv_get_addr per access (no initial-exec on Mach-O); this is the
// load of tsd[key] that pthread_getspecific does, inline (NOTES "Allocator", thread-local access).
static inline void *clj_tsd_get(pthread_key_t key) {
#if defined(__APPLE__) && defined(__aarch64__)
	uintptr_t base;
	// volatile: a coroutine resumed on another carrier must not reuse the old thread's base.
	__asm__ volatile("mrs %0, tpidrro_el0" : "=r"(base));
	return ((void *const *)(base & ~(uintptr_t)7))[key];
#elif defined(__APPLE__) && defined(__x86_64__)
	void *v;
	__asm__ volatile("movq %%gs:(,%1,8), %0" : "=r"(v) : "r"((uintptr_t)key));
	return v;
#else
	return pthread_getspecific(key);
#endif
}

// Writes stay pthread_setspecific: it also enrolls the key in the thread's destructor pass.
// pthread_key_create, then the check that clj_tsd_get reads what pthread_setspecific wrote; fatal otherwise.
void clj_tsd_key_create(pthread_key_t *key, void (*destructor)(void *));

#endif
