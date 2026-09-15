// @ai-generated(guided)
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "clj/atom.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/map.h"
#include "clj/string.h"
#include "clj/vector.h"

static void atom_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_atom *a = self;
	visit(a->value, ctx);
	visit(a->meta, ctx);
	visit(a->validator, ctx);
	visit(a->watches, ctx);
}

static void atom_finalize(void *self) { clj_lock_destroy(&((clj_atom *)self)->lock); }

static uint32_t atom_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool atom_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

const clj_type clj_atom_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "atom",
	.core_bits = CLJ_CORE_META,
	.each_child = atom_each_child,
	.finalize = atom_finalize,
	.hash = atom_hash,
	.equals = atom_equals,
	.meta = clj_atom_meta,
};

static uintptr_t self_id(void) { return (uintptr_t)pthread_self(); }

// The lock is not recursive, so a thread that already holds it must not wait on it: throw instead.
static bool enter(clj_atom *a, const char *op) {
	uintptr_t me = self_id();
	if (atomic_load_explicit(&a->owner, memory_order_relaxed) == me) {
		clj_throw_msg("%s on an atom this thread is already swapping (nested swap! trap)", op);
		return false;
	}
	clj_lock_lock(&a->lock);
	atomic_store_explicit(&a->owner, me, memory_order_relaxed);
	return true;
}

static void leave(clj_atom *a) {
	atomic_store_explicit(&a->owner, 0, memory_order_relaxed);
	clj_lock_unlock(&a->lock);
}

// Consumes cause.
static clj_value invalid_state(clj_value cause) {
	clj_value msg = clj_string_from_cstr("Invalid reference state");
	clj_value ex = clj_ex_info_cause(msg, CLJ_NIL, clj_is_exception(cause) ? cause : CLJ_NIL);
	clj_release(msg);
	clj_release(cause);
	return clj_throw(ex);
}

// false with the exception pending. Runs under the lock: the validator sees the value before anyone else.
static bool validate_with(clj_value validator, clj_value v) {
	if (clj_is_nil(validator)) return true;
	clj_value r = clj_invoke(validator, &v, 1);
	if (r == CLJ_THROWN) {
		invalid_state(clj_take_pending());
		return false;
	}
	bool ok = clj_truthy(r);
	clj_release(r);
	if (!ok) invalid_state(CLJ_NIL);
	return ok;
}

typedef struct {
	clj_value atom, old, new;
	bool      ok;
} notify_ctx;

static bool notify_entry(clj_value key, clj_value f, void *ctx) {
	notify_ctx *c = ctx;
	clj_value   args[4] = {key, c->atom, c->old, c->new};
	clj_value   r = clj_invoke(f, args, 4);
	c->ok = r != CLJ_THROWN;
	clj_release(r);
	return c->ok;
}

// After the lock: a watch may deref or swap the atom. false with the exception pending.
static bool notify(clj_value atom, clj_value watches, clj_value old, clj_value new) {
	if (clj_is_nil(watches)) return true;
	notify_ctx c = {atom, old, new, true};
	clj_map_each(watches, notify_entry, &c);
	return c.ok;
}

clj_value clj_atom_new(clj_value value, clj_value meta, clj_value validator) {
	if (!validate_with(validator, value)) return CLJ_THROWN;
	clj_atom *a = clj_alloc(&clj_atom_type, sizeof *a);
	clj_lock_init(&a->lock);
	clj_share(value);
	clj_share(meta);
	clj_share(validator);
	a->value = clj_retain(value);
	a->meta = clj_retain(meta);
	a->validator = clj_retain(validator);
	return clj_from_ptr(a);
}

clj_value clj_atom_deref(clj_value atom) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "deref")) return CLJ_THROWN;
	clj_value v = clj_retain(a->value);
	leave(a);
	return v;
}

// Takes the lock the caller holds; the old value outlives the watches, which see it at +0.
static bool commit(clj_value atom, clj_value new) {
	clj_atom *a = clj_atom_of(atom);
	clj_share(new);
	clj_value old = a->value;
	a->value = clj_retain(new);
	clj_value watches = clj_retain(a->watches);
	leave(a);
	bool ok = notify(atom, watches, old, new);
	clj_release(watches);
	clj_release(old);
	return ok;
}

clj_value clj_atom_reset(clj_value atom, clj_value value) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "reset!")) return CLJ_THROWN;
	if (!validate_with(a->validator, value)) {
		leave(a);
		return CLJ_THROWN;
	}
	return commit(atom, value) ? clj_retain(value) : CLJ_THROWN;
}

static clj_value pair(clj_value old, clj_value new) {
	clj_value items[2] = {old, new};
	return clj_vector_from_array(items, 2);
}

clj_value clj_atom_reset_vals(clj_value atom, clj_value value) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "reset-vals!")) return CLJ_THROWN;
	if (!validate_with(a->validator, value)) {
		leave(a);
		return CLJ_THROWN;
	}
	clj_value old = clj_retain(a->value);
	bool      ok = commit(atom, value);
	clj_value r = ok ? pair(old, value) : CLJ_THROWN;
	clj_release(old);
	return r;
}

// A caller wanting *old (owned) forgoes the hand-over: the old value must survive f.
static clj_value apply_under_lock(clj_value atom, clj_value f, const clj_value *args, size_t nargs, clj_value *old, const char *op) {
	clj_atom  *a = clj_atom_of(atom);
	clj_value  small[8];
	clj_value *call = nargs + 1 <= sizeof small / sizeof *small ? small : malloc((nargs + 1) * sizeof *call);
	if (!call) clj_fatal("out of memory");
	memcpy(call + 1, args, nargs * sizeof *call);
	clj_call c = clj_call_prepare(f, nargs + 1);
	if (!enter(a, op)) {
		if (call != small) free(call);
		return CLJ_THROWN;
	}
	clj_value new;
	bool      handover = !old && clj_is_nil(a->validator) && clj_is_nil(a->watches);
	call[0] = a->value;
	if (handover) {
		a->value = CLJ_NIL;
		new = clj_call_invoke_owning(&c, call);
	} else {
		if (old) *old = clj_retain(a->value);
		new = clj_call_invoke(&c, call);
	}
	if (call != small) free(call);
	if (new == CLJ_THROWN) {
		if (old) clj_release(*old);
		leave(a);
		return CLJ_THROWN;
	}
	if (handover) {
		clj_share(new);
		a->value = clj_retain(new);
		leave(a);
		return new;
	}
	if (!validate_with(a->validator, new)) {
		if (old) clj_release(*old);
		clj_release(new);
		leave(a);
		return CLJ_THROWN;
	}
	if (!commit(atom, new)) {
		if (old) clj_release(*old);
		clj_release(new);
		return CLJ_THROWN;
	}
	return new;
}

clj_value clj_atom_swap(clj_value atom, clj_value f, const clj_value *args, size_t nargs) {
	return apply_under_lock(atom, f, args, nargs, NULL, "swap!");
}

clj_value clj_atom_swap_vals(clj_value atom, clj_value f, const clj_value *args, size_t nargs) {
	clj_value old;
	clj_value new = apply_under_lock(atom, f, args, nargs, &old, "swap-vals!");
	if (new == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = pair(old, new);
	clj_release(old);
	clj_release(new);
	return r;
}

clj_value clj_atom_compare_and_set(clj_value atom, clj_value expected, clj_value value) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "compare-and-set!")) return CLJ_THROWN;
	if (a->value != expected) {
		leave(a);
		return CLJ_FALSE;
	}
	if (!validate_with(a->validator, value)) {
		leave(a);
		return CLJ_THROWN;
	}
	return commit(atom, value) ? CLJ_TRUE : CLJ_THROWN;
}

clj_value clj_atom_add_watch(clj_value atom, clj_value key, clj_value f) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "add-watch")) return CLJ_THROWN;
	clj_value w = clj_map_assoc(clj_is_nil(a->watches) ? clj_map_empty() : a->watches, key, f);
	clj_share(w);
	a->watches = w;
	leave(a);
	return clj_retain(atom);
}

clj_value clj_atom_remove_watch(clj_value atom, clj_value key) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "remove-watch")) return CLJ_THROWN;
	if (!clj_is_nil(a->watches)) {
		clj_value w = clj_map_dissoc(a->watches, key);
		if (clj_map_count(w) == 0) {
			clj_release(w);
			w = CLJ_NIL;
		}
		clj_share(w);
		a->watches = w;
	}
	leave(a);
	return clj_retain(atom);
}

clj_value clj_atom_set_validator(clj_value atom, clj_value f) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "set-validator!")) return CLJ_THROWN;
	if (!validate_with(f, a->value)) {
		leave(a);
		return CLJ_THROWN;
	}
	clj_share(f);
	clj_value old = a->validator;
	a->validator = clj_retain(f);
	leave(a);
	clj_release(old);
	return CLJ_NIL;
}

clj_value clj_atom_get_validator(clj_value atom) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "get-validator")) return CLJ_THROWN;
	clj_value f = clj_retain(a->validator);
	leave(a);
	return f;
}

clj_value clj_atom_meta(clj_value atom) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "meta")) return CLJ_THROWN;
	clj_value m = clj_retain(a->meta);
	leave(a);
	return m;
}

clj_value clj_atom_reset_meta(clj_value atom, clj_value m) {
	clj_atom *a = clj_atom_of(atom);
	if (!enter(a, "reset-meta!")) return CLJ_THROWN;
	clj_share(m);
	clj_value old = a->meta;
	a->meta = clj_retain(m);
	leave(a);
	clj_release(old);
	return clj_retain(m);
}

clj_value clj_atom_alter_meta(clj_value atom, clj_value f, const clj_value *args, size_t nargs) {
	clj_atom  *a = clj_atom_of(atom);
	clj_value  small[8];
	clj_value *call = nargs + 1 <= sizeof small / sizeof *small ? small : malloc((nargs + 1) * sizeof *call);
	if (!call) clj_fatal("out of memory");
	memcpy(call + 1, args, nargs * sizeof *call);
	if (!enter(a, "alter-meta!")) {
		if (call != small) free(call);
		return CLJ_THROWN;
	}
	call[0] = a->meta;
	clj_value m = clj_invoke(f, call, nargs + 1);
	if (call != small) free(call);
	if (m != CLJ_THROWN && !clj_is_nil(m) && !clj_is_map(m)) {
		clj_value e = clj_throw_msg("alter-meta! fn must return a map, got: %s", clj_type_name(m));
		clj_release(m);
		m = e;
	}
	if (m == CLJ_THROWN) {
		leave(a);
		return CLJ_THROWN;
	}
	clj_share(m);
	clj_value old = a->meta;
	a->meta = clj_retain(m);
	leave(a);
	clj_release(old);
	return m;
}
