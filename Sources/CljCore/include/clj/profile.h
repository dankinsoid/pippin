// @ai-generated(guided)
#ifndef CLJ_PROFILE_H
#define CLJ_PROFILE_H

#include <stdbool.h>

#include "value.h"

// Instruments signposts (subsystem "clj", category PointsOfInterest, name "invoke") around every closure
// call. Apple only: elsewhere enable is a no-op and enabled stays false.
void clj_signposts_enable(bool on);
bool clj_signposts_enabled(void);

// Fn-level profiler: inclusive wall time per fn node, aggregated when a call returns. Natives and self
// time are not measured.
void clj_profile_start(void);
// {:fns [{:name sym-or-nil :line n :column n :calls n :ns n} ...]} sorted by :ns descending; owned. Turns the profiler off.
clj_value clj_profile_stop(void);
bool      clj_profile_running(void);

#endif
