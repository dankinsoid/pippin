// @ai-generated(guided)
#include <pthread.h>

#include "clj/epoch.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"

static void var_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_var *v = self;
	visit(v->ns, ctx);
	visit(v->name, ctx);
	visit(atomic_load_explicit(&v->root, memory_order_acquire), ctx);
	visit(atomic_load_explicit(&v->meta, memory_order_acquire), ctx);
}

static uint32_t var_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool var_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

// A var invokes its current root, as Clojure's Var implements IFn.
static clj_value var_invoke(clj_value self, const clj_value *args, size_t n) {
	clj_value f = clj_var_deref(self);
	if (f == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_invoke(f, args, n);
	clj_release(f);
	return r;
}

static clj_value var_meta(clj_value self) { return clj_retain(clj_var_meta(self)); }

// IMeta without IObj: a var is a reference, its meta changes through alter-meta!/reset-meta!.
const clj_type clj_var_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "var",
	.core_bits = CLJ_CORE_FN | CLJ_CORE_META,
	.each_child = var_each_child,
	.hash = var_hash,
	.equals = var_equals,
	.invoke = var_invoke,
	.meta = var_meta,
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
	clj_epoch_bump();
}

clj_value clj_var_meta(clj_value var) { return atomic_load_explicit(&clj_var_of(var)->meta, memory_order_acquire); }

// @ai-generated(guided)
void clj_var_set_meta(clj_value var, clj_value m) {
	clj_share(m);
	clj_value old = atomic_exchange_explicit(&clj_var_of(var)->meta, clj_retain(m), memory_order_acq_rel);
	clj_release(old);
}

// @ai-generated(guided)
bool clj_var_cas_meta(clj_value var, clj_value expected, clj_value m) {
	clj_share(m);
	clj_retain(m);
	if (!atomic_compare_exchange_strong_explicit(&clj_var_of(var)->meta, &expected, m, memory_order_acq_rel, memory_order_acquire)) {
		clj_release(m);
		return false;
	}
	clj_release(expected);
	return true;
}

static pthread_once_t private_once = PTHREAD_ONCE_INIT;
static clj_value      kw_private;

static void intern_private(void) { kw_private = clj_keyword_from_cstr("private"); }

// @ai-generated(guided)
bool clj_var_is_private(clj_value var) {
	clj_value m = clj_var_meta(var);
	if (clj_is_nil(m)) return false;
	pthread_once(&private_once, intern_private);
	return clj_truthy(clj_map_get(m, kw_private, CLJ_NIL));
}

clj_value clj_var_deref(clj_value var) {
	clj_value root = clj_var_root(var);
	if (root == CLJ_UNBOUND) {
		clj_value ns = clj_symbol_name(clj_var_ns(var)), name = clj_symbol_name(clj_var_name(var));
		return clj_throw_msg("Unbound var: #'%s/%s", clj_string_bytes(ns), clj_string_bytes(name));
	}
	return clj_retain(root);
}
