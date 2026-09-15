// @ai-generated(guided)
// Runs before numbering, so the rewrites are what serializes and what every backend sees.
#include <stdlib.h>
#include <string.h>

#include "clj/analyzer.h"
#include "clj/fusion.h"
#include "clj/intrinsics.h"
#include "clj/symbol.h"
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
	n->u.index = index;
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
	if (n->kind == CLJ_NODE_LOCAL) return depth == 0 && n->u.index == slot;
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

// The tree is still the analyzer's own here: the const on the visitor's argument is dropped once.
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
}

void clj_optimize(clj_node *root) {
	direct_pass(root, NULL);
	optimize(root, NULL);
}
