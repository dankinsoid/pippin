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
	clj_value  trace;   // vector set at the first throw, nil before; a rethrow keeps it
} clj_exception;

extern const clj_type clj_exception_type;

// ex-info. message must be a string, data a map or nil, cause an exception or nil.
clj_value clj_ex_info(clj_value message, clj_value data);
clj_value clj_ex_info_cause(clj_value message, clj_value data, clj_value cause);

// Consumes ex (+1 in): the pending slot takes it over, dropping any exception already pending.
// Any value can be thrown, as in ClojureScript; (throw 42) leaves the fixnum pending.
// The trace is captured here, not at construction: the frames of the thrower are what a reader wants.
clj_value clj_throw(clj_value ex);
// Consumes both; trace nil captures the shadow stack. A rethrow of a caught non-error value keeps its frames.
clj_value clj_throw_traced(clj_value ex, clj_value trace);
// ex-info with the formatted message and nil data.
clj_value clj_throw_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Borrowed; nil when nothing is pending.
clj_value clj_pending(void);
// Owned; clears the slot and drops the pending trace. nil when nothing is pending.
clj_value clj_take_pending(void);
// Trace of the pending value (shadow.h), borrowed / owned; the slot is cleared by either take.
clj_value clj_pending_trace(void);
clj_value clj_take_pending_trace(void);

// Interns the keywords this module otherwise makes on first use; clj_init calls it (runtime.c).
void clj_error_intern_keywords(void);

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
static inline clj_value clj_exception_trace(clj_value ex) { return clj_exception_of(ex)->trace; }

// A host (Swift) error carried through Clojure code. The payload is opaque to the core; the message is
// captured when the value is made. ex-data is {:host/error <the value itself>}, built on each call.
typedef struct {
	clj_header h;
	clj_value  message; // string
	void      *payload;
	void (*release)(void *payload); // NULL when the payload needs no cleanup
} clj_host_error;

extern const clj_type clj_host_error_type;

// message is borrowed. The value owns payload: release runs once, when it dies, on any thread.
clj_value clj_host_error_new(clj_value message, void *payload, void (*release)(void *payload));

static inline bool  clj_is_host_error(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_host_error_type; }
// Borrowed: valid while v is.
static inline void     *clj_host_error_payload(clj_value v) { return ((clj_host_error *)clj_to_ptr(v))->payload; }
static inline clj_value clj_host_error_message(clj_value v) { return ((clj_host_error *)clj_to_ptr(v))->message; }

// ex-message / ex-data / ex-cause of any value through the type's slots: owned, nil when v is not an
// error, except that a string is its own message (NOTES.md).
clj_value clj_ex_message(clj_value v);
clj_value clj_ex_data(clj_value v);
clj_value clj_ex_cause(clj_value v);
// ex-trace: the trace of an ex-info, owned; nil for one never thrown and for every other value.
clj_value clj_ex_trace(clj_value v);

#endif
