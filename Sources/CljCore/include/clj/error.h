// @ai-generated(guided)
#ifndef CLJ_ERROR_H
#define CLJ_ERROR_H

#include <stdarg.h>

#include "object.h"

// Runtime errors are values, as in CPython: a failing function returns CLJ_THROWN and leaves the
// exception pending in the calling thread. Callers propagate with `if (v == CLJ_THROWN) return CLJ_THROWN;`.
// clj_fatal stays for runtime bugs; user code never reaches it.
// An error is any value whose type has CLJ_CORE_ERROR; ex-info (clj_exception) is the builtin one.
typedef struct {
	clj_header h;
	clj_slot   message; // string
	clj_slot   data;    // map or nil
	clj_slot   cause;   // exception or nil
	clj_atomic_slot trace;   // vector set at the first throw, nil before; a rethrow keeps it
	clj_slot   type;    // keyword or nil, lifted from a keyword under :type in data at construction (design.md §4)
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
// Consumes ex, captures nothing: the pending trace is nil. For a throw whose frames nobody reads (a cancellation).
clj_value clj_throw_untraced(clj_value ex);
// A message string built to its own length: design §3 «Диагностика» counts truncating a diagnostic to a
// fixed buffer a defect, so no caller formats one into a char[]. A long form inside it is shortened by
// clj_pr_str_max before it is formatted in; the assembled message is never cut. Owned.
clj_value clj_error_message(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
clj_value clj_error_message_v(const char *fmt, va_list ap) __attribute__((format(printf, 1, 0)));

// ex-info with the formatted message and nil data.
clj_value clj_throw_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// The same, for a throw that interrupts an unwind: cause is the in-flight value, dropped if it is no error.
clj_value clj_throw_msg_cause(clj_value cause, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// Borrowed; nil when nothing is pending.
clj_value clj_pending(void);
// Owned; clears the slot and drops the pending trace. nil when nothing is pending.
clj_value clj_take_pending(void);
// Trace of the pending value (shadow.h), borrowed / owned; the slot is cleared by either take.
clj_value clj_pending_trace(void);
clj_value clj_take_pending_trace(void);

// A cancellation that equals/hash dropped must not become an answer: `=`, `not=` and `hash` rethrow it.
// rethrow is true with the cancellation pending.
void clj_equals_drop_pending(void);
void clj_equals_watch(void);
bool clj_equals_rethrow(void);

// Slots cannot throw, so a refusing hash/equals records here (design §5 «Равенство и хэш бокса»); storing a key,
// `=`, `not=` and `hash` throw the record, a lookup drops it ("absent" is true). message is immortal.
void clj_refuse(const char *message);
// Nonzero while some execution holds a record, so a process where nothing refused pays one load.
extern _Atomic uint32_t clj_refusals_held;
bool clj_refusal_rethrow_slow(void);
void clj_refusal_drop_slow(void);
// True with the refusal pending as an ex-info carrying the trace.
static inline bool clj_refusal_rethrow(void) {
	return atomic_load_explicit(&clj_refusals_held, memory_order_relaxed) && clj_refusal_rethrow_slow();
}
static inline void clj_refusal_drop(void) {
	if (atomic_load_explicit(&clj_refusals_held, memory_order_relaxed)) clj_refusal_drop_slow();
}

// Interns the keywords this module otherwise makes on first use; clj_init calls it (runtime.c).
void clj_error_intern_keywords(void);

static inline bool           clj_is_thrown(clj_value v) { return v == CLJ_THROWN; }
// Any error type (CLJ_CORE_ERROR).
static inline bool           clj_is_exception(clj_value v) { return clj_has_core(v, CLJ_CORE_ERROR); }
// The ex-info type only.
static inline bool           clj_is_ex_info(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_exception_type; }
static inline clj_exception *clj_exception_of(clj_value v) { return (clj_exception *)clj_to_ptr(v); }
// Fields of an ex-info, borrowed: valid while ex is. Other error types go through the clj_ex_* slots.
static inline clj_value clj_exception_message(clj_value ex) { return clj_exception_of(ex)->message.v; }
static inline clj_value clj_exception_data(clj_value ex) { return clj_exception_of(ex)->data.v; }
static inline clj_value clj_exception_cause(clj_value ex) { return clj_exception_of(ex)->cause.v; }
static inline clj_value clj_exception_trace(clj_value ex) { return clj_slot_load(&clj_exception_of(ex)->trace, memory_order_acquire); }

// A host (Swift) error carried through Clojure code. The payload is opaque to the core; message and type are
// captured when the value is made. ex-data is {:host/error <the value itself>}, built on each call.
typedef struct {
	clj_header h;
	clj_slot   message; // string
	// ex-type: the host type (hosttype.h), interned by the host that boxed the error; nil when it could not.
	clj_slot  type;
	void     *payload;
	void (*release)(void *payload); // NULL when the payload needs no cleanup
} clj_host_error;

extern const clj_type clj_host_error_type;

// message is borrowed, type borrowed (an interned host type is immortal) and nil when the host had none.
// The value owns payload: release runs once, when it dies, on any thread.
clj_value clj_host_error_new(clj_value message, clj_value type, void *payload, void (*release)(void *payload));

static inline bool  clj_is_host_error(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_host_error_type; }
// Borrowed: valid while v is.
static inline void     *clj_host_error_payload(clj_value v) { return ((clj_host_error *)clj_to_ptr(v))->payload; }
static inline clj_value clj_host_error_message(clj_value v) { return ((clj_host_error *)clj_to_ptr(v))->message.v; }

// A string is its own ex-message (NOTES.md); nil for a non-error value otherwise.
clj_value clj_ex_message(clj_value v);
clj_value clj_ex_data(clj_value v);
clj_value clj_ex_cause(clj_value v);
// nil for a value never thrown and for every non-ex-info.
clj_value clj_ex_trace(clj_value v);

// Total and heterogeneous as clojure.core/type is (design §4): keyword -> itself, ex-info -> its :type
// slot, a cancellation -> :cancelled, host error -> its host type, else nil.
clj_value clj_ex_type(clj_value v);
// isa? k on ex-type, scalar case only; reads global-hierarchy's map directly, safe to call mid-unwind.
bool clj_ex_isa(clj_value thrown, clj_value k);
// Caches the global-hierarchy var; runtime.c calls it once after boot, before any keyword catch runs.
void clj_isa_install(void);
// What deadline/cancel throws as ex-type, and what :default/Throwable/Exception/Object let by.
clj_value clj_cancelled_keyword(void);

// Not an ex-info (design.md §4): a selector naming no specific error misses it by construction, not a carve-out.
typedef struct {
	clj_header h;
	clj_slot   message;
	clj_slot   data;
	clj_slot   cause; // what a scope cancelled this coroutine for, nil otherwise (Go's context.Cause)
} clj_cancellation;

extern const clj_type clj_cancellation_type;

static inline bool clj_is_cancellation(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_cancellation_type; }
static inline clj_cancellation *clj_cancellation_of(clj_value v) { return (clj_cancellation *)clj_to_ptr(v); }
// Borrowed, valid while v is; mirrors clj_exception_message/data (the printer reads these directly).
static inline clj_value clj_cancellation_message(clj_value v) { return clj_cancellation_of(v)->message.v; }
static inline clj_value clj_cancellation_data(clj_value v) { return clj_cancellation_of(v)->data.v; }
static inline clj_value clj_cancellation_cause(clj_value v) { return clj_cancellation_of(v)->cause.v; }

// Throws a clj_cancellation: explicit cancel, a coroutine's own deadline check, an nREPL interrupt
// (clj_coro_cancel) and a cancelled channel op all arrive here. Untraced, and the value is one of two
// immortal singletons, so two plain cancellations are identical? (design.md §4).
clj_value clj_throw_cancelled(bool deadline);

#endif
