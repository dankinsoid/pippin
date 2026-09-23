// @ai-generated(guided)
// Runs before numbering, so the rewrites are what serializes and what every backend sees.
#include <stdlib.h>
#include <string.h>

#include "clj/analyzer.h"
#include "clj/error.h"
#include "clj/fusion.h"
#include "clj/intrinsics.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "node.h"

static void *zalloc(size_t n, size_t size) {
	void *p = calloc(n ? n : 1, size);
	if (!p) clj_fatal("out of memory");
	return p;
}

// Keyed by the var the head resolved to: a local shadowing the name or a user namespace's own `+` stays an
// INVOKE. The var is kept for the runtime guard (eval.c), the VAR node itself goes.
static void rewrite_invoke(clj_node *n) {
	const clj_node *head = n->u.invoke.fn;
	if (head->kind != CLJ_NODE_VAR) return;
	const clj_intrinsic *op = clj_intrinsic_find(head->u.var, n->u.invoke.n);
	if (!op) return;
	const clj_node **args = n->u.invoke.args;
	uint32_t         nargs = n->u.invoke.n;
	clj_value        var = clj_retain(head->u.var);
	clj_release(clj_from_ptr((void *)head));
	n->kind = CLJ_NODE_INTRINSIC;
	n->u.intrinsic.op = op;
	n->u.intrinsic.var = var;
	n->u.intrinsic.args = args;
	n->u.intrinsic.n = nargs;
}

// ---- fusion: (reduce f [init] P), (into to P), (vec P), (count P), P a nest of stages over a source

enum { MAX_STAGES = 64 };

typedef struct {
	const clj_fusion_var *fv;
	const clj_node       *call; // the stage's INVOKE
} stage;

typedef struct {
	const clj_fusion_var *fv;
	const clj_node       *consumer;
	stage                 stages[MAX_STAGES];
	uint32_t              nstages;
	const clj_node       *source;
} pipeline;

static const clj_fusion_var *fusion_head(const clj_node *n, clj_fusion_role role) {
	if (n->kind != CLJ_NODE_INVOKE || n->u.invoke.fn->kind != CLJ_NODE_VAR) return NULL;
	const clj_fusion_var *fv = clj_fusion_find(n->u.invoke.fn->u.var);
	return fv && fv->role == role ? fv : NULL;
}

static bool consumer_arity_ok(const clj_fusion_var *fv, uint32_t n) {
	const char *name = strchr(fv->name, '/') + 1;
	if (strcmp(name, "reduce") == 0) return n == 2 || n == 3;
	if (strcmp(name, "into") == 0) return n == 2;
	return n == 1; // vec, count
}

static bool match(const clj_node *n, pipeline *p) {
	p->fv = fusion_head(n, CLJ_FUSION_CONSUMER);
	if (!p->fv || !consumer_arity_ok(p->fv, n->u.invoke.n)) return false;
	p->consumer = n;
	p->nstages = 0;
	const clj_node *coll = n->u.invoke.args[n->u.invoke.n - 1];
	for (;;) {
		const clj_fusion_var *fv = fusion_head(coll, CLJ_FUSION_STAGE);
		if (!fv || fv->arity != coll->u.invoke.n || p->nstages == MAX_STAGES) break;
		p->stages[p->nstages++] = (stage){fv, coll};
		coll = coll->u.invoke.args[coll->u.invoke.n - 1];
	}
	p->source = coll;
	return p->nstages > 0;
}

typedef struct {
	uint32_t line, col;
} position;

static clj_node *node_at(clj_node_kind kind, position at) {
	clj_node *n = clj_node_alloc(kind);
	n->line = at.line;
	n->col = at.col;
	return n;
}

static clj_node *local_node(uint32_t index, position at) {
	clj_node *n = node_at(CLJ_NODE_LOCAL, at);
	n->u.local.index = index;
	return n;
}

static clj_node *var_node(clj_value var, position at) {
	clj_node *n = node_at(CLJ_NODE_VAR, at);
	n->u.var = clj_retain(var);
	return n;
}

static clj_node *const_node(clj_value v, position at) {
	clj_node *n = node_at(CLJ_NODE_CONST, at);
	n->u.value = clj_retain(v);
	return n;
}

// Takes ownership of the args array and its nodes.
static clj_node *invoke_node(clj_node *fn, const clj_node **args, uint32_t n, position at) {
	clj_node *node = node_at(CLJ_NODE_INVOKE, at);
	node->u.invoke.fn = fn;
	node->u.invoke.args = args;
	node->u.invoke.n = n;
	return node;
}

static clj_node *vector_node(const clj_node **items, uint32_t n, position at) {
	clj_node *node = node_at(CLJ_NODE_VECTOR, at);
	node->u.seq.items = items;
	node->u.seq.n = n;
	return node;
}

static const clj_fusion_var *driver_for(const clj_fusion_var *consumer) {
	const char *name = strchr(consumer->name, '/') + 1;
	clj_value   sym = clj_symbol_from_cstr(strcmp(name, "reduce") == 0 ? "clojure.core/fused-reduce*"
	                                     : strcmp(name, "count") == 0 ? "clojure.core/fused-count*"
	                                                                    : "clojure.core/fused-into*");
	const clj_fusion_var *fv = clj_fusion_find_named(sym);
	clj_release(sym);
	CLJ_ASSERT(fv, "fusion driver missing");
	return fv;
}

// (driver consumer-args... source [(stage args...)...]); vec's `to` is the literal [].
static clj_node *build_fused(const pipeline *p, uint32_t nlead, uint32_t nargs, position at) {
	const clj_fusion_var *driver = driver_for(p->fv);
	bool             is_vec = strcmp(strchr(p->fv->name, '/') + 1, "vec") == 0;
	uint32_t         n = nlead + (is_vec ? 1 : 0) + 2, k = 0;
	const clj_node **args = zalloc(n, sizeof *args);
	if (is_vec) args[k++] = const_node(clj_vector_empty(), at);
	for (uint32_t i = 0; i < nlead; i++) args[k++] = local_node(i, at);
	args[k++] = local_node(nargs - 1, at);
	const clj_node **xfs = zalloc(p->nstages, sizeof *xfs);
	uint32_t         slot = nlead;
	for (uint32_t i = 0; i < p->nstages; i++) {
		uint32_t         nx = p->stages[i].fv->arity - 1;
		const clj_node **xargs = zalloc(nx, sizeof *xargs);
		for (uint32_t j = 0; j < nx; j++) xargs[j] = local_node(slot++, at);
		xfs[i] = invoke_node(var_node(clj_fusion_var_of(p->stages[i].fv), at), xargs, nx, at);
	}
	args[k++] = vector_node(xfs, p->nstages, at);
	return invoke_node(var_node(clj_fusion_var_of(driver), at), args, n, at);
}

// (consumer consumer-args... (stage1 args... (stage2 args... source)))
static clj_node *build_original(const pipeline *p, uint32_t nlead, uint32_t nargs, position at) {
	uint32_t  slot = nlead;
	clj_node *inner = local_node(nargs - 1, at);
	uint32_t  starts[MAX_STAGES];
	for (uint32_t i = 0; i < p->nstages; i++) {
		starts[i] = slot;
		slot += p->stages[i].fv->arity - 1;
	}
	for (uint32_t i = p->nstages; i-- > 0;) {
		uint32_t         nx = p->stages[i].fv->arity - 1;
		const clj_node **args = zalloc(nx + 1, sizeof *args);
		for (uint32_t j = 0; j < nx; j++) args[j] = local_node(starts[i] + j, at);
		args[nx] = inner;
		inner = invoke_node(var_node(clj_fusion_var_of(p->stages[i].fv), at), args, nx + 1, at);
	}
	const clj_node **args = zalloc(nlead + 1, sizeof *args);
	for (uint32_t i = 0; i < nlead; i++) args[i] = local_node(i, at);
	args[nlead] = inner;
	return invoke_node(var_node(clj_fusion_var_of(p->fv), at), args, nlead + 1, at);
}

static void add_guard(const clj_fusion_var **guards, uint32_t *n, const clj_fusion_var *fv) {
	for (uint32_t i = 0; i < *n; i++) {
		if (guards[i] == fv) return;
	}
	guards[(*n)++] = fv;
}

// @ai-generated(guided)
static bool rewrite_fused(clj_node *n) {
	pipeline p;
	if (!clj_fusion_enabled() || !match(n, &p)) return false;
	position at = {n->line, n->col};
	uint32_t nlead = n->u.invoke.n - 1, nargs = nlead + 1;
	for (uint32_t i = 0; i < p.nstages; i++) nargs += p.stages[i].fv->arity - 1;
	// Args keep the original evaluation order: consumer args, each stage's own args consumer-first, the source.
	const clj_node **args = zalloc(nargs, sizeof *args);
	uint32_t         k = 0;
	for (uint32_t i = 0; i < nlead; i++) args[k++] = n->u.invoke.args[i];
	for (uint32_t i = 0; i < p.nstages; i++) {
		const clj_node *call = p.stages[i].call;
		for (uint32_t j = 0; j + 1 < call->u.invoke.n; j++) args[k++] = call->u.invoke.args[j];
	}
	args[k++] = p.source;
	CLJ_ASSERT(k == nargs, "fusion argument count");
	// The leading args keep the reference the consumer held; the rest are still owned by the stage calls released below.
	for (uint32_t i = nlead; i < nargs; i++) clj_retain(clj_from_ptr((void *)args[i]));
	const clj_fusion_var **guards = zalloc(p.nstages + 2, sizeof *guards);
	uint32_t               nguards = 0;
	add_guard(guards, &nguards, p.fv);
	for (uint32_t i = 0; i < p.nstages; i++) add_guard(guards, &nguards, p.stages[i].fv);
	add_guard(guards, &nguards, driver_for(p.fv));
	clj_node *fused = build_fused(&p, nlead, nargs, at);
	clj_node *original = build_original(&p, nlead, nargs, at);
	clj_release(clj_from_ptr((void *)n->u.invoke.fn));
	clj_release(clj_from_ptr((void *)n->u.invoke.args[nlead]));
	free(n->u.invoke.args);
	n->kind = CLJ_NODE_FUSED;
	n->u.fused.guards = guards;
	n->u.fused.nguards = nguards;
	n->u.fused.args = args;
	n->u.fused.nargs = nargs;
	n->u.fused.fused = fused;
	n->u.fused.original = original;
	return true;
}

// ---- direct local fns (design §6b item 7): a let/loop-bound fn whose binding is only ever the head of a
// call runs without a closure. The rule is decided here, on the analyzed tree, because macros decide what
// a use is: a syntactic scan of the forms would take `(m (f 1))` for a head use before `m` expands it.
//
// The binding at `slot` of the defining frame escapes when any reference to it is not the head of an
// INVOKE with an arity the fn has as fixed: an argument, a value returned, a recur argument or target, an
// init of a later binding, a capture of a closure. A reference inside an inner *direct* fn body reaches
// the slot through the static link (`depth` frames up) and is a head use like any other, so helpers may
// call helpers; a self-reference through the fn's own name is the same rule on the fn's self slot. A
// variadic arity keeps the closure (the rest list would need a buffer), and so does a call past
// CLJ_FN_MAX_FIXED arguments.

typedef struct {
	uint32_t   slot;
	uint32_t   base; // added to a head's depth: 1 when scanning the fn's own body for its self slot
	clj_node **heads;
	uint32_t  *depths;
	uint32_t   nheads, cap;
	bool       escapes;
} direct_scan;

static bool refers(const clj_node *n, uint32_t depth, uint32_t slot) {
	if (n->kind == CLJ_NODE_LOCAL) return depth == 0 && n->u.local.index == slot;
	if (n->kind == CLJ_NODE_OUTER) return n->u.outer.depth == depth && n->u.outer.index == slot;
	return false;
}

static void record_head(direct_scan *d, const clj_node *invoke, uint32_t depth) {
	if (d->nheads == d->cap) {
		d->cap = d->cap ? d->cap * 2 : 8;
		d->heads = realloc(d->heads, d->cap * sizeof *d->heads);
		d->depths = realloc(d->depths, d->cap * sizeof *d->depths);
		if (!d->heads || !d->depths) clj_fatal("out of memory");
	}
	d->heads[d->nheads] = (clj_node *)invoke;
	d->depths[d->nheads++] = depth + d->base;
}

typedef struct {
	direct_scan    *d;
	const clj_node *fn;
	uint32_t        depth;
} scan_ctx;

static void scan(const clj_node *n, void *ctx);

static void scan_at(const clj_node *n, const scan_ctx *c, uint32_t depth) {
	scan_ctx inner = {c->d, c->fn, depth};
	scan(n, &inner);
}

static void scan_arities(const clj_node *fn, const scan_ctx *c, uint32_t depth) {
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (fn->u.fn.fixed[i]) scan_at(fn->u.fn.fixed[i]->body, c, depth);
	}
	if (fn->u.fn.variadic) scan_at(fn->u.fn.variadic->body, c, depth);
}

// @ai-generated(guided)
static void scan(const clj_node *n, void *ctx) {
	const scan_ctx *c = ctx;
	direct_scan    *d = c->d;
	if (d->escapes) return;
	switch (n->kind) {
	case CLJ_NODE_LOCAL:
	case CLJ_NODE_OUTER:
		if (refers(n, c->depth, d->slot)) d->escapes = true;
		return;
	case CLJ_NODE_INVOKE:
		if (refers(n->u.invoke.fn, c->depth, d->slot)) {
			if (n->u.invoke.n > CLJ_FN_MAX_FIXED || !c->fn->u.fn.fixed[n->u.invoke.n]) {
				d->escapes = true;
				return;
			}
			record_head(d, n, c->depth);
		} else {
			scan(n->u.invoke.fn, ctx);
		}
		for (uint32_t i = 0; i < n->u.invoke.n && !d->escapes; i++) scan(n->u.invoke.args[i], ctx);
		return;
	case CLJ_NODE_RECUR:
		if (c->depth == 0) {
			for (uint32_t i = 0; i < n->u.recur.n; i++) {
				if (n->u.recur.slots[i] == d->slot) d->escapes = true;
			}
		}
		break;
	case CLJ_NODE_FN:
		// A closure's body reads its own environment: a reference to the slot shows up as a capture.
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cap = &n->u.fn.captures[i];
			if ((cap->kind == CLJ_CAPTURE_LOCAL && c->depth == 0) || (cap->kind == CLJ_CAPTURE_OUTER && cap->depth == c->depth)) {
				if (cap->index == d->slot) d->escapes = true;
			}
		}
		return;
	case CLJ_NODE_DIRECT_FN: scan_arities(n, c, c->depth + 1); return;
	case CLJ_NODE_FUSED:
		// The two programs read a frame of their own; only the args run in this one.
		for (uint32_t i = 0; i < n->u.fused.nargs && !d->escapes; i++) scan(n->u.fused.args[i], ctx);
		return;
	default: break;
	}
	clj_node_children(n, scan, ctx);
}

// ---- conversion: the body was analyzed as a closure over `captures`; every read of that environment
// becomes a read of the defining frame through the static link (a captured value of the definer stays a
// captured read: the direct frame shares the definer's environment). Inner closures keep their bodies and
// have their capture sources remapped the same way; inner direct fns are one link deeper.

typedef struct {
	const clj_capture *captures;
	uint32_t           depth;
} remap_ctx;

static clj_capture remap_capture(const clj_capture *table, uint32_t depth, const clj_capture *c) {
	if (c->kind != CLJ_CAPTURE_CAPTURED) return *c;
	const clj_capture *t = &table[c->index];
	switch (t->kind) {
	case CLJ_CAPTURE_LOCAL: return (clj_capture){CLJ_CAPTURE_OUTER, depth + 1, t->index};
	case CLJ_CAPTURE_CAPTURED: return (clj_capture){CLJ_CAPTURE_CAPTURED, 0, t->index};
	case CLJ_CAPTURE_OUTER: return (clj_capture){CLJ_CAPTURE_OUTER, depth + 1 + t->depth, t->index};
	}
	clj_fatal("unknown capture kind");
}

static void remap(const clj_node *n, void *ctx);

static void remap_arities(const clj_node *fn, const remap_ctx *c, uint32_t depth) {
	remap_ctx inner = {c->captures, depth};
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (fn->u.fn.fixed[i]) remap(fn->u.fn.fixed[i]->body, &inner);
	}
	if (fn->u.fn.variadic) remap(fn->u.fn.variadic->body, &inner);
}

// @ai-generated(guided)
static void remap(const clj_node *n, void *ctx) {
	const remap_ctx *c = ctx;
	clj_node        *m = (clj_node *)n;
	switch (n->kind) {
	case CLJ_NODE_CAPTURED: {
		clj_capture from = {CLJ_CAPTURE_CAPTURED, 0, n->u.index};
		clj_capture to = remap_capture(c->captures, c->depth, &from);
		if (to.kind == CLJ_CAPTURE_OUTER) {
			m->kind = CLJ_NODE_OUTER;
			m->u.outer.depth = to.depth;
			m->u.outer.index = to.index;
		} else {
			m->u.index = to.index;
		}
		return;
	}
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) m->u.fn.captures[i] = remap_capture(c->captures, c->depth, &n->u.fn.captures[i]);
		return;
	case CLJ_NODE_DIRECT_FN: remap_arities(n, c, c->depth + 1); return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) remap(n->u.fused.args[i], ctx);
		return;
	default: break;
	}
	clj_node_children(n, remap, ctx);
}

static void rewrite_direct_call(clj_node *invoke, const clj_node *fn, uint32_t slot, uint32_t depth) {
	const clj_node **args = invoke->u.invoke.args;
	uint32_t         nargs = invoke->u.invoke.n;
	clj_release(clj_from_ptr((void *)invoke->u.invoke.fn));
	invoke->kind = CLJ_NODE_DIRECT_CALL;
	invoke->u.direct.fn = fn;
	invoke->u.direct.arity = fn->u.fn.fixed[nargs];
	invoke->u.direct.args = args;
	invoke->u.direct.n = nargs;
	invoke->u.direct.slot = slot;
	invoke->u.direct.depth = depth;
}

// Binding i of a let/loop: its later inits and the body at depth 0, each arity's body for the self slot.
// @ai-generated(guided)
static void try_direct(clj_node *let, uint32_t i) {
	clj_node *fn = (clj_node *)let->u.let.inits[i];
	if (fn->kind != CLJ_NODE_FN || fn->u.fn.variadic) return;
	uint32_t    slot = let->u.let.slots[i];
	direct_scan d = {.slot = slot};
	scan_ctx    c = {&d, fn, 0};
	for (uint32_t k = i + 1; k < let->u.let.n && !d.escapes; k++) scan(let->u.let.inits[k], &c);
	if (!d.escapes) scan(let->u.let.body, &c);
	for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED && !d.escapes; k++) {
		const clj_fn_arity *a = fn->u.fn.fixed[k];
		if (!a || a->self_slot < 0) continue;
		d.slot = (uint32_t)a->self_slot;
		d.base = 1;
		scan(a->body, &c);
	}
	if (!d.escapes) {
		remap_ctx c = {fn->u.fn.captures, 0};
		remap_arities(fn, &c, 0);
		for (uint32_t k = 0; k < d.nheads; k++) rewrite_direct_call(d.heads[k], fn, slot, d.depths[k]);
		fn->kind = CLJ_NODE_DIRECT_FN;
		free(fn->u.fn.captures);
		fn->u.fn.captures = NULL;
		fn->u.fn.ncaptures = 0;
	}
	free(d.heads);
	free(d.depths);
}

// Children first, so an inner binding is decided before the outer one whose fn it may call; the bindings
// of one let from the last to the first, since a later one may reference an earlier one.
static void direct_pass(const clj_node *n, void *ctx) {
	clj_node_children(n, direct_pass, ctx);
	if (n->kind != CLJ_NODE_LET && n->kind != CLJ_NODE_LOOP) return;
	for (uint32_t i = n->u.let.n; i-- > 0;) try_direct((clj_node *)n, i);
}

// ---- constant folding (design §6b item 4): a pure intrinsic on constant arguments, an `if` on a constant test

static bool const_arg(const clj_node *n, clj_value *out) {
	if (n->kind != CLJ_NODE_CONST || !clj_node_foldable(n->u.value)) return false;
	*out = n->u.value;
	return true;
}

// A var rebound before analysis keeps the guarded call; a rebind after it does not unfold (Clojure's :inline
// speculates the same way). A fold that throws leaves the node, so the program throws where it would have.
// @ai-generated(guided)
static void fold_intrinsic(clj_node *n) {
	const clj_intrinsic *op = n->u.intrinsic.op;
	clj_value            vals[3];
	if (!op->pure || clj_var_root(n->u.intrinsic.var) != clj_intrinsic_builtin(op)) return;
	for (uint32_t i = 0; i < n->u.intrinsic.n; i++) {
		if (!const_arg(n->u.intrinsic.args[i], &vals[i])) return;
	}
	clj_value r = clj_intrinsic_call(op, vals);
	if (r == CLJ_THROWN) {
		clj_release(clj_take_pending());
		return;
	}
	if (!clj_node_foldable(r)) {
		clj_release(r);
		return;
	}
	for (uint32_t i = 0; i < n->u.intrinsic.n; i++) clj_release(clj_from_ptr((void *)n->u.intrinsic.args[i]));
	free(n->u.intrinsic.args);
	clj_release(n->u.intrinsic.var);
	n->kind = CLJ_NODE_CONST;
	n->u.value = r;
}

// The taken branch's contents move into n, which the parent points at (only a DIRECT_FN is referenced by
// pointer, and one never sits in a branch); the emptied branch dies as a nil constant.
// @ai-generated(guided)
static void fold_if(clj_node *n) {
	const clj_node *test = n->u.if_.test;
	if (test->kind != CLJ_NODE_CONST) return;
	bool      truthy = clj_truthy(test->u.value);
	clj_node *taken = (clj_node *)(truthy ? n->u.if_.then : n->u.if_.else_);
	clj_node *dropped = (clj_node *)(truthy ? n->u.if_.else_ : n->u.if_.then);
	clj_release(clj_from_ptr((void *)test));
	if (dropped) clj_release(clj_from_ptr(dropped));
	if (!taken) {
		n->kind = CLJ_NODE_CONST;
		n->u.value = CLJ_NIL;
		return;
	}
	n->kind = taken->kind;
	n->u = taken->u;
	n->line = taken->line;
	n->col = taken->col;
	taken->kind = CLJ_NODE_CONST;
	taken->u.value = CLJ_NIL;
	clj_release(clj_from_ptr(taken));
}

// The tree is still the analyzer's own here: the const on the visitor's argument is dropped once. Children
// first, so a fold sees folded arguments.
static void optimize(const clj_node *n, void *ctx) {
	clj_node *m = (clj_node *)n;
	if (m->kind == CLJ_NODE_INVOKE && !rewrite_fused(m)) rewrite_invoke(m);
	if (m->kind == CLJ_NODE_FUSED) {
		for (uint32_t i = 0; i < m->u.fused.nargs; i++) optimize(m->u.fused.args[i], ctx);
		optimize(m->u.fused.fused, ctx);
		// A consumer over locals cannot match again; its head may still be an intrinsic (count).
		clj_node *original = (clj_node *)m->u.fused.original;
		rewrite_invoke(original);
		clj_node_children(original, optimize, ctx);
		return;
	}
	clj_node_children(m, optimize, ctx);
	if (m->kind == CLJ_NODE_INTRINSIC) fold_intrinsic(m);
	else if (m->kind == CLJ_NODE_IF) fold_if(m);
}

// ---- last-use marks (design §6b item 4, "reuse"): a LOCAL read after which its slot is dead on every path
// is flagged `last`, and the evaluator hands the frame's reference to the consumer instead of borrowing it,
// so a conj/assoc on a local nobody else holds runs in place. Backward liveness over the evaluation order of
// one frame (the top level, a fn arity, a direct fn arity), bitsets of the first 64 slots (a higher slot is
// never marked). A loop or a fn body with a recur is a fixpoint: a recur's live-out is the body's live-in minus
// the slots it rebinds, so a value only the next iteration reads is live across the recur and dead on the exit
// path. A try body keeps everything its handlers and finally read live at every point, since any point may
// throw. A slot a direct fn body reads through the static link is pinned live for the whole frame: the body
// runs whenever the fn is called. Marking happens in one final pass per frame with the converged sets.

typedef struct {
	uint64_t slots;   // rebound by a recur of this target
	uint64_t body_in; // live at the target body's entry, from the fixpoint
} live_target;

typedef struct {
	uint64_t     held;  // never marked, whatever the live sets say: pinned by a direct fn body, or borrowed by an earlier operand
	live_target *recur; // innermost loop or the arity
	bool         mark;
} live_ctx;

// Whether a read's value is one the consumer can own: an owned position (a let init, a recur argument, a body's
// value: eval_child) or the collection of a consuming intrinsic. A borrowed operand elsewhere (an `inc`
// argument, a literal item, an `if` test) is marked nothing: the hand-over would cost the slot write and the
// release for a +1 nobody uses. Call arguments are borrowed too: handing them into the callee's frame let a
// callee update in place but cost ~2 ns per call on fixnum arguments (bench/RESULTS.md, "Last-use reuse").
typedef enum { LIVE_BORROWED, LIVE_OWNED } live_use;

static uint64_t slot_bit(uint32_t slot) { return slot < 64 ? (uint64_t)1 << slot : 0; }

static uint64_t live(const clj_node *n, uint64_t out, live_ctx *c, live_use use);

// Evaluated left to right, so the live sets flow from the last one back.
static uint64_t live_seq(const clj_node *const *nodes, uint32_t n, uint64_t out, live_ctx *c, live_use use) {
	while (n-- > 0) out = live(nodes[n], out, c, use);
	return out;
}

static bool is_local(const clj_node *n) { return n->kind == CLJ_NODE_LOCAL; }

// The operands of a call or a literal (eval_all with borrowing): a direct local operand is read at +0 and used
// only once every later operand has run, so nothing inside a later operand may hand its slot over — that would
// free what the earlier operand still points at. Held, not live: the sets stay exact, only the mark is withheld.
// `head` is the fn position of an invoke, evaluated first; the first `owned` operands go to a consumer.
static uint64_t live_args(const clj_node *head, const clj_node *const *nodes, uint32_t n, uint32_t owned, uint64_t out, live_ctx *c) {
	uint64_t  small[16];
	uint64_t *before = n <= 16 ? small : zalloc(n, sizeof *before);
	uint64_t  borrowed = head && is_local(head) ? slot_bit(head->u.local.index) : 0, held = c->held;
	for (uint32_t i = 0; i < n; i++) {
		before[i] = borrowed;
		if (is_local(nodes[i])) borrowed |= slot_bit(nodes[i]->u.local.index);
	}
	for (uint32_t i = n; i-- > 0;) {
		c->held = held | before[i];
		out = live(nodes[i], out, c, i < owned ? LIVE_OWNED : LIVE_BORROWED);
	}
	c->held = held;
	if (before != small) free(before);
	return head ? live(head, out, c, LIVE_BORROWED) : out;
}

// The body as often as it takes for its live-in to settle, with marking off; the caller marks in one more pass.
static uint64_t live_fixpoint(const clj_node *body, uint64_t out, live_target *t, live_ctx *c) {
	live_target *saved = c->recur;
	bool         mark = c->mark;
	c->recur = t;
	c->mark = false;
	for (;;) {
		uint64_t in = live(body, out, c, LIVE_OWNED);
		if (in == t->body_in) break;
		t->body_in = in;
	}
	c->mark = mark;
	uint64_t in = mark ? live(body, out, c, LIVE_OWNED) : t->body_in;
	c->recur = saved;
	return in;
}

static uint64_t live_bindings(const clj_node *n, uint64_t in, live_ctx *c) {
	for (uint32_t i = n->u.let.n; i-- > 0;) {
		in &= ~slot_bit(n->u.let.slots[i]);
		in = live(n->u.let.inits[i], in, c, LIVE_OWNED);
	}
	return in;
}

// @ai-generated(guided)
static uint64_t live(const clj_node *n, uint64_t out, live_ctx *c, live_use use) {
	switch (n->kind) {
	case CLJ_NODE_LOCAL: {
		uint64_t b = slot_bit(n->u.local.index);
		if (c->mark && use == LIVE_OWNED && b && !(out & b) && !(c->held & b)) ((clj_node *)n)->u.local.last = true;
		return out | b;
	}
	case CLJ_NODE_IF: {
		uint64_t then = live(n->u.if_.then, out, c, use);
		uint64_t else_ = n->u.if_.else_ ? live(n->u.if_.else_, out, c, use) : out;
		return live(n->u.if_.test, then | else_, c, LIVE_BORROWED);
	}
	case CLJ_NODE_DO: {
		uint32_t last = n->u.seq.n - 1;
		out = live(n->u.seq.items[last], out, c, use);
		return live_seq(n->u.seq.items, last, out, c, LIVE_BORROWED);
	}
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET: return live_args(NULL, n->u.seq.items, n->u.seq.n, 0, out, c);
	case CLJ_NODE_LET: return live_bindings(n, live(n->u.let.body, out, c, use), c);
	case CLJ_NODE_LOOP: {
		live_target t = {0, 0};
		for (uint32_t i = 0; i < n->u.let.n; i++) t.slots |= slot_bit(n->u.let.slots[i]);
		return live_bindings(n, live_fixpoint(n->u.let.body, out, &t, c), c);
	}
	case CLJ_NODE_RECUR: return live_seq(n->u.recur.args, n->u.recur.n, c->recur->body_in & ~c->recur->slots, c, LIVE_OWNED);
	case CLJ_NODE_INVOKE: return live_args(n->u.invoke.fn, n->u.invoke.args, n->u.invoke.n, 0, out, c);
	case CLJ_NODE_OBJC_SEND: return live_args(n->u.objc.target, n->u.objc.args, n->u.objc.n, 0, out, c);
	case CLJ_NODE_INTRINSIC: return live_args(NULL, n->u.intrinsic.args, n->u.intrinsic.n, clj_intrinsic_consumes(n->u.intrinsic.op) ? 1 : 0, out, c);
	case CLJ_NODE_DIRECT_CALL: return live_args(NULL, n->u.direct.args, n->u.direct.n, 0, out, c);
	case CLJ_NODE_FUSED: return live_args(NULL, n->u.fused.args, n->u.fused.nargs, 0, out, c); // the programs read a frame of their own
	case CLJ_NODE_DEF: {
		out = live(n->u.def.meta, out, c, LIVE_OWNED);
		return n->u.def.init ? live(n->u.def.init, out, c, LIVE_OWNED) : out;
	}
	case CLJ_NODE_THROW: return live(n->u.throw_, out, c, LIVE_OWNED);
	case CLJ_NODE_TRY: {
		uint64_t after = n->u.try_.finally_ ? live(n->u.try_.finally_, out, c, LIVE_BORROWED) : out;
		uint64_t handlers = 0;
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
			const clj_catch *k = &n->u.try_.catches[i];
			handlers |= live(k->handler, after, c, use) & ~slot_bit(k->slot);
		}
		return live(n->u.try_.body, after | handlers, c, use);
	}
	case CLJ_NODE_FN: // a capture reads the slot when the closure is made; the body is a frame of its own
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			if (n->u.fn.captures[i].kind == CLJ_CAPTURE_LOCAL) out |= slot_bit(n->u.fn.captures[i].index);
		}
		return out;
	case CLJ_NODE_CONST:
	case CLJ_NODE_CAPTURED:
	case CLJ_NODE_OUTER:
	case CLJ_NODE_VAR:
	case CLJ_NODE_DIRECT_FN: return out;
	}
	clj_fatal("unknown node kind");
}

typedef struct {
	uint32_t  depth; // direct fn bodies entered below the frame
	uint64_t *pinned;
} pin_ctx;

// Slots of the frame that a direct fn body below it reads through the static link, or a closure made there captures.
static void pin_scan(const clj_node *n, void *ctx) {
	pin_ctx *p = ctx;
	switch (n->kind) {
	case CLJ_NODE_OUTER:
		if (n->u.outer.depth == p->depth) *p->pinned |= slot_bit(n->u.outer.index);
		return;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cap = &n->u.fn.captures[i];
			if (cap->kind == CLJ_CAPTURE_OUTER && cap->depth == p->depth) *p->pinned |= slot_bit(cap->index);
		}
		return;
	case CLJ_NODE_DIRECT_FN: {
		pin_ctx inner = {p->depth + 1, p->pinned};
		clj_node_children(n, pin_scan, &inner);
		return;
	}
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) pin_scan(n->u.fused.args[i], ctx);
		return;
	default: clj_node_children(n, pin_scan, ctx);
	}
}

// One frame: its body's live-out is empty (the value is owned by whoever evaluates the body).
static void live_frame(const clj_node *body, uint64_t recur_slots) {
	live_target self = {recur_slots, 0};
	uint64_t    pinned = 0;
	pin_ctx     p = {0, &pinned};
	pin_scan(body, &p);
	live_ctx c = {pinned, &self, true};
	live_fixpoint(body, 0, &self, &c);
}

// Every fn arity below the root is a frame of its own; the walk finds them, live() does not enter them.
static void live_fns(const clj_node *n, void *ctx) {
	if (n->kind == CLJ_NODE_FN || n->kind == CLJ_NODE_DIRECT_FN) {
		for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED + 1; i++) {
			const clj_fn_arity *a = i <= CLJ_FN_MAX_FIXED ? n->u.fn.fixed[i] : n->u.fn.variadic;
			if (a) live_frame(a->body, (a->nparams + (a->variadic ? 1 : 0)) >= 64 ? UINT64_MAX : ((uint64_t)1 << (a->nparams + (a->variadic ? 1 : 0))) - 1);
		}
	}
	clj_node_children(n, live_fns, ctx);
}

void clj_optimize(clj_node *root) {
	direct_pass(root, NULL);
	optimize(root, NULL);
	live_frame(root, 0);
	live_fns(root, NULL);
}
