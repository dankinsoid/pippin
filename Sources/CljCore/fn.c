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

const clj_type clj_fn_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "fn",
	.each_child = fn_each_child,
	.hash = fn_hash,
	.equals = fn_equals,
};

clj_value clj_fn_native(clj_value name, clj_native_fn fn, uint32_t min_arity, uint32_t max_arity) {
	CLJ_ASSERT(clj_is_nil(name) || clj_is_symbol(name), "fn name must be a symbol or nil");
	clj_fn *f = clj_alloc(&clj_fn_type, sizeof *f);
	f->name = clj_retain(name);
	f->kind = CLJ_FN_NATIVE;
	f->min_arity = min_arity;
	f->max_arity = max_arity;
	f->native = fn;
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

static clj_value arity_error(clj_value f, size_t n) {
	clj_value text = clj_pr_str(clj_is_fn(f) && !clj_is_nil(clj_fn_of(f)->name) ? clj_fn_of(f)->name : f);
	clj_value r = clj_throw_msg("Wrong number of args (%zu) passed to: %s", n, clj_string_bytes(text));
	clj_release(text);
	return r;
}

clj_value clj_invoke(clj_value f, const clj_value *args, size_t n) {
	if (clj_is_fn(f)) {
		const clj_fn *fn = clj_fn_of(f);
		if (fn->kind == CLJ_FN_CLOSURE) return clj_closure_invoke(f, args, n);
		if (n < fn->min_arity || (fn->max_arity != CLJ_ARITY_ANY && n > fn->max_arity)) return arity_error(f, n);
		return fn->native(args, n);
	}
	if (clj_is_keyword(f)) {
		if (n != 1 && n != 2) return arity_error(f, n);
		return clj_get(args[0], f, n == 2 ? args[1] : CLJ_NIL);
	}
	if (clj_is_ptr(f) && clj_header_of(f)->type == &clj_map_type) {
		if (n != 1 && n != 2) return arity_error(f, n);
		return clj_get(f, args[0], n == 2 ? args[1] : CLJ_NIL);
	}
	if (clj_is_vector(f)) {
		if (n != 1) return arity_error(f, n);
		return clj_nth(f, args[0], false, CLJ_NIL);
	}
	clj_value text = clj_pr_str(f);
	clj_value r = clj_throw_msg("%s cannot be invoked", clj_string_bytes(text));
	clj_release(text);
	return r;
}

clj_value clj_apply(clj_value f, const clj_value *args, size_t n) {
	CLJ_ASSERT(n >= 1, "apply needs the sequence argument");
	clj_value seq = args[n - 1];
	if (!clj_is_nil(seq) && !clj_is_list(seq) && !clj_is_vector(seq)) return clj_throw_msg("Don't know how to create ISeq from: %s", clj_type_name(seq));
	size_t     spread = clj_is_nil(seq) ? 0 : clj_list_count(seq);
	size_t     total = n - 1 + spread;
	clj_value *all = malloc((total ? total : 1) * sizeof *all);
	if (!all) clj_fatal("out of memory");
	memcpy(all, args, (n - 1) * sizeof *all);
	clj_seq_iter it = clj_seq_iter_start(seq);
	size_t       i = n - 1;
	while (clj_seq_iter_next(&it, &all[i])) i++;
	clj_value r = clj_invoke(f, all, total);
	free(all);
	return r;
}
