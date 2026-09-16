// @ai-generated(guided)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/eval.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/lock.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/printer.h"
#include "clj/reader.h"
#include "clj/runtime.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"

#include "libs_clj.inc"

static const char embedded_prefix[] = "<embedded>/";

static clj_lock lock = CLJ_LOCK_INIT;
static char   **roots;
static size_t   nroots;
static bool     lenient;
static clj_value failures; // vector or nil

static pthread_once_t file_var_once = PTHREAD_ONCE_INIT;
static clj_value      file_var;

static void make_file_var(void) {
	clj_value sym = clj_symbol_from_cstr("*file*");
	file_var = clj_ns_intern(clj_ns_core(), sym);
	clj_release(sym);
	clj_var_set_dynamic(file_var, true);
	clj_var_bind_root(file_var, CLJ_NIL);
}

clj_value clj_load_file_var(void) {
	pthread_once(&file_var_once, make_file_var);
	return file_var;
}

void clj_load_path_set(const char *const *given, size_t n) {
	clj_lock_lock(&lock);
	for (size_t i = 0; i < nroots; i++) free(roots[i]);
	free(roots);
	roots = n ? calloc(n, sizeof *roots) : NULL;
	if (n && !roots) clj_fatal("out of memory");
	for (size_t i = 0; i < n; i++) {
		roots[i] = strdup(given[i]);
		if (!roots[i]) clj_fatal("out of memory");
	}
	nroots = n;
	clj_lock_unlock(&lock);
}

const char *clj_embedded_source(const char *lib_path, size_t *len) {
	for (size_t i = 0; i < sizeof embedded_libs / sizeof *embedded_libs; i++) {
		if (strcmp(embedded_libs[i].name, lib_path) == 0) {
			*len = embedded_libs[i].len;
			return (const char *)embedded_libs[i].bytes;
		}
	}
	return NULL;
}

clj_value clj_load_resource_path(const char *lib_path) {
	size_t len;
	if (clj_embedded_source(lib_path, &len)) {
		size_t cap = sizeof embedded_prefix + strlen(lib_path) + 5;
		char  *text = malloc(cap);
		if (!text) clj_fatal("out of memory");
		snprintf(text, cap, "%s%s.clj", embedded_prefix, lib_path);
		clj_value s = clj_string_from_cstr(text);
		free(text);
		return s;
	}
	static const char *const exts[] = {".cljc", ".clj"};
	clj_value                found = CLJ_NIL;
	clj_lock_lock(&lock);
	for (size_t i = 0; i < nroots && clj_is_nil(found); i++) {
		for (size_t e = 0; e < 2 && clj_is_nil(found); e++) {
			size_t cap = strlen(roots[i]) + strlen(lib_path) + 8;
			char  *text = malloc(cap);
			if (!text) clj_fatal("out of memory");
			snprintf(text, cap, "%s/%s%s", roots[i], lib_path, exts[e]);
			FILE *f = fopen(text, "rb");
			if (f) {
				fclose(f);
				found = clj_string_from_cstr(text);
			}
			free(text);
		}
	}
	clj_lock_unlock(&lock);
	return found;
}

void clj_load_set_lenient(bool on) {
	clj_lock_lock(&lock);
	lenient = on;
	clj_lock_unlock(&lock);
}

clj_value clj_load_take_failures(void) {
	clj_lock_lock(&lock);
	clj_value v = clj_is_nil(failures) ? clj_vector_empty() : failures;
	failures = CLJ_NIL;
	clj_lock_unlock(&lock);
	return v;
}

// The name a (def… name …) form defines, or nil.
static clj_value form_name(clj_value form) {
	if (!clj_is_list(form) || clj_is_empty_list(form)) return CLJ_NIL;
	clj_value head = clj_cons_of(form)->first;
	if (!clj_is_symbol(head) || strncmp(clj_string_bytes(clj_symbol_name(head)), "def", 3) != 0) return CLJ_NIL;
	clj_value rest = clj_cons_of(form)->rest;
	if (!clj_is_list(rest) || clj_is_empty_list(rest)) return CLJ_NIL;
	clj_value name = clj_cons_of(rest)->first;
	return clj_is_symbol(name) ? name : CLJ_NIL;
}

static clj_value position_data(clj_value file, uint32_t line, uint32_t col) {
	clj_value m = clj_map_empty();
	m = clj_map_assoc(m, clj_keyword_from_cstr("file"), file);
	m = clj_map_assoc(m, clj_keyword_from_cstr("line"), clj_fixnum(line));
	m = clj_map_assoc(m, clj_keyword_from_cstr("column"), clj_fixnum(col));
	return m;
}

static void record_failure(clj_value file, uint32_t line, uint32_t col, clj_value name, clj_value message) {
	clj_value m = position_data(file, line, col);
	m = clj_map_assoc(m, clj_keyword_from_cstr("name"), name);
	m = clj_map_assoc(m, clj_keyword_from_cstr("message"), message);
	clj_lock_lock(&lock);
	if (clj_is_nil(failures)) failures = clj_vector_empty();
	failures = clj_vector_conj(failures, m);
	clj_lock_unlock(&lock);
	clj_release(m);
}

// The pending exception wrapped as Clojure's CompilerException: the position in the message and the data.
static clj_value wrap_pending(clj_value file, uint32_t line, uint32_t col) {
	clj_value trace = clj_take_pending_trace();
	clj_value cause = clj_take_pending();
	clj_value cause_msg = clj_ex_message(cause);
	char      text[600];
	snprintf(text, sizeof text, "Syntax error compiling at (%s:%u:%u).%s%s", clj_is_string(file) ? clj_string_bytes(file) : "NO_SOURCE_PATH", line, col,
	         clj_is_string(cause_msg) ? " " : "", clj_is_string(cause_msg) ? clj_string_bytes(cause_msg) : "");
	clj_release(cause_msg);
	clj_value msg = clj_string_from_cstr(text);
	clj_value data = position_data(file, line, col);
	clj_value ex = clj_is_exception(cause) ? clj_ex_info_cause(msg, data, cause) : clj_ex_info(msg, data);
	clj_release(msg);
	clj_release(data);
	clj_release(cause);
	return clj_throw_traced(ex, trace);
}

clj_value clj_load_source(const char *bytes, size_t len, clj_value file) {
	clj_value bindings = clj_map_empty();
	bindings = clj_map_assoc(bindings, clj_ns_var(), clj_ns_current());
	bindings = clj_map_assoc(bindings, clj_load_file_var(), file);
	clj_value pushed = clj_var_push_bindings(bindings);
	clj_release(bindings);
	if (pushed == CLJ_THROWN) return CLJ_THROWN;
	clj_reader r;
	clj_reader_init(&r, bytes, len);
	clj_reader_use_namespaces(&r);
	clj_value result = CLJ_NIL;
	for (;;) {
		clj_value       form;
		clj_read_status st = clj_read(&r, &form);
		if (st == CLJ_READ_EOF) break;
		if (st == CLJ_READ_ERROR) {
			clj_value msg = clj_string_from_cstr(clj_reader_message(&r));
			if (lenient) {
				record_failure(file, r.error_line, r.error_col, CLJ_NIL, msg);
				clj_release(msg);
				break;
			}
			clj_value data = position_data(file, r.error_line, r.error_col);
			clj_value ex = clj_ex_info(msg, data);
			clj_release(msg);
			clj_release(data);
			result = clj_throw(ex);
			break;
		}
		clj_env   env = {CLJ_NIL, r.form_line, r.form_col};
		clj_value v = clj_eval(form, &env);
		if (v == CLJ_THROWN) {
			if (lenient) {
				clj_value ex = clj_take_pending();
				clj_value msg = clj_ex_message(ex);
				if (!clj_is_string(msg)) {
					clj_release(msg);
					msg = clj_pr_str(ex);
					if (msg == CLJ_THROWN) msg = clj_string_from_cstr("unprintable exception");
				}
				record_failure(file, r.form_line, r.form_col, form_name(form), msg);
				clj_release(msg);
				clj_release(ex);
				clj_release(form);
				continue;
			}
			clj_release(form);
			result = wrap_pending(file, r.form_line, r.form_col);
			break;
		}
		clj_release(v);
		clj_release(form);
	}
	if (clj_var_pop_bindings() == CLJ_THROWN) clj_fatal("load bindings vanished");
	return result;
}

clj_value clj_load_file(clj_value path) {
	if (!clj_is_string(path)) return clj_throw_msg("load-file expects a path string, got: %s", clj_type_name(path));
	const char *text = clj_string_bytes(path);
	if (strncmp(text, embedded_prefix, sizeof embedded_prefix - 1) == 0) {
		const char *lib = text + sizeof embedded_prefix - 1;
		size_t      lib_len = strlen(lib);
		char       *name = strdup(lib);
		if (!name) clj_fatal("out of memory");
		if (lib_len > 4 && strcmp(lib + lib_len - 4, ".clj") == 0) name[lib_len - 4] = '\0';
		size_t      len;
		const char *bytes = clj_embedded_source(name, &len);
		free(name);
		if (!bytes) return clj_throw_msg("No embedded source: %s", text);
		return clj_load_source(bytes, len, path);
	}
	FILE *f = fopen(text, "rb");
	if (!f) return clj_throw_msg("Could not open %s", text);
	char  *bytes = NULL;
	size_t len = 0, cap = 0;
	for (;;) {
		if (len == cap) {
			cap = cap ? cap * 2 : 1 << 16;
			bytes = realloc(bytes, cap);
			if (!bytes) clj_fatal("out of memory");
		}
		size_t n = fread(bytes + len, 1, cap - len, f);
		if (n == 0) break;
		len += n;
	}
	fclose(f);
	clj_value r = clj_load_source(bytes, len, path);
	free(bytes);
	return r;
}
