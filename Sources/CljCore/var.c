// @ai-generated(guided)
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "clj/box.h"
#include "clj/core.h"
#include "clj/analyzer.h"
#include "clj/epoch.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/keyword.h"
#include "clj/lock.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/runtime.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "cc_internal.h"
#include "coro_internal.h"
#include "force_internal.h"
#include "specialize_internal.h"

static void var_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_var *v = self;
	visit(v->ns.v, ctx);
	visit(v->name.v, ctx);
	visit(clj_slot_load(&v->root, memory_order_acquire), ctx);
	visit(clj_slot_load(&v->meta, memory_order_acquire), ctx);
	visit(clj_slot_load(&v->lazy, memory_order_acquire), ctx);
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
	.mutable_children = true,
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
	clj_slot_store(&v->h, &v->ns, clj_retain(ns));
	clj_slot_store(&v->h, &v->name, clj_retain(name));
	clj_slot_store_atomic(&v->h, &v->root, CLJ_UNBOUND, memory_order_relaxed);
	clj_slot_store_atomic(&v->h, &v->lazy, CLJ_NIL, memory_order_relaxed);
	return clj_from_ptr(v);
}

clj_value clj_var_root(clj_value var) { return clj_slot_load(&clj_var_of(var)->root, memory_order_acquire); }

static void redefinition_barrier(clj_value except);

static void root_moved(clj_value var) {
	atomic_fetch_add_explicit(&clj_var_of(var)->epoch, 1, memory_order_release);
	clj_epoch_bump();
	clj_exec_root_rebound(var);
}

// lazy is the thunk the var keeps past its force, nil for a plain root. The old thunk goes first: it holds the old
// value too, and the deep release of the old root must see that value's last outside reference gone.
static void bind_unbarriered(clj_value var, clj_value val, clj_value lazy) {
	clj_var  *v = clj_var_of(var);
	clj_value old = clj_slot_exchange(&v->h, &v->root, clj_retain(val), memory_order_acq_rel);
	clj_release(clj_slot_exchange(&v->h, &v->lazy, clj_retain(lazy), memory_order_acq_rel));
	if (old != CLJ_UNBOUND && !clj_eval_retire_root(old)) clj_rc_release_root(old);
	root_moved(var);
}

void clj_var_bind_root(clj_value var, clj_value val) {
	clj_var *v = clj_var_of(var);
	if (!v->dynamic && clj_slot_load(&v->root, memory_order_acquire) != CLJ_UNBOUND) redefinition_barrier(var);
	bind_unbarriered(var, val, CLJ_NIL);
}

clj_value clj_var_meta(clj_value var) { return clj_slot_load(&clj_var_of(var)->meta, memory_order_acquire); }

// @ai-generated(guided)
void clj_var_set_meta(clj_value var, clj_value m) {
	clj_value old = clj_slot_exchange(clj_header_of(var), &clj_var_of(var)->meta, clj_retain(m), memory_order_acq_rel);
	clj_release(old);
}

// @ai-generated(guided)
bool clj_var_cas_meta(clj_value var, clj_value expected, clj_value m) {
	clj_retain(m);
	if (!clj_slot_cas(clj_header_of(var), &clj_var_of(var)->meta, &expected, m, memory_order_acq_rel, memory_order_acquire)) {
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
// so a pop knows which vars lose a thread binding. Shared with spawned coroutines: it lives while any chain reaches it.
typedef struct frame {
	clj_value        bindings; // map var → box
	clj_value        pushed;   // map var → box of this push only
	struct frame    *prev;
	_Atomic uint32_t rc;
	uint64_t         owner;    // the id of the execution that pushed it: only it may set! (the JVM's non-binding-thread rule)
} frame;

#define frames (clj_coro_current()->bindings)

static frame *top_frame(void) { return frames; }

clj_value clj_var_thread_binding(clj_value var) {
	clj_coro *c = clj_coro_tls;
	frame    *f = c ? c->bindings : NULL;
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

// A var's thread_bound count drops when the frame dies, not when it is popped: a child may still see the binding.
static void frame_release(frame *f) {
	while (f && atomic_fetch_sub_explicit(&f->rc, 1, memory_order_acq_rel) == 1) {
		frame *prev = f->prev;
		clj_map_each(f->pushed, count_binding, (void *)(intptr_t)-1);
		clj_release(f->bindings);
		clj_release(f->pushed);
		free(f);
		f = prev;
	}
}

// @ai-generated(guided)
clj_value clj_var_push_bindings(clj_value bindings) {
	if (!clj_is_map(bindings)) return clj_throw_msg("push-thread-bindings expects a map, got: %s", clj_type_name(bindings));
	frame   *top = top_frame();
	push_ctx c = {clj_retain(top ? top->bindings : clj_map_empty()), clj_map_empty(), CLJ_NIL};
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
	f->prev = top;
	atomic_init(&f->rc, 1);
	f->owner = clj_coro_current()->id;
	frames = f;
	clj_map_each(f->pushed, count_binding, (void *)(intptr_t)1);
	return CLJ_NIL;
}

clj_value clj_var_pop_bindings(void) {
	frame *f = top_frame();
	if (!f) return clj_throw_msg("Pop without matching push");
	frames = f->prev;
	if (f->prev) atomic_fetch_add_explicit(&f->prev->rc, 1, memory_order_relaxed);
	frame_release(f);
	return CLJ_NIL;
}

void *clj_var_bindings_mark(void) { return top_frame(); }

// Pops what a landing at a recovery point left pushed (guard.c): the frames above the mark were abandoned.
void clj_var_bindings_unwind(void *mark) {
	while (top_frame() && top_frame() != mark) clj_release(clj_var_pop_bindings());
}

// Conveyance: the child's chain starts at the spawner's top frame; the maps cross threads, so they are shared.
void *clj_var_bindings_share(void) {
	frame *f = top_frame();
	if (!f) return NULL;
	atomic_fetch_add_explicit(&f->rc, 1, memory_order_relaxed);
	for (frame *g = f; g; g = g->prev) {
		if (clj_is_ptr(g->bindings) && (clj_header_of(g->bindings)->flags & CLJ_FLAG_SHARED)) break;
		clj_share(g->bindings);
		clj_share(g->pushed);
	}
	return f;
}

void clj_var_bindings_release(void *chain) { frame_release(chain); }

static bool collect_value(clj_value key, clj_value box, void *ctx) {
	clj_value *m = ctx;
	*m = clj_map_assoc(*m, key, clj_volatile_value(box));
	return true;
}

clj_value clj_var_get_thread_bindings(void) {
	clj_value m = clj_map_empty();
	frame    *f = top_frame();
	if (f) clj_map_each(f->bindings, collect_value, &m);
	return m;
}

// The frame whose own push holds the var's binding, or NULL.
static frame *binding_frame(clj_value var) {
	for (frame *f = top_frame(); f; f = f->prev) {
		if (clj_map_contains(f->pushed, var)) return f;
	}
	return NULL;
}

clj_value clj_var_set(clj_value var, clj_value val) {
	clj_value box = clj_var_thread_binding(var);
	if (clj_is_nil(box)) {
		return clj_throw_msg("Can't change/establish root binding of: %s/%s with set", clj_string_bytes(clj_symbol_name(clj_var_ns(var))),
		                     clj_string_bytes(clj_symbol_name(clj_var_name(var))));
	}
	frame *f = binding_frame(var);
	if (f && f->owner != clj_coro_current()->id) {
		return clj_throw_msg("Can't set!: %s/%s from non-binding thread", clj_string_bytes(clj_symbol_name(clj_var_ns(var))),
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
	return clj_var_root_value(var);
}

// ---- lazy def (design §4 «Var и ленивые def»)

static void lazy_def_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_lazy_def *t = self;
	visit(t->env.v, ctx);
	visit(t->value.v, ctx);
	visit(t->error.v, ctx);
	visit(t->trace.v, ctx);
	visit(t->prev.v, ctx);
}

const clj_type clj_lazy_def_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "lazy-def",
	.mutable_children = true,
	.each_child = lazy_def_each_child,
};

static clj_value unbound(clj_value var) {
	clj_value ns = clj_symbol_name(clj_var_ns(var)), name = clj_symbol_name(clj_var_name(var));
	return clj_throw_msg("Unbound var: #'%s/%s", clj_string_bytes(ns), clj_string_bytes(name));
}

// The init and the root it replaced are of no more use once the thunk holds its outcome.
static void drop_inputs(clj_lazy_def *t) {
	clj_value env = t->env.v, prev = t->prev.v;
	clj_slot_store(&t->h, &t->env, CLJ_NIL);
	clj_slot_store(&t->h, &t->prev, CLJ_UNBOUND);
	clj_release(env);
	clj_release(prev);
}

// The claim is held: runs the init, publishes the value as the root or the exception as the thunk's own.
static clj_value run_lazy(clj_value var, clj_lazy_def *t) {
	clj_value   tv = clj_from_ptr(t);
	clj_forcing frame;
	clj_force_claimed();
	clj_force_push(&frame, tv);
	clj_value r = t->fn(t->code, t->env.v);
	clj_force_pop(&frame);
	if (r == CLJ_THROWN) {
		// a cancellation is the forcer's, not the definition's: the next deref computes it again
		if (clj_is_cancellation(clj_pending())) {
			clj_force_set(tv, &t->state, CLJ_FORCE_UNFORCED);
			clj_force_unclaimed();
			return CLJ_THROWN;
		}
		clj_value trace = clj_take_pending_trace();
		clj_value ex = clj_take_pending();
		clj_slot_store(&t->h, &t->error, ex);
		clj_slot_store(&t->h, &t->trace, trace);
		drop_inputs(t);
		clj_force_set(tv, &t->state, CLJ_FORCE_FAILED);
		clj_force_unclaimed();
		return clj_throw_traced(clj_retain(ex), clj_retain(trace));
	}
	clj_var  *v = clj_var_of(var);
	clj_value expected = tv;
	clj_slot_store(&t->h, &t->value, clj_retain(r));
	// the root moved on while the init ran (a redefinition, or this thunk forced as another's prev): the value is the
	// thunk's alone
	if (clj_slot_cas(&v->h, &v->root, &expected, r, memory_order_acq_rel, memory_order_acquire)) {
		clj_release(tv);
		root_moved(var);
	} else {
		clj_release(r);
	}
	drop_inputs(t);
	clj_force_set(tv, &t->state, CLJ_FORCE_FORCED);
	clj_force_unclaimed();
	return clj_retain(t->value.v);
}

static clj_value thunk_value(clj_value var, clj_lazy_def *t);

// A read of the var by its own init, while this execution forces it.
static clj_value own_read(clj_value var, clj_lazy_def *t) {
	clj_value prev = t->prev.v;
	if (prev == CLJ_UNBOUND) {
		clj_value ns = clj_symbol_name(clj_var_ns(var)), name = clj_symbol_name(clj_var_name(var));
		return clj_throw_msg("Recursive definition of #'%s/%s: its value is read while it is being computed", clj_string_bytes(ns),
		                     clj_string_bytes(name));
	}
	return clj_is_lazy_def(prev) ? thunk_value(var, clj_lazy_def_of(prev)) : clj_retain(prev);
}

static clj_value thunk_value(clj_value var, clj_lazy_def *t) {
	clj_value tv = clj_from_ptr(t);
	for (;;) {
		uint32_t st = atomic_load_explicit(&t->state, memory_order_acquire);
		if (st == CLJ_FORCE_FAILED) return clj_throw_traced(clj_retain(t->error.v), clj_retain(t->trace.v));
		if (st == CLJ_FORCE_FORCED) return clj_retain(t->value.v);
		if (st == CLJ_FORCE_UNFORCED) {
			uint32_t expected = CLJ_FORCE_UNFORCED;
			if (atomic_compare_exchange_strong_explicit(&t->state, &expected, CLJ_FORCE_FORCING, memory_order_acq_rel, memory_order_acquire))
				return run_lazy(var, t);
			continue;
		}
		if (clj_force_here(tv)) return own_read(var, t);
		clj_force_wait(tv, &t->state);
	}
}

// The thunk stays alive while it is read at +0: the var's lazy slot holds it until the next def.
clj_value clj_var_root_value(clj_value var) {
	clj_value root = clj_var_root(var);
	if (!clj_is_lazy_def(root)) return root == CLJ_UNBOUND ? unbound(var) : clj_retain(root);
	return thunk_value(var, clj_lazy_def_of(root));
}

static bool pending_thunk(clj_value root, bool inferred_only) {
	if (!clj_is_lazy_def(root)) return false;
	const clj_lazy_def *t = clj_lazy_def_of(root);
	uint32_t            st = atomic_load_explicit(&t->state, memory_order_acquire);
	return st != CLJ_FORCE_FORCED && st != CLJ_FORCE_FAILED && (t->inferred || !inferred_only);
}

bool clj_var_is_pending(clj_value var) { return pending_thunk(clj_var_root(var), false); }

bool clj_def_defers(uint8_t lazy) { return lazy != CLJ_DEF_EAGER && clj_lazy_defs_mode_now() != CLJ_LAZY_DEFS_EAGER; }

// Inferred lazy defs in def order: what a redefinition forces first and what :after-load forces at the end of a load.
// Entries outlive their force until a scan drops them; a var may be in twice when it was defined twice.
typedef struct {
	clj_value var;
	uint64_t  seq;
} lazy_entry;

static clj_lock         lazy_lock = CLJ_LOCK_INIT;
static lazy_entry      *lazy_entries;
static size_t           nlazy, clazy;
static uint64_t         lazy_seq;
static _Atomic size_t   lazy_count; // nlazy, read without the lock by the barrier's fast path

// Under lazy_lock.
static void lazy_compact(void) {
	size_t k = 0;
	for (size_t i = 0; i < nlazy; i++) {
		if (pending_thunk(clj_var_root(lazy_entries[i].var), true)) lazy_entries[k++] = lazy_entries[i];
	}
	nlazy = k;
	atomic_store_explicit(&lazy_count, nlazy, memory_order_release);
}

static void lazy_register(clj_value var) {
	clj_lock_lock(&lazy_lock);
	if (nlazy == clazy) lazy_compact();
	if (nlazy == clazy) {
		clazy = clazy ? clazy * 2 : 64;
		lazy_entries = realloc(lazy_entries, clazy * sizeof *lazy_entries);
		if (!lazy_entries) clj_fatal("out of memory");
	}
	lazy_entries[nlazy++] = (lazy_entry){var, lazy_seq++};
	atomic_store_explicit(&lazy_count, nlazy, memory_order_release);
	clj_lock_unlock(&lazy_lock);
}

// The vars to force, copied out: an init runs Clojure code, never under the lock.
static clj_value *lazy_collect(uint64_t since, clj_value except, size_t *n) {
	clj_lock_lock(&lazy_lock);
	lazy_compact();
	clj_value *vars = malloc((nlazy ? nlazy : 1) * sizeof *vars);
	if (!vars) clj_fatal("out of memory");
	size_t k = 0;
	for (size_t i = 0; i < nlazy; i++) {
		if (lazy_entries[i].seq >= since && lazy_entries[i].var != except) vars[k++] = lazy_entries[i].var;
	}
	clj_lock_unlock(&lazy_lock);
	*n = k;
	return vars;
}

// A pure init reads var roots when it is forced: a root about to change is read first by every init deferred before
// it, so each sees what an eager load would have shown it (design §4 «Var и ленивые def», the barrier).
static void redefinition_barrier(clj_value except) {
	if (!atomic_load_explicit(&lazy_count, memory_order_acquire)) return;
	size_t     n;
	clj_value *vars = lazy_collect(0, except, &n);
	for (size_t i = 0; i < n; i++) {
		if (!pending_thunk(clj_var_root(vars[i]), true)) continue;
		clj_value v = clj_var_root_value(vars[i]);
		// the failure stays in its var and is thrown by its deref; the rebind goes on
		if (v == CLJ_THROWN) clj_release(clj_take_pending());
		else clj_release(v);
	}
	free(vars);
}

void clj_var_bind_lazy(clj_value var, clj_lazy_def_fn fn, const void *code, clj_value env, bool inferred) {
	clj_var  *v = clj_var_of(var);
	clj_value prev = clj_slot_load(&v->root, memory_order_acquire);
	if (!v->dynamic && prev != CLJ_UNBOUND) {
		redefinition_barrier(var);
		prev = clj_slot_load(&v->root, memory_order_acquire);
	}
	clj_lazy_def *t = clj_alloc(&clj_lazy_def_type, sizeof *t);
	t->h.flags |= CLJ_FLAG_MUTABLE;
	atomic_init(&t->state, CLJ_FORCE_UNFORCED);
	t->inferred = inferred;
	t->fn = fn;
	t->code = code;
	t->var = var;
	clj_slot_init(&t->h, &t->env, clj_retain(env));
	clj_slot_init(&t->h, &t->value, CLJ_NIL);
	clj_slot_init(&t->h, &t->error, CLJ_NIL);
	clj_slot_init(&t->h, &t->trace, CLJ_NIL);
	// ^:lazy is a delay: its init reading its own var is the recursion, as in (def x (delay @x))
	clj_slot_init(&t->h, &t->prev, inferred ? clj_retain(prev) : CLJ_UNBOUND);
	clj_value tv = clj_from_ptr(t);
	bind_unbarriered(var, tv, tv);
	clj_release(tv);
	if (inferred) lazy_register(var);
}

uint64_t clj_lazy_defs_mark(void) {
	clj_lock_lock(&lazy_lock);
	uint64_t mark = lazy_seq;
	clj_lock_unlock(&lazy_lock);
	return mark;
}

clj_value clj_lazy_defs_force_since(uint64_t mark, clj_value *failed) {
	*failed = CLJ_NIL;
	size_t     n;
	clj_value *vars = lazy_collect(mark, CLJ_NIL, &n);
	clj_value  r = CLJ_NIL;
	for (size_t i = 0; i < n && r == CLJ_NIL; i++) {
		if (!pending_thunk(clj_var_root(vars[i]), true)) continue;
		clj_value v = clj_var_root_value(vars[i]);
		if (v == CLJ_THROWN) {
			*failed = vars[i];
			r = CLJ_THROWN;
		} else {
			clj_release(v);
		}
	}
	free(vars);
	return r;
}

static clj_value lazy_defs_var = CLJ_NIL, kw_lazy, kw_after_load, kw_eager;

static clj_value mode_keyword(clj_lazy_defs_mode mode) {
	return mode == CLJ_LAZY_DEFS_EAGER ? kw_eager : mode == CLJ_LAZY_DEFS_AFTER_LOAD ? kw_after_load : kw_lazy;
}

void clj_lazy_defs_install(void) {
	kw_lazy = clj_keyword_from_cstr("lazy");
	kw_after_load = clj_keyword_from_cstr("after-load");
	kw_eager = clj_keyword_from_cstr("eager");
	clj_value sym = clj_symbol_from_cstr("*lazy-defs*");
	lazy_defs_var = clj_ns_intern(clj_ns_core(), sym);
	clj_release(sym);
	clj_var_set_dynamic(lazy_defs_var, true);
	const char        *e = getenv("CLJ_LAZY_DEFS");
	clj_lazy_defs_mode mode = CLJ_LAZY_DEFS_LAZY;
	if (e && strcmp(e, "eager") == 0) mode = CLJ_LAZY_DEFS_EAGER;
	else if (e && strcmp(e, "after-load") == 0) mode = CLJ_LAZY_DEFS_AFTER_LOAD;
	clj_var_bind_root(lazy_defs_var, mode_keyword(mode));
	clj_core_mark_extension("*lazy-defs*");
}

void clj_lazy_defs_set_default(clj_lazy_defs_mode mode) { clj_var_bind_root(lazy_defs_var, mode_keyword(mode)); }

// Any other value is :lazy, as an unknown option is ignored.
clj_lazy_defs_mode clj_lazy_defs_mode_now(void) {
	if (clj_is_nil(lazy_defs_var)) return CLJ_LAZY_DEFS_LAZY;
	clj_value box = clj_var_thread_binding(lazy_defs_var);
	clj_value v = clj_is_nil(box) ? clj_var_root(lazy_defs_var) : clj_volatile_value(box);
	return v == kw_eager ? CLJ_LAZY_DEFS_EAGER : v == kw_after_load ? CLJ_LAZY_DEFS_AFTER_LOAD : CLJ_LAZY_DEFS_LAZY;
}
