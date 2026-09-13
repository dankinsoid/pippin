// @ai-generated(guided)
#include <pthread.h>
#include <stdio.h>

#include "clj/keyword.h"
#include "clj/ns.h"
#include "clj/runtime.h"

static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static clj_output_fn out_fn;
static void         *out_ctx;

static void init(void) {
	clj_ns_core();
	clj_ns_user();
	// Interned up front so printing an error or an analysis position allocates nothing lasting later.
	for (const char *const *k = (const char *const[]){"message", "data", "cause", "line", "column", NULL}; *k; k++) clj_keyword_from_cstr(*k);
	clj_builtins_install();
}

void clj_init(void) { pthread_once(&init_once, init); }

void clj_set_output(clj_output_fn fn, void *ctx) {
	out_fn = fn;
	out_ctx = ctx;
}

void clj_output(const char *bytes, size_t len) {
	if (out_fn) out_fn(bytes, len, out_ctx);
	else fwrite(bytes, 1, len, stdout);
}
