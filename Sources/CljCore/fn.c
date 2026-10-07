// @ai-generated(guided)
#include <pthread.h>
#include <stdio.h>
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
	visit(f->name.v, ctx);
	visit(f->code.v, ctx);
	visit(f->meta.v, ctx);
	for (uint32_t i = 0; i < f->nenv; i++) visit(f->env[i].v, ctx);
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

static clj_value fn_meta(clj_value self) { return clj_retain(clj_fn_of(self)->meta.v); }

// A shared copy keeps the original alive through code and borrows its ctx: a ctx has one release callback,
// so it cannot be owned twice.
// @ai-generated(guided)
static clj_value fn_with_meta(clj_value self, clj_value m) {
	clj_fn *f = clj_fn_of(self);
	if (clj_is_nil(m) && clj_is_nil(f->meta.v)) return self;
	if (!clj_is_unique(self)) {
		size_t  size = sizeof *f + f->nenv * sizeof *f->env;
		clj_fn *c = clj_alloc(&clj_fn_type, size);
		memcpy((char *)c + sizeof c->h, (const char *)f + sizeof f->h, size - sizeof f->h);
		c->h.flags |= f->h.flags & (CLJ_FLAG_REACH | CLJ_FLAG_REACH_LOCAL);
		clj_retain(c->name.v);
		clj_retain(c->code.v);
		clj_slot_init(&c->h, &c->meta, CLJ_NIL);
		for (uint32_t i = 0; i < c->nenv; i++) clj_retain(c->env[i].v);
		if (c->kind == CLJ_FN_NATIVE_CTX && c->u.native_ctx.ctx == f) c->u.native_ctx.ctx = c;
		if (c->kind == CLJ_FN_NATIVE_CTX && c->u.native_ctx.release) {
			c->u.native_ctx.release = NULL;
			clj_release(c->code.v);
			clj_slot_init(&c->h, &c->code, clj_retain(self));
		}
		clj_release(self);
		f = c;
	}
	clj_value old = f->meta.v;
	clj_slot_store(&f->h, &f->meta, clj_retain(m));
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
	clj_slot_init(&f->h, &f->name, clj_retain(name));
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

// @ai-generated(solo)
clj_value clj_fn_native_env(clj_value name, clj_native_ctx_fn fn, const clj_value *env, uint32_t nenv, uint32_t arities, uint32_t min_arity, uint32_t max_arity) {
	CLJ_ASSERT(clj_is_nil(name) || clj_is_symbol(name), "fn name must be a symbol or nil");
	clj_fn *f = clj_alloc(&clj_fn_type, sizeof *f + nenv * sizeof *f->env);
	clj_slot_init(&f->h, &f->name, clj_retain(name));
	f->kind = CLJ_FN_NATIVE_CTX;
	f->arities = arities;
	f->min_arity = min_arity;
	f->max_arity = max_arity;
	f->u.native_ctx.fn = fn;
	f->u.native_ctx.ctx = f;
	f->nenv = nenv;
	for (uint32_t i = 0; i < nenv; i++) clj_slot_init(&f->h, &f->env[i], clj_retain(env[i]));
	return clj_from_ptr(f);
}

clj_value clj_fn_closure(clj_value exec, const clj_node *node, clj_value name, const clj_value *env, uint32_t nenv) {
	CLJ_ASSERT(clj_is_nil(name) || clj_is_symbol(name), "fn name must be a symbol or nil");
	CLJ_ASSERT(node->kind == CLJ_NODE_FN, "closure code must be a fn node");
	clj_fn *f = clj_alloc(&clj_fn_type, sizeof *f + nenv * sizeof *f->env);
	clj_slot_init(&f->h, &f->name, clj_retain(name));
	f->kind = CLJ_FN_CLOSURE;
	f->u.node = node;
	clj_slot_init(&f->h, &f->code, clj_retain(exec));
	f->nenv = nenv;
	for (uint32_t i = 0; i < nenv; i++) clj_slot_init(&f->h, &f->env[i], clj_retain(env[i]));
	return clj_from_ptr(f);
}

clj_value clj_rest_args(const clj_value *args, size_t nargs, uint32_t nparams) {
	if (nargs == CLJ_NARGS_REST) return clj_retain(args[nparams]);
	return nargs > nparams ? clj_list_from_array(args + nparams, nargs - nparams) : CLJ_NIL;
}

// The argument counts f accepts, as a bit per count below *variadic, and *variadic the count from which
// every larger one is accepted too (CLJ_ARITY_ANY when f is bounded). Read through clj_fn_accepts and not
// off the fn's own fields: a compiled closure keeps only its lowest arity where an interpreted one keeps
// the rest arity's own count, so the fields would answer differently in the two backends.
// Anything that is not a fn answers nothing: its invoke slot checks on the call.
// @ai-generated(solo)
static uint32_t fn_arities(clj_value f, uint32_t *variadic) {
	*variadic = CLJ_ARITY_ANY;
	if (!clj_is_fn(f)) return 0;
	uint32_t bits = 0;
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (clj_fn_accepts(f, i)) bits |= 1u << i;
	}
	if (!clj_fn_accepts(f, CLJ_FN_MAX_FIXED + 1)) return bits;
	// Open above: the lowest count from which the run to the top is unbroken is where "at least" starts.
	uint32_t from = CLJ_FN_MAX_FIXED + 1;
	for (uint32_t i = CLJ_FN_MAX_FIXED + 1; i-- > 0;) {
		if (!((bits >> i) & 1)) break;
		from = i;
	}
	*variadic = from;
	return bits & ((from > 31 ? 0u : (1u << from)) - 1);
}

// "1", "1 or 2", "at least 1", "1 or at least 2", "0, 1 or at least 3" — the counts a reader could have
// written. "N or more" alongside a fixed N reads as one number, hence "at least".
// @ai-generated(solo)
static bool arities_text(uint32_t bits, uint32_t variadic, char *buf, size_t cap) {
	char   items[CLJ_FN_MAX_FIXED + 2][24];
	size_t n = 0;
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if ((bits >> i) & 1) snprintf(items[n++], sizeof items[0], "%u", i);
	}
	if (variadic != CLJ_ARITY_ANY) snprintf(items[n++], sizeof items[0], "at least %u", variadic);
	if (!n) return false;
	size_t at = 0;
	for (size_t i = 0; i < n; i++) {
		const char *sep = i == 0 ? "" : (i + 1 == n ? " or " : ", ");
		int         wrote = snprintf(buf + at, cap - at, "%s%s", sep, items[i]);
		if (wrote < 0 || at + (size_t)wrote >= cap) return false;
		at += (size_t)wrote;
	}
	return true;
}

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_arities, kw_variadic, kw_given, kw_fn;

static void intern_keywords(void) {
	kw_arities = clj_keyword_from_cstr("arities");
	kw_variadic = clj_keyword_from_cstr("variadic");
	kw_given = clj_keyword_from_cstr("given");
	kw_fn = clj_keyword_from_cstr("fn");
}

void clj_fn_intern_keywords(void) { pthread_once(&keywords_once, intern_keywords); }

static clj_value arity_data(clj_value f, size_t n, uint32_t bits, uint32_t variadic) {
	pthread_once(&keywords_once, intern_keywords);
	clj_value data = clj_map_empty();
	if (bits || variadic != CLJ_ARITY_ANY) {
		clj_value arities = clj_vector_empty();
		for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
			if ((bits >> i) & 1) arities = clj_vector_conj(arities, clj_fixnum(i));
		}
		data = clj_map_assoc(data, kw_arities, arities);
		clj_release(arities);
	}
	if (variadic != CLJ_ARITY_ANY) data = clj_map_assoc(data, kw_variadic, clj_fixnum(variadic));
	data = clj_map_assoc(data, kw_given, clj_fixnum((int64_t)n));
	if (clj_is_fn(f) && !clj_is_nil(clj_fn_of(f)->name.v)) data = clj_map_assoc(data, kw_fn, clj_fn_of(f)->name.v);
	return data;
}

// Whether the counts answer that this very call was fine. Then they are not the truth about f and must
// not be shown: a compiled closure's bounds keep one minimum for its fixed and its rest arity, so a fn
// whose rest arity takes more fixed parameters than one of its fixed ones over-accepts the gap through
// clj_fn_accepts, and naming the gap as accepted would contradict the refusal beside it.
static bool arities_disagree(uint32_t bits, uint32_t variadic, size_t n) {
	if (variadic != CLJ_ARITY_ANY && n >= variadic) return true;
	return n <= CLJ_FN_MAX_FIXED && ((bits >> n) & 1);
}

static clj_value arity_error(clj_value f, const char *over, size_t n) {
	clj_value name = clj_is_fn(f) && !clj_is_nil(clj_fn_of(f)->name.v) ? clj_fn_of(f)->name.v : CLJ_NIL;
	clj_value text = clj_is_nil(name) && clj_is_fn(f) ? clj_string_from_cstr("fn") : clj_pr_str_max(clj_is_nil(name) ? f : name, CLJ_ERROR_PRINT_MAX);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	uint32_t variadic, bits = fn_arities(f, &variadic);
	// An exact count disagreeing with the refusal drops the counts whole; "> n" is a lower bound and says
	// nothing about which count was passed, so it is not checked against them.
	if (!*over && arities_disagree(bits, variadic, n)) {
		bits = 0;
		variadic = CLJ_ARITY_ANY;
	}
	char      takes[256];
	clj_value message = arities_text(bits, variadic, takes, sizeof takes)
	                        ? clj_error_message("Wrong number of args (%s%zu) passed to: %s, which takes %s", over, n, clj_string_bytes(text), takes)
	                        : clj_error_message("Wrong number of args (%s%zu) passed to: %s", over, n, clj_string_bytes(text));
	clj_release(text);
	clj_value data = arity_data(f, n, bits, variadic);
	clj_value ex = clj_ex_info(message, data);
	clj_release(message);
	clj_release(data);
	return clj_throw(ex);
}

// Past CLJ_FN_MAX_FIXED only a rest parameter answers, so the exact count says nothing a reader can act on.
clj_value clj_arity_error(clj_value f, size_t n) {
	return n > CLJ_FN_MAX_FIXED ? arity_error(f, "> ", CLJ_FN_MAX_FIXED) : arity_error(f, "", n);
}

clj_value clj_arity_error_over(clj_value f, size_t n) { return arity_error(f, "> ", n); }

// @ai-generated(guided)
bool clj_fn_accepts(clj_value f, size_t n) {
	if (!clj_is_fn(f)) return true;
	const clj_fn *fn = clj_fn_of(f);
	if (fn->arities) return (n <= CLJ_FN_MAX_FIXED && ((fn->arities >> n) & 1)) || (fn->max_arity == CLJ_ARITY_ANY && n >= fn->min_arity);
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

// clj_fn_native_env makes a compiled closure its own ctx, which a host block never is.
static bool is_compiled_closure(const clj_fn *fn, clj_value f) {
	return fn->kind == CLJ_FN_NATIVE_CTX && fn->u.native_ctx.ctx == clj_to_ptr(f);
}

// A hand-written native takes a flat array whatever its arity, so only a closure's rest parameter takes a seq.
static size_t rest_at(clj_value f) {
	if (!clj_is_fn(f)) return SIZE_MAX;
	const clj_fn *fn = clj_fn_of(f);
	if (fn->kind == CLJ_FN_CLOSURE) {
		const clj_fn_arity *v = fn->u.node->u.fn.variadic;
		return v ? v->nparams : SIZE_MAX;
	}
	return is_compiled_closure(fn, f) && fn->max_arity == CLJ_ARITY_ANY ? fn->min_arity : SIZE_MAX;
}

// A hand-written variadic native reads its arguments out of the array, so apply must spread the whole seq.
static bool spreads_everything(clj_value f) {
	return clj_is_fn(f) && clj_fn_of(f)->kind != CLJ_FN_CLOSURE && clj_fn_of(f)->max_arity == CLJ_ARITY_ANY;
}

clj_value clj_apply(clj_value f, const clj_value *args, size_t n) {
	CLJ_ASSERT(n >= 1, "apply needs the sequence argument");
	// A var invokes its value, so its value answers for the shape; the call still names the var in an error.
	clj_value held = clj_is_var(f) ? clj_var_deref(f) : CLJ_NIL;
	if (held == CLJ_THROWN) return CLJ_THROWN;
	clj_value shape = clj_is_var(f) ? held : f;
	size_t fixed = n - 1;
	size_t over = rest_at(shape);
	// CLJ_FN_MAX_FIXED, never a lower ceiling of the callee's own: the arity error is to name the count passed.
	size_t bound = over == SIZE_MAX && spreads_everything(shape) ? SIZE_MAX
	               : over > CLJ_FN_MAX_FIXED && over != SIZE_MAX ? over
	                                                             : CLJ_FN_MAX_FIXED;
	// One argument past the bound: that one tells a whole spread from an arity error.
	size_t probe = bound == SIZE_MAX ? SIZE_MAX : (bound >= fixed ? bound - fixed : 0) + 1;
	clj_value tail = clj_seq(args[n - 1]);
	if (tail == CLJ_THROWN) {
		clj_release(held);
		return CLJ_THROWN;
	}

	size_t     cap = 8, taken = 0;
	clj_value *spread = malloc(cap * sizeof *spread);
	if (!spread) clj_fatal("out of memory");
	bool failed = false;
	while (!clj_is_nil(tail) && taken < probe) {
		clj_value x = clj_first(tail);
		if (x == CLJ_THROWN) {
			failed = true;
			break;
		}
		if (taken == cap) {
			cap *= 2;
			spread = realloc(spread, cap * sizeof *spread);
			if (!spread) clj_fatal("out of memory");
		}
		spread[taken++] = x;
		clj_value nx = clj_next(tail);
		if (nx == CLJ_THROWN) {
			failed = true;
			break;
		}
		clj_release(tail);
		tail = nx;
	}

	clj_value r;
	if (failed) {
		r = CLJ_THROWN;
	} else if (clj_is_nil(tail)) {
		size_t     total = fixed + taken;
		clj_value *all = malloc((total ? total : 1) * sizeof *all);
		if (!all) clj_fatal("out of memory");
		memcpy(all, args, fixed * sizeof *all);
		memcpy(all + fixed, spread, taken * sizeof *all);
		r = clj_invoke(f, all, total);
		free(all);
	} else if (over == SIZE_MAX) {
		r = clj_arity_error_over(f, bound);
	} else {
		// Everything past the rest parameter goes back in front of the seq, so the parameter sees one seq.
		size_t keep = over >= fixed ? over - fixed : 0;
		CLJ_ASSERT(keep <= taken, "the walk stopped short of the rest parameter");
		for (size_t i = taken; i > keep; i--) {
			clj_value c = clj_cons_new(spread[i - 1], tail);
			clj_release(tail);
			tail = c;
		}
		for (size_t i = fixed; i > over; i--) {
			clj_value c = clj_cons_new(args[i - 1], tail);
			clj_release(tail);
			tail = c;
		}
		size_t     head = over < fixed ? over : fixed;
		clj_value *all = malloc((over + 1) * sizeof *all);
		if (!all) clj_fatal("out of memory");
		memcpy(all, args, head * sizeof *all);
		memcpy(all + head, spread, keep * sizeof *all);
		all[over] = tail;
		r = clj_invoke(f, all, CLJ_NARGS_REST);
		free(all);
	}
	for (size_t i = 0; i < taken; i++) clj_release(spread[i]);
	free(spread);
	clj_release(tail);
	clj_release(held);
	return r;
}
