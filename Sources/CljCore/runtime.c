// @ai-generated(guided)
#include <pthread.h>
#include <stdio.h>

#include "clj/analyzer.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/intrinsics.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/printer.h"
#include "clj/proto.h"
#include "clj/reader.h"
#include "clj/runtime.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"

static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static clj_output_fn out_fn;
static void         *out_ctx;

#include "core_clj.inc"

const char *clj_core_source(size_t *len) {
	*len = core_clj_len;
	return (const char *)core_clj;
}

static void boot_failed(const char *what, uint32_t line, uint32_t col, const char *detail) {
	char msg[640];
	snprintf(msg, sizeof msg, "core.clj failed to load: %s at %u:%u: %s", what, line, col, detail);
	clj_fatal(msg);
}

static void print_trace(clj_value trace) {
	if (!clj_is_vector(trace)) return;
	clj_value kw_fn = clj_keyword_from_cstr("fn"), kw_line = clj_keyword_from_cstr("line"), kw_column = clj_keyword_from_cstr("column");
	for (uint32_t i = 0; i < clj_vector_count(trace); i++) {
		clj_value frame = clj_vector_nth(trace, i), fn = clj_map_get(frame, kw_fn, CLJ_NIL);
		clj_value name = clj_is_nil(fn) ? clj_string_from_cstr("fn") : clj_pr_str(fn);
		fprintf(stderr, "  at %s (%ld:%ld)\n", clj_string_bytes(name), (long)clj_fixnum_val(clj_map_get(frame, kw_line, clj_fixnum(0))),
		        (long)clj_fixnum_val(clj_map_get(frame, kw_column, clj_fixnum(0))));
		clj_release(name);
	}
}

// Evaluates core.clj in clojure.core; the caller has made it the current namespace for the resolver.
static void load_core(void) {
	clj_reader r;
	clj_reader_init(&r, (const char *)core_clj, core_clj_len);
	r.resolve = clj_syntax_quote_resolve;
	for (;;) {
		clj_value form;
		clj_read_status st = clj_read(&r, &form);
		if (st == CLJ_READ_EOF) return;
		if (st == CLJ_READ_ERROR) boot_failed("reader error", r.error_line, r.error_col, clj_reader_message(&r));
		clj_env   env = {clj_ns_core(), r.form_line, r.form_col};
		clj_value v = clj_eval(form, &env);
		clj_release(form);
		if (v == CLJ_THROWN) {
			clj_value trace = clj_take_pending_trace();
			clj_value ex = clj_take_pending();
			clj_value text = clj_pr_str(ex);
			print_trace(trace);
			boot_failed("exception", r.form_line, r.form_col, clj_string_bytes(text));
		}
		clj_release(v);
	}
}

static void init(void) {
	clj_value core = clj_ns_core();
	// Interned up front so printing an error, an analysis position or a trace allocates nothing lasting later.
	for (const char *const *k = (const char *const[]){"message", "data", "cause", "line", "column", "tag", "ns", "name", "doc", "arglists",
	                                                    "macro", "dynamic", "private", "fn", "fns", "calls", NULL}; *k; k++) clj_keyword_from_cstr(*k);
	clj_builtins_install();
	clj_proto_install();
	clj_intrinsics_install();
	clj_ns_set_current(core);
	load_core();
	clj_ns_set_current(clj_ns_user());
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
