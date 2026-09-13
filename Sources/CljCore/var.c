// @ai-generated(guided)
#include "clj/var.h"
#include "clj/error.h"
#include "clj/string.h"
#include "clj/symbol.h"

static void var_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_var *v = self;
	visit(v->ns, ctx);
	visit(v->name, ctx);
	visit(atomic_load_explicit(&v->root, memory_order_acquire), ctx);
}

static uint32_t var_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool var_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

const clj_type clj_var_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "var",
	.each_child = var_each_child,
	.hash = var_hash,
	.equals = var_equals,
};

clj_value clj_var_new(clj_value ns, clj_value name) {
	CLJ_ASSERT(clj_is_symbol(ns) && clj_is_symbol(name), "var ns and name must be symbols");
	clj_var *v = clj_alloc(&clj_var_type, sizeof *v);
	v->h.flags |= CLJ_FLAG_IMMORTAL | CLJ_FLAG_SHARED;
	clj_share(ns);
	clj_share(name);
	v->ns = clj_retain(ns);
	v->name = clj_retain(name);
	atomic_store_explicit(&v->root, CLJ_UNBOUND, memory_order_relaxed);
	return clj_from_ptr(v);
}

clj_value clj_var_root(clj_value var) { return atomic_load_explicit(&clj_var_of(var)->root, memory_order_acquire); }

void clj_var_bind_root(clj_value var, clj_value val) {
	clj_share(val);
	clj_value old = atomic_exchange_explicit(&clj_var_of(var)->root, clj_retain(val), memory_order_acq_rel);
	if (old != CLJ_UNBOUND) clj_release(old);
}

clj_value clj_var_deref(clj_value var) {
	clj_value root = clj_var_root(var);
	if (root == CLJ_UNBOUND) {
		clj_value ns = clj_symbol_name(clj_var_ns(var)), name = clj_symbol_name(clj_var_name(var));
		return clj_throw_msg("Unbound var: #'%s/%s", clj_string_bytes(ns), clj_string_bytes(name));
	}
	return clj_retain(root);
}
