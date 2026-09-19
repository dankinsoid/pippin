// @ai-generated(guided)
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#include "clj/error.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/shadow.h"
#include "clj/string.h"
#include "coro_internal.h"

#define pending (clj_coro_current()->pending)
#define pending_trace (clj_coro_current()->pending_trace)

enum { TRACE_FRAMES = 256 };

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_host_error;

static void intern_keywords(void) { kw_host_error = clj_keyword_from_cstr("host/error"); }

void clj_error_intern_keywords(void) { pthread_once(&keywords_once, intern_keywords); }

static void exception_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_exception *e = self;
	visit(e->message, ctx);
	visit(e->data, ctx);
	visit(e->cause, ctx);
	visit(e->trace, ctx);
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

clj_value clj_ex_info_cause(clj_value message, clj_value data, clj_value cause) {
	CLJ_ASSERT(clj_is_string(message), "exception message must be a string");
	CLJ_ASSERT(clj_is_nil(data) || clj_header_of(data)->type == &clj_map_type, "exception data must be a map or nil");
	CLJ_ASSERT(clj_is_nil(cause) || clj_is_exception(cause), "exception cause must be an exception or nil");
	clj_exception *e = clj_alloc(&clj_exception_type, sizeof *e);
	e->message = clj_retain(message);
	e->data = clj_retain(data);
	e->cause = clj_retain(cause);
	return clj_from_ptr(e);
}

clj_value clj_ex_info(clj_value message, clj_value data) { return clj_ex_info_cause(message, data, CLJ_NIL); }

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
	} else if (clj_is_nil(trace)) {
		trace = clj_shadow_stack_trace(TRACE_FRAMES);
	}
	clj_release(pending);
	clj_release(pending_trace);
	pending = ex;
	pending_trace = trace;
	return CLJ_THROWN;
}

clj_value clj_throw(clj_value ex) { return clj_throw_traced(ex, CLJ_NIL); }

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
	return clj_is_exception(v) ? clj_type_of(v)->ex_message(v) : CLJ_NIL;
}

clj_value clj_ex_data(clj_value v) { return clj_is_exception(v) ? clj_type_of(v)->ex_data(v) : CLJ_NIL; }

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
