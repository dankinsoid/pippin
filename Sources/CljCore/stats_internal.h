// @ai-generated(solo)
#ifndef CLJ_STATS_INTERNAL_H
#define CLJ_STATS_INTERNAL_H

#include "clj/object.h"
#include "clj/stats.h"

// cls is the allocator's size class, CLJ_CENSUS_LARGE past the pool.
enum { CLJ_CENSUS_LARGE = 255 };

#if CLJ_STATS
void clj_stats_alloc(clj_header *h, const clj_type *type, size_t size, uint32_t cls);
// The object's last reference went: rc.c's bury and every dealloc; a second call for one object finds nothing.
void clj_census_death(clj_header *h);
void clj_census_move(clj_header *from, clj_header *to, uint32_t cls);
// A Clojure fn frame, interpreted (eval.c run_body) or compiled (CLJC_ENTER); fn_node names it.
clj_census_mark clj_census_enter(const void *fn_node);
void            clj_census_leave(clj_census_mark m);
// A builder in C (into): what it allocates counts as built.
void clj_census_builder(int delta);
// A ring's census state goes with its execution (coro.c).
void clj_census_ring_free(void *ring);
#endif

#endif
