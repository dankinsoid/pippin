// @ai-generated(guided)
#include <pthread.h>
#include <stdlib.h>

#include "clj/box.h"
#include "clj/core.h"
#include "clj/epoch.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "specialize_internal.h"

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
	if (old != CLJ_UNBOUND && !clj_eval_retire_root(old)) clj_release(old);
	atomic_fetch_add_explicit(&clj_var_of(var)->epoch, 1, memory_order_release);
	clj_epoch_bump();
	clj_exec_root_rebound(var);
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

void clj_var_intern_keywords(void) { pthread_once(&private_once, intern_private); }

// @ai-generated(guided)
bool clj_var_is_private(clj_value var) {
	clj_value m = clj_var_meta(var);
	if (clj_is_nil(m)) return false;
	pthread_once(&private_once, intern_private);
	// `def` only ever stores a hash map, and reading another representation as one would be a type confusion.
	return clj_is_map(m) && clj_truthy(clj_map_get(m, kw_private, CLJ_NIL));
}

// A frame holds the bindings visible while it is on top (its own over the previous frame's) and its own alone,
// so a pop knows which vars lose a thread binding.
typedef struct frame {
	clj_value     bindings; // map var → box
	clj_value     pushed;   // map var → box of this push only
	struct frame *prev;
} frame;

static _Thread_local frame *frames;

clj_value clj_var_thread_binding(clj_value var) {
	frame *f = frames;
	if (!f) return CLJ_NIL;
	return clj_map_get(f->bindings, var, CLJ_NIL);
}

typedef struct {
	clj_value merged, pushed;
	clj_value error;
} push_ctx;

static bool push_entry(clj_value key, clj_value val, void *ctx) {
	push_ctx *c = ctx;
	if (!clj_is_var(key)) {
		c->error = clj_throw_msg("binding requires vars, got: %s", clj_type_name(key));
		return false;
	}
	if (!clj_var_is_dynamic(key)) {
		c->error = clj_throw_msg("Can't dynamically bind non-dynamic var: %s/%s", clj_string_bytes(clj_symbol_name(clj_var_ns(key))),
		                         clj_string_bytes(clj_symbol_name(clj_var_name(key))));
		return false;
	}
	clj_value box = clj_volatile_new(val);
	c->merged = clj_map_assoc(c->merged, key, box);
	c->pushed = clj_map_assoc(c->pushed, key, box);
	clj_release(box);
	return true;
}

static bool count_binding(clj_value key, clj_value val, void *ctx) {
	(void)val;
	atomic_fetch_add_explicit(&clj_var_of(key)->thread_bound, (uint32_t)(intptr_t)ctx, memory_order_relaxed);
	return true;
}

// @ai-generated(guided)
clj_value clj_var_push_bindings(clj_value bindings) {
	if (!clj_is_map(bindings)) return clj_throw_msg("push-thread-bindings expects a map, got: %s", clj_type_name(bindings));
	push_ctx c = {clj_retain(frames ? frames->bindings : clj_map_empty()), clj_map_empty(), CLJ_NIL};
	clj_map_each(bindings, push_entry, &c);
	if (c.error == CLJ_THROWN) {
		clj_release(c.merged);
		clj_release(c.pushed);
		return CLJ_THROWN;
	}
	frame *f = malloc(sizeof *f);
	if (!f) clj_fatal("out of memory");
	f->bindings = c.merged;
	f->pushed = c.pushed;
	f->prev = frames;
	frames = f;
	clj_map_each(f->pushed, count_binding, (void *)(intptr_t)1);
	return CLJ_NIL;
}

clj_value clj_var_pop_bindings(void) {
	frame *f = frames;
	if (!f) return clj_throw_msg("Pop without matching push");
	frames = f->prev;
	clj_map_each(f->pushed, count_binding, (void *)(intptr_t)-1);
	clj_release(f->bindings);
	clj_release(f->pushed);
	free(f);
	return CLJ_NIL;
}

static bool collect_value(clj_value key, clj_value box, void *ctx) {
	clj_value *m = ctx;
	*m = clj_map_assoc(*m, key, clj_volatile_value(box));
	return true;
}

clj_value clj_var_get_thread_bindings(void) {
	clj_value m = clj_map_empty();
	if (frames) clj_map_each(frames->bindings, collect_value, &m);
	return m;
}

clj_value clj_var_set(clj_value var, clj_value val) {
	clj_value box = clj_var_thread_binding(var);
	if (clj_is_nil(box)) {
		return clj_throw_msg("Can't change/establish root binding of: %s/%s with set", clj_string_bytes(clj_symbol_name(clj_var_ns(var))),
		                     clj_string_bytes(clj_symbol_name(clj_var_name(var))));
	}
	return clj_volatile_reset(box, val);
}

clj_value clj_var_deref(clj_value var) {
	clj_var *v = clj_var_of(var);
	if (v->dynamic && atomic_load_explicit(&v->thread_bound, memory_order_relaxed)) {
		clj_value box = clj_var_thread_binding(var);
		if (!clj_is_nil(box)) return clj_volatile_deref(box);
	}
	clj_value root = clj_var_root(var);
	if (root == CLJ_UNBOUND) {
		clj_value ns = clj_symbol_name(clj_var_ns(var)), name = clj_symbol_name(clj_var_name(var));
		return clj_throw_msg("Unbound var: #'%s/%s", clj_string_bytes(ns), clj_string_bytes(name));
	}
	return clj_retain(root);
}
