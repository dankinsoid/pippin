// @ai-generated(guided)
#include <pthread.h>
#include <stdio.h>

#include "clj/analyzer.h"
#include "clj/keyword.h"
#include "clj/ns.h"
#include "clj/runtime.h"
#include "clj/symbol.h"
#include "clj/var.h"

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

clj_value clj_syntax_quote_resolve(clj_value sym, void *ctx) {
	(void)ctx;
	if (!clj_is_nil(clj_symbol_ns(sym)) || clj_is_special_symbol(sym)) return clj_retain(sym);
	clj_value ns = clj_ns_current();
	clj_value var = clj_ns_resolve(ns, sym);
	clj_value ns_name = clj_is_nil(var) ? clj_ns_name(ns) : clj_var_ns(var);
	return clj_symbol_new(clj_symbol_name(ns_name), clj_symbol_name(sym));
}

void clj_output(const char *bytes, size_t len) {
	if (out_fn) out_fn(bytes, len, out_ctx);
	else fwrite(bytes, 1, len, stdout);
}
