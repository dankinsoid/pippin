// @ai-generated(solo)
#include "clj/lowered.h"

#include <string.h>

#include "clj/ice.h"
#include "clj/ns.h"
#include "clj/reduce.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "roots_internal.h"

typedef struct {
	const char *name;
	uint32_t    at, min, max;
	int32_t     calls; // the count the native calls the fn with; -1: the call's own count less one (swap!'s f and args)
} native;

static const native natives[CLJ_LOWER_HOW_COUNT] = {
	[CLJ_LOWER_REDUCE] = {"reduce", 0, 2, 3, 2},
	[CLJ_LOWER_REDUCE_KV] = {"reduce-kv", 0, 3, 3, 3},
	[CLJ_LOWER_SWAP] = {"swap!", 1, 2, 2 + CLJ_FN_MAX_FIXED - 1, -1},
	[CLJ_LOWER_SWAP_VALS] = {"swap-vals!", 1, 2, 2 + CLJ_FN_MAX_FIXED - 1, -1},
};

// Written once by clj_lowered_install, before core.clj loads; NULL before it, so nothing lowers.
static clj_value vars[CLJ_LOWER_HOW_COUNT];
static clj_value roots[CLJ_LOWER_HOW_COUNT];

void clj_lowered_install(void) {
	clj_value core = clj_ns_core();
	for (uint32_t k = 0; k < CLJ_LOWER_HOW_COUNT; k++) {
		CLJ_NO_CYCLE clj_value sym = clj_symbol_from_cstr(natives[k].name);
		clj_value              var = clj_ns_resolve(core, sym);
		clj_release(sym);
		clj_value root = clj_is_nil(var) ? CLJ_NIL : clj_var_root(var);
		if (!clj_is_fn(root) || clj_fn_of(root)->kind != CLJ_FN_NATIVE) clj_ice("boot/lowered-table", NULL, "clojure.core/%s is no native builtin", natives[k].name);
		vars[k] = var;
		roots[k] = clj_retain(root);
	}
}

bool clj_lowered_find(clj_value var, uint32_t nargs, clj_lowered_how *how) {
	for (uint32_t k = 0; k < CLJ_LOWER_HOW_COUNT; k++) {
		if (!clj_is_nil(var) && vars[k] == var && nargs >= natives[k].min && nargs <= natives[k].max) {
			*how = (clj_lowered_how)k;
			return true;
		}
	}
	return false;
}

void clj_lowered_shape(clj_lowered_how how, uint32_t nargs, uint32_t *at, uint32_t *arity) {
	*at = natives[how].at;
	*arity = natives[how].calls < 0 ? nargs - 1 : (uint32_t)natives[how].calls;
}

CLJ_UNIT_ENTRY clj_value clj_lowered_root(clj_lowered_how how) { return roots[how]; }

// reduce-kv walks maps, sorted maps, records and vectors in C; swap!'s apply_under_lock only calls its fn.
static bool fn_stays(clj_lowered_how how, const clj_value *args, size_t n) {
	return how != CLJ_LOWER_REDUCE || clj_reduce_fn_stays(args[n - 1]);
}

static clj_value closure_of(clj_lowered *l) {
	if (clj_is_nil(l->closure)) l->closure = l->make(l->ctx);
	return l->closure;
}

static clj_value entry(void *ctx, const clj_value *args, size_t n) {
	clj_lowered *l = ctx;
	if (n == l->arity) return l->step(l->ctx, args, n);
	return clj_invoke(closure_of(l), args, n);
}

bool clj_lowered_prepare(const clj_fn *fn, size_t n, clj_native_ctx_fn *step, clj_native_ctx_fn *owned, void **ctx) {
	if (fn->kind != CLJ_FN_NATIVE_CTX || fn->u.native_ctx.fn != entry) return false;
	const clj_lowered *l = fn->u.native_ctx.ctx;
	if (n != l->arity) return false;
	*step = l->step;
	*owned = l->owned;
	*ctx = l->ctx;
	return true;
}

void clj_lowered_prepare_split(const clj_fn *fn, size_t n, clj_native_ctx_fn *split, clj_native_ctx_fn *split_owned) {
	*split = *split_owned = NULL;
	if (fn->kind != CLJ_FN_NATIVE_CTX || fn->u.native_ctx.fn != entry || n != 2) return;
	const clj_lowered *l = fn->u.native_ctx.ctx;
	*split = l->split;
	*split_owned = l->split ? l->split_owned : NULL;
}

CLJ_UNIT_ENTRY clj_value clj_lowered_call(clj_lowered_how how, clj_value *args, size_t n, clj_lowered *l) {
	// Immortal, so a retain or release of it is a no-op; nothing past the call reaches it (fn_stays).
	clj_fn fn = {.h = {1, CLJ_FLAG_IMMORTAL, &clj_fn_type}, .kind = CLJ_FN_NATIVE_CTX, .min_arity = 0, .max_arity = CLJ_ARITY_ANY};
	clj_slot_clear(&fn.name);
	clj_slot_clear(&fn.code);
	clj_slot_clear(&fn.meta);
	fn.u.native_ctx.fn = entry;
	fn.u.native_ctx.ctx = l;
	fn.u.native_ctx.release = NULL;
	l->closure = CLJ_NIL;
	// made lazily by closure_of, for a call that lets the fn escape
	CLJ_ROOTS(&l->closure);
	args[natives[how].at] = fn_stays(how, args, n) ? clj_from_ptr(&fn) : closure_of(l);
	clj_value r = clj_fn_of(roots[how])->u.native.fn(args, n);
	clj_release(l->closure);
	l->closure = CLJ_NIL;
	return r;
}
