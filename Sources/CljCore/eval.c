// @ai-generated(guided)
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/core.h"
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
#include "shadow_internal.h"

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

static inline clj_value eval_child(const clj_node *n, clj_frame *f) { return f->exec->nodes[n->id].eval(n, f); }

// A local, captured or constant read without a retain: its home (the frame, the closure's env, the tree)
// outlives the consumer. Every other kind evaluates owned.
// @ai-generated(guided)
static inline clj_value eval_borrowed(const clj_node *n, clj_frame *f, bool *owned) {
	*owned = false;
	switch (n->kind) {
	case CLJ_NODE_LOCAL: return f->slots[n->u.index];
	case CLJ_NODE_CAPTURED: return f->captured[n->u.index];
	case CLJ_NODE_CONST: return n->u.value;
	default:
		*owned = true;
		return eval_child(n, f);
	}
}

static inline bool slot_owned(const clj_frame *f, uint32_t i) { return i >= 64 || (f->owned >> i) & 1; }

// Stores an owned value; the old one is released only when the frame owned it.
static inline void slot_set(clj_frame *f, uint32_t i, clj_value v) {
	clj_value old = f->slots[i];
	f->slots[i] = v;
	if (slot_owned(f, i)) {
		clj_release(old);
	} else {
		f->owned |= (uint64_t)1 << i;
	}
}

static inline void release_all(const clj_value *vals, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) clj_release(vals[i]);
}

// Releases the values whose bit is set; n > 64 means every one.
static inline void release_owned(const clj_value *vals, uint32_t n, uint64_t owned) {
	if (n > 64) {
		release_all(vals, n);
		return;
	}
	while (owned) {
		clj_release(vals[__builtin_ctzll(owned)]);
		owned &= owned - 1;
	}
}

static void slots_release(const clj_frame *f, uint32_t n) { release_owned(f->slots, n, f->owned); }

// Evaluates nodes into out; on failure releases what was already evaluated. With `owned` the reads that can
// be borrowed are, and the mask says which results the caller must release (n > 64: every one); NULL evaluates
// everything owned, for values that are stored.
// @ai-generated(guided)
// Always inlined: the borrow/owned branch folds away per call site.
static inline __attribute__((always_inline)) bool eval_all(const clj_node *const *nodes, uint32_t n, clj_frame *f, clj_value *out, uint64_t *owned) {
	uint64_t mask = 0;
	bool     borrow = owned && n <= 64;
	for (uint32_t i = 0; i < n; i++) {
		bool      own = true;
		clj_value v = borrow ? eval_borrowed(nodes[i], f, &own) : eval_child(nodes[i], f);
		if (v == CLJ_THROWN) {
			if (borrow) release_owned(out, i, mask);
			else release_all(out, i);
			return false;
		}
		if (borrow && own) mask |= (uint64_t)1 << i;
		out[i] = v;
	}
	if (owned) *owned = borrow ? mask : UINT64_MAX;
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
	bool      owned;
	clj_value test = eval_borrowed(n->u.if_.test, f, &owned);
	if (test == CLJ_THROWN) return CLJ_THROWN;
	bool truthy = clj_truthy(test);
	if (owned) clj_release(test);
	const clj_node *branch = truthy ? n->u.if_.then : n->u.if_.else_;
	return branch ? eval_child(branch, f) : CLJ_NIL;
}

static clj_value eval_do(const clj_node *n, clj_frame *f) {
	uint32_t last = n->u.seq.n - 1;
	for (uint32_t i = 0; i < last; i++) {
		bool      owned;
		clj_value v = eval_borrowed(n->u.seq.items[i], f, &owned);
		if (v == CLJ_THROWN) return CLJ_THROWN;
		if (owned) clj_release(v);
	}
	return eval_child(n->u.seq.items[last], f);
}

static bool bind_all(const clj_node *n, clj_frame *f) {
	for (uint32_t i = 0; i < n->u.let.n; i++) {
		clj_value v = eval_child(n->u.let.inits[i], f);
		if (v == CLJ_THROWN) return false;
		slot_set(f, n->u.let.slots[i], v);
	}
	return true;
}

static clj_value eval_let(const clj_node *n, clj_frame *f) {
	if (!bind_all(n, f)) return CLJ_THROWN;
	return eval_child(n->u.let.body, f);
}

// recur has already rebound the slots when the body yields CLJ_RECUR; the loop is a C loop, not a call.
static clj_value eval_loop(const clj_node *n, clj_frame *f) {
	if (!bind_all(n, f)) return CLJ_THROWN;
	for (;;) {
		clj_value v = eval_child(n->u.let.body, f);
		if (v != CLJ_RECUR) return v;
	}
}

// All arguments are evaluated before any slot changes, so (recur b a) swaps.
static clj_value eval_recur(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *vals = buf_alloc(small, n->u.recur.n);
	bool ok = eval_all(n->u.recur.args, n->u.recur.n, f, vals, NULL);
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
	clj_value fn = clj_fn_closure(clj_from_ptr((void *)f->exec), n, n->u.fn.name, env, n->u.fn.ncaptures);
	buf_free(small, env);
	return fn;
}

// A var in fn position stays owned: the +1 held here is what keeps a running body alive when a concurrent
// def replaces the var's root; borrowing it needs deferred freeing of old roots (NOTES.md).
static clj_value eval_invoke(const clj_node *n, clj_frame *f) {
	bool      fn_owned;
	clj_value fn = eval_borrowed(n->u.invoke.fn, f, &fn_owned);
	if (fn == CLJ_THROWN) return CLJ_THROWN;
	clj_value  small[SMALL_ARGS];
	clj_value *args = buf_alloc(small, n->u.invoke.n);
	clj_value  result = CLJ_THROWN;
	uint64_t   owned;
	if (eval_all(n->u.invoke.args, n->u.invoke.n, f, args, &owned)) {
		if (clj_is_fn(fn) && clj_fn_of(fn)->kind == CLJ_FN_CLOSURE) result = clj_closure_invoke_at(fn, args, n->u.invoke.n, n);
		else result = clj_invoke(fn, args, n->u.invoke.n);
		release_owned(args, n->u.invoke.n, owned);
	}
	buf_free(small, args);
	if (fn_owned) clj_release(fn);
	return result;
}

// Root first, then meta, then the flags, as DefExpr.eval does.
static clj_value eval_def(const clj_node *n, clj_frame *f) {
	if (n->u.def.init) {
		clj_value v = eval_child(n->u.def.init, f);
		if (v == CLJ_THROWN) return CLJ_THROWN;
		clj_var_bind_root(n->u.def.var, v);
		clj_release(v);
	}
	clj_value m = eval_child(n->u.def.meta, f);
	if (m == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_is_map(m)) {
		clj_value e = clj_throw_msg("def metadata must be a map, got: %s", clj_type_name(m));
		clj_release(m);
		return e;
	}
	clj_var_set_meta(n->u.def.var, m);
	clj_release(m);
	clj_var_set_macro(n->u.def.var, n->u.def.macro);
	clj_var_set_dynamic(n->u.def.var, n->u.def.dynamic);
	return clj_retain(n->u.def.var);
}

static clj_value eval_vector(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *items = buf_alloc(small, n->u.seq.n);
	clj_value  result = CLJ_THROWN;
	uint64_t   owned;
	if (eval_all(n->u.seq.items, n->u.seq.n, f, items, &owned)) {
		result = clj_vector_from_array(items, n->u.seq.n);
		release_owned(items, n->u.seq.n, owned);
	}
	buf_free(small, items);
	return result;
}

static clj_value eval_map(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *items = buf_alloc(small, n->u.seq.n);
	clj_value  result = CLJ_THROWN;
	uint64_t   owned;
	if (eval_all(n->u.seq.items, n->u.seq.n, f, items, &owned)) {
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
		release_owned(items, n->u.seq.n, owned);
	}
	buf_free(small, items);
	return result;
}

// @ai-generated(guided)
static clj_value eval_throw(const clj_node *n, clj_frame *f) {
	clj_value v = eval_child(n->u.throw_, f);
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
	clj_value v = eval_child(n->u.try_.body, f);
	CLJ_ASSERT(v != CLJ_RECUR, "recur escaped a try body");
	if (v == CLJ_THROWN && n->u.try_.ncatches) {
		clj_value trace = clj_take_pending_trace();
		clj_value ex = clj_take_pending();
		bool      handled = false;
		for (uint32_t i = 0; i < n->u.try_.ncatches && !handled; i++) {
			const clj_catch *c = &n->u.try_.catches[i];
			if (!catch_matches(c, ex)) continue;
			slot_set(f, c->slot, ex);
			v = eval_child(c->handler, f);
			handled = true;
		}
		if (handled) clj_release(trace);
		else clj_throw_traced(ex, trace);
	}
	if (n->u.try_.finally_) {
		// The pending slot is free while finally runs; the in-flight exception is parked here.
		clj_value parked_trace = v == CLJ_THROWN ? clj_take_pending_trace() : CLJ_NIL;
		clj_value parked = v == CLJ_THROWN ? clj_take_pending() : CLJ_NIL;
		clj_value fv = eval_child(n->u.try_.finally_, f);
		if (fv == CLJ_THROWN) {
			clj_release(v == CLJ_THROWN ? parked : v);
			clj_release(parked_trace);
			return CLJ_THROWN;
		}
		clj_release(fv);
		if (v == CLJ_THROWN) clj_throw_traced(parked, parked_trace);
	}
	return v;
}

static clj_eval_fn eval_fn_of(clj_node_kind kind) {
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

static void exec_each_child(void *self, clj_visitor visit, void *ctx) {
	const clj_exec *e = self;
	visit(clj_from_ptr((void *)e->root), ctx);
}

const clj_type clj_exec_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "exec",
	.each_child = exec_each_child,
};

typedef struct {
	clj_exec *exec;
	bool      top; // outside every fn: these slots belong to the root's frame
} build_ctx;

static void note_slot(build_ctx *b, uint32_t slot) {
	if (b->top && slot >= b->exec->nslots) b->exec->nslots = slot + 1;
}

// @ai-generated(guided)
static void build(const clj_node *n, void *ctx) {
	build_ctx *b = ctx;
	b->exec->nodes[n->id].eval = eval_fn_of(n->kind);
	switch (n->kind) {
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n; i++) note_slot(b, n->u.let.slots[i]);
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) note_slot(b, n->u.try_.catches[i].slot);
		break;
	case CLJ_NODE_FN: {
		build_ctx inner = {b->exec, false};
		clj_node_children(n, build, &inner);
		return;
	}
	default: break;
	}
	clj_node_children(n, build, b);
}

clj_value clj_exec_new(const clj_node *root) {
	CLJ_ASSERT(root->id == 0, "exec needs the root of a tree");
	clj_exec *e = clj_alloc(&clj_exec_type, sizeof *e + root->nnodes * sizeof *e->nodes);
	clj_retain(clj_from_ptr((void *)root));
	e->root = root;
	build_ctx b = {e, true};
	build(root, &b);
	return clj_from_ptr(e);
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

clj_value clj_closure_invoke(clj_value f, const clj_value *args, size_t n) { return clj_closure_invoke_at(f, args, n, NULL); }

clj_value clj_closure_invoke_at(clj_value f, const clj_value *args, size_t n, const clj_node *site) {
	const clj_fn       *fn = clj_fn_of(f);
	const clj_node     *code = fn->u.node;
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
	bool     big = arity->nslots > 64;
	uint64_t owned = big ? UINT64_MAX : 0;
	for (uint32_t i = 0; i < arity->nparams; i++) slots[i] = big ? clj_retain(args[i]) : args[i];
	if (arity->variadic) {
		slots[arity->nparams] = n > arity->nparams ? clj_list_from_array(args + arity->nparams, n - arity->nparams) : CLJ_NIL;
		owned |= (uint64_t)1 << arity->nparams;
	}
	if (arity->self_slot >= 0) slots[arity->self_slot] = big ? clj_retain(f) : f;

	clj_frame frame = {slots, (clj_value *)fn->env, clj_exec_of(fn->code), owned};
	clj_shadow_push(code, site);
	clj_value v;
	for (;;) {
		v = eval_child(arity->body, &frame);
		if (v != CLJ_RECUR) break;
	}
	clj_shadow_pop();
	slots_release(&frame, arity->nslots);
	if (slots != small) free(slots);
	return v;
}

clj_value clj_exec_run(clj_value exec) {
	const clj_exec *e = clj_exec_of(exec);
	uint32_t        nslots = e->nslots;
	clj_value       small[SMALL_SLOTS];
	clj_value      *slots = small;
	if (nslots > SMALL_SLOTS) {
		slots = malloc(nslots * sizeof *slots);
		if (!slots) clj_fatal("out of memory");
	}
	memset(slots, 0, nslots * sizeof *slots);
	clj_frame frame = {slots, NULL, e, 0};
	clj_value v = eval_child(e->root, &frame);
	CLJ_ASSERT(v != CLJ_RECUR, "recur escaped its target");
	slots_release(&frame, nslots);
	if (slots != small) free(slots);
	return v;
}

clj_value clj_eval_node(const clj_node *node) {
	clj_value exec = clj_exec_new(node);
	clj_value v = clj_exec_run(exec);
	clj_release(exec);
	return v;
}

// Positions it after the head when form is (do ...); a macro may have produced any seq type.
static bool is_do_form(clj_value form, clj_seq_iter *it) {
	if (!clj_is_seq(form)) return false;
	*it = clj_seq_iter_start(form);
	clj_value head;
	if (!clj_seq_iter_next(it, &head)) return false;
	if (clj_is_symbol(head) && clj_is_nil(clj_symbol_ns(head)) && strcmp(clj_string_bytes(clj_symbol_name(head)), "do") == 0) return true;
	clj_seq_iter_close(it);
	return false;
}

// A top-level (do ...) is a sequence of top-level forms: a defmacro in it is visible to the next form.
// Without a position in env the form's own :line/:column stand in, so the expansion's errors keep them.
clj_value clj_eval(clj_value form, const clj_env *given) {
	clj_env local = given ? *given : (clj_env){0};
	if (!local.line) clj_form_position(form, &local.line, &local.col);
	const clj_env *env = &local;
	clj_value      expanded = clj_macroexpand(form, env);
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
		clj_seq_iter_close(&it);
		if (it.thrown) {
			clj_release(v);
			v = CLJ_THROWN;
		}
	} else {
		const clj_node *node = clj_analyze(expanded, env);
		v = node ? clj_eval_node(node) : CLJ_THROWN;
		if (node) clj_release(clj_from_ptr((void *)node));
	}
	clj_release(expanded);
	return v;
}
