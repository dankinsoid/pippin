// @ai-generated(solo)
#ifndef CLJ_ROOTS_INTERNAL_H
#define CLJ_ROOTS_INTERNAL_H

#include <stddef.h>

#include "shadow_internal.h"

// C variables of the runtime the collector reads while their execution is parked (design §7, «Фаза 3»).
// Invariant: at each call that may park a named variable holds an owned reference or nil, never a handed-over one.

typedef struct {
	clj_frefs *fl;
	uint32_t   n;
} clj_roots_mark;

static inline void clj_roots_drop(clj_roots_mark *m) { m->fl->n = m->n; }

static inline clj_roots_mark clj_roots_push_words(const uintptr_t *w, uint32_t k) {
	clj_frefs *fl = &clj_shadow_running()->frefs;
	uint32_t   n = fl->n;
	if (__builtin_expect(n + k > fl->cap, 0)) clj_frefs_grow(fl, n + k);
	for (uint32_t i = 0; i < k; i++) fl->v[n + i] = w[i];
	fl->n = n + k;
	return (clj_roots_mark){fl, n};
}

static inline clj_roots_mark clj_roots_push(clj_value *const *addrs, uint32_t k) {
	uintptr_t w[8];
	for (uint32_t i = 0; i < k; i++) w[i] = (uintptr_t)addrs[i] | CLJ_FREF_ADDR;
	return clj_roots_push_words(w, k);
}

static inline clj_roots_mark clj_roots_push_span(clj_value *const *base, const size_t *n) {
	uintptr_t w[3] = {CLJ_FREF_SPAN, (uintptr_t)base, (uintptr_t)n};
	return clj_roots_push_words(w, 3);
}

#define CLJ_ROOTS_CAT2(a, b) a##b
#define CLJ_ROOTS_CAT(a, b) CLJ_ROOTS_CAT2(a, b)
#define CLJ_ROOTS_N(...) ((uint32_t)(sizeof((clj_value *const[]){__VA_ARGS__}) / sizeof(clj_value *)))

// CLJ_ROOTS(&acc, &s): at most 8 addresses of clj_value variables.
#define CLJ_ROOTS(...)                                                                                                \
	_Static_assert(CLJ_ROOTS_N(__VA_ARGS__) <= 8, "CLJ_ROOTS names at most 8 variables");                              \
	clj_roots_mark CLJ_ROOTS_CAT(clj_roots_, __LINE__) __attribute__((cleanup(clj_roots_drop))) =                       \
	    clj_roots_push((clj_value *const[]){__VA_ARGS__}, CLJ_ROOTS_N(__VA_ARGS__))

// CLJ_ROOTS_SPAN(&items, &n): the first n of items, both read where they are.
#define CLJ_ROOTS_SPAN(base, n)                                                                                       \
	clj_roots_mark CLJ_ROOTS_CAT(clj_roots_, __LINE__) __attribute__((cleanup(clj_roots_drop))) = clj_roots_push_span(base, n)

#endif
