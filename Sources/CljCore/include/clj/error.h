// @ai-generated(guided)
#ifndef CLJ_ERROR_H
#define CLJ_ERROR_H

#include "object.h"

// Runtime errors are values, as in CPython: a failing function returns CLJ_THROWN and leaves the
// exception pending in the calling thread. Callers propagate with `if (v == CLJ_THROWN) return CLJ_THROWN;`.
// clj_fatal stays for runtime bugs; user code never reaches it.
typedef struct {
	clj_header h;
	clj_value  message; // string
	clj_value  data;    // map or nil
	clj_value  cause;   // exception or nil
} clj_exception;

extern const clj_type clj_exception_type;

// ex-info. message must be a string, data a map or nil, cause an exception or nil.
clj_value clj_ex_info(clj_value message, clj_value data);
clj_value clj_ex_info_cause(clj_value message, clj_value data, clj_value cause);

// Consumes ex (+1 in): the pending slot takes it over, dropping any exception already pending.
clj_value clj_throw(clj_value ex);
// ex-info with the formatted message and nil data.
clj_value clj_throw_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Borrowed; nil when nothing is pending.
clj_value clj_pending(void);
// Owned; clears the slot. nil when nothing is pending.
clj_value clj_take_pending(void);

static inline bool           clj_is_thrown(clj_value v) { return v == CLJ_THROWN; }
static inline bool           clj_is_exception(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_exception_type; }
static inline clj_exception *clj_exception_of(clj_value v) { return (clj_exception *)clj_to_ptr(v); }
// Borrowed: valid while ex is.
static inline clj_value clj_exception_message(clj_value ex) { return clj_exception_of(ex)->message; }
static inline clj_value clj_exception_data(clj_value ex) { return clj_exception_of(ex)->data; }
static inline clj_value clj_exception_cause(clj_value ex) { return clj_exception_of(ex)->cause; }

#endif
