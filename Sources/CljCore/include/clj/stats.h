// @ai-generated(solo)
#ifndef CLJ_STATS_H
#define CLJ_STATS_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Boxed numbers and lazy-seq objects are allocations of their types: clj_debug_allocs_by_type.
typedef enum {
	CLJ_STAT_ALLOC,
	CLJ_STAT_ALLOC_BYTES,
	CLJ_STAT_FREE,
	CLJ_STAT_INVOKE,
	CLJ_STAT_C_INVOKE,
	CLJ_STAT_APPLY,
	CLJ_STAT_PROTO_GENERIC,
	CLJ_STAT_PROTO_MISS,
	CLJ_STAT_LAZY_FORCE,
	CLJ_STAT_HASH,
	CLJ_STAT_EQUALS,
	// clj_is_unique asked of a heap object: in place (rc 1) or a copy.
	CLJ_STAT_REUSE_TAKEN,
	CLJ_STAT_REUSE_COPIED,
	CLJ_STAT_COUNT
} clj_stat;

#if CLJ_STATS
extern _Atomic uint64_t clj_stats[CLJ_STAT_COUNT];
#define CLJ_STAT_ADD(k, n) atomic_fetch_add_explicit(&clj_stats[k], (uint64_t)(n), memory_order_relaxed)
#else
#define CLJ_STAT_ADD(k, n) ((void)0)
#endif
#define CLJ_STAT(k) CLJ_STAT_ADD(k, 1)

// False, with out untouched, in a build without -DCLJ_STATS.
bool        clj_debug_stats(uint64_t out[CLJ_STAT_COUNT]);
const char *clj_debug_stat_name(int k);
// Retains and releases by path (CLJ_RC_PLAIN, _SHARED, _IMMORTAL): the debug build's counters, kept by a stats build.
bool clj_debug_stats_rc(uint64_t out[3]);
// Objects allocated per type since the start, by type name; 0 in a build without -DCLJ_STATS.
size_t clj_debug_allocs_by_type(const char **names, uint64_t *counts, size_t cap);
// clj_is_unique's answers per type of the object asked, since the start; 0 in a build without -DCLJ_STATS.
size_t clj_debug_reuse_by_type(const char **names, uint64_t *taken, uint64_t *copied, size_t cap);
// ns per inline retain or release of an unshared object on this machine, the unit the RC share is estimated in.
double clj_debug_rc_op_ns(size_t ops);
// The same over `objects` objects visited in a shuffled order: a header the cache no longer holds.
double clj_debug_rc_op_ns_cold(size_t objects, size_t ops);

// The allocation census (stats.c): where each object allocated between begin and end dies relative to the fn frame
// it was born in. Begin is false, and the rest do nothing, in a build without -DCLJ_STATS.
bool clj_debug_census_begin(void);
void clj_debug_census_end(void);
// `census <type> <field> <count / iterations>` lines on stdout, the fields named in stats.c.
void clj_debug_census_print(unsigned iterations);

// A census frame's entry: the execution's ring and the depth to return to (clj_census_enter, stats_internal.h).
typedef struct {
	void    *ring;
	uint32_t depth;
} clj_census_mark;

#endif
