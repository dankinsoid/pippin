// @ai-generated(solo)
#ifndef CLJ_COMPILED_INTERNAL_H
#define CLJ_COMPILED_INTERNAL_H

#include <stdlib.h>
#include <string.h>

#include "clj/array.h"
#include "clj/atom.h"
#include "clj/bigint.h"
#include "clj/coll.h"
#include "clj/compiled.h"
#include "clj/core.h"
#include "clj/decimal.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/inst.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/long.h"
#include "clj/map.h"
#include "clj/number.h"
#include "clj/proto.h"
#include "clj/ratio.h"
#include "clj/regex.h"
#include "clj/set.h"
#include "clj/sorted.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/uuid.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "epoch_internal.h"
#include "profile_internal.h"
#include "proto_internal.h"
#include "shadow_internal.h"

// Every helper mirrors one step of eval.c so that a compiled body and the interpreter agree on ownership.

// The closed world (CLJ_CLOSED, design §6): no var can be rebound, so the intrinsic and fusion guards are constant.
#ifdef CLJ_CLOSED
#define CLJC_GUARD(var, boot) 1
#define CLJC_FUSED(guards, n) 1
#else
#define CLJC_GUARD(var, boot) (clj_var_root_relaxed(var) == (boot))
#define CLJC_FUSED(guards, n) clj_fusion_guard(guards, n)
#endif

static inline bool clj_c_slot_owned(const clj_cframe *f, uint32_t i) { return i >= 64 || (f->owned >> i) & 1; }

static inline void clj_c_set(clj_cframe *f, uint32_t i, clj_value v) {
	clj_value old = f->slots[i];
	f->slots[i] = v;
	if (clj_c_slot_owned(f, i)) clj_release(old);
	else f->owned |= (uint64_t)1 << i;
}

// A slot the generator keeps in a C variable holds nil or an owned reference, never a borrowed one (NOTES.md, "Compiler").
static inline void clj_c_rebind(clj_value *slot, clj_value v) {
	clj_value old = *slot;
	*slot = v;
	clj_release(old);
}

// An owned read of a slot; a last use hands the frame's reference over.
static inline clj_value clj_c_local(clj_cframe *f, uint32_t i, bool last) {
	clj_value v = f->slots[i];
	if (last && clj_c_slot_owned(f, i)) {
		f->slots[i] = CLJ_NIL;
		if (i < 64) f->owned &= ~((uint64_t)1 << i);
		return v;
	}
	return clj_retain(v);
}

// A borrowed read of a slot whose read is a last use: owned when the frame handed its reference over.
static inline clj_value clj_c_take(clj_cframe *f, uint32_t i, bool *owned) {
	clj_value v = f->slots[i];
	*owned = false;
	if (clj_c_slot_owned(f, i)) {
		f->slots[i] = CLJ_NIL;
		if (i < 64) f->owned &= ~((uint64_t)1 << i);
		*owned = true;
	}
	return v;
}

static inline const clj_cframe *clj_c_outer(const clj_cframe *f, uint32_t depth) {
	while (depth--) f = f->outer;
	return f;
}

// A var read in a borrowed position: +0 for an immortal or fn root, retained otherwise (eval_borrowed).
static inline clj_value clj_c_var_borrow(clj_value var, bool *owned) {
	*owned = false;
	if (__builtin_expect(clj_var_of(var)->dynamic, 0)) {
		*owned = true;
		return clj_var_deref(var);
	}
	clj_value root = clj_var_root(var);
	if (!clj_is_ptr(root)) {
		if (root == CLJ_UNBOUND) {
			*owned = true;
			return clj_var_deref(var);
		}
		return root;
	}
	const clj_header *h = clj_header_of(root);
	if ((h->flags & CLJ_FLAG_IMMORTAL) || h->type == &clj_fn_type) return root;
	*owned = true;
	return clj_retain(root);
}

static inline void clj_c_release_slots(const clj_cframe *f, uint32_t n) {
	if (n > 64) {
		for (uint32_t i = 0; i < n; i++) clj_release(f->slots[i]);
		return;
	}
	uint64_t owned = f->owned;
	while (owned) {
		clj_release(f->slots[__builtin_ctzll(owned)]);
		owned &= owned - 1;
	}
}

// The guard, the deadline, the shadow frame and the instrumentation of run_body; false leaves a throw pending.
static inline bool clj_c_enter(const clj_node *stub, clj_ccall *c) {
	clj_shadow_stack *s = clj_shadow_tls;
	if (__builtin_expect(!s, 0)) s = clj_shadow_stack_init();
	char *limit = s->stack_limit;
	if (__builtin_expect(!limit, 0)) limit = clj_eval_stack_limit(s);
	char here;
	if (__builtin_expect(&here < limit, 0)) {
		clj_throw_msg("Stack overflow");
		return false;
	}
	if (__builtin_expect(s->deadline != 0, 0) && clj_eval_deadline_hit(s)) return false;
	s->frames[s->depth & s->mask] = (clj_shadow_frame){stub, NULL};
	s->depth++;
	c->instrument = clj_instrument;
	c->t0 = 0;
	c->signpost = 0;
	if (__builtin_expect(c->instrument, 0)) {
		if (c->instrument & CLJ_INSTRUMENT_PROFILE) c->t0 = clj_profile_now();
#ifdef __APPLE__
		if (c->instrument & CLJ_INSTRUMENT_SIGNPOSTS) c->signpost = clj_signpost_begin(stub->u.fn.name);
#endif
	}
	return true;
}

static inline void clj_c_leave(const clj_node *stub, clj_ccall *c) {
	if (__builtin_expect(c->instrument, 0)) {
#ifdef __APPLE__
		if (c->signpost) clj_signpost_end(c->signpost);
#endif
		if (c->instrument & CLJ_INSTRUMENT_PROFILE) clj_profile_record(stub, clj_profile_now() - c->t0);
	}
	if (__builtin_expect(clj_shadow_pop() == 0, 0)) clj_eval_drain_retired();
}

// One loop turn: true when the deadline throw is pending.
static inline bool clj_c_loop_tick(void) {
	clj_shadow_stack *s = clj_shadow_tls;
	return s && __builtin_expect(s->deadline != 0, 0) && clj_eval_deadline_hit(s);
}

// Retains every param of a frame past 64 slots, which then treats every slot as owned (closure_run).
static inline void clj_c_retain_params(clj_cframe *f, uint32_t nparams) {
	for (uint32_t i = 0; i < nparams; i++) {
		if (!((f->owned >> i) & 1)) clj_retain(f->slots[i]);
	}
	f->owned = UINT64_MAX;
}

// What the interpreter calls a native at the head with: its arity check, then the C function.
static inline clj_value clj_c_invoke(clj_value f, const clj_value *args, size_t n) {
	if (clj_is_fn(f) && clj_fn_of(f)->kind == CLJ_FN_NATIVE) {
		const clj_fn *nf = clj_fn_of(f);
		if (n < nf->min_arity || (nf->max_arity != CLJ_ARITY_ANY && n > nf->max_arity)) return clj_arity_error(f, n);
		return nf->u.native.fn(args, n);
	}
	return clj_invoke(f, args, n);
}

// The generic path of an INTRINSIC whose guard failed: the var's current root through clj_invoke.
static inline clj_value clj_c_intrinsic_fallback(clj_value var, const clj_value *args, size_t n) {
	clj_value fn = clj_var_deref(var);
	if (fn == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_invoke(fn, args, n);
	clj_release(fn);
	return r;
}

// ---- protocol call sites (NOTES.md "Compiler", protocol calls)

// The descriptor a type var holds, for the guard of a direct arm; NULL while the var holds no type.
static inline const clj_type *clj_c_var_type(clj_value var) {
	clj_value root = clj_var_root_relaxed(var);
	return clj_is_type(root) ? (const clj_type *)clj_to_ptr(root) : NULL;
}

// A direct arm's cell: the epoch at which the tables were last seen to bind the arm's descriptor to the arm's impl.
static inline bool clj_c_arm_hit(const _Atomic uint64_t *cell, uint64_t epoch) { return atomic_load_explicit(cell, memory_order_relaxed) == epoch; }

// An impl of another unit, bound through the registry at the first fill of an arm that names it.
typedef struct {
	const char       *code_name, *fn_name;
	clj_native_ctx_fn code;
	clj_compiled_fn   fn;
} clj_cproto_impl;

static inline clj_native_ctx_fn clj_c_impl_code(clj_cproto_impl *i) {
	if (!i->code) i->code = clj_compiled_impl(i->code_name);
	return i->code;
}

static inline clj_compiled_fn clj_c_impl_fn(clj_cproto_impl *i) {
	if (!i->fn) i->fn = clj_compiled_symbol(i->fn_name);
	return i->fn;
}

// The per-thread inline cache of a site: the method fn, the receiver's descriptor and its impl, held retained,
// valid while the epoch stands. Per thread, so a fill never races a hit on another thread.
typedef struct {
	clj_value       method;
	const clj_type *type;
	clj_value       impl;
	uint64_t        epoch;
} clj_cproto_ic;

// A protocol method's impl at a call: a compiled closure's dispatcher directly (it checks the arity itself),
// anything else through clj_invoke.
static inline clj_value clj_c_call_impl(clj_value impl, const clj_value *args, size_t n) {
	const clj_fn *f = clj_fn_of(impl);
	if (f->kind == CLJ_FN_NATIVE_CTX && f->u.native_ctx.ctx == f) return f->u.native_ctx.fn((void *)f, args, n);
	return clj_invoke(impl, args, n);
}

// The tables and the fill: the miss path of an inline cache, in compiled.c.
clj_value clj_c_proto_miss(clj_cproto_ic *ic, clj_value method, const clj_value *args, size_t n, const clj_type *t, uint64_t epoch);
// The tables, then the cell set when method's impl for args[0] is the compiled closure whose dispatcher is code.
bool clj_c_arm_fill(_Atomic uint64_t *cell, clj_value method, const clj_value *args, size_t n, clj_native_ctx_fn code, clj_compiled_fn fn, uint64_t epoch);
// satisfies?/extends? computed, the cell set when the answer is the constant the site carries.
bool clj_c_satisfies_fill(_Atomic uint64_t *cell, clj_value proto, clj_value x, clj_value expected, uint64_t epoch);
bool clj_c_extends_fill(_Atomic uint64_t *cell, clj_value proto, clj_value type, clj_value expected, uint64_t epoch);

static inline clj_value clj_c_proto_ic_call(clj_cproto_ic *ic, clj_value method, const clj_value *args, size_t n, const clj_type *t, uint64_t epoch) {
	if (__builtin_expect(ic->epoch == epoch && ic->type == t && ic->method == method, 1)) return clj_c_call_impl(ic->impl, args, n);
	return clj_c_proto_miss(ic, method, args, n, t, epoch);
}

#endif
