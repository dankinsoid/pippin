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
#include "compiled_internal.h"
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

bool clj_core_instrumented(void) {
#if defined(CLJ_COMPILED_CORE) && !defined(CLJC_INSTRUMENT)
	return false;
#else
	return true;
#endif
}

// ---- protocol call sites (compiled_internal.h)

#if CLJ_DEBUG
_Atomic int64_t clj_debug_proto_counters[2];
#endif

int64_t clj_debug_proto_arm_hits(void) {
#if CLJ_DEBUG
	return atomic_load_explicit(&clj_debug_proto_counters[0], memory_order_relaxed);
#else
	return -1;
#endif
}

int64_t clj_debug_proto_cache_hits(void) {
#if CLJ_DEBUG
	return atomic_load_explicit(&clj_debug_proto_counters[1], memory_order_relaxed);
#else
	return -1;
#endif
}

static bool method_accepts(clj_value method, size_t n) {
	const clj_fn *mf = clj_fn_of(method);
	return n >= mf->min_arity && (mf->max_arity == CLJ_ARITY_ANY || n <= mf->max_arity);
}

// The tables under their window, as the interpreter's miss; the cell takes the impl when the epoch read before the
// lookup still stands, releasing the previous one through the retire list, since a call on this thread may be in it.
// @ai-generated(solo)
clj_value clj_c_proto_miss(clj_cproto_ic *ic, clj_value method, const clj_value *args, size_t n, const clj_type *t, uint64_t epoch) {
	if (!clj_is_protocol_method(method)) return clj_c_invoke(method, args, n);
	if (!method_accepts(method, n)) return clj_arity_error(method, n);
	clj_value impl = clj_protocol_method_impl(method, args[0]);
	if (clj_is_nil(impl)) return clj_protocol_no_impl(method, args[0]);
	if (clj_is_fn(impl) && clj_epoch_load() == epoch) {
		clj_value old = ic->impl;
		ic->method = method;
		ic->type = t;
		ic->impl = clj_retain(impl);
		ic->epoch = epoch;
		if (!clj_is_nil(old) && !clj_eval_retire_root(old)) clj_release(old);
	}
	clj_value r = clj_c_call_impl(impl, args, n);
	clj_release(impl);
	return r;
}

// @ai-generated(solo)
bool clj_c_arm_fill(_Atomic uint64_t *cell, clj_value method, const clj_value *args, size_t n, clj_native_ctx_fn code, clj_compiled_fn fn, uint64_t epoch) {
	if (!code || !fn || !clj_is_protocol_method(method) || !method_accepts(method, n)) return false;
	clj_value impl = clj_protocol_method_impl(method, args[0]);
	bool      ok = clj_is_fn(impl) && clj_fn_of(impl)->kind == CLJ_FN_NATIVE_CTX && clj_fn_of(impl)->u.native_ctx.fn == code;
	if (ok && clj_epoch_load() == epoch) atomic_store_explicit(cell, epoch, memory_order_relaxed);
	clj_release(impl);
	return ok;
}

// @ai-generated(solo)
bool clj_c_satisfies_fill(_Atomic uint64_t *cell, clj_value proto, clj_value x, clj_value expected, uint64_t epoch) {
	if (!clj_is_protocol(proto)) return false;
	bool ok = clj_proto_satisfies(proto, x) == expected;
	if (ok && clj_epoch_load() == epoch) atomic_store_explicit(cell, epoch, memory_order_relaxed);
	return ok;
}

// @ai-generated(solo)
bool clj_c_extends_fill(_Atomic uint64_t *cell, clj_value proto, clj_value type, clj_value expected, uint64_t epoch) {
	if (!clj_is_protocol(proto) || !(clj_is_nil(type) || clj_is_type(type))) return false;
	bool ok = clj_proto_extends(proto, type) == expected;
	if (ok && clj_epoch_load() == epoch) atomic_store_explicit(cell, epoch, memory_order_relaxed);
	return ok;
}
