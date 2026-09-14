// @ai-generated(guided)
#ifndef CLJ_ERROR_H
#define CLJ_ERROR_H

#include "object.h"

// Runtime errors are values, as in CPython: a failing function returns CLJ_THROWN and leaves the
// exception pending in the calling thread. Callers propagate with `if (v == CLJ_THROWN) return CLJ_THROWN;`.
// clj_fatal stays for runtime bugs; user code never reaches it.
// An error is any value whose type has CLJ_CORE_ERROR; ex-info (clj_exception) is the builtin one.
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
// Any value can be thrown, as in ClojureScript; (throw 42) leaves the fixnum pending.
clj_value clj_throw(clj_value ex);
// ex-info with the formatted message and nil data.
clj_value clj_throw_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Borrowed; nil when nothing is pending.
clj_value clj_pending(void);
// Owned; clears the slot. nil when nothing is pending.
clj_value clj_take_pending(void);

static inline bool           clj_is_thrown(clj_value v) { return v == CLJ_THROWN; }
// Any error type (CLJ_CORE_ERROR).
static inline bool           clj_is_exception(clj_value v) { return clj_has_core(v, CLJ_CORE_ERROR); }
// The ex-info type only.
static inline bool           clj_is_ex_info(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_exception_type; }
static inline clj_exception *clj_exception_of(clj_value v) { return (clj_exception *)clj_to_ptr(v); }
// Fields of an ex-info, borrowed: valid while ex is. Other error types go through the clj_ex_* slots.
static inline clj_value clj_exception_message(clj_value ex) { return clj_exception_of(ex)->message; }
static inline clj_value clj_exception_data(clj_value ex) { return clj_exception_of(ex)->data; }
static inline clj_value clj_exception_cause(clj_value ex) { return clj_exception_of(ex)->cause; }

// ex-message / ex-data / ex-cause of any value through the type's slots: owned, nil when v is not an
// error, except that a string is its own message (NOTES.md).
clj_value clj_ex_message(clj_value v);
clj_value clj_ex_data(clj_value v);
clj_value clj_ex_cause(clj_value v);

#endif
