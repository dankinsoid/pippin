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
#include "shape_internal.h"

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

// A compiled fn's body is an ordinary C function: no shadow frame (traces walk the real stack, trace.c), no stack
// check (the guard page, guard.c), no deadline (the loop tick and the seq drivers). The section keeps the frame
// functions contiguous for the unit's range table, noinline keeps each one a frame of its own, and a tail call
// turned into a jump would hide an infinite recursion from the guard page.
#if defined(__APPLE__)
#define CLJC_FRAME __attribute__((noinline, disable_tail_calls, section("__TEXT,__cljframe,regular,pure_instructions")))
#else
#define CLJC_FRAME __attribute__((noinline, disable_tail_calls))
#endif
#define CLJC_INLINE static inline __attribute__((always_inline))

// After a call in a frame fn's body: the address the call returns to and the fn it belongs to, so a body inlined
// into another frame still names itself in a trace (trace.c). Data only: no instruction is emitted.
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__x86_64__))
#define CLJC_SITE(stub) __asm__ volatile(".pushsection __TEXT,__cljsite,regular\n\t.p2align 2\n\t.long 1f - .\n\t.long %c0 - .\n\t.popsection\n1:" ::"i"(stub))
#else
#define CLJC_SITE(stub) ((void)0)
#endif

// The profiler and signposts of run_body, only in a unit built with CLJC_INSTRUMENT (clj-compile --instrument).
#ifdef CLJC_INSTRUMENT
#define CLJC_ENTER(stub, cc) clj_c_instrument_enter(stub, cc)
#define CLJC_LEAVE(stub, cc) clj_c_instrument_leave(stub, cc)
#else
#define CLJC_ENTER(stub, cc) ((void)(cc))
#define CLJC_LEAVE(stub, cc) ((void)(cc))
#endif

static inline void clj_c_instrument_enter(const clj_node *stub, clj_ccall *c) {
	c->instrument = clj_instrument;
	c->t0 = 0;
	c->signpost = 0;
	if (__builtin_expect(c->instrument, 0)) {
		if (c->instrument & CLJ_INSTRUMENT_PROFILE) c->t0 = clj_profile_now();
#ifdef __APPLE__
		if (c->instrument & CLJ_INSTRUMENT_SIGNPOSTS) c->signpost = clj_signpost_begin(stub->u.fn.name);
#endif
	}
}

static inline void clj_c_instrument_leave(const clj_node *stub, clj_ccall *c) {
	if (__builtin_expect(c->instrument, 0)) {
#ifdef __APPLE__
		if (c->signpost) clj_signpost_end(c->signpost);
#endif
		if (c->instrument & CLJ_INSTRUMENT_PROFILE) clj_profile_record(stub, clj_profile_now() - c->t0);
	}
}

// One loop turn: true when the deadline throw is pending.
static inline bool clj_c_loop_tick(void) { return clj_deadline_tick(); }

// ---- the primitive entry (NOTES.md "Compiler", worker/wrapper): a worker takes int64_t/double arguments and answers
// its result unboxed, with thrown set in place of CLJ_THROWN (the exception is pending as usual).
typedef struct {
	int64_t v;
	bool    thrown;
} clj_wlong;

typedef struct {
	double v;
	bool   thrown;
} clj_wdouble;

// A worker in the symbol registry under its signature-carrying name: the caller's type comes from the name. Inline
// (a symbol only generated code names would be dead-stripped from a release host).
static inline void clj_compiled_register_worker(const char *name, void (*fn)(void)) { clj_compiled_register_symbol(name, (clj_compiled_fn)fn); }

static inline void (*clj_compiled_worker(const char *name))(void) { return (void (*)(void))clj_compiled_symbol(name); }

static inline bool clj_c_as_int64(clj_value v, int64_t *out) { return clj_int64_of(v, out); }

static inline bool clj_c_as_double(clj_value v, double *out) {
	if (!clj_is_double(v)) return false;
	*out = clj_double_val(v);
	return true;
}

// An owned box the facts say holds a long or a double; anything else is a lattice bug, never a program error.
static inline int64_t clj_c_unbox_long(clj_value v) {
	int64_t r;
	if (!clj_int64_of(v, &r)) clj_fatal("compiled worker: a value the facts call a long is not one");
	clj_release(v);
	return r;
}

static inline double clj_c_unbox_double(clj_value v) {
	if (!clj_is_double(v)) clj_fatal("compiled worker: a value the facts call a double is not one");
	double r = clj_double_val(v);
	clj_release(v);
	return r;
}

// A borrowed box read the same way, kept.
static inline int64_t clj_c_peek_long(clj_value v) {
	int64_t r;
	if (!clj_int64_of(v, &r)) clj_fatal("compiled worker: a value the facts call a long is not one");
	return r;
}

static inline double clj_c_peek_double(clj_value v) {
	if (!clj_is_double(v)) clj_fatal("compiled worker: a value the facts call a double is not one");
	return clj_double_val(v);
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

// A primitive site whose worker symbol is not registered (the callee's unit is not loaded): the var's root over the
// boxed arguments, which are owned and released here.
static inline clj_value clj_c_prim_fallback(clj_value var, clj_value *args, size_t n) {
	clj_value r = clj_c_intrinsic_fallback(var, args, n);
	for (size_t i = 0; i < n; i++) clj_release(args[i]);
	return r;
}

// ---- protocol call sites (NOTES.md "Compiler", protocol calls)

// The descriptor a type var holds, for the guard of a direct arm; NULL while the var holds no type.
static inline const clj_type *clj_c_var_type(clj_value var) {
	clj_value root = clj_var_root_relaxed(var);
	return clj_is_type(root) ? (const clj_type *)clj_to_ptr(root) : NULL;
}

#if CLJ_DEBUG
extern _Atomic int64_t clj_debug_proto_counters[2]; // arm hits, cache hits
#define CLJ_C_PROTO_COUNT(i) atomic_fetch_add_explicit(&clj_debug_proto_counters[i], 1, memory_order_relaxed)
#else
#define CLJ_C_PROTO_COUNT(i) ((void)0)
#endif

// A direct arm's cell: the epoch at which the tables were last seen to bind the arm's descriptor to the arm's impl.
static inline bool clj_c_arm_hit(const _Atomic uint64_t *cell, uint64_t epoch) {
	bool hit = atomic_load_explicit(cell, memory_order_relaxed) == epoch;
	if (hit) CLJ_C_PROTO_COUNT(0);
	return hit;
}

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
	if (__builtin_expect(ic->epoch == epoch && ic->type == t && ic->method == method, 1)) {
		CLJ_C_PROTO_COUNT(1);
		return clj_c_call_impl(ic->impl, args, n);
	}
	return clj_c_proto_miss(ic, method, args, n, t, epoch);
}

// ---- keyword-lookup sites (NOTES.md "Shapes", the cache): per thread, as the protocol cache, so a fill never races a hit.
typedef struct {
	uint32_t     n; // CLJ_KW_IC_MEGA once the site gave up
	clj_kw_entry e[CLJ_KW_IC_ENTRIES];
} clj_ckw_ic;

#if CLJ_DEBUG
extern _Atomic int64_t clj_debug_ckw_counters[2]; // hits, misses
#define CLJ_C_KW_COUNT(i) atomic_fetch_add_explicit(&clj_debug_ckw_counters[i], 1, memory_order_relaxed)
#else
#define CLJ_C_KW_COUNT(i) ((void)0)
#endif

// The generic lookup and the fill, in compiled.c.
clj_value clj_c_kw_miss(clj_ckw_ic *ic, clj_value key, clj_value m, clj_value not_found);

// (:key m not_found) / (get m key not_found) with a literal keyword: the interpreter's kw_lookup.
static inline clj_value clj_c_kw_get(clj_ckw_ic *ic, clj_value key, clj_value m, clj_value not_found) {
	if (__builtin_expect(ic->n <= CLJ_KW_IC_ENTRIES, 1)) {
		bool        record;
		const void *k = clj_kw_key_of(m, &record);
		for (uint32_t i = 0; i < ic->n; i++) {
			if (clj_kw_entry_hit(&ic->e[i], k, key)) {
				CLJ_C_KW_COUNT(0);
				return clj_retain(clj_kw_entry_read(&ic->e[i], m, not_found));
			}
		}
	}
	return clj_c_kw_miss(ic, key, m, not_found);
}

// A site building a map literal whose keys are literal keywords: the shape, resolved once, and the slot of each key.
typedef struct {
	_Atomic(const clj_shape *) shape;
	uint8_t                    slot[CLJ_SHAPE_MAX_KEYS];
} clj_cmap_site;

// items alternate key and value.
clj_value clj_c_map_shaped(clj_cmap_site *site, const clj_value *items, uint32_t n);

#endif
