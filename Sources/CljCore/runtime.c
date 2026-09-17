// @ai-generated(guided)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/analyzer.h"
#include "clj/coll.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fusion.h"
#include "clj/intrinsics.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/printer.h"
#include "clj/proto.h"
#include "clj/queue.h"
#include "clj/reader.h"
#include "clj/record.h"
#include "clj/runtime.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "clj/compiled.h"
#include "load_internal.h"

static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static clj_output_fn out_fn;
static void         *out_ctx;

// with-out-str: a per-thread stack of byte buffers the output hook writes into while one is open.
typedef struct capture {
	char           *data;
	size_t          len, cap;
	struct capture *prev;
} capture;

static _Thread_local capture *captures;

void clj_output_push_capture(void) {
	capture *c = calloc(1, sizeof *c);
	if (!c) clj_fatal("out of memory");
	c->prev = captures;
	captures = c;
}

clj_value clj_output_pop_capture(void) {
	capture *c = captures;
	CLJ_ASSERT(c, "output capture pop without push");
	captures = c->prev;
	clj_value s = clj_string_new(c->data, c->len);
	free(c->data);
	free(c);
	return s;
}

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

#ifndef CLJ_COMPILED_CORE
// Evaluates core.clj in clojure.core; the caller has made it the current namespace for the resolver.
// The load hook sees every form under this path, which is what the compiler names in its #line directives.
static void load_core(void) {
	clj_reader r;
	clj_reader_init(&r, (const char *)core_clj, core_clj_len);
	clj_reader_use_namespaces(&r);
	bool      hooked = clj_load_hook_get() != NULL;
	clj_value file = hooked ? clj_string_from_cstr(CLJ_CORE_CLJ_PATH) : CLJ_NIL;
	for (;;) {
		clj_value form;
		clj_read_status st = clj_read(&r, &form);
		if (st == CLJ_READ_EOF) break;
		if (st == CLJ_READ_ERROR) boot_failed("reader error", r.error_line, r.error_col, clj_reader_message(&r));
		clj_env env = {clj_ns_core(), r.form_line, r.form_col};
		clj_load_arm_tls = (clj_load_arm){hooked, {file, r.form_line, r.form_col, clj_load_form_name(form), clj_load_next_serial()}};
		clj_value v = clj_eval(form, &env);
		clj_load_arm_tls.armed = false;
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
	clj_release(file);
}
#endif

// Every root bound by boot outlives the process, so a read of it needs no retain (eval_borrowed): what a
// later rebind of the var "releases" is a no-op, a bounded leak per redefinition. Type descriptors are left
// alone: clj_is_user_type reads the flag as "builtin".
// @ai-generated(guided)
static bool immortalize_root(clj_value sym, clj_value var, void *ctx) {
	(void)sym;
	(void)ctx;
	clj_value root = clj_var_root(var);
	if (clj_is_ptr(root) && !clj_is_type(root)) clj_header_of(root)->flags |= CLJ_FLAG_IMMORTAL;
	return true;
}

static void init(void) {
	clj_value core = clj_ns_core();
	// Interned up front so printing an error, an analysis position or a trace allocates nothing lasting later.
	for (const char *const *k = (const char *const[]){"message", "data", "cause", "line", "column", "tag", "ns", "name", "doc", "arglists",
	                                                    "macro", "dynamic", "private", "fn", "fns", "calls", NULL}; *k; k++) clj_keyword_from_cstr(*k);
	clj_ns_var();
	clj_load_file_var();
	clj_builtins_install();
	clj_proto_install();
	clj_queue_install();
	clj_record_install();
	clj_intrinsics_install();
	clj_ns_set_current(core);
#ifdef CLJ_COMPILED_CORE
	if (clj_compiled_core_init() == CLJ_THROWN) {
		clj_value trace = clj_take_pending_trace();
		clj_value ex = clj_take_pending();
		clj_value text = clj_pr_str(ex);
		print_trace(trace);
		boot_failed("exception", 0, 0, text == CLJ_THROWN ? "unprintable" : clj_string_bytes(text));
	}
	clj_compiled_libs_register();
	// The interpreter's lazily interned names are made during an interpreted boot; tests baseline after clj_init.
	clj_env   warm = {core, 0, 0};
	clj_value warmed = clj_eval(CLJ_NIL, &warm);
	if (warmed == CLJ_THROWN) clj_fatal("warm-up eval failed");
	clj_release(warmed);
#else
	load_core();
#endif
	clj_fusion_install();
	clj_map_each(clj_ns_of(core)->mappings, immortalize_root, NULL);
	clj_ns_set_current(clj_ns_user());
	if (clj_host_boot) clj_host_boot();
	clj_compiled_eval_boot();
}

__attribute__((weak)) void clj_compiled_eval_boot(void) {}

void clj_init(void) { pthread_once(&init_once, init); }

void clj_set_output(clj_output_fn fn, void *ctx) {
	out_fn = fn;
	out_ctx = ctx;
}

clj_value clj_syntax_quote_resolve(clj_value sym, void *ctx) {
	(void)ctx;
	if (clj_is_special_symbol(sym)) return clj_retain(sym);
	clj_value ns = clj_ns_current();
	if (!clj_is_nil(clj_symbol_ns(sym))) {
		clj_value alias = clj_symbol_new(CLJ_NIL, clj_symbol_ns(sym));
		clj_value target = clj_ns_resolve_ns(ns, alias);
		clj_release(alias);
		if (clj_is_nil(target) || clj_equals(clj_symbol_name(clj_ns_name(target)), clj_symbol_ns(sym))) return clj_retain(sym);
		return clj_symbol_new(clj_symbol_name(clj_ns_name(target)), clj_symbol_name(sym));
	}
	clj_value var = clj_ns_resolve(ns, sym);
	clj_value ns_name = clj_is_nil(var) ? clj_ns_name(ns) : clj_var_ns(var);
	return clj_symbol_new(clj_symbol_name(ns_name), clj_symbol_name(sym));
}

clj_value clj_reader_resolve_ns(clj_value alias, void *ctx) {
	(void)ctx;
	clj_value ns = clj_ns_current();
	if (clj_is_nil(alias)) return clj_retain(clj_symbol_name(clj_ns_name(ns)));
	clj_value sym = clj_symbol_new(CLJ_NIL, alias);
	clj_value target = clj_ns_resolve_ns(ns, sym);
	clj_release(sym);
	return clj_is_nil(target) ? CLJ_NIL : clj_retain(clj_symbol_name(clj_ns_name(target)));
}

// Owned value of a clojure.core var, nil while core.clj has not defined it yet.
static clj_value core_var_value(const char *name) {
	clj_value sym = clj_symbol_from_cstr(name);
	clj_value var = clj_ns_resolve(clj_ns_core(), sym);
	clj_release(sym);
	return clj_is_nil(var) || !clj_var_is_bound(var) ? CLJ_NIL : clj_var_deref(var);
}

// #ns.Name{...} and #ns.Name[...]: the record type behind the var Name of namespace ns, as LispReader's CtorReader.
static clj_value read_record_literal(clj_value tag, clj_value form) {
	const char *text = clj_string_bytes(clj_symbol_name(tag));
	const char *dot = strrchr(text, '.');
	clj_value   ns_name = clj_string_new(text, (size_t)(dot - text));
	clj_value   ns_sym = clj_symbol_new(CLJ_NIL, ns_name);
	clj_value   ns = clj_ns_find(ns_sym);
	clj_value   var = CLJ_NIL;
	if (!clj_is_nil(ns)) {
		clj_value name = clj_symbol_from_cstr(dot + 1);
		var = clj_ns_resolve(ns, name);
		clj_release(name);
	}
	clj_release(ns_sym);
	clj_release(ns_name);
	if (clj_is_nil(var) || !clj_var_is_bound(var) || !clj_is_record_type(clj_var_root(var))) return clj_throw_msg("Unable to resolve classname: %s", text);
	clj_value type = clj_var_root(var);
	if (clj_is_map(form)) return clj_record_from_map(type, form);
	if (clj_is_vector(form)) {
		size_t     n;
		clj_value  keep;
		clj_value *items = clj_seq_items(form, &n, &keep);
		if (!items) return CLJ_THROWN;
		clj_value r = clj_record_new(type, items, n);
		free(items);
		clj_release(keep);
		return r;
	}
	return clj_throw_msg("Unreadable constructor form starting with \"#%s\"", text);
}

// LispReader's order: a dotted tag is a constructor, then *data-readers*, default-data-readers, *default-data-reader-fn*.
clj_value clj_reader_read_tag(clj_value tag, clj_value form, void *ctx) {
	(void)ctx;
	if (clj_is_nil(clj_symbol_ns(tag)) && strchr(clj_string_bytes(clj_symbol_name(tag)), '.')) return read_record_literal(tag, form);
	clj_value readers = core_var_value("*data-readers*");
	clj_value f = clj_is_nil(readers) ? CLJ_NIL : clj_get(readers, tag, CLJ_NIL);
	clj_release(readers);
	if (f == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_is_nil(f)) {
		clj_value r = clj_invoke(f, &form, 1);
		clj_release(f);
		return r;
	}
	clj_value r = clj_default_data_reader(tag, form);
	if (r != CLJ_UNBOUND) return r;
	clj_value fallback = core_var_value("*default-data-reader-fn*");
	if (clj_is_nil(fallback)) return CLJ_UNBOUND;
	clj_value args[2] = {tag, form};
	r = clj_invoke(fallback, args, 2);
	clj_release(fallback);
	return r;
}

void clj_reader_use_namespaces(clj_reader *r) {
	r->resolve = clj_syntax_quote_resolve;
	r->resolve_ns = clj_reader_resolve_ns;
	r->read_tag = clj_reader_read_tag;
}

void clj_output(const char *bytes, size_t len) {
	capture *c = captures;
	if (c) {
		if (c->len + len > c->cap) {
			size_t cap = c->cap ? c->cap : 256;
			while (cap < c->len + len) cap *= 2;
			c->data = realloc(c->data, cap);
			if (!c->data) clj_fatal("out of memory");
			c->cap = cap;
		}
		memcpy(c->data + c->len, bytes, len);
		c->len += len;
		return;
	}
	if (out_fn) out_fn(bytes, len, out_ctx);
	else fwrite(bytes, 1, len, stdout);
}
