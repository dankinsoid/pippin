// @ai-generated(solo)
#ifndef CLJ_CC_H
#define CLJ_CC_H

#include <stdint.h>

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
	CLJ_CC_STAT_COUNT
};
void clj_debug_cc_stats(int64_t out[CLJ_CC_STAT_COUNT]);
// Candidates waiting: the calling execution's local buffer, and the shared one.
int64_t clj_debug_cc_pending_local(void);
int64_t clj_debug_cc_pending_shared(void);
// Treat the calling thread as the main carrier for the candidate policy only (tests of the hand-off and the idle hook).
void clj_debug_cc_as_main(int on);
// Runs the main run loop's idle hook now; the number of candidates it processed.
int64_t clj_debug_cc_main_idle(void);

#endif
