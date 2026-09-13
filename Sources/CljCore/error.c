// @ai-generated(guided)
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "clj/error.h"
#include "clj/map.h"
#include "clj/string.h"

static _Thread_local clj_value pending;

static void exception_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_exception *e = self;
	visit(e->message, ctx);
	visit(e->data, ctx);
	visit(e->cause, ctx);
}

const clj_type clj_exception_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "exception",
	.each_child = exception_each_child,
};

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

clj_value clj_throw(clj_value ex) {
	CLJ_ASSERT(clj_is_exception(ex), "throw of a non-exception");
	clj_release(pending);
	pending = ex;
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

clj_value clj_pending(void) { return pending; }

clj_value clj_take_pending(void) {
	clj_value ex = pending;
	pending = CLJ_NIL;
	return ex;
}
