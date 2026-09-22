// @ai-generated(guided)
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#include "clj/error.h"
#include "clj/eval.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/set.h"
#include "clj/shadow.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "coro_internal.h"

#define pending (clj_coro_current()->pending)
#define pending_trace (clj_coro_current()->pending_trace)

enum { TRACE_FRAMES = 256 };

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_host_error, kw_type, kw_cancelled, kw_cancel_kind, kw_explicit, kw_deadline, kw_ancestors;
static clj_value      cancelled_explicit, cancelled_deadline;

static clj_value cancellation_new(bool deadline);

static void intern_keywords(void) {
	kw_host_error = clj_keyword_from_cstr("host/error");
	kw_type = clj_keyword_from_cstr("type");
	kw_cancelled = clj_keyword_from_cstr("cancelled");
	kw_cancel_kind = clj_keyword_from_cstr("cancel/kind");
	kw_explicit = clj_keyword_from_cstr("explicit");
	kw_deadline = clj_keyword_from_cstr("deadline");
	kw_ancestors = clj_keyword_from_cstr("ancestors");
	int64_t before = clj_debug_live_objects();
	cancelled_explicit = cancellation_new(false);
	cancelled_deadline = cancellation_new(true);
	// Immortal by construction, not leaked: the count of a debug build must not carry them.
	if (before >= 0) clj_debug_live_objects_exclude(clj_debug_live_objects() - before);
}

void clj_error_intern_keywords(void) { pthread_once(&keywords_once, intern_keywords); }

clj_value clj_cancelled_keyword(void) { return kw_cancelled; }

static void exception_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_exception *e = self;
	visit(e->message, ctx);
	visit(e->data, ctx);
	visit(e->cause, ctx);
	visit(e->trace, ctx);
	visit(e->type, ctx);
}

static uint32_t exception_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool exception_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

static clj_value exception_message(clj_value self) { return clj_retain(clj_exception_of(self)->message); }
static clj_value exception_data(clj_value self) { return clj_retain(clj_exception_of(self)->data); }
static clj_value exception_cause(clj_value self) { return clj_retain(clj_exception_of(self)->cause); }

// Identity hash and equality, as Throwable on the JVM.
const clj_type clj_exception_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "exception",
	.core_bits = CLJ_CORE_ERROR,
	.each_child = exception_each_child,
	.hash = exception_hash,
	.equals = exception_equals,
	.ex_message = exception_message,
	.ex_data = exception_data,
	.ex_cause = exception_cause,
};

static void host_error_each_child(void *self, clj_visitor visit, void *ctx) { visit(((clj_host_error *)self)->message, ctx); }

static void host_error_finalize(void *self) {
	clj_host_error *e = self;
	if (e->release) e->release(e->payload);
}

static clj_value host_error_message(clj_value self) { return clj_retain(((clj_host_error *)clj_to_ptr(self))->message); }

// Built per call: storing the map would make the value its own child, a cycle RC never frees.
static clj_value host_error_data(clj_value self) {
	pthread_once(&keywords_once, intern_keywords);
	return clj_map_assoc(clj_map_empty(), kw_host_error, self);
}

static clj_value host_error_cause(clj_value self) {
	(void)self;
	return CLJ_NIL;
}

// @ai-generated(guided)
const clj_type clj_host_error_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "host-error",
	.core_bits = CLJ_CORE_ERROR,
	.each_child = host_error_each_child,
	.finalize = host_error_finalize,
	.hash = exception_hash,
	.equals = exception_equals,
	.ex_message = host_error_message,
	.ex_data = host_error_data,
	.ex_cause = host_error_cause,
};

// @ai-generated(guided)
clj_value clj_host_error_new(clj_value message, void *payload, void (*release)(void *payload)) {
	CLJ_ASSERT(clj_is_string(message), "host error message must be a string");
	clj_host_error *e = clj_alloc(&clj_host_error_type, sizeof *e);
	e->message = clj_retain(message);
	e->payload = payload;
	e->release = release;
	return clj_from_ptr(e);
}

static void cancellation_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_cancellation *c = self;
	visit(c->message, ctx);
	visit(c->data, ctx);
}

// core_bits carries no CLJ_CORE_ERROR: a selector naming no specific error misses this by construction.
const clj_type clj_cancellation_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "cancellation",
	.each_child = cancellation_each_child,
	.hash = exception_hash,
	.equals = exception_equals,
};

// type is set directly here, not derived from data: clj_throw_cancelled tags :cancelled without a :type key.
static clj_value ex_info_new(clj_value message, clj_value data, clj_value cause, clj_value type) {
	CLJ_ASSERT(clj_is_string(message), "exception message must be a string");
	CLJ_ASSERT(clj_is_nil(data) || clj_header_of(data)->type == &clj_map_type, "exception data must be a map or nil");
	CLJ_ASSERT(clj_is_nil(cause) || clj_is_exception(cause), "exception cause must be an exception or nil");
	clj_exception *e = clj_alloc(&clj_exception_type, sizeof *e);
	e->message = clj_retain(message);
	e->data = clj_retain(data);
	e->cause = clj_retain(cause);
	e->type = type;
	return clj_from_ptr(e);
}

// ex-info lifts a keyword under :type into its own slot; the key stays in data (design.md §4, scope item 1).
clj_value clj_ex_info_cause(clj_value message, clj_value data, clj_value cause) {
	pthread_once(&keywords_once, intern_keywords);
	clj_value type = clj_is_nil(data) ? CLJ_NIL : clj_map_get(data, kw_type, CLJ_NIL);
	return ex_info_new(message, data, cause, clj_is_keyword(type) ? type : CLJ_NIL);
}

clj_value clj_ex_info(clj_value message, clj_value data) { return clj_ex_info_cause(message, data, CLJ_NIL); }

clj_value clj_ex_type(clj_value v) {
	if (clj_is_keyword(v)) return clj_retain(v);
	if (clj_is_cancellation(v)) return clj_retain(kw_cancelled);
	if (clj_is_ex_info(v)) return clj_retain(clj_exception_of(v)->type);
	return CLJ_NIL;
}

// Resolved once, after boot/core.clj defines it; clj_isa_install (runtime.c) sets it before any user code runs.
static clj_value hierarchy_var = CLJ_NIL;

void clj_isa_install(void) {
	clj_value sym = clj_symbol_from_cstr("global-hierarchy");
	hierarchy_var = clj_ns_resolve(clj_ns_core(), sym);
	clj_release(sym);
	if (clj_is_nil(hierarchy_var)) clj_fatal("isa bridge names no core var");
}

bool clj_ex_isa(clj_value thrown, clj_value k) {
	clj_value t = clj_ex_type(thrown);
	bool      match = t == k;
	if (!match && !clj_is_nil(t) && !clj_is_nil(hierarchy_var)) {
		clj_value h = clj_var_root(hierarchy_var);
		clj_value ancestors = clj_has_core(h, CLJ_CORE_MAP) ? clj_map_get(h, kw_ancestors, CLJ_NIL) : CLJ_NIL;
		clj_value tset = clj_has_core(ancestors, CLJ_CORE_MAP) ? clj_map_get(ancestors, t, CLJ_NIL) : CLJ_NIL;
		match = clj_has_core(tset, CLJ_CORE_SET) && clj_set_contains(tset, k);
	}
	clj_release(t);
	return match;
}

// @ai-generated(guided)
static clj_value cancellation_new(bool deadline) {
	clj_cancellation *c = clj_alloc(&clj_cancellation_type, sizeof *c);
	c->data = clj_map_assoc(clj_map_empty(), kw_cancel_kind, deadline ? kw_deadline : kw_explicit);
	c->message = clj_string_from_cstr(deadline ? CLJ_DEADLINE_MESSAGE : CLJ_CANCELLED_MESSAGE);
	clj_value v = clj_from_ptr(c);
	// Share before the flag: clj_share stops at an immortal root, so the children would stay unshared after it.
	clj_share(v);
	c->h.flags |= CLJ_FLAG_IMMORTAL;
	return v;
}

// The space of plain cancellations is two values, so two of them are identical? — intended and observable.
clj_value clj_throw_cancelled(bool deadline) {
	pthread_once(&keywords_once, intern_keywords);
	return clj_throw_untraced(clj_retain(deadline ? cancelled_deadline : cancelled_explicit));
}

// @ai-generated(guided)
clj_value clj_throw_traced(clj_value ex, clj_value trace) {
	if (clj_is_ex_info(ex)) {
		clj_exception *e = clj_exception_of(ex);
		if (clj_is_nil(e->trace)) {
			if (clj_is_nil(trace)) trace = clj_shadow_stack_trace(TRACE_FRAMES);
			if (e->h.flags & CLJ_FLAG_SHARED) clj_share(trace);
			e->trace = trace;
		} else {
			clj_release(trace);
		}
		trace = clj_retain(e->trace);
	} else if (clj_is_nil(trace) && !clj_is_cancellation(ex)) {
		trace = clj_shadow_stack_trace(TRACE_FRAMES);
	}
	clj_release(pending);
	clj_release(pending_trace);
	pending = ex;
	pending_trace = trace;
	return CLJ_THROWN;
}

clj_value clj_throw(clj_value ex) { return clj_throw_traced(ex, CLJ_NIL); }

// @ai-generated(guided)
clj_value clj_throw_untraced(clj_value ex) {
	clj_release(pending);
	clj_release(pending_trace);
	pending = ex;
	pending_trace = CLJ_NIL;
	return CLJ_THROWN;
}

clj_value clj_throw_msg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0) clj_fatal("vsnprintf failed");
	char *text = malloc((size_t)n + 1);
	if (!text) clj_fatal("out of memory");
	va_start(ap, fmt);
	vsnprintf(text, (size_t)n + 1, fmt, ap);
	va_end(ap);
	clj_value message = clj_string_new(text, (size_t)n);
	free(text);
	clj_value ex = clj_ex_info(message, CLJ_NIL);
	clj_release(message);
	return clj_throw(ex);
}

// A thrown string is its own message (CLJS says nil), so a :default handler reads (throw "m") like an ex-info.
clj_value clj_ex_message(clj_value v) {
	if (clj_is_string(v)) return clj_retain(v);
	if (clj_is_cancellation(v)) return clj_retain(clj_cancellation_message(v));
	return clj_is_exception(v) ? clj_type_of(v)->ex_message(v) : CLJ_NIL;
}

clj_value clj_ex_data(clj_value v) {
	if (clj_is_cancellation(v)) return clj_retain(clj_cancellation_data(v));
	return clj_is_exception(v) ? clj_type_of(v)->ex_data(v) : CLJ_NIL;
}

clj_value clj_ex_cause(clj_value v) { return clj_is_exception(v) ? clj_type_of(v)->ex_cause(v) : CLJ_NIL; }

clj_value clj_ex_trace(clj_value v) { return clj_is_ex_info(v) ? clj_retain(clj_exception_of(v)->trace) : CLJ_NIL; }

clj_value clj_pending(void) { return pending; }

clj_value clj_take_pending(void) {
	clj_value ex = pending;
	pending = CLJ_NIL;
	clj_release(pending_trace);
	pending_trace = CLJ_NIL;
	return ex;
}

clj_value clj_pending_trace(void) { return pending_trace; }

clj_value clj_take_pending_trace(void) {
	clj_value trace = pending_trace;
	pending_trace = CLJ_NIL;
	return trace;
}

#undef pending
#undef pending_trace
void clj_coro_drop_pending(clj_coro *c) {
	clj_release(c->pending);
	clj_release(c->pending_trace);
	c->pending = CLJ_NIL;
	c->pending_trace = CLJ_NIL;
}
