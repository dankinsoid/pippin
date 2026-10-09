// @ai-generated(solo)
#include "clj/stats.h"

#include <time.h>

#include "clj/number.h"
#include "clj/object.h"
#include "stats_internal.h"

static const char *const names[CLJ_STAT_COUNT] = {
	[CLJ_STAT_ALLOC] = "alloc",
	[CLJ_STAT_ALLOC_BYTES] = "alloc_bytes",
	[CLJ_STAT_FREE] = "free",
	[CLJ_STAT_INVOKE] = "invoke",
	[CLJ_STAT_C_INVOKE] = "c_invoke",
	[CLJ_STAT_APPLY] = "apply",
	[CLJ_STAT_PROTO_GENERIC] = "proto_generic",
	[CLJ_STAT_PROTO_MISS] = "proto_miss",
	[CLJ_STAT_LAZY_FORCE] = "lazy_force",
	[CLJ_STAT_HASH] = "hash",
	[CLJ_STAT_EQUALS] = "equals",
};

const char *clj_debug_stat_name(int k) { return k >= 0 && k < CLJ_STAT_COUNT ? names[k] : "?"; }

#if CLJ_STATS
_Atomic uint64_t clj_stats[CLJ_STAT_COUNT];

#if !CLJ_DEBUG
_Atomic uint64_t clj_debug_rc_counters[3];
#endif

// Keyed by descriptor and never freed, as the debug live-object table in alloc.c.
enum { TYPE_SLOTS = 1024 };

typedef struct {
	_Atomic(const clj_type *) type;
	_Atomic uint64_t          allocs;
} type_allocs;

static type_allocs by_type[TYPE_SLOTS];

void clj_stats_alloc(const clj_type *type, size_t size) {
	CLJ_STAT(CLJ_STAT_ALLOC);
	CLJ_STAT_ADD(CLJ_STAT_ALLOC_BYTES, size);
	size_t i = (((uintptr_t)type >> 4) * 0x9E3779B97F4A7C15u) & (TYPE_SLOTS - 1);
	for (size_t probes = 0; probes < TYPE_SLOTS; probes++, i = (i + 1) & (TYPE_SLOTS - 1)) {
		const clj_type *cur = atomic_load_explicit(&by_type[i].type, memory_order_acquire);
		if (!cur && atomic_compare_exchange_strong_explicit(&by_type[i].type, &cur, type, memory_order_acq_rel, memory_order_acquire)) cur = type;
		if (cur == type) {
			atomic_fetch_add_explicit(&by_type[i].allocs, 1, memory_order_relaxed);
			return;
		}
	}
}

bool clj_debug_stats(uint64_t out[CLJ_STAT_COUNT]) {
	for (int k = 0; k < CLJ_STAT_COUNT; k++) out[k] = atomic_load_explicit(&clj_stats[k], memory_order_relaxed);
	return true;
}

bool clj_debug_stats_rc(uint64_t out[3]) {
	for (int k = 0; k < 3; k++) out[k] = atomic_load_explicit(&clj_debug_rc_counters[k], memory_order_relaxed);
	return true;
}

size_t clj_debug_allocs_by_type(const char **type_names, uint64_t *counts, size_t cap) {
	size_t n = 0;
	for (size_t i = 0; i < TYPE_SLOTS && n < cap; i++) {
		const clj_type *type = atomic_load_explicit(&by_type[i].type, memory_order_acquire);
		if (!type) continue;
		type_names[n] = type->name;
		counts[n++] = atomic_load_explicit(&by_type[i].allocs, memory_order_relaxed);
	}
	return n;
}
#else
bool clj_debug_stats(uint64_t out[CLJ_STAT_COUNT]) {
	(void)out;
	return false;
}

bool clj_debug_stats_rc(uint64_t out[3]) {
	(void)out;
	return false;
}

size_t clj_debug_allocs_by_type(const char **type_names, uint64_t *counts, size_t cap) {
	(void)type_names;
	(void)counts;
	(void)cap;
	return 0;
}
#endif

// Retains of 64 live objects, then their releases: never the last one, and the barrier keeps the compiler from
// pairing a retain with its release.
double clj_debug_rc_op_ns(size_t ops) {
	enum { N = 64 };
	clj_value objs[N];
	for (int i = 0; i < N; i++) objs[i] = clj_double_new((double)i + 0.5);
	size_t rounds = ops / (2 * N) + 1;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (size_t r = 0; r < rounds; r++) {
		for (int i = 0; i < N; i++) {
			clj_retain(objs[i]);
			__asm__ volatile("" ::: "memory");
		}
		for (int i = 0; i < N; i++) {
			clj_release(objs[i]);
			__asm__ volatile("" ::: "memory");
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	for (int i = 0; i < N; i++) clj_release(objs[i]);
	double ns = (double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec);
	return ns / (double)(rounds * 2 * N);
}
