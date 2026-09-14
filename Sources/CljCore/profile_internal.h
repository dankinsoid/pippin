// @ai-generated(guided)
#ifndef CLJ_PROFILE_INTERNAL_H
#define CLJ_PROFILE_INTERNAL_H

#include <stdint.h>

#include "clj/analyzer.h"
#include "clj/profile.h"
#include "clj/string.h"
#include "clj/symbol.h"

// One load and branch in the invoke path covers both tools; the bits say which is on.
enum { CLJ_INSTRUMENT_PROFILE = 1, CLJ_INSTRUMENT_SIGNPOSTS = 2 };
extern uint8_t clj_instrument;

uint64_t clj_profile_now(void);
void     clj_profile_record(const clj_node *fn_node, uint64_t ns);

#ifdef __APPLE__
#include <os/signpost.h>

extern os_log_t clj_signposts_log;

static inline os_signpost_id_t clj_signpost_begin(clj_value name) {
	os_signpost_id_t id = os_signpost_id_generate(clj_signposts_log);
	if (clj_is_nil(name)) {
		os_signpost_interval_begin(clj_signposts_log, id, "invoke", "%{public}s", "fn");
	} else if (clj_is_nil(clj_symbol_ns(name))) {
		os_signpost_interval_begin(clj_signposts_log, id, "invoke", "%{public}s", clj_string_bytes(clj_symbol_name(name)));
	} else {
		os_signpost_interval_begin(clj_signposts_log, id, "invoke", "%{public}s/%{public}s", clj_string_bytes(clj_symbol_ns(name)),
		                           clj_string_bytes(clj_symbol_name(name)));
	}
	return id;
}

static inline void clj_signpost_end(os_signpost_id_t id) { os_signpost_interval_end(clj_signposts_log, id, "invoke", ""); }
#endif

#endif
