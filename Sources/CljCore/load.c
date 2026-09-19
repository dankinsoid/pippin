// @ai-generated(guided)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clj/core.h"
#include "clj/eval.h"
#include "clj/guard.h"
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

#include "coro_internal.h"
#include "libs_clj.inc"
#include "load_internal.h"

static const char embedded_prefix[] = "<embedded>/";

static clj_lock lock = CLJ_LOCK_INIT;
static char   **roots;
static size_t   nroots;
static bool     lenient;
static clj_value failures; // vector or nil

static pthread_once_t file_var_once = PTHREAD_ONCE_INIT;
static clj_value      file_var;


static clj_load_hook    hook;
static bool             hook_set;
static _Atomic uint64_t serial;

void clj_load_set_hook(const clj_load_hook *h) {
	hook_set = h != NULL;
	if (h) hook = *h;
}

const clj_load_hook *clj_load_hook_get(void) { return hook_set ? &hook : NULL; }

uint64_t clj_load_next_serial(void) { return atomic_fetch_add_explicit(&serial, 1, memory_order_relaxed) + 1; }

// ---- compiled units, keyed by the path clj_load_file is given
typedef struct {
	char             *path;
	clj_compiled_init init;
} unit_entry;

static unit_entry *units;
static size_t      nunits, units_cap;

// @ai-generated(solo)
void clj_compiled_register(const char *path, clj_compiled_init init) {
	clj_lock_lock(&lock);
	for (size_t i = 0; i < nunits; i++) {
		if (strcmp(units[i].path, path) == 0) {
			units[i].init = init;
			clj_lock_unlock(&lock);
			return;
		}
	}
	if (nunits == units_cap) {
		units_cap = units_cap ? units_cap * 2 : 8;
		units = realloc(units, units_cap * sizeof *units);
		if (!units) clj_fatal("out of memory");
	}
	units[nunits].path = strdup(path);
	if (!units[nunits].path) clj_fatal("out of memory");
	units[nunits].init = init;
	nunits++;
	clj_lock_unlock(&lock);
}

typedef struct {
	char           *name;
	clj_compiled_fn fn;
} symbol_entry;

static symbol_entry *symbols;
static size_t        nsymbols, symbols_cap;

void clj_compiled_register_symbol(const char *name, clj_compiled_fn fn) {
	clj_lock_lock(&lock);
	for (size_t i = 0; i < nsymbols; i++) {
		if (strcmp(symbols[i].name, name) == 0) {
			symbols[i].fn = fn;
			clj_lock_unlock(&lock);
			return;
		}
	}
	if (nsymbols == symbols_cap) {
		symbols_cap = symbols_cap ? symbols_cap * 2 : 256;
		symbols = realloc(symbols, symbols_cap * sizeof *symbols);
		if (!symbols) clj_fatal("out of memory");
	}
	symbols[nsymbols].name = strdup(name);
	if (!symbols[nsymbols].name) clj_fatal("out of memory");
	symbols[nsymbols].fn = fn;
	nsymbols++;
	clj_lock_unlock(&lock);
}

clj_compiled_fn clj_compiled_symbol(const char *name) {
	clj_compiled_fn found = NULL;
	clj_lock_lock(&lock);
	for (size_t i = 0; i < nsymbols && !found; i++) {
		if (strcmp(symbols[i].name, name) == 0) found = symbols[i].fn;
	}
	clj_lock_unlock(&lock);
	return found;
}

typedef struct {
	char             *name;
	clj_native_ctx_fn fn;
} impl_entry;

static impl_entry *impls;
static size_t      nimpls, impls_cap;

void clj_compiled_register_impl(const char *name, clj_native_ctx_fn fn) {
	clj_lock_lock(&lock);
	for (size_t i = 0; i < nimpls; i++) {
		if (strcmp(impls[i].name, name) == 0) {
			impls[i].fn = fn;
			clj_lock_unlock(&lock);
			return;
		}
	}
	if (nimpls == impls_cap) {
		impls_cap = impls_cap ? impls_cap * 2 : 64;
		impls = realloc(impls, impls_cap * sizeof *impls);
		if (!impls) clj_fatal("out of memory");
	}
	impls[nimpls].name = strdup(name);
	if (!impls[nimpls].name) clj_fatal("out of memory");
	impls[nimpls].fn = fn;
	nimpls++;
	clj_lock_unlock(&lock);
}

clj_native_ctx_fn clj_compiled_impl(const char *name) {
	clj_native_ctx_fn found = NULL;
	clj_lock_lock(&lock);
	for (size_t i = 0; i < nimpls && !found; i++) {
		if (strcmp(impls[i].name, name) == 0) found = impls[i].fn;
	}
	clj_lock_unlock(&lock);
	return found;
}

const char *clj_compiled_impl_name(clj_native_ctx_fn fn) {
	const char *found = NULL;
	clj_lock_lock(&lock);
	for (size_t i = nimpls; i-- > 0 && !found;) {
		if (impls[i].fn == fn) found = impls[i].name;
	}
	clj_lock_unlock(&lock);
	return found;
}

clj_compiled_init clj_compiled_find(const char *path) {
	clj_compiled_init found = NULL;
	clj_lock_lock(&lock);
	for (size_t i = 0; i < nunits && !found; i++) {
		if (strcmp(units[i].path, path) == 0) found = units[i].init;
	}
	clj_lock_unlock(&lock);
	return found;
}

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
	// The roots are copied out: the file system is probed with no runtime lock held (design §4, "Два лока").
	clj_lock_lock(&lock);
	size_t n = nroots;
	char **copy = n ? malloc(n * sizeof *copy) : NULL;
	if (n && !copy) clj_fatal("out of memory");
	for (size_t i = 0; i < n; i++) {
		copy[i] = strdup(roots[i]);
		if (!copy[i]) clj_fatal("out of memory");
	}
	clj_lock_unlock(&lock);
	for (size_t i = 0; i < n; i++) {
		for (size_t e = 0; e < 2 && clj_is_nil(found); e++) {
			size_t cap = strlen(copy[i]) + strlen(lib_path) + 8;
			char  *text = malloc(cap);
			if (!text) clj_fatal("out of memory");
			snprintf(text, cap, "%s/%s%s", copy[i], lib_path, exts[e]);
			if (access(text, R_OK) == 0) found = clj_string_from_cstr(text);
			free(text);
		}
		free(copy[i]);
	}
	free(copy);
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

clj_value clj_load_form_name(clj_value form) {
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

// @ai-generated(solo)
bool clj_load_form_failed(clj_value file, uint32_t line, uint32_t col, clj_value name) {
	if (!lenient) {
		wrap_pending(file, line, col);
		return false;
	}
	clj_value ex = clj_take_pending();
	clj_value msg = clj_ex_message(ex);
	if (!clj_is_string(msg)) {
		clj_release(msg);
		msg = clj_pr_str(ex);
		if (msg == CLJ_THROWN) msg = clj_string_from_cstr("unprintable exception");
	}
	record_failure(file, line, col, name, msg);
	clj_release(msg);
	clj_release(ex);
	return true;
}

// A registered unit runs under the bindings a source load has: its init sees the same *ns* and *file*.
static clj_value run_unit(clj_compiled_init init, clj_value file) {
	clj_value bindings = clj_map_empty();
	bindings = clj_map_assoc(bindings, clj_ns_var(), clj_ns_current());
	bindings = clj_map_assoc(bindings, clj_load_file_var(), file);
	clj_value pushed = clj_var_push_bindings(bindings);
	clj_release(bindings);
	if (pushed == CLJ_THROWN) return CLJ_THROWN;
	clj_release(pushed);
	// The unit's forms run outside clj_eval, so the recovery point of guard.h is here.
	clj_recovery rec;
	clj_recovery_push(&rec);
	clj_value r;
	if (sigsetjmp(rec.buf, 0)) r = clj_recovery_throw(&rec);
	else r = init();
	clj_recovery_pop(&rec);
	if (clj_var_pop_bindings() == CLJ_THROWN) clj_fatal("load bindings vanished");
	if (r == CLJ_THROWN) return r;
	clj_release(r);
	return CLJ_NIL;
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
				if (hook_set && hook.failed) {
					clj_load_form f = {file, r.error_line, r.error_col, CLJ_NIL, clj_load_next_serial()};
					hook.failed(&f, msg, hook.ctx);
				}
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
		clj_env      env = {CLJ_NIL, r.form_line, r.form_col};
		clj_load_arm arm = {hook_set, {file, r.form_line, r.form_col, clj_load_form_name(form), clj_load_next_serial()}};
		clj_load_arm_tls = arm;
		clj_value v = clj_eval(form, &env);
		clj_load_arm_tls.armed = false;
		if (v == CLJ_THROWN) {
			if (lenient) {
				clj_value ex = clj_take_pending();
				clj_value msg = clj_ex_message(ex);
				if (!clj_is_string(msg)) {
					clj_release(msg);
					msg = clj_pr_str(ex);
					if (msg == CLJ_THROWN) msg = clj_string_from_cstr("unprintable exception");
				}
				record_failure(file, r.form_line, r.form_col, clj_load_form_name(form), msg);
				if (hook_set && hook.failed && clj_load_analysis_failed) hook.failed(&arm.form, msg, hook.ctx);
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

typedef struct {
	const char *path;
	char       *bytes;
	size_t      len;
	bool        ok;
} read_job;

// Runs on the blocking pool while the loading coroutine parks (design §4: no fread on a carrier).
static void read_file(void *ctx) {
	read_job *j = ctx;
	FILE     *f = fopen(j->path, "rb");
	if (!f) return;
	size_t cap = 0;
	for (;;) {
		if (j->len == cap) {
			cap = cap ? cap * 2 : 1 << 16;
			j->bytes = realloc(j->bytes, cap);
			if (!j->bytes) clj_fatal("out of memory");
		}
		size_t n = fread(j->bytes + j->len, 1, cap - j->len, f);
		if (n == 0) break;
		j->len += n;
	}
	fclose(f);
	j->ok = true;
}

clj_value clj_load_file(clj_value path) {
	if (!clj_is_string(path)) return clj_throw_msg("load-file expects a path string, got: %s", clj_type_name(path));
	const char *text = clj_string_bytes(path);
	clj_compiled_init unit = nunits ? clj_compiled_find(text) : NULL;
	if (unit) return run_unit(unit, path);
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
	read_job job = {text, NULL, 0, false};
	clj_blocking(read_file, &job);
	if (!job.ok) return clj_throw_msg("Could not open %s", text);
	clj_value r = clj_load_source(job.bytes, job.len, path);
	free(job.bytes);
	return r;
}
