// @ai-generated(solo)
#ifndef CLJ_LOAD_INTERNAL_H
#define CLJ_LOAD_INTERNAL_H

#include "clj/runtime.h"

// Set by a loader before clj_eval of a top-level form: the hook fires for that form's nodes and is disarmed after.
typedef struct {
	bool          armed;
	clj_load_form form; // file borrowed from the loader for the call
} clj_load_arm;

extern _Thread_local clj_load_arm clj_load_arm_tls;
// True after a clj_eval whose form never reached evaluation (a macro or analysis failure): no node was seen.
extern _Thread_local bool clj_load_analysis_failed;

uint64_t clj_load_next_serial(void);
// The name a (def… name …) form defines, or nil; borrowed from the form.
clj_value clj_load_form_name(clj_value form);

#endif
