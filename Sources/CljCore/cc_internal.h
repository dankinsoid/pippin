// @ai-generated(solo)
#ifndef CLJ_CC_INTERNAL_H
#define CLJ_CC_INTERNAL_H

#include <stdbool.h>

#include "clj/cc.h"
#include "clj/coro.h"
#include "clj/object.h"

// A dead object a candidate entry still names: torn down, out of the live count, its cell kept for the entry.
#define CLJ_RC_ZOMBIE (CLJ_RC_WATCH | CLJ_RC_BUFFERED)

// rc.c's release paths. The shared one decrements and files in one CAS (true when it did); the local one only files.
bool clj_cc_enabled(void);
bool clj_cc_shared_candidate(clj_header *h);
void clj_cc_local_candidate(clj_header *h);
// A shared object's RC changed while a collection watched it.
void clj_cc_unwatch(clj_header *h);
// A shared object reached zero while a collection runs: true when it is kept whole until the collection ends.
bool clj_cc_defer_free(clj_header *h);
// After the teardown of a buffered object: the entry gives the cell back when it is processed.
void clj_cc_zombie(clj_header *h);

// rc.c: the free of an object at zero, and a decrement that never makes a candidate (the collector's own references).
void clj_rc_free(clj_header *dead);
void clj_rc_drop(clj_header *h);

// Candidates go to owner's buffer until the return: the two transfers of unshared objects (NOTES "RC", owner check).
void *clj_cc_local_borrow(clj_coro *owner);
void  clj_cc_local_return(clj_coro *owner, void *saved);
// A finished execution's candidates: collected, or handed to the background on the main carrier.
void  clj_cc_execution_done(clj_coro *c);
// An execution freed, or a thread exiting with no TLS left: what its buffer holds goes to the background.
void  clj_cc_execution_free(clj_coro *c);
// The main run loop is about to sleep: its execution's candidates, within a time budget.
void  clj_cc_main_idle(void);

// clj_share without the owner check: a dying thread's TLS is gone.
void clj_share_unowned(clj_value v);

#endif
