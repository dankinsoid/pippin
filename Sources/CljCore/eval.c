// @ai-generated(guided)
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/list.h"
#include "clj/map.h"
#include "clj/printer.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"

enum {
	SMALL_SLOTS = 16, // frame slots kept on the C stack
	SMALL_ARGS  = 8,  // argument buffers kept on the C stack
	// Covers the C frames between two guard checks: a few eval steps plus one native (printer and reader are iterative).
	STACK_MARGIN = 64 * 1024,
};

static clj_value *buf_alloc(clj_value *small, size_t n) {
	if (n <= SMALL_ARGS) return small;
	clj_value *p = malloc(n * sizeof *p);
	if (!p) clj_fatal("out of memory");
	return p;
}

static void buf_free(clj_value *small, clj_value *p) {
	if (p != small) free(p);
}

static void slot_set(clj_frame *f, uint32_t i, clj_value v) {
	clj_value old = f->slots[i];
	f->slots[i] = v;
	clj_release(old);
}

// Evaluates nodes into out; on failure releases what was already evaluated.
static bool eval_all(clj_node *const *nodes, uint32_t n, clj_frame *f, clj_value *out) {
	for (uint32_t i = 0; i < n; i++) {
		clj_value v = nodes[i]->eval(nodes[i], f);
		if (v == CLJ_THROWN) {
			for (uint32_t j = 0; j < i; j++) clj_release(out[j]);
			return false;
		}
		out[i] = v;
	}
	return true;
}

static clj_value eval_const(const clj_node *n, clj_frame *f) {
	(void)f;
	return clj_retain(n->u.value);
}

static clj_value eval_local(const clj_node *n, clj_frame *f) { return clj_retain(f->slots[n->u.index]); }

static clj_value eval_captured(const clj_node *n, clj_frame *f) { return clj_retain(f->captured[n->u.index]); }

static clj_value eval_var(const clj_node *n, clj_frame *f) {
	(void)f;
	return clj_var_deref(n->u.var);
}

static clj_value eval_if(const clj_node *n, clj_frame *f) {
	clj_value test = n->u.if_.test->eval(n->u.if_.test, f);
	if (test == CLJ_THROWN) return CLJ_THROWN;
	bool truthy = clj_truthy(test);
	clj_release(test);
	const clj_node *branch = truthy ? n->u.if_.then : n->u.if_.else_;
	return branch ? branch->eval(branch, f) : CLJ_NIL;
}

static clj_value eval_do(const clj_node *n, clj_frame *f) {
	uint32_t last = n->u.seq.n - 1;
	for (uint32_t i = 0; i < last; i++) {
		clj_value v = n->u.seq.items[i]->eval(n->u.seq.items[i], f);
		if (v == CLJ_THROWN) return CLJ_THROWN;
		clj_release(v);
	}
	return n->u.seq.items[last]->eval(n->u.seq.items[last], f);
}

static bool bind_all(const clj_node *n, clj_frame *f) {
	for (uint32_t i = 0; i < n->u.let.n; i++) {
		clj_value v = n->u.let.inits[i]->eval(n->u.let.inits[i], f);
		if (v == CLJ_THROWN) return false;
		slot_set(f, n->u.let.slots[i], v);
	}
	return true;
}

static clj_value eval_let(const clj_node *n, clj_frame *f) {
	if (!bind_all(n, f)) return CLJ_THROWN;
	return n->u.let.body->eval(n->u.let.body, f);
}

// recur has already rebound the slots when the body yields CLJ_RECUR; the loop is a C loop, not a call.
static clj_value eval_loop(const clj_node *n, clj_frame *f) {
	if (!bind_all(n, f)) return CLJ_THROWN;
	for (;;) {
		clj_value v = n->u.let.body->eval(n->u.let.body, f);
		if (v != CLJ_RECUR) return v;
	}
}

// All arguments are evaluated before any slot changes, so (recur b a) swaps.
static clj_value eval_recur(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *vals = buf_alloc(small, n->u.recur.n);
	bool ok = eval_all(n->u.recur.args, n->u.recur.n, f, vals);
	if (ok) {
		for (uint32_t i = 0; i < n->u.recur.n; i++) slot_set(f, n->u.recur.slots[i], vals[i]);
	}
	buf_free(small, vals);
	return ok ? CLJ_RECUR : CLJ_THROWN;
}

static clj_value eval_fn(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *env = buf_alloc(small, n->u.fn.ncaptures);
	for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
		const clj_capture *c = &n->u.fn.captures[i];
		env[i] = c->from_captured ? f->captured[c->index] : f->slots[c->index];
	}
	clj_value fn = clj_fn_closure(clj_from_ptr((void *)n), n->u.fn.name, env, n->u.fn.ncaptures);
	buf_free(small, env);
	return fn;
}

static clj_value eval_invoke(const clj_node *n, clj_frame *f) {
	clj_value fn = n->u.invoke.fn->eval(n->u.invoke.fn, f);
	if (fn == CLJ_THROWN) return CLJ_THROWN;
	clj_value  small[SMALL_ARGS];
	clj_value *args = buf_alloc(small, n->u.invoke.n);
	clj_value  result = CLJ_THROWN;
	if (eval_all(n->u.invoke.args, n->u.invoke.n, f, args)) {
		result = clj_invoke(fn, args, n->u.invoke.n);
		for (uint32_t i = 0; i < n->u.invoke.n; i++) clj_release(args[i]);
	}
	buf_free(small, args);
	clj_release(fn);
	return result;
}

static clj_value eval_def(const clj_node *n, clj_frame *f) {
	if (n->u.def.init) {
		clj_value v = n->u.def.init->eval(n->u.def.init, f);
		if (v == CLJ_THROWN) return CLJ_THROWN;
		clj_var_bind_root(n->u.def.var, v);
		clj_release(v);
	}
	clj_var_set_macro(n->u.def.var, n->u.def.macro);
	return clj_retain(n->u.def.var);
}

static clj_value eval_vector(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *items = buf_alloc(small, n->u.seq.n);
	clj_value  result = CLJ_THROWN;
	if (eval_all(n->u.seq.items, n->u.seq.n, f, items)) {
		result = clj_vector_from_array(items, n->u.seq.n);
		for (uint32_t i = 0; i < n->u.seq.n; i++) clj_release(items[i]);
	}
	buf_free(small, items);
	return result;
}

static clj_value eval_map(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *items = buf_alloc(small, n->u.seq.n);
	clj_value  result = CLJ_THROWN;
	if (eval_all(n->u.seq.items, n->u.seq.n, f, items)) {
		result = clj_map_empty();
		for (uint32_t i = 0; i < n->u.seq.n; i += 2) {
			if (clj_map_contains(result, items[i])) {
				clj_value text = clj_pr_str(items[i]);
				clj_release(result);
				result = text == CLJ_THROWN ? CLJ_THROWN : clj_throw_msg("Duplicate key: %s", clj_string_bytes(text));
				clj_release(text);
				break;
			}
			result = clj_map_assoc(result, items[i], items[i + 1]);
		}
		for (uint32_t i = 0; i < n->u.seq.n; i++) clj_release(items[i]);
	}
	buf_free(small, items);
	return result;
}

// @ai-generated(guided)
static clj_value eval_throw(const clj_node *n, clj_frame *f) {
	clj_value v = n->u.throw_->eval(n->u.throw_, f);
	if (v == CLJ_THROWN) return CLJ_THROWN;
	return clj_throw(v);
}

static bool catch_matches(const clj_catch *c, clj_value ex) {
	return c->kind == CLJ_CATCH_ALL || clj_is_exception(ex);
}

// Unwinding is the ordinary return path: the body has already released its temporaries when CLJ_THROWN
// arrives here. finally runs on both paths; when it throws, its exception replaces the in-flight one.
// @ai-generated(guided)
static clj_value eval_try(const clj_node *n, clj_frame *f) {
	clj_value v = n->u.try_.body->eval(n->u.try_.body, f);
	CLJ_ASSERT(v != CLJ_RECUR, "recur escaped a try body");
	if (v == CLJ_THROWN && n->u.try_.ncatches) {
		clj_value ex = clj_take_pending();
		bool      handled = false;
		for (uint32_t i = 0; i < n->u.try_.ncatches && !handled; i++) {
			const clj_catch *c = &n->u.try_.catches[i];
			if (!catch_matches(c, ex)) continue;
			slot_set(f, c->slot, ex);
			v = c->handler->eval(c->handler, f);
			handled = true;
		}
		if (!handled) clj_throw(ex);
	}
	if (n->u.try_.finally_) {
		// The pending slot is free while finally runs; the in-flight exception is parked here.
		clj_value parked = v == CLJ_THROWN ? clj_take_pending() : CLJ_NIL;
		clj_value fv = n->u.try_.finally_->eval(n->u.try_.finally_, f);
		if (fv == CLJ_THROWN) {
			clj_release(v == CLJ_THROWN ? parked : v);
			return CLJ_THROWN;
		}
		clj_release(fv);
		if (v == CLJ_THROWN) clj_throw(parked);
	}
	return v;
}

clj_eval_fn clj_node_eval_fn(clj_node_kind kind) {
	switch (kind) {
	case CLJ_NODE_CONST: return eval_const;
	case CLJ_NODE_LOCAL: return eval_local;
	case CLJ_NODE_CAPTURED: return eval_captured;
	case CLJ_NODE_VAR: return eval_var;
	case CLJ_NODE_IF: return eval_if;
	case CLJ_NODE_DO: return eval_do;
	case CLJ_NODE_LET: return eval_let;
	case CLJ_NODE_LOOP: return eval_loop;
	case CLJ_NODE_RECUR: return eval_recur;
	case CLJ_NODE_FN: return eval_fn;
	case CLJ_NODE_INVOKE: return eval_invoke;
	case CLJ_NODE_DEF: return eval_def;
	case CLJ_NODE_VECTOR: return eval_vector;
	case CLJ_NODE_MAP: return eval_map;
	case CLJ_NODE_TRY: return eval_try;
	case CLJ_NODE_THROW: return eval_throw;
	}
	clj_fatal("unknown node kind");
}

// Lowest address the interpreter may still use on this thread; computed once per thread.
static _Thread_local char *stack_limit;

static bool stack_exhausted(void) {
	char here;
	if (!stack_limit) {
#if defined(__APPLE__)
		pthread_t self = pthread_self();
		char     *top = pthread_get_stackaddr_np(self);
		size_t    size = pthread_get_stacksize_np(self);
		size_t    margin = size / 4 < STACK_MARGIN ? size / 4 : STACK_MARGIN;
		stack_limit = top - size + margin;
#else
		// No portable stack bounds: assume the thread's default and that we are near the top.
		stack_limit = &here - (512 * 1024 - STACK_MARGIN);
#endif
	}
	return &here < stack_limit;
}

static clj_value arity_error(clj_value f, size_t n) {
	clj_value name = clj_fn_of(f)->name;
	clj_value text = clj_is_nil(name) ? clj_string_from_cstr("fn") : clj_pr_str(name);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("Wrong number of args (%zu) passed to: %s", n, clj_string_bytes(text));
	clj_release(text);
	return r;
}

clj_value clj_closure_invoke(clj_value f, const clj_value *args, size_t n) {
	const clj_fn       *fn = clj_fn_of(f);
	const clj_node     *code = clj_node_of(fn->code);
	const clj_fn_arity *arity = NULL;
	if (n <= CLJ_FN_MAX_FIXED) arity = code->u.fn.fixed[n];
	if (!arity && code->u.fn.variadic && n >= code->u.fn.variadic->nparams) arity = code->u.fn.variadic;
	if (!arity) return arity_error(f, n);
	if (stack_exhausted()) return clj_throw_msg("Stack overflow");

	clj_value  small[SMALL_SLOTS];
	clj_value *slots = small;
	if (arity->nslots > SMALL_SLOTS) {
		slots = malloc(arity->nslots * sizeof *slots);
		if (!slots) clj_fatal("out of memory");
	}
	memset(slots, 0, arity->nslots * sizeof *slots);
	for (uint32_t i = 0; i < arity->nparams; i++) slots[i] = clj_retain(args[i]);
	if (arity->variadic) slots[arity->nparams] = n > arity->nparams ? clj_list_from_array(args + arity->nparams, n - arity->nparams) : CLJ_NIL;
	if (arity->self_slot >= 0) slots[arity->self_slot] = clj_retain(f);

	clj_frame frame = {slots, (clj_value *)fn->env};
	clj_value v;
	for (;;) {
		v = arity->body->eval(arity->body, &frame);
		if (v != CLJ_RECUR) break;
	}
	for (uint32_t i = 0; i < arity->nslots; i++) clj_release(slots[i]);
	if (slots != small) free(slots);
	return v;
}

clj_value clj_eval_node(const clj_node *node, uint32_t nslots) {
	clj_value  small[SMALL_SLOTS];
	clj_value *slots = small;
	if (nslots > SMALL_SLOTS) {
		slots = malloc(nslots * sizeof *slots);
		if (!slots) clj_fatal("out of memory");
	}
	memset(slots, 0, nslots * sizeof *slots);
	clj_frame frame = {slots, NULL};
	clj_value v = node->eval(node, &frame);
	CLJ_ASSERT(v != CLJ_RECUR, "recur escaped its target");
	for (uint32_t i = 0; i < nslots; i++) clj_release(slots[i]);
	if (slots != small) free(slots);
	return v;
}

// Positions it after the head when form is (do ...); a macro may have produced any seq type.
static bool is_do_form(clj_value form, clj_seq_iter *it) {
	if (!clj_is_seq(form)) return false;
	*it = clj_seq_iter_start(form);
	clj_value head;
	if (!clj_seq_iter_next(it, &head)) return false;
	return clj_is_symbol(head) && clj_is_nil(clj_symbol_ns(head)) && strcmp(clj_string_bytes(clj_symbol_name(head)), "do") == 0;
}

// A top-level (do ...) is a sequence of top-level forms: a defmacro in it is visible to the next form.
clj_value clj_eval(clj_value form, const clj_env *env) {
	clj_value expanded = clj_macroexpand(form, env);
	if (expanded == CLJ_THROWN) return CLJ_THROWN;
	clj_value    v = CLJ_NIL;
	clj_seq_iter it;
	if (is_do_form(expanded, &it)) {
		clj_value item;
		while (clj_seq_iter_next(&it, &item)) {
			clj_release(v);
			v = clj_eval(item, env);
			if (v == CLJ_THROWN) break;
		}
		if (it.thrown) {
			clj_release(v);
			v = CLJ_THROWN;
		}
	} else {
		uint32_t  nslots;
		clj_node *node = clj_analyze(expanded, env, &nslots);
		v = node ? clj_eval_node(node, nslots) : CLJ_THROWN;
		if (node) clj_release(clj_from_ptr(node));
	}
	clj_release(expanded);
	return v;
}
