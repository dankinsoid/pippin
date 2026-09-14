// @ai-generated(guided)
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/map.h"
#include "clj/printer.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/vector.h"

static void fn_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_fn *f = self;
	visit(f->name, ctx);
	visit(f->code, ctx);
	for (uint32_t i = 0; i < f->nenv; i++) visit(f->env[i], ctx);
}

static uint32_t fn_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool fn_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

static void fn_finalize(void *self) {
	clj_fn *f = self;
	if (f->kind == CLJ_FN_NATIVE_CTX && f->u.native_ctx.release) f->u.native_ctx.release(f->u.native_ctx.ctx);
}

static clj_value fn_invoke(clj_value f, const clj_value *args, size_t n) {
	const clj_fn *fn = clj_fn_of(f);
	if (fn->kind == CLJ_FN_CLOSURE) return clj_closure_invoke(f, args, n);
	if (n < fn->min_arity || (fn->max_arity != CLJ_ARITY_ANY && n > fn->max_arity)) return clj_arity_error(f, n);
	if (fn->kind == CLJ_FN_NATIVE_CTX) return fn->u.native_ctx.fn(fn->u.native_ctx.ctx, args, n);
	return fn->u.native(args, n);
}

const clj_type clj_fn_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "fn",
	.core_bits = CLJ_CORE_FN,
	.each_child = fn_each_child,
	.finalize = fn_finalize,
	.hash = fn_hash,
	.equals = fn_equals,
	.invoke = fn_invoke,
};

static clj_fn *native_new(clj_value name, clj_fn_kind kind, uint32_t min_arity, uint32_t max_arity) {
	CLJ_ASSERT(clj_is_nil(name) || clj_is_symbol(name), "fn name must be a symbol or nil");
	clj_fn *f = clj_alloc(&clj_fn_type, sizeof *f);
	f->name = clj_retain(name);
	f->kind = kind;
	f->min_arity = min_arity;
	f->max_arity = max_arity;
	return f;
}

clj_value clj_fn_native(clj_value name, clj_native_fn fn, uint32_t min_arity, uint32_t max_arity) {
	clj_fn *f = native_new(name, CLJ_FN_NATIVE, min_arity, max_arity);
	f->u.native = fn;
	return clj_from_ptr(f);
}

// @ai-generated(guided)
clj_value clj_fn_native_ctx(clj_value name, clj_native_ctx_fn fn, void *ctx, void (*release)(void *ctx), uint32_t min_arity, uint32_t max_arity) {
	clj_fn *f = native_new(name, CLJ_FN_NATIVE_CTX, min_arity, max_arity);
	f->u.native_ctx.fn = fn;
	f->u.native_ctx.ctx = ctx;
	f->u.native_ctx.release = release;
	return clj_from_ptr(f);
}

clj_value clj_fn_closure(clj_value code, clj_value name, const clj_value *env, uint32_t nenv) {
	CLJ_ASSERT(clj_is_nil(name) || clj_is_symbol(name), "fn name must be a symbol or nil");
	clj_fn *f = clj_alloc(&clj_fn_type, sizeof *f + nenv * sizeof *f->env);
	f->name = clj_retain(name);
	f->kind = CLJ_FN_CLOSURE;
	f->code = clj_retain(code);
	f->nenv = nenv;
	for (uint32_t i = 0; i < nenv; i++) f->env[i] = clj_retain(env[i]);
	return clj_from_ptr(f);
}

clj_value clj_arity_error(clj_value f, size_t n) {
	clj_value text = clj_pr_str(clj_is_fn(f) && !clj_is_nil(clj_fn_of(f)->name) ? clj_fn_of(f)->name : f);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("Wrong number of args (%zu) passed to: %s", n, clj_string_bytes(text));
	clj_release(text);
	return r;
}

clj_value clj_invoke(clj_value f, const clj_value *args, size_t n) {
	if (clj_is_ptr(f) && clj_type_of(f)->invoke) return clj_type_of(f)->invoke(f, args, n);
	clj_value text = clj_pr_str(f);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("%s cannot be invoked", clj_string_bytes(text));
	clj_release(text);
	return r;
}

clj_value clj_apply(clj_value f, const clj_value *args, size_t n) {
	CLJ_ASSERT(n >= 1, "apply needs the sequence argument");
	size_t     spread;
	clj_value  keep;
	clj_value *items = clj_seq_items(args[n - 1], &spread, &keep);
	if (!items) return CLJ_THROWN;
	size_t     total = n - 1 + spread;
	clj_value *all = malloc((total ? total : 1) * sizeof *all);
	if (!all) clj_fatal("out of memory");
	memcpy(all, args, (n - 1) * sizeof *all);
	memcpy(all + n - 1, items, spread * sizeof *all);
	clj_value r = clj_invoke(f, all, total);
	free(all);
	free(items);
	clj_release(keep);
	return r;
}
