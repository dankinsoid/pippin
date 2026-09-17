// @ai-generated(solo)
#include <stdio.h>
#include <string.h>

#include "clj/compiled.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/printer.h"
#include "clj/reader.h"
#include "clj/set.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "node.h"

clj_value clj_c_const(const char *edn, size_t len) {
	clj_reader r;
	clj_reader_init(&r, edn, len);
	clj_reader_use_namespaces(&r);
	clj_value v;
	if (clj_read(&r, &v) != CLJ_READ_OK) {
		char msg[300];
		snprintf(msg, sizeof msg, "compiled constant does not read back: %s", clj_reader_message(&r));
		clj_fatal(msg);
	}
	if (v == CLJ_THROWN) clj_fatal("compiled constant threw while reading");
	return v;
}

clj_value clj_c_var(const char *ns, const char *name) {
	clj_value ns_sym = clj_symbol_from_cstr(ns);
	clj_value ns_obj = clj_ns_find_or_create(ns_sym);
	clj_release(ns_sym);
	clj_value name_str = clj_string_from_cstr(name);
	clj_value sym = clj_symbol_new(CLJ_NIL, name_str);
	clj_release(name_str);
	clj_value var = clj_ns_intern(ns_obj, sym);
	clj_release(sym);
	return var;
}

const clj_intrinsic *clj_c_intrinsic(const char *qualified, uint32_t arity) {
	clj_value            sym = clj_symbol_from_cstr(qualified);
	const clj_intrinsic *op = clj_intrinsic_find_named(sym, arity);
	clj_release(sym);
	if (!op) clj_fatal("compiled unit names an unknown intrinsic");
	return op;
}

const clj_fusion_var *clj_c_fusion_var(const char *qualified) {
	clj_value             sym = clj_symbol_from_cstr(qualified);
	const clj_fusion_var *fv = clj_fusion_find_named(sym);
	clj_release(sym);
	if (!fv) clj_fatal("compiled unit names an unknown fusion var");
	return fv;
}

clj_value clj_c_closure(clj_value name, clj_native_ctx_fn fn, const clj_value *env, uint32_t nenv, uint32_t arities, uint32_t min_arity, uint32_t max_arity) {
	return clj_fn_native_env(name, fn, env, nenv, arities, min_arity, max_arity);
}

clj_value clj_c_def(clj_value var, clj_value meta, bool macro, bool dynamic) {
	if (!clj_is_map(meta)) return clj_throw_msg("def metadata must be a map, got: %s", clj_type_name(meta));
	clj_var_set_meta(var, meta);
	clj_var_set_macro(var, macro);
	clj_var_set_dynamic(var, dynamic);
	return clj_retain(var);
}

static clj_value duplicate_key(clj_value result, clj_value key) {
	clj_value text = clj_pr_str_max(key, CLJ_ERROR_PRINT_MAX);
	clj_release(result);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("Duplicate key: %s", clj_string_bytes(text));
	clj_release(text);
	return r;
}

clj_value clj_c_map_literal(const clj_value *items, uint32_t n) {
	clj_value result = clj_map_empty();
	for (uint32_t i = 0; i < n; i += 2) {
		if (clj_map_contains(result, items[i])) return duplicate_key(result, items[i]);
		result = clj_map_assoc(result, items[i], items[i + 1]);
	}
	return result;
}

clj_value clj_c_set_literal(const clj_value *items, uint32_t n) {
	clj_value result = clj_set_empty();
	for (uint32_t i = 0; i < n; i++) {
		if (clj_set_contains(result, items[i])) return duplicate_key(result, items[i]);
		result = clj_set_conj(result, items[i]);
	}
	return result;
}

clj_value clj_c_arity_error(clj_value f, size_t n) {
	clj_value name = clj_fn_of(f)->name;
	clj_value text = clj_is_nil(name) ? clj_string_from_cstr("fn") : clj_pr_str(name);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("Wrong number of args (%zu) passed to: %s", n, clj_string_bytes(text));
	clj_release(text);
	return r;
}

void clj_c_stub_init(clj_node *stub, clj_value name, uint32_t line, uint32_t col) {
	memset(stub, 0, sizeof *stub);
	stub->h.rc = 1;
	stub->h.flags = CLJ_FLAG_IMMORTAL;
	stub->h.type = &clj_node_type;
	stub->kind = CLJ_NODE_FN;
	stub->line = line;
	stub->col = col;
	stub->u.fn.name = clj_retain(name);
}
