// @ai-generated(solo)
#include "clj/stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clj/number.h"
#include "clj/object.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "shadow_internal.h"
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
	[CLJ_STAT_REUSE_TAKEN] = "reuse_taken",
	[CLJ_STAT_REUSE_COPIED] = "reuse_copied",
};

const char *clj_debug_stat_name(int k) { return k >= 0 && k < CLJ_STAT_COUNT ? names[k] : "?"; }

#if CLJ_STATS
_Atomic uint64_t clj_stats[CLJ_STAT_COUNT];

#if !CLJ_DEBUG
_Atomic uint64_t clj_debug_rc_counters[3];
#endif

// ---- the allocation census
//
// Frame serials grow with every push of an execution, so the frames on its stack older than a birth frame are
// exactly that frame's callers still running: the deepest of them is the closest frame that held the object.

enum {
	K_FRAME,      // died in its birth frame
	K_CALLEE,     // died deeper, the birth frame still running
	K_UP1,        // the birth frame returned; the closest frame on both stacks is 1, 2, 3+ calls above it
	K_UP2,
	K_UP3,
	K_ESCAPED,    // no frame of its execution held it: returned out of the outermost fn
	K_OTHER_EXEC, // died in another execution (a coroutine, a thread)
	K_OUTSIDE,    // born outside any fn frame (the host, a top-level form)
	NKIND
};

static const char *const kind_names[NKIND] = {"frame", "callee", "up1", "up2", "up3", "escaped", "other_exec", "outside"};

enum {
	F_DIED,     // by kind
	F_RETAINED, // rc went past 1 at least once (CLJ_FLAG_RETAINED)
	F_SHARED,   // published (CLJ_FLAG_SHARED) when it died
	F_BUILT,    // born under a builder frame
	F_REUSE1,   // an allocation of its size class came next in the frame it died in
	F_REUSE4,   // ... within the next REUSE_WINDOW allocations of the execution
	NFIELD
};

static const char *const field_names[NFIELD] = {"", ".retained", ".shared", ".built", ".reuse1", ".reuse4"};

enum { EXEC_SHIFT = 40, REUSE_WINDOW = 4, REUSE_WAYS = 4, POOL_CLASSES = 21, CLASSES = POOL_CLASSES + 1 };

// Keyed by descriptor and never freed, as the debug live-object table in alloc.c.
enum { TYPE_SLOTS = 1024 };

typedef struct {
	_Atomic(const clj_type *) type;
	_Atomic uint64_t          allocs, reuse_taken, reuse_copied;
	_Atomic uint64_t          born, born_built;
	_Atomic uint64_t          count[NFIELD][NKIND];
} type_allocs;

static type_allocs by_type[TYPE_SLOTS];

static uint32_t type_index(const clj_type *type) {
	size_t i = (((uintptr_t)type >> 4) * 0x9E3779B97F4A7C15u) & (TYPE_SLOTS - 1);
	for (size_t probes = 0; probes < TYPE_SLOTS; probes++, i = (i + 1) & (TYPE_SLOTS - 1)) {
		const clj_type *cur = atomic_load_explicit(&by_type[i].type, memory_order_acquire);
		if (!cur && atomic_compare_exchange_strong_explicit(&by_type[i].type, &cur, type, memory_order_acq_rel, memory_order_acquire)) cur = type;
		if (cur == type) return (uint32_t)i;
	}
	clj_fatal("too many types for the stats table");
}

static inline void bump(_Atomic uint64_t *c) { atomic_fetch_add_explicit(c, 1, memory_order_relaxed); }

typedef struct {
	uint64_t serial;
	uint32_t builders; // builder frames from the bottom up to this one
} census_frame;

// A death waiting for an allocation of its size class in its frame (Perceus' reuse pairing).
typedef struct {
	uint64_t serial, at;
	uint16_t type;
	uint8_t  kind, live;
} pending;

typedef struct {
	census_frame *frames;
	uint32_t      depth, cap;
	uint32_t      cbuilders;
	uint64_t      exec, next, allocs;
	pending       recent[CLASSES][REUSE_WAYS];
} census_ring;

static _Atomic bool     census_on;
static _Atomic uint64_t census_execs, census_tracked, census_stale;

static census_ring *ring_of(clj_shadow_stack *s) {
	census_ring *r = s->census;
	if (__builtin_expect(!r, 0)) {
		r = calloc(1, sizeof *r);
		if (!r) clj_fatal("out of memory");
		r->exec = (1 + atomic_fetch_add_explicit(&census_execs, 1, memory_order_relaxed)) << EXEC_SHIFT;
		s->census = r;
	}
	return r;
}

void clj_census_ring_free(void *ring) {
	clj_shadow_stack *s = ring;
	census_ring      *r = s->census;
	if (!r) return;
	free(r->frames);
	free(r);
	s->census = NULL;
}

// Direct-mapped, racy by design: a lost entry is recomputed. Bit 0 is the answer, the rest the node.
static _Atomic uintptr_t builder_cache[4096];

static bool builder_name(const void *fn_node) {
	static const char *const builders[] = {"transient", "persistent!", "conj!",       "assoc!",      "dissoc!",
	                                       "disj!",     "pop!",        "into",        "frequencies", "group-by",
	                                       "zipmap",    "mapv",        "filterv",     "update-vals", "update-keys"};
	clj_value name = ((const clj_node *)fn_node)->u.fn.name.v;
	if (!clj_is_symbol(name)) return false;
	const char *s = clj_string_bytes(clj_symbol_name(name));
	for (size_t i = 0; i < sizeof builders / sizeof *builders; i++)
		if (strcmp(s, builders[i]) == 0) return true;
	return false;
}

static bool is_builder(const void *fn_node) {
	if (!fn_node) return false;
	uintptr_t        key = (uintptr_t)fn_node;
	_Atomic uintptr_t *slot = &builder_cache[((key >> 3) * 0x9E3779B97F4A7C15u) >> 52];
	uintptr_t        e = atomic_load_explicit(slot, memory_order_relaxed);
	if ((e & ~(uintptr_t)1) == key) return e & 1;
	bool b = builder_name(fn_node);
	atomic_store_explicit(slot, key | (b ? 1 : 0), memory_order_relaxed);
	return b;
}

clj_census_mark clj_census_enter(const void *fn_node) {
	clj_shadow_stack *s = clj_shadow_tls;
	if (__builtin_expect(!s, 0)) s = clj_shadow_stack_init();
	census_ring    *r = ring_of(s);
	clj_census_mark m = {r, r->depth};
	if (r->depth == r->cap) {
		r->cap = r->cap ? r->cap * 2 : 64;
		census_frame *grown = realloc(r->frames, r->cap * sizeof *grown);
		if (!grown) clj_fatal("out of memory");
		r->frames = grown;
	}
	uint32_t below = r->depth ? r->frames[r->depth - 1].builders : 0;
	r->frames[r->depth++] = (census_frame){r->exec | ++r->next, below + (is_builder(fn_node) ? 1 : 0)};
	return m;
}

// To the depth at entry, not one down: a frame a stack-overflow recovery skipped goes with its caller.
void clj_census_leave(clj_census_mark m) { ((census_ring *)m.ring)->depth = m.depth; }

void clj_census_builder(int delta) {
	clj_shadow_stack *s = clj_shadow_tls;
	if (__builtin_expect(!s, 0)) s = clj_shadow_stack_init();
	ring_of(s)->cbuilders += (uint32_t)delta;
}

// ---- the records, by address: sharded open addressing, linear probes, deletion by backward shift

typedef struct {
	uintptr_t addr; // 0: empty
	uint64_t  serial;
	uint32_t  depth;
	uint16_t  type;
	uint8_t   cls, built;
} record;

typedef struct {
	atomic_flag lock;
	record     *slots;
	size_t      cap, n;
} shard;

enum { SHARDS = 256 };

static shard shards[SHARDS];

static inline uint64_t mix(uintptr_t a) {
	uint64_t h = a;
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdu;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53u;
	h ^= h >> 33;
	return h;
}

static shard *shard_lock(uintptr_t addr) {
	shard *s = &shards[mix(addr) >> 56];
	while (atomic_flag_test_and_set_explicit(&s->lock, memory_order_acquire)) {
	}
	return s;
}

static void shard_unlock(shard *s) { atomic_flag_clear_explicit(&s->lock, memory_order_release); }

static inline size_t home(const shard *s, uintptr_t addr) { return (size_t)mix(addr) & (s->cap - 1); }

static void shard_put(shard *s, record rec);

static void shard_grow(shard *s) {
	record *old = s->slots;
	size_t  ocap = s->cap;
	s->cap = ocap ? ocap * 2 : 1024;
	s->slots = calloc(s->cap, sizeof *s->slots);
	if (!s->slots) clj_fatal("out of memory");
	s->n = 0;
	for (size_t i = 0; i < ocap; i++)
		if (old[i].addr) shard_put(s, old[i]);
	free(old);
}

// A record already under the address is one whose death no hook saw: replaced and counted.
static void shard_put(shard *s, record rec) {
	if ((s->n + 1) * 4 > s->cap * 3) shard_grow(s);
	for (size_t i = home(s, rec.addr);; i = (i + 1) & (s->cap - 1)) {
		if (s->slots[i].addr == rec.addr) {
			atomic_fetch_add_explicit(&census_stale, 1, memory_order_relaxed);
			s->slots[i] = rec;
			return;
		}
		if (!s->slots[i].addr) {
			s->slots[i] = rec;
			s->n++;
			return;
		}
	}
}

static bool shard_take(shard *s, uintptr_t addr, record *out) {
	if (!s->cap) return false;
	size_t mask = s->cap - 1, i = home(s, addr);
	for (;; i = (i + 1) & mask) {
		if (!s->slots[i].addr) return false;
		if (s->slots[i].addr == addr) break;
	}
	*out = s->slots[i];
	for (size_t j = i;;) {
		j = (j + 1) & mask;
		if (!s->slots[j].addr) break;
		size_t k = home(s, s->slots[j].addr);
		// slots[j] may move into the hole at i unless its home lies cyclically in (i, j]
		bool stays = i <= j ? (i < k && k <= j) : (i < k || k <= j);
		if (stays) continue;
		s->slots[i] = s->slots[j];
		i = j;
	}
	s->slots[i].addr = 0;
	s->n--;
	return true;
}

static bool take(uintptr_t addr, record *out) {
	shard *s = shard_lock(addr);
	bool   found = shard_take(s, addr, out);
	shard_unlock(s);
	return found;
}

static void put(record rec) {
	shard *s = shard_lock(rec.addr);
	shard_put(s, rec);
	shard_unlock(s);
}

static inline uint32_t class_slot(uint32_t cls) { return cls < POOL_CLASSES ? cls : POOL_CLASSES; }

static int classify(const record *rec, const census_ring *r) {
	if (!rec->serial) return K_OUTSIDE;
	if (!r || (rec->serial >> EXEC_SHIFT) != (r->exec >> EXEC_SHIFT)) return K_OTHER_EXEC;
	// the number of frames on the stack now whose serial is at most the birth frame's
	uint32_t lo = 0, hi = r->depth;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (r->frames[mid].serial <= rec->serial) lo = mid + 1;
		else hi = mid;
	}
	if (!lo) return K_ESCAPED;
	uint32_t at = lo - 1;
	if (r->frames[at].serial == rec->serial) return r->depth == rec->depth ? K_FRAME : K_CALLEE;
	uint32_t up = rec->depth - 1 - at;
	return up == 1 ? K_UP1 : up == 2 ? K_UP2 : K_UP3;
}

static void census_birth(clj_header *h, const clj_type *type, uint32_t cls) {
	clj_shadow_stack *s = clj_shadow_tls;
	census_ring      *r = s ? ring_of(s) : NULL;
	record            rec = {.addr = (uintptr_t)h, .type = (uint16_t)type_index(type), .cls = (uint8_t)cls};
	if (r) {
		r->allocs++;
		if (r->depth) {
			rec.serial = r->frames[r->depth - 1].serial;
			rec.depth = r->depth;
			rec.built = r->frames[r->depth - 1].builders > 0;
		}
		rec.built |= r->cbuilders > 0;
		pending *best = NULL;
		for (pending *p = r->recent[class_slot(cls)]; p < r->recent[class_slot(cls)] + REUSE_WAYS; p++) {
			if (p->live && p->serial == rec.serial && r->allocs - p->at <= REUSE_WINDOW && (!best || p->at > best->at)) best = p;
		}
		if (best) {
			best->live = 0;
			bump(&by_type[best->type].count[F_REUSE4][best->kind]);
			if (r->allocs - best->at == 1) bump(&by_type[best->type].count[F_REUSE1][best->kind]);
		}
	}
	bump(&by_type[rec.type].born);
	if (rec.built) bump(&by_type[rec.type].born_built);
	atomic_fetch_add_explicit(&census_tracked, 1, memory_order_relaxed);
	put(rec);
}

void clj_census_death(clj_header *h) {
	if (!atomic_load_explicit(&census_tracked, memory_order_relaxed)) return;
	record rec;
	if (!take((uintptr_t)h, &rec)) return;
	atomic_fetch_sub_explicit(&census_tracked, 1, memory_order_relaxed);
	clj_shadow_stack *s = clj_shadow_tls;
	census_ring      *r = s ? s->census : NULL;
	int               k = classify(&rec, r);
	uint32_t          flags = h->flags;
	type_allocs      *t = &by_type[rec.type];
	bump(&t->count[F_DIED][k]);
	if (flags & CLJ_FLAG_RETAINED) bump(&t->count[F_RETAINED][k]);
	if (flags & CLJ_FLAG_SHARED) bump(&t->count[F_SHARED][k]);
	if (rec.built) bump(&t->count[F_BUILT][k]);
	if (!r || !atomic_load_explicit(&census_on, memory_order_relaxed)) return;
	// the oldest way of the class, or a free one
	pending *ways = r->recent[class_slot(rec.cls)], *slot = ways;
	for (pending *p = ways; p < ways + REUSE_WAYS; p++) {
		if (!p->live) {
			slot = p;
			break;
		}
		if (p->at < slot->at) slot = p;
	}
	*slot = (pending){r->depth ? r->frames[r->depth - 1].serial : 0, r->allocs, rec.type, (uint8_t)k, 1};
}

void clj_census_move(clj_header *from, clj_header *to, uint32_t cls) {
	if (!atomic_load_explicit(&census_tracked, memory_order_relaxed)) return;
	record rec;
	if (!take((uintptr_t)from, &rec)) return;
	rec.addr = (uintptr_t)to;
	rec.cls = (uint8_t)cls;
	put(rec);
}

bool clj_debug_census_begin(void) {
	atomic_store_explicit(&census_on, true, memory_order_seq_cst);
	return true;
}

void clj_debug_census_end(void) { atomic_store_explicit(&census_on, false, memory_order_seq_cst); }

void clj_debug_census_print(unsigned iterations) {
	double per = iterations ? (double)iterations : 1.0;
	for (size_t i = 0; i < TYPE_SLOTS; i++) {
		const clj_type *type = atomic_load_explicit(&by_type[i].type, memory_order_acquire);
		uint64_t        born = atomic_load_explicit(&by_type[i].born, memory_order_relaxed);
		if (!type || !born) continue;
		char name[128];
		snprintf(name, sizeof name, "%s", type->name);
		for (char *c = name; *c; c++)
			if (*c == ' ') *c = '_';
		printf("census %s born %.1f\n", name, (double)born / per);
		printf("census %s born_built %.1f\n", name, (double)atomic_load_explicit(&by_type[i].born_built, memory_order_relaxed) / per);
		for (int f = 0; f < NFIELD; f++) {
			for (int k = 0; k < NKIND; k++) {
				uint64_t n = atomic_load_explicit(&by_type[i].count[f][k], memory_order_relaxed);
				if (n) printf("census %s %s%s %.1f\n", name, kind_names[k], field_names[f], (double)n / per);
			}
		}
	}
	printf("census _meta stale %llu\n", (unsigned long long)atomic_load(&census_stale));
	fflush(stdout);
}

void clj_stats_alloc(clj_header *h, const clj_type *type, size_t size, uint32_t cls) {
	CLJ_STAT(CLJ_STAT_ALLOC);
	CLJ_STAT_ADD(CLJ_STAT_ALLOC_BYTES, size);
	bump(&by_type[type_index(type)].allocs);
	if (atomic_load_explicit(&census_on, memory_order_relaxed)) census_birth(h, type, cls);
}

void clj_stats_reuse(const clj_type *type, bool taken) {
	CLJ_STAT(taken ? CLJ_STAT_REUSE_TAKEN : CLJ_STAT_REUSE_COPIED);
	type_allocs *row = &by_type[type_index(type)];
	bump(taken ? &row->reuse_taken : &row->reuse_copied);
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

size_t clj_debug_reuse_by_type(const char **type_names, uint64_t *taken, uint64_t *copied, size_t cap) {
	size_t n = 0;
	for (size_t i = 0; i < TYPE_SLOTS && n < cap; i++) {
		const clj_type *type = atomic_load_explicit(&by_type[i].type, memory_order_acquire);
		if (!type) continue;
		type_names[n] = type->name;
		taken[n] = atomic_load_explicit(&by_type[i].reuse_taken, memory_order_relaxed);
		copied[n++] = atomic_load_explicit(&by_type[i].reuse_copied, memory_order_relaxed);
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

size_t clj_debug_reuse_by_type(const char **type_names, uint64_t *taken, uint64_t *copied, size_t cap) {
	(void)type_names;
	(void)taken;
	(void)copied;
	(void)cap;
	return 0;
}

bool clj_debug_census_begin(void) { return false; }
void clj_debug_census_end(void) {}
void clj_debug_census_print(unsigned iterations) { (void)iterations; }
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

// Shuffled, so neither the prefetcher nor the cache holds the next header.
double clj_debug_rc_op_ns_cold(size_t objects, size_t ops) {
	clj_value *objs = malloc(objects * sizeof *objs);
	uint32_t  *order = malloc(objects * sizeof *order);
	if (!objs || !order) clj_fatal("out of memory");
	for (size_t i = 0; i < objects; i++) {
		objs[i] = clj_double_new((double)i + 0.5);
		order[i] = (uint32_t)i;
	}
	uint64_t x = 0x9E3779B97F4A7C15u;
	for (size_t i = objects - 1; i > 0; i--) {
		x ^= x << 13, x ^= x >> 7, x ^= x << 17;
		size_t   j = x % (i + 1);
		uint32_t t = order[i];
		order[i] = order[j];
		order[j] = t;
	}
	size_t          rounds = ops / (2 * objects) + 1;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (size_t r = 0; r < rounds; r++) {
		for (size_t i = 0; i < objects; i++) {
			clj_retain(objs[order[i]]);
			__asm__ volatile("" ::: "memory");
		}
		for (size_t i = 0; i < objects; i++) {
			clj_release(objs[order[i]]);
			__asm__ volatile("" ::: "memory");
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	for (size_t i = 0; i < objects; i++) clj_release(objs[i]);
	free(objs);
	free(order);
	double ns = (double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec);
	return ns / (double)(rounds * 2 * objects);
}
