// @ai-generated(guided)
#include <stdlib.h>
#include <string.h>

#include "clj/analyzer.h"
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
	visit(f->meta, ctx);
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
	return fn->u.native.fn(args, n);
}

static clj_value fn_meta(clj_value self) { return clj_retain(clj_fn_of(self)->meta); }

// A shared copy keeps the original alive through code and borrows its ctx: a ctx has one release callback,
// so it cannot be owned twice.
// @ai-generated(guided)
static clj_value fn_with_meta(clj_value self, clj_value m) {
	clj_fn *f = clj_fn_of(self);
	if (clj_is_nil(m) && clj_is_nil(f->meta)) return self;
	if (!clj_is_unique(self)) {
		size_t  size = sizeof *f + f->nenv * sizeof *f->env;
		clj_fn *c = clj_alloc(&clj_fn_type, size);
		memcpy((char *)c + sizeof c->h, (const char *)f + sizeof f->h, size - sizeof f->h);
		clj_retain(c->name);
		clj_retain(c->code);
		c->meta = CLJ_NIL;
		for (uint32_t i = 0; i < c->nenv; i++) clj_retain(c->env[i]);
		if (c->kind == CLJ_FN_NATIVE_CTX && c->u.native_ctx.release) {
			c->u.native_ctx.release = NULL;
			clj_release(c->code);
			c->code = clj_retain(self);
		}
		clj_release(self);
		f = c;
	} else if (f->h.flags & CLJ_FLAG_SHARED) {
		clj_share(m);
	}
	clj_value old = f->meta;
	f->meta = clj_retain(m);
	clj_release(old);
	return clj_from_ptr(f);
}

const clj_type clj_fn_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "fn",
	.core_bits = CLJ_CORE_FN | CLJ_CORE_META | CLJ_CORE_OBJ,
	.each_child = fn_each_child,
	.finalize = fn_finalize,
	.hash = fn_hash,
	.equals = fn_equals,
	.invoke = fn_invoke,
	.meta = fn_meta,
	.with_meta = fn_with_meta,
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
	f->u.native.fn = fn;
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

clj_value clj_fn_closure(clj_value exec, const clj_node *node, clj_value name, const clj_value *env, uint32_t nenv) {
	CLJ_ASSERT(clj_is_nil(name) || clj_is_symbol(name), "fn name must be a symbol or nil");
	CLJ_ASSERT(node->kind == CLJ_NODE_FN, "closure code must be a fn node");
	clj_fn *f = clj_alloc(&clj_fn_type, sizeof *f + nenv * sizeof *f->env);
	f->name = clj_retain(name);
	f->kind = CLJ_FN_CLOSURE;
	f->u.node = node;
	f->code = clj_retain(exec);
	f->nenv = nenv;
	for (uint32_t i = 0; i < nenv; i++) f->env[i] = clj_retain(env[i]);
	return clj_from_ptr(f);
}

clj_value clj_arity_error(clj_value f, size_t n) {
	clj_value text = clj_pr_str_max(clj_is_fn(f) && !clj_is_nil(clj_fn_of(f)->name) ? clj_fn_of(f)->name : f, CLJ_ERROR_PRINT_MAX);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("Wrong number of args (%zu) passed to: %s", n, clj_string_bytes(text));
	clj_release(text);
	return r;
}

// @ai-generated(guided)
bool clj_fn_accepts(clj_value f, size_t n) {
	if (!clj_is_fn(f)) return true;
	const clj_fn *fn = clj_fn_of(f);
	if (fn->kind != CLJ_FN_CLOSURE) return n >= fn->min_arity && (fn->max_arity == CLJ_ARITY_ANY || n <= fn->max_arity);
	const clj_node *code = fn->u.node;
	if (n <= CLJ_FN_MAX_FIXED && code->u.fn.fixed[n]) return true;
	return code->u.fn.variadic && n >= code->u.fn.variadic->nparams;
}

clj_value clj_invoke(clj_value f, const clj_value *args, size_t n) {
	if (clj_is_ptr(f) && clj_type_of(f)->invoke) return clj_type_of(f)->invoke(f, args, n);
	clj_value text = clj_pr_str_max(f, CLJ_ERROR_PRINT_MAX);
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
