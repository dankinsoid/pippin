// @ai-generated(solo)
#ifndef CLJ_CC_H
#define CLJ_CC_H

#include <stdint.h>

#include "value.h"

// The cycle collector (design §7, «Сборщик циклов: как он устроен»): trial deletion over the candidates RC leaves.

// A full collection of the calling execution's candidates and the shared buffer, on the calling thread; the number
// of objects it freed.
int64_t clj_cc_collect(void);

enum {
	CLJ_CC_STAT_COLLECTIONS,     // collections run, local and shared
	CLJ_CC_STAT_FREED,           // objects freed as members of a garbage cycle
	CLJ_CC_STAT_CANDIDATES,      // entries filed
	CLJ_CC_STAT_HANDOFFS,        // main-carrier candidates handed to the background
	CLJ_CC_STAT_VISITED,         // nodes visited
	CLJ_CC_STAT_INTERFERED,      // roots put back because a mutator touched them mid-collection
	CLJ_CC_STAT_HANDOFF_MAX_NS,  // the longest hand-off of one main-carrier candidate past the bound
	CLJ_CC_STAT_DEEP_FILED,      // entries a replaced var root's release filed for the deep walk
	CLJ_CC_STAT_DEEP_RETRIES,    // rings a deep walk found alive, and touched roots, filed again for a later walk
	CLJ_CC_STAT_DEEP_CUT,        // deep collections that stopped adding nodes at their cap
	CLJ_CC_STAT_COROUTINES,      // parked coroutines found garbage and cancelled (design §7, «Фаза 3»)
	CLJ_CC_STAT_MARKED,          // acyclic marks set by local collections (design §7, «Метка «ацикличен»»)
	CLJ_CC_STAT_SKIPPED,         // marked nodes a local walk did not enter, candidates among them
	CLJ_CC_STAT_EPOCHS,          // edges stored into a guarded reference type, each voiding every mark
	CLJ_CC_STAT_UNFILED,         // nonzero releases of a validly marked node, filed as no candidate
	CLJ_CC_STAT_SHARED_FILED,    // of the candidates, shared ones: a release's, a parked coroutine's
	CLJ_CC_STAT_COUNT
};
void clj_debug_cc_stats(int64_t out[CLJ_CC_STAT_COUNT]);
// Candidates waiting: the calling execution's local buffer, and the shared one.
int64_t clj_debug_cc_pending_local(void);
int64_t clj_debug_cc_pending_shared(void);
// Deep entries the calling thread filed, ever; retries waiting.
int64_t clj_debug_cc_deep_filed_here(void);
int64_t clj_debug_cc_deep_retries(void);
// Treat the calling thread as the main carrier for the candidate policy only (tests of the hand-off and the idle hook).
void clj_debug_cc_as_main(int on);
// Runs the main run loop's idle hook now; the number of candidates it processed.
int64_t clj_debug_cc_main_idle(void);
// The calling execution's local collection now, without the shared one; the objects it freed.
int64_t clj_debug_cc_collect_local(void);
// Files an unshared object that may lie on a cycle as a local candidate, as a release to a nonzero count would.
void clj_debug_cc_file_local(clj_value v);
// Whether v carries an acyclic mark the calling execution's checked table makes valid.
int clj_debug_cc_acyclic(clj_value v);
// The calling thread audits one skip of a marked node in n; 0 restores the default.
void clj_debug_cc_audit_every(uint32_t n);

#endif
