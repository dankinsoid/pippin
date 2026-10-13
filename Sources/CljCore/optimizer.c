// @ai-generated(guided)
// Runs before numbering, so the rewrites are what serializes and what every backend sees.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/analyzer.h"
#include "clj/coll.h"
#include "clj/decimal.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/fusion.h"
#include "clj/ice.h"
#include "clj/intrinsics.h"
#include "clj/keyword.h"
#include "clj/lowered.h"
#include "clj/map.h"
#include "clj/string.h"
#include "clj/summary.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "clj/verify.h"
#include "ice_internal.h"
#include "node.h"
#include "specialize_internal.h"

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
	const clj_intrinsic *op = clj_intrinsic_find(head->u.var.v, n->u.invoke.n);
	if (!op) return;
	const clj_node **args = n->u.invoke.args;
	uint32_t         nargs = n->u.invoke.n;
	clj_value        var = clj_retain(head->u.var.v);
	clj_release(clj_from_ptr((void *)head));
	n->kind = CLJ_NODE_INTRINSIC;
	n->u.intrinsic.op = op;
	clj_slot_store(&n->h, &n->u.intrinsic.var, var);
	n->u.intrinsic.args = args;
	n->u.intrinsic.n = nargs;
}

// ---- fusion: (reduce f [init] P), (into to P), (vec P), (count P), (mapv f P), P stage calls over a source, a multi-coll
// map innermost, or (sequence XF src) as the consumer's coll, XF a stage's transducer or a comp of them; and
// (into to XF P), (transduce XF f [init] P)

enum { MAX_STAGES = 64, MAX_SOURCES = 3, MAX_SPINE = 2 * MAX_STAGES };

typedef struct {
	const clj_fusion_var *fv;
	const clj_fusion_var *xfv;  // the var whose call is its transducer: fv but for reductions, fused-scan*
	const clj_node       *call; // its INVOKE: the lazy arity, a transducer arity in an XF, or mapv itself
	uint32_t              nown; // its own args before the coll: f, n, or none
	uint32_t              slot; // the FUSED local of its first own arg
} stage;

typedef struct {
	const clj_fusion_var *fv; // the consumer
	const clj_node       *consumer;
	uint32_t              nlead;
	uint32_t              lead_arg[2];  // the consumer's args that are its lead: f [init], to
	uint32_t              lead_slot[2]; // their FUSED locals
	stage                 stages[MAX_STAGES]; // consumer-first: the last one sees each input first
	uint32_t              nstages;
	bool                  sequence;
	const clj_node       *sources[MAX_SOURCES];
	uint32_t              nsources;
	uint32_t              source; // the FUSED local of the first source
	// the calls the original program rebuilds over the locals: the consumer, sequence, the comps and the stages
	const clj_node       *spine[MAX_SPINE];
	uint32_t              nspine;
	const clj_fusion_var *forms[MAX_SPINE];
	uint32_t              nforms;
} pipeline;

static const clj_fusion_var *named(const char *qualified);

static const clj_fusion_var *fusion_head(const clj_node *n, clj_fusion_role role) {
	if (n->kind != CLJ_NODE_INVOKE || n->u.invoke.fn->kind != CLJ_NODE_VAR) return NULL;
	const clj_fusion_var *fv = clj_fusion_find(n->u.invoke.fn->u.var.v);
	return fv && fv->role == role ? fv : NULL;
}

static bool fv_is(const clj_fusion_var *fv, const char *name) { return fv && strcmp(strchr(fv->name, '/') + 1, name) == 0; }

static bool consumer_arity_ok(const clj_fusion_var *fv, uint32_t n) {
	if (fv_is(fv, "reduce")) return n == 2 || n == 3;
	if (fv_is(fv, "into")) return n == 2 || (n == 3 && !clj_opt_off(CLJ_NO_OPT_FUSE));
	if (fv_is(fv, "transduce")) return (n == 3 || n == 4) && !clj_opt_off(CLJ_NO_OPT_FUSE);
	if (fv_is(fv, "frequencies")) return n == 1 && !clj_opt_off(CLJ_NO_OPT_FUSE);
	if (fv_is(fv, "group-by")) return n == 2 && !clj_opt_off(CLJ_NO_OPT_FUSE);
	if (fv_is(fv, "mapv")) return n >= 2 && n <= 1 + MAX_SOURCES && !clj_opt_off(CLJ_NO_OPT_FUSE);
	return n == 1; // vec, count
}

static bool spine_add(pipeline *p, const clj_node *n) {
	if (p->nspine == MAX_SPINE) return false;
	p->spine[p->nspine++] = n;
	return true;
}

static bool stage_add(pipeline *p, const clj_fusion_var *fv, const clj_node *call, uint32_t nown) {
	if (p->nstages == MAX_STAGES) return false;
	p->stages[p->nstages++] = (stage){fv, fv, call, nown, 0};
	return true;
}

static void form_add(pipeline *p, const clj_node *call) {
	const clj_fusion_var *fv = clj_fusion_find(call->u.invoke.fn->u.var.v);
	for (uint32_t i = 0; i < p->nforms; i++) {
		if (p->forms[i] == fv) return;
	}
	p->forms[p->nforms++] = fv;
}

// XF's stages, pushed consumer-first: a comp's last transducer is the one nearest the consumer.
static bool xform_stages(pipeline *p, const clj_node *xf) {
	const clj_fusion_var *fv = fusion_head(xf, CLJ_FUSION_STAGE);
	if (!fv) fv = fusion_head(xf, CLJ_FUSION_XF_STAGE);
	if (fv) return xf->u.invoke.n + 1 == fv->arity && spine_add(p, xf) && stage_add(p, fv, xf, xf->u.invoke.n);
	if (!fv_is(fusion_head(xf, CLJ_FUSION_FORM), "comp") || xf->u.invoke.n == 0 || !spine_add(p, xf)) return false;
	form_add(p, xf);
	for (uint32_t i = xf->u.invoke.n; i-- > 0;) {
		if (!xform_stages(p, xf->u.invoke.args[i])) return false;
	}
	return true;
}

// Under reduce and into a stage below sequence would move its calls across the per-input buffer: there it is the source.
static bool sequence_ok(const pipeline *p, const clj_node *call) {
	// group-by's f and frequencies' hashes would interleave with the stage calls of a multi-output input
	bool consumer = fv_is(p->fv, "count") || fv_is(p->fv, "vec") || fv_is(p->fv, "reduce") || fv_is(p->fv, "into");
	if (clj_opt_off(CLJ_NO_OPT_FUSE) || call->u.invoke.n != 2 || p->nstages || !consumer) return false;
	return fv_is(fusion_head(call, CLJ_FUSION_FORM), "sequence");
}

static bool buffered(const pipeline *p) { return p->sequence && (fv_is(p->fv, "reduce") || fv_is(p->fv, "into")); }

// @ai-generated(solo)
static bool match(const clj_node *n, pipeline *p) {
	p->fv = fusion_head(n, CLJ_FUSION_CONSUMER);
	if (!p->fv || !consumer_arity_ok(p->fv, n->u.invoke.n)) return false;
	p->consumer = n;
	p->nstages = p->nspine = p->nforms = p->nsources = 0;
	p->sequence = false;
	spine_add(p, n);
	const clj_node *coll = n->u.invoke.args[n->u.invoke.n - 1];
	p->nlead = n->u.invoke.n - 1;
	for (uint32_t i = 0; i < p->nlead && i < 2; i++) p->lead_arg[i] = i;
	// (into to XF coll) and (transduce XF f [init] coll): the XF's stages nearest the consumer, P's below
	const clj_node *xf = fv_is(p->fv, "into") && n->u.invoke.n == 3 ? n->u.invoke.args[1] : fv_is(p->fv, "transduce") ? n->u.invoke.args[0] : NULL;
	if (xf) {
		if (!xform_stages(p, xf)) return false;
		p->nlead = fv_is(p->fv, "into") ? 1 : n->u.invoke.n - 2;
		for (uint32_t i = 0; i < p->nlead; i++) p->lead_arg[i] = fv_is(p->fv, "into") ? 0 : 1 + i;
	}
	if (fv_is(p->fv, "mapv")) {
		// (into [] (map f c…)): the map is a stage of the consumer's own call
		p->nlead = 0;
		stage_add(p, clj_fusion_find_named_cstr("clojure.core/map"), n, 1);
		if (n->u.invoke.n > 2) {
			for (uint32_t i = 1; i < n->u.invoke.n; i++) p->sources[p->nsources++] = n->u.invoke.args[i];
			return true;
		}
	}
	for (;;) {
		const clj_fusion_var *fv = fusion_head(coll, CLJ_FUSION_STAGE);
		uint32_t              na = fv ? coll->u.invoke.n : 0;
		if (fv && na == fv->arity) {
			if (buffered(p) || !stage_add(p, fv, coll, na - 1) || !spine_add(p, coll)) break;
			coll = coll->u.invoke.args[na - 1];
			continue;
		}
		if (fv_is(fv, "reductions") && (na == 2 || na == 3) && !buffered(p) && !clj_opt_off(CLJ_NO_OPT_FUSE)) {
			if (!stage_add(p, fv, coll, na - 1) || !spine_add(p, coll)) break;
			p->stages[p->nstages - 1].xfv = named("clojure.core/fused-scan*");
			coll = coll->u.invoke.args[na - 1];
			continue;
		}
		if (fv_is(fv, "map") && na > 2 && na <= 1 + MAX_SOURCES && !buffered(p) && !clj_opt_off(CLJ_NO_OPT_FUSE)) {
			if (!stage_add(p, fv, coll, 1) || !spine_add(p, coll)) break;
			for (uint32_t i = 1; i < na; i++) p->sources[p->nsources++] = coll->u.invoke.args[i];
			return true;
		}
		if (!p->sequence && coll->kind == CLJ_NODE_INVOKE && coll->u.invoke.fn->kind == CLJ_NODE_VAR && sequence_ok(p, coll)) {
			uint32_t nstages = p->nstages, nspine = p->nspine, nforms = p->nforms;
			if (spine_add(p, coll) && xform_stages(p, coll->u.invoke.args[0])) {
				form_add(p, coll);
				p->sequence = true;
				coll = coll->u.invoke.args[1];
				continue;
			}
			p->nstages = nstages;
			p->nspine = nspine;
			p->nforms = nforms;
		}
		break;
	}
	p->sources[p->nsources++] = coll;
	// a lone mapv over one coll is core's lowered reduce already
	return fv_is(p->fv, "mapv") ? p->nstages > 1 : p->nstages > 0;
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
	clj_slot_store(&n->h, &n->u.var, clj_retain(var));
	return n;
}

static clj_node *const_node(clj_value v, position at) {
	clj_node *n = node_at(CLJ_NODE_CONST, at);
	clj_slot_store(&n->h, &n->u.value, clj_retain(v));
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

static bool on_spine(const pipeline *p, const clj_node *n) {
	for (uint32_t i = 0; i < p->nspine; i++) {
		if (p->spine[i] == n) return true;
	}
	return false;
}

// The args in evaluation order, every node off the spine one local: each stage learns its first own arg's slot.
static void collect(pipeline *p, const clj_node *n, const clj_node **args, uint32_t *k) {
	if (!on_spine(p, n)) {
		if (n == p->sources[0]) p->source = *k;
		if (args) args[*k] = n;
		(*k)++;
		return;
	}
	for (uint32_t i = 0; i < p->nstages; i++) {
		if (p->stages[i].call == n) p->stages[i].slot = *k;
	}
	for (uint32_t i = 0; i < n->u.invoke.n; i++) {
		for (uint32_t j = 0; n == p->consumer && j < p->nlead; j++) {
			if (p->lead_arg[j] == i) p->lead_slot[j] = *k;
		}
		collect(p, n->u.invoke.args[i], args, k);
	}
}

// The spine again over the locals, in the same order.
static clj_node *rebuild(const pipeline *p, const clj_node *n, uint32_t *k, position at) {
	if (!on_spine(p, n)) return local_node((*k)++, at);
	const clj_node **args = zalloc(n->u.invoke.n, sizeof *args);
	for (uint32_t i = 0; i < n->u.invoke.n; i++) args[i] = rebuild(p, n->u.invoke.args[i], k, at);
	return invoke_node(var_node(n->u.invoke.fn->u.var.v, at), args, n->u.invoke.n, at);
}

static const clj_fusion_var *named(const char *qualified) {
	const clj_fusion_var *fv = clj_fusion_find_named_cstr(qualified);
	CLJ_ASSERT(fv, "fusion table entry missing");
	return fv;
}

// fused-reduce*, fused-into* and fused-count* keep the shapes they had; the rest is fused* with a mode.
static const clj_fusion_var *driver_for(const pipeline *p, clj_fused_mode *mode) {
	bool many = p->nsources > 1, buf = buffered(p);
	if (fv_is(p->fv, "transduce") || fv_is(p->fv, "frequencies") || fv_is(p->fv, "group-by")) {
		*mode = fv_is(p->fv, "transduce") ? CLJ_FUSED_MODE_TRANSDUCE : fv_is(p->fv, "group-by") ? CLJ_FUSED_MODE_GROUP_BY : CLJ_FUSED_MODE_FREQUENCIES;
		return named("clojure.core/fused*");
	}
	if (fv_is(p->fv, "reduce")) *mode = buf ? CLJ_FUSED_MODE_SEQ_REDUCE : CLJ_FUSED_MODE_REDUCE;
	else if (fv_is(p->fv, "count")) *mode = CLJ_FUSED_MODE_COUNT;
	else *mode = buf ? CLJ_FUSED_MODE_SEQ_INTO : CLJ_FUSED_MODE_INTO;
	// a reductions' begin is fused*'s alone: the older drivers run no 0-arity before the walk
	bool scan = false;
	for (uint32_t i = 0; i < p->nstages; i++) scan = scan || p->stages[i].xfv != p->stages[i].fv;
	if (many || buf || scan || fv_is(p->fv, "mapv")) return named("clojure.core/fused*");
	return named(*mode == CLJ_FUSED_MODE_REDUCE ? "clojure.core/fused-reduce*" : *mode == CLJ_FUSED_MODE_COUNT ? "clojure.core/fused-count*" : "clojure.core/fused-into*");
}

static clj_node *build_fused(const pipeline *p, position at) {
	clj_fused_mode        mode;
	const clj_fusion_var *driver = driver_for(p, &mode);
	bool                  into_empty = fv_is(p->fv, "vec") || fv_is(p->fv, "mapv");
	const clj_node      **xfs = zalloc(p->nstages, sizeof *xfs);
	for (uint32_t i = 0; i < p->nstages; i++) {
		const clj_node **xargs = zalloc(p->stages[i].nown, sizeof *xargs);
		for (uint32_t j = 0; j < p->stages[i].nown; j++) xargs[j] = local_node(p->stages[i].slot + j, at);
		xfs[i] = invoke_node(var_node(clj_fusion_var_of(p->stages[i].xfv), at), xargs, p->stages[i].nown, at);
	}
	clj_node *xv = vector_node(xfs, p->nstages, at);
	uint32_t  nlead = p->nlead + (into_empty ? 1 : 0), k = 0;
	if (fv_is(driver, "fused*")) {
		const clj_node **lead = zalloc(nlead, sizeof *lead);
		if (into_empty) lead[k++] = const_node(clj_vector_empty(), at);
		for (uint32_t i = 0; i < p->nlead; i++) lead[k++] = local_node(p->lead_slot[i], at);
		const clj_node **args = zalloc(3 + p->nsources, sizeof *args);
		args[0] = const_node(clj_fixnum(mode), at);
		args[1] = vector_node(lead, nlead, at);
		args[2] = xv;
		for (uint32_t i = 0; i < p->nsources; i++) args[3 + i] = local_node(p->source + i, at);
		return invoke_node(var_node(clj_fusion_var_of(driver), at), args, 3 + p->nsources, at);
	}
	// (driver lead... source xfs); vec's `to` is the literal []
	const clj_node **args = zalloc(nlead + 2, sizeof *args);
	if (into_empty) args[k++] = const_node(clj_vector_empty(), at);
	for (uint32_t i = 0; i < p->nlead; i++) args[k++] = local_node(p->lead_slot[i], at);
	args[k++] = local_node(p->source, at);
	args[k++] = xv;
	return invoke_node(var_node(clj_fusion_var_of(driver), at), args, k, at);
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
	uint32_t nargs = 0;
	collect(&p, n, NULL, &nargs);
	// The args keep the original evaluation order: the consumer's, each stage's own consumer-first, the sources.
	const clj_node **args = zalloc(nargs, sizeof *args);
	uint32_t         k = 0;
	collect(&p, n, args, &k);
	CLJ_ASSERT(k == nargs, "fusion argument count");
	CLJ_ASSERT(p.source + p.nsources <= nargs, "fusion sources");
	// Each arg keeps a reference of its own; the spine below lets go of the ones its calls held.
	for (uint32_t i = 0; i < nargs; i++) clj_retain(clj_from_ptr((void *)args[i]));
	clj_fused_mode         mode;
	const clj_fusion_var  *driver = driver_for(&p, &mode);
	const clj_fusion_var **guards = zalloc(2 * p.nstages + p.nforms + 3, sizeof *guards);
	uint32_t               nguards = 0;
	add_guard(guards, &nguards, p.fv);
	for (uint32_t i = 0; i < p.nforms; i++) add_guard(guards, &nguards, p.forms[i]);
	for (uint32_t i = 0; i < p.nstages; i++) {
		add_guard(guards, &nguards, p.stages[i].fv);
		add_guard(guards, &nguards, p.stages[i].xfv);
	}
	// mapv's body is (into [] (map f c…)); frequencies' and group-by's reduce over the stages
	if (fv_is(p.fv, "mapv")) add_guard(guards, &nguards, named("clojure.core/into"));
	if (fv_is(p.fv, "frequencies") || fv_is(p.fv, "group-by")) add_guard(guards, &nguards, named("clojure.core/reduce"));
	add_guard(guards, &nguards, driver);
	clj_node *fused = build_fused(&p, at);
	k = 0;
	clj_node *original = rebuild(&p, n, &k, at);
	// n itself is reused as the FUSED node: its own head and args go, the spine below it with them
	clj_release(clj_from_ptr((void *)n->u.invoke.fn));
	for (uint32_t i = 0; i < n->u.invoke.n; i++) clj_release(clj_from_ptr((void *)n->u.invoke.args[i]));
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
	clj_ice("optimize/capture-kind", NULL, "capture kind %u", (unsigned)t->kind);
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
	if (n->kind != CLJ_NODE_CONST || !clj_node_foldable(n->u.value.v)) return false;
	*out = n->u.value.v;
	return true;
}

// A var rebound before analysis keeps the guarded call; a rebind after it does not unfold (Clojure's :inline
// speculates the same way). A fold that throws leaves the node, so the program throws where it would have.
// @ai-generated(guided)
static void fold_intrinsic(clj_node *n) {
	const clj_intrinsic *op = n->u.intrinsic.op;
	clj_value            vals[3];
	if (!op->pure || clj_var_root(n->u.intrinsic.var.v) != clj_intrinsic_builtin(op)) return;
	for (uint32_t i = 0; i < n->u.intrinsic.n; i++) {
		if (!const_arg(n->u.intrinsic.args[i], &vals[i])) return;
		// Decimal arithmetic rounds to the *math-context* of the call, which analysis cannot know.
		if (clj_is_decimal(vals[i])) return;
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
	clj_release(n->u.intrinsic.var.v);
	n->kind = CLJ_NODE_CONST;
	clj_slot_store(&n->h, &n->u.value, r);
}

// The taken branch's contents move into n, which the parent points at (only a DIRECT_FN is referenced by
// pointer, and one never sits in a branch); the emptied branch dies as a nil constant.
// @ai-generated(guided)
static void fold_if(clj_node *n) {
	const clj_node *test = n->u.if_.test;
	if (test->kind != CLJ_NODE_CONST) return;
	bool      truthy = clj_truthy(test->u.value.v);
	clj_node *taken = (clj_node *)(truthy ? n->u.if_.then : n->u.if_.else_);
	clj_node *dropped = (clj_node *)(truthy ? n->u.if_.else_ : n->u.if_.then);
	clj_release(clj_from_ptr((void *)test));
	if (dropped) clj_release(clj_from_ptr(dropped));
	if (!taken) {
		n->kind = CLJ_NODE_CONST;
		clj_slot_clear(&n->u.value);
		return;
	}
	n->kind = taken->kind;
	n->u = taken->u;
	n->line = taken->line;
	n->col = taken->col;
	taken->kind = CLJ_NODE_CONST;
	clj_slot_clear(&taken->u.value);
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

// ---- inlining (design §6 «Инлайнинг без JIT», §10 step 7a): (f a b) of a var holding a small closure becomes
//   (if ROOT_IS[f, closure] (let [p a'] INLINE[fn, body']) (f a b))
// The guard reads the root before the arguments, as the call does, and the call itself is the else branch, so a
// redefined or rebound var runs what it ran before. Copied bodies are never inlined into again: no unrolling.
// A parameter the body calls keeps its call there, unless the argument is a literal fn, whose arity body is then
// copied in its place (beta-reduction), or a var of an intrinsic or of a closure this pass would inline.

static bool inline_enabled = true;

bool clj_inline_set_enabled(bool on) {
	bool was = inline_enabled;
	inline_enabled = on;
	return was;
}

enum {
	INLINE_CALLEE_MAX = 32,  // nodes of the callee's arity body: what "small" means
	INLINE_GROWTH_MIN = 64,  // nodes a frame may grow by, or by its own size when that is larger
	INLINE_SLOTS_SMALL = 16, // eval.c's SMALL_SLOTS: a frame past it goes to the heap, so a small frame stays small
	INLINE_SLOTS_MAX = 64,   // the owned mask and the last-use marks stop at 64 slots
};

bool clj_inline_callee_shaped(const clj_node *fn) {
	if (fn->u.fn.ncaptures) return false;
	for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
		if (fn->u.fn.fixed[k] && fn->u.fn.fixed[k]->body->nnodes <= INLINE_CALLEE_MAX) return true;
	}
	return false;
}

// An inlined body runs where the call ran, inside the caller's frame: only effects with no order to keep.
#define INLINE_EFFECTS (CLJ_EFFECT_ALLOC | CLJ_EFFECT_THROW)
#define SLOT_NONE UINT32_MAX

typedef struct {
	const uint32_t    *map;     // callee slot -> slot of this frame; SLOT_NONE for a substituted parameter and the self slot
	const clj_node   **subst;   // per callee slot: the read a parameter's local reads become, or NULL
	const clj_node   **literal; // per callee slot: the literal fn whose body the parameter's calls become, or NULL
	uint32_t           nmap;
	const clj_capture *caps;    // a literal fn's captures: its environment's reads become reads of this frame
	uint32_t           ncaps;
	uint32_t          *next;    // the frame's next free slot, for the slots of a literal fn's body
	const clj_node   **from, **to; // direct fns copied so far, for their calls
	uint32_t           nfn, cfn;
	clj_node         **heads;   // calls whose head a substituted var became: rewritten or inlined after the copy
	uint32_t           nheads, cheads;
	uint32_t           nodes;   // made
	uint32_t           depth;   // COPY and PATH bodies around the copy point within the frame, the new one included
} copy_ctx;

// What a candidate needs to know of its callee's arity body, or of a literal fn's.
typedef struct {
	uint32_t  nparams, self;
	uint64_t  local_only;  // params read only by local reads of the arity's own frame: a substituted read may stand in
	uint64_t  head_only;   // params read only as the head of a call in the arity's own frame
	uint64_t  head_looped; // such a call inside a loop of the body
	uint32_t  heads[CLJ_FN_MAX_FIXED + 1];       // per param: the calls it heads
	uint32_t  head_counts[CLJ_FN_MAX_FIXED + 1]; // per param, bit k: a call of k args
	bool      arity_recur; // a recur back to the arity, which its copy would have nothing to go back to
	bool      self_read;   // the fn's own name, which no frame here holds
	bool      has_fn;      // a fn inside: each copy would be a closure, and in C functions, of its own
	bool      calls_def;   // a call of the var being defined: the copy would make the caller its own callee
	clj_value def_var;
	int       level;       // direct fn bodies entered
	bool      in_loop;     // a recur here goes back to a loop of the body
} callee_scan;

static uint64_t param_bit(uint32_t slot) { return slot < 64 ? (uint64_t)1 << slot : 0; }

static void scan_callee_uses(callee_scan *s, uint32_t slot) {
	if (slot == s->self) s->self_read = true;
	if (slot < s->nparams) {
		s->local_only &= ~param_bit(slot);
		s->head_only &= ~param_bit(slot);
	}
}

// @ai-generated(solo)
static void scan_callee(const clj_node *n, void *ctx) {
	callee_scan *s = ctx;
	switch (n->kind) {
	case CLJ_NODE_LOCAL:
		if (s->level == 0 && n->u.local.index == s->self) s->self_read = true;
		if (s->level == 0 && n->u.local.index < s->nparams) s->head_only &= ~param_bit(n->u.local.index);
		return;
	case CLJ_NODE_OUTER:
		if ((int)n->u.outer.depth == s->level) scan_callee_uses(s, n->u.outer.index);
		return;
	case CLJ_NODE_RECUR:
		if (s->level == 0 && !s->in_loop) s->arity_recur = true;
		break;
	case CLJ_NODE_LOOP: {
		for (uint32_t i = 0; i < n->u.let.n; i++) scan_callee(n->u.let.inits[i], s);
		bool in_loop = s->in_loop;
		s->in_loop = true;
		scan_callee(n->u.let.body, s);
		s->in_loop = in_loop;
		return;
	}
	case CLJ_NODE_FN:
		s->has_fn = true;
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cap = &n->u.fn.captures[i];
			if ((cap->kind == CLJ_CAPTURE_LOCAL && s->level == 0) || (cap->kind == CLJ_CAPTURE_OUTER && (int)cap->depth == s->level)) scan_callee_uses(s, cap->index);
		}
		return;
	case CLJ_NODE_DIRECT_FN: {
		s->has_fn = true;
		callee_scan inner = *s;
		inner.level++;
		inner.in_loop = false;
		clj_node_children(n, scan_callee, &inner);
		s->local_only = inner.local_only;
		s->head_only = inner.head_only;
		s->self_read = inner.self_read;
		return;
	}
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) scan_callee(n->u.fused.args[i], s);
		return;
	case CLJ_NODE_INVOKE: {
		const clj_node *h = n->u.invoke.fn;
		if (s->level == 0 && h->kind == CLJ_NODE_LOCAL && h->u.local.index < s->nparams) {
			uint32_t p = h->u.local.index;
			s->heads[p]++;
			// past the fixed arities a call spreads its arguments into a rest list, which no copy has
			if (n->u.invoke.n <= CLJ_FN_MAX_FIXED) s->head_counts[p] |= 1u << n->u.invoke.n;
			else s->head_only &= ~param_bit(p);
			if (s->in_loop) s->head_looped |= param_bit(p);
			for (uint32_t i = 0; i < n->u.invoke.n; i++) scan_callee(n->u.invoke.args[i], s);
			return;
		}
		if (h->kind == CLJ_NODE_VAR && h->u.var.v == s->def_var) s->calls_def = true;
		break;
	}
	default: break;
	}
	clj_node_children(n, scan_callee, ctx);
}

static callee_scan scan_arity(const clj_fn_arity *a, clj_value def_var) {
	callee_scan s = {.nparams = a->nparams, .self = a->self_slot >= 0 ? (uint32_t)a->self_slot : SLOT_NONE, .local_only = UINT64_MAX,
	                 .head_only = UINT64_MAX, .def_var = def_var};
	scan_callee(a->body, &s);
	return s;
}

static bool trivial_read(const clj_node *n) {
	return n->kind == CLJ_NODE_CONST || n->kind == CLJ_NODE_LOCAL || n->kind == CLJ_NODE_CAPTURED || n->kind == CLJ_NODE_OUTER;
}

static void count_nodes(const clj_node *n, void *ctx) {
	(*(uint32_t *)ctx)++;
	clj_node_children(n, count_nodes, ctx);
}

static uint32_t nodes_in(const clj_node *n) {
	uint32_t k = 0;
	count_nodes(n, &k);
	return k;
}

// `level`: direct fn bodies entered below the callee's frame; VERBATIM is another frame, whose slots stay as they are.
enum { VERBATIM = -1 };

static clj_node *copy_node(const clj_node *n, copy_ctx *c, int level);

static const clj_node **copy_all(const clj_node *const *nodes, uint32_t n, copy_ctx *c, int level) {
	const clj_node **out = zalloc(n, sizeof *out);
	for (uint32_t i = 0; i < n; i++) out[i] = copy_node(nodes[i], c, level);
	return out;
}

static uint32_t copy_slot(const copy_ctx *c, int level, uint32_t slot, const clj_node *at) {
	if (level != 0) return slot;
	if (slot >= c->nmap || c->map[slot] == SLOT_NONE) clj_ice("optimize/inline-slot", at, "slot %u of an inlined body has no slot in its new frame", slot);
	return c->map[slot];
}

static uint32_t *copy_slots(const copy_ctx *c, int level, const uint32_t *slots, uint32_t n, const clj_node *at) {
	uint32_t *out = zalloc(n, sizeof *out);
	for (uint32_t i = 0; i < n; i++) out[i] = copy_slot(c, level, slots[i], at);
	return out;
}

// A callee has no environment, so a static link reaches at most its own frame.
static uint32_t copy_linked(const copy_ctx *c, int level, uint32_t depth, uint32_t slot, const clj_node *at) {
	if (level == VERBATIM || depth < (uint32_t)level) return slot;
	if (depth > (uint32_t)level) clj_ice("optimize/inline-outer", at, "a static-link reference %u up from %d frames into an inlined body", depth, level);
	return copy_slot(c, 0, slot, at);
}

static void fn_map_add(copy_ctx *c, const clj_node *from, const clj_node *to) {
	if (c->nfn == c->cfn) {
		c->cfn = c->cfn ? c->cfn * 2 : 8;
		c->from = realloc(c->from, c->cfn * sizeof *c->from);
		c->to = realloc(c->to, c->cfn * sizeof *c->to);
		if (!c->from || !c->to) clj_fatal("out of memory");
	}
	c->from[c->nfn] = from;
	c->to[c->nfn++] = to;
}

// A verbatim copy of an argument may call a direct fn bound outside it, which the call keeps.
static const clj_node *copy_direct_target(const copy_ctx *c, const clj_node *from, int level, const clj_node *at) {
	for (uint32_t i = c->nfn; i-- > 0;) {
		if (c->from[i] == from) return c->to[i];
	}
	if (level == VERBATIM) return from;
	clj_ice("optimize/inline-direct-call", at, "a direct call whose fn was not copied before it");
}

// A body or a lowered call that borrows a literal fn argument of its guarded call, which is copied before it.
static const clj_node *copied_literal(const copy_ctx *c, const clj_node *fn, const clj_node *at) {
	for (uint32_t i = c->nfn; i-- > 0;) {
		if (c->from[i] == fn) return c->to[i];
	}
	clj_ice("optimize/inline-literal", at, "a copied literal fn whose guarded call was not copied before it");
}

static clj_capture copy_capture(const copy_ctx *c, int level, const clj_capture *cap, const clj_node *at) {
	clj_capture out = *cap;
	if (level == VERBATIM) return out;
	switch (cap->kind) {
	case CLJ_CAPTURE_LOCAL: out.index = copy_slot(c, level, cap->index, at); break;
	case CLJ_CAPTURE_OUTER: out.index = copy_linked(c, level, cap->depth, cap->index, at); break;
	case CLJ_CAPTURE_CAPTURED: clj_ice("optimize/inline-captured", at, "a capture of an environment the inlined closure does not have");
	}
	return out;
}

// @ai-generated(solo)
static clj_node *copy_fn(const clj_node *n, copy_ctx *c, int level) {
	clj_node *m = node_at(n->kind, (position){n->line, n->col});
	c->nodes++;
	clj_slot_store(&m->h, &m->u.fn.name, clj_retain(n->u.fn.name.v));
	if (n->kind == CLJ_NODE_DIRECT_FN) fn_map_add(c, n, m);
	if (n->u.fn.ncaptures) {
		clj_capture *caps = zalloc(n->u.fn.ncaptures, sizeof *caps);
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) caps[i] = copy_capture(c, level, &n->u.fn.captures[i], n);
		m->u.fn.captures = caps;
		m->u.fn.ncaptures = n->u.fn.ncaptures;
	}
	// A closure's arities are frames of their own; a direct fn's reach this one one static link further up.
	int      inner = n->kind == CLJ_NODE_DIRECT_FN && level != VERBATIM ? level + 1 : VERBATIM;
	uint32_t depth = c->depth;
	if (inner == VERBATIM) c->depth = 0;
	for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED + 1; k++) {
		const clj_fn_arity *a = k <= CLJ_FN_MAX_FIXED ? n->u.fn.fixed[k] : n->u.fn.variadic;
		if (!a) continue;
		clj_fn_arity *b = zalloc(1, sizeof *b);
		*b = *a;
		b->body = copy_node(a->body, c, inner);
		if (k <= CLJ_FN_MAX_FIXED) m->u.fn.fixed[k] = b;
		else m->u.fn.variadic = b;
	}
	c->depth = depth;
	return m;
}

// A literal fn's environment read, as the read of this frame the fn's capture took it from.
static clj_node *env_read(copy_ctx *c, const clj_node *n) {
	if (n->u.index >= c->ncaps) clj_ice("optimize/inline-captured", n, "entry %u of an environment of %u", n->u.index, c->ncaps);
	const clj_capture *cap = &c->caps[n->u.index];
	position           at = {n->line, n->col};
	c->nodes++;
	switch (cap->kind) {
	case CLJ_CAPTURE_LOCAL: return local_node(cap->index, at);
	case CLJ_CAPTURE_CAPTURED: {
		clj_node *m = node_at(CLJ_NODE_CAPTURED, at);
		m->u.index = cap->index;
		return m;
	}
	case CLJ_CAPTURE_OUTER: {
		clj_node *m = node_at(CLJ_NODE_OUTER, at);
		m->u.outer.depth = cap->depth;
		m->u.outer.index = cap->index;
		return m;
	}
	}
	clj_ice("optimize/capture-kind", n, "capture kind %u", (unsigned)cap->kind);
}

static void head_add(copy_ctx *c, clj_node *call) {
	if (c->nheads == c->cheads) {
		c->cheads = c->cheads ? c->cheads * 2 : 4;
		c->heads = realloc(c->heads, c->cheads * sizeof *c->heads);
		if (!c->heads) clj_fatal("out of memory");
	}
	c->heads[c->nheads++] = call;
}

static clj_node *literal_call(const clj_node *lit, const clj_node **args, uint32_t nargs, position at, uint32_t *next, uint32_t depth, uint32_t *nodes);

// Every last-use mark is dropped: the liveness pass marks the frame the copy lands in afresh.
// @ai-generated(solo)
static clj_node *copy_node(const clj_node *n, copy_ctx *c, int level) {
	if (n->kind == CLJ_NODE_FN || n->kind == CLJ_NODE_DIRECT_FN) return copy_fn(n, c, level);
	// A nested inline past the depth keeps its call: the guard's else branch is the call as it was.
	if (n->kind == CLJ_NODE_IF && n->u.if_.test->kind == CLJ_NODE_ROOT_IS && level != VERBATIM && c->depth + 1 > CLJ_INLINE_DEPTH_MAX)
		return copy_node(n->u.if_.else_, c, level);
	if (n->kind == CLJ_NODE_LOCAL && level == 0 && n->u.local.index < c->nmap && c->subst[n->u.local.index])
		return copy_node(c->subst[n->u.local.index], c, VERBATIM);
	if (n->kind == CLJ_NODE_CAPTURED && level == 0 && c->caps) return env_read(c, n);
	if (n->kind == CLJ_NODE_INVOKE && level == 0 && c->literal && n->u.invoke.fn->kind == CLJ_NODE_LOCAL) {
		uint32_t p = n->u.invoke.fn->u.local.index;
		if (p < c->nmap && c->literal[p]) {
			const clj_node **args = copy_all(n->u.invoke.args, n->u.invoke.n, c, level);
			return literal_call(c->literal[p], args, n->u.invoke.n, (position){n->line, n->col}, c->next, c->depth, &c->nodes);
		}
	}
	clj_node *m = node_at(n->kind, (position){n->line, n->col});
	c->nodes++;
	switch (n->kind) {
	case CLJ_NODE_CONST: clj_slot_store(&m->h, &m->u.value, clj_retain(n->u.value.v)); break;
	case CLJ_NODE_LOCAL: m->u.local.index = copy_slot(c, level, n->u.local.index, n); break;
	case CLJ_NODE_CAPTURED:
		if (level != VERBATIM) clj_ice("optimize/inline-captured", n, "a read of an environment the inlined closure does not have");
		m->u.index = n->u.index;
		break;
	case CLJ_NODE_OUTER:
		m->u.outer.depth = n->u.outer.depth;
		m->u.outer.index = copy_linked(c, level, n->u.outer.depth, n->u.outer.index, n);
		break;
	case CLJ_NODE_VAR: clj_slot_store(&m->h, &m->u.var, clj_retain(n->u.var.v)); break;
	case CLJ_NODE_IF:
		m->u.if_.test = copy_node(n->u.if_.test, c, level);
		if (n->u.if_.test->kind == CLJ_NODE_ROOT_IS) {
			// the call first: a literal fn copied into the then branch names the fn node the copied call passes
			m->u.if_.else_ = copy_node(n->u.if_.else_, c, level);
			const clj_node *from = n->u.if_.else_, *to = m->u.if_.else_;
			for (uint32_t i = 0; i < from->u.invoke.n; i++) {
				if (from->u.invoke.args[i]->kind == CLJ_NODE_FN) fn_map_add(c, from->u.invoke.args[i], to->u.invoke.args[i]);
			}
			m->u.if_.then = copy_node(n->u.if_.then, c, level);
			break;
		}
		m->u.if_.then = copy_node(n->u.if_.then, c, level);
		m->u.if_.else_ = n->u.if_.else_ ? copy_node(n->u.if_.else_, c, level) : NULL;
		break;
	case CLJ_NODE_DO:
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET:
		m->u.seq.items = copy_all(n->u.seq.items, n->u.seq.n, c, level);
		m->u.seq.n = n->u.seq.n;
		break;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		m->u.let.slots = copy_slots(c, level, n->u.let.slots, n->u.let.n, n);
		m->u.let.inits = copy_all(n->u.let.inits, n->u.let.n, c, level);
		m->u.let.n = n->u.let.n;
		m->u.let.body = copy_node(n->u.let.body, c, level);
		break;
	case CLJ_NODE_RECUR:
		m->u.recur.slots = copy_slots(c, level, n->u.recur.slots, n->u.recur.n, n);
		m->u.recur.args = copy_all(n->u.recur.args, n->u.recur.n, c, level);
		m->u.recur.n = n->u.recur.n;
		break;
	case CLJ_NODE_INVOKE:
		m->u.invoke.fn = copy_node(n->u.invoke.fn, c, level);
		m->u.invoke.args = copy_all(n->u.invoke.args, n->u.invoke.n, c, level);
		m->u.invoke.n = n->u.invoke.n;
		if (level == 0 && n->u.invoke.fn->kind == CLJ_NODE_LOCAL && m->u.invoke.fn->kind == CLJ_NODE_VAR) head_add(c, m);
		break;
	case CLJ_NODE_DIRECT_CALL: {
		const clj_node *fn = copy_direct_target(c, n->u.direct.fn, level, n);
		m->u.direct.fn = fn;
		m->u.direct.arity = fn->u.fn.fixed[n->u.direct.n];
		m->u.direct.args = copy_all(n->u.direct.args, n->u.direct.n, c, level);
		m->u.direct.n = n->u.direct.n;
		m->u.direct.depth = n->u.direct.depth;
		m->u.direct.slot = copy_linked(c, level, n->u.direct.depth, n->u.direct.slot, n);
		break;
	}
	case CLJ_NODE_OBJC_SEND:
		clj_slot_store(&m->h, &m->u.objc.selector, clj_retain(n->u.objc.selector.v));
		m->u.objc.target = copy_node(n->u.objc.target, c, level);
		m->u.objc.args = copy_all(n->u.objc.args, n->u.objc.n, c, level);
		m->u.objc.n = n->u.objc.n;
		break;
	case CLJ_NODE_DEF:
		clj_slot_store(&m->h, &m->u.def.var, clj_retain(n->u.def.var.v));
		m->u.def.init = n->u.def.init ? copy_node(n->u.def.init, c, level) : NULL;
		m->u.def.meta = copy_node(n->u.def.meta, c, level);
		m->u.def.macro = n->u.def.macro;
		m->u.def.dynamic = n->u.def.dynamic;
		m->u.def.lazy = n->u.def.lazy;
		m->u.def.asked = n->u.def.asked;
		break;
	case CLJ_NODE_TRY: {
		m->u.try_.body = copy_node(n->u.try_.body, c, level);
		clj_catch *ks = zalloc(n->u.try_.ncatches, sizeof *ks);
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
			const clj_catch *k = &n->u.try_.catches[i];
			ks[i] = *k;
			ks[i].slot = copy_slot(c, level, k->slot, n);
			if (k->kind == CLJ_CATCH_TYPE || k->kind == CLJ_CATCH_HOST) clj_retain(k->selector);
			ks[i].handler = copy_node(k->handler, c, level);
		}
		m->u.try_.catches = ks;
		m->u.try_.ncatches = n->u.try_.ncatches;
		m->u.try_.finally_ = n->u.try_.finally_ ? copy_node(n->u.try_.finally_, c, level) : NULL;
		break;
	}
	case CLJ_NODE_THROW: m->u.throw_ = copy_node(n->u.throw_, c, level); break;
	case CLJ_NODE_INTRINSIC:
		m->u.intrinsic.op = n->u.intrinsic.op;
		clj_slot_store(&m->h, &m->u.intrinsic.var, clj_retain(n->u.intrinsic.var.v));
		m->u.intrinsic.args = copy_all(n->u.intrinsic.args, n->u.intrinsic.n, c, level);
		m->u.intrinsic.n = n->u.intrinsic.n;
		break;
	case CLJ_NODE_FUSED: {
		const clj_fusion_var **guards = zalloc(n->u.fused.nguards, sizeof *guards);
		memcpy(guards, n->u.fused.guards, n->u.fused.nguards * sizeof *guards);
		m->u.fused.guards = guards;
		m->u.fused.nguards = n->u.fused.nguards;
		m->u.fused.args = copy_all(n->u.fused.args, n->u.fused.nargs, c, level);
		m->u.fused.nargs = n->u.fused.nargs;
		// the two programs read the argument frame and nothing else
		m->u.fused.fused = copy_node(n->u.fused.fused, c, VERBATIM);
		m->u.fused.original = copy_node(n->u.fused.original, c, VERBATIM);
		break;
	}
	case CLJ_NODE_ROOT_IS:
		clj_slot_store(&m->h, &m->u.root_is.var, clj_retain(n->u.root_is.var.v));
		clj_slot_store(&m->h, &m->u.root_is.expect, clj_retain(n->u.root_is.expect.v));
		if (n->u.root_is.nguards) {
			const clj_fusion_var **guards = zalloc(n->u.root_is.nguards, sizeof *guards);
			memcpy(guards, n->u.root_is.guards, n->u.root_is.nguards * sizeof *guards);
			m->u.root_is.guards = guards;
			m->u.root_is.nguards = n->u.root_is.nguards;
		}
		if (n->u.root_is.nops) {
			const clj_intrinsic **ops = zalloc(n->u.root_is.nops, sizeof *ops);
			memcpy(ops, n->u.root_is.ops, n->u.root_is.nops * sizeof *ops);
			m->u.root_is.ops = ops;
			m->u.root_is.nops = n->u.root_is.nops;
		}
		break;
	case CLJ_NODE_INLINE: {
		// a guard's body is a level of the budget; a literal fn's body and a path's levels sit inside one
		bool guarded = n->u.inline_.how == CLJ_INLINE_COPY || n->u.inline_.how == CLJ_INLINE_PATH;
		m->u.inline_.fn = n->u.inline_.how == CLJ_INLINE_LITERAL ? copied_literal(c, n->u.inline_.fn, n) : n->u.inline_.fn;
		m->u.inline_.how = n->u.inline_.how;
		c->depth += guarded;
		m->u.inline_.body = copy_node(n->u.inline_.body, c, level);
		c->depth -= guarded;
		break;
	}
	case CLJ_NODE_LOWERED:
		m->u.lowered.fn = copied_literal(c, n->u.lowered.fn, n);
		m->u.lowered.args = copy_all(n->u.lowered.args, n->u.lowered.n, c, level);
		m->u.lowered.n = n->u.lowered.n;
		m->u.lowered.how = n->u.lowered.how;
		m->u.lowered.split = n->u.lowered.split ? copy_node(n->u.lowered.split, c, level) : NULL;
		break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: break; // copy_fn
	}
	return m;
}

static void copy_ctx_free(copy_ctx *c) {
	free(c->from);
	free(c->to);
	free(c->heads);
}

static clj_node *let_node(uint32_t *slots, const clj_node **inits, uint32_t n, clj_node *body, position at) {
	clj_node *let = node_at(CLJ_NODE_LET, at);
	let->u.let.slots = slots;
	let->u.let.inits = inits;
	let->u.let.n = n;
	let->u.let.body = body;
	return let;
}

static clj_node *inline_node(const clj_node *fn, clj_inline_how how, clj_node *body, position at) {
	clj_node *inl = node_at(CLJ_NODE_INLINE, at);
	inl->u.inline_.fn = fn;
	inl->u.inline_.how = (uint8_t)how;
	inl->u.inline_.body = body;
	return inl;
}

// (L a...) of a literal fn L, as L's arity body in this frame: (let [q a'] INLINE[L, body]) over fresh slots, a trivial
// argument read wherever the body reads its parameter. L's captures are reads of this frame, the one L was made in,
// and none of them is rebound between the call's arguments and here. Takes args and its nodes.
// @ai-generated(solo)
static clj_node *literal_call(const clj_node *lit, const clj_node **args, uint32_t nargs, position at, uint32_t *next, uint32_t depth, uint32_t *nodes) {
	const clj_fn_arity *a = lit->u.fn.fixed[nargs];
	if (!a) clj_ice("optimize/inline-literal", lit, "a literal fn without the arity of %u its copy was planned for", nargs);
	callee_scan      s = scan_arity(a, CLJ_NIL);
	uint32_t        *map = zalloc(a->nslots, sizeof *map);
	const clj_node **sub = zalloc(a->nslots, sizeof *sub);
	uint32_t         nbound = 0;
	for (uint32_t k = 0; k < a->nslots; k++) {
		if (k < nargs && trivial_read(args[k]) && ((s.local_only >> k) & 1)) {
			sub[k] = args[k];
			map[k] = SLOT_NONE;
		} else if (k == s.self) {
			map[k] = SLOT_NONE;
		} else {
			map[k] = (*next)++;
			nbound += k < nargs;
		}
	}
	copy_ctx  c = {.map = map, .subst = sub, .nmap = a->nslots, .caps = lit->u.fn.captures, .ncaps = lit->u.fn.ncaptures, .next = next, .depth = depth};
	clj_node *r = inline_node(lit, CLJ_INLINE_LITERAL, copy_node(a->body, &c, 0), at);
	*nodes += c.nodes + 1;
	if (nbound) {
		uint32_t        *slots = zalloc(nbound, sizeof *slots);
		const clj_node **inits = zalloc(nbound, sizeof *inits);
		for (uint32_t k = 0, b = 0; k < nargs; k++) {
			if (sub[k]) continue;
			slots[b] = map[k];
			inits[b++] = args[k];
		}
		r = let_node(slots, inits, nbound, r, at);
		(*nodes)++;
	}
	for (uint32_t k = 0; k < nargs; k++) {
		if (sub[k]) clj_release(clj_from_ptr((void *)args[k]));
	}
	free(args);
	free(map);
	free(sub);
	copy_ctx_free(&c);
	return r;
}

typedef struct {
	clj_fn_arity *arity; // the frame the sites below are in; NULL outside every fn
	uint32_t      grown, budget;
	uint32_t      orig;    // the frame's slots before the pass: what the analyzer bound
	clj_value     def_var; // a call of the var a def is binding reaches the old root, never the fn being defined
} inline_frame;

// One past the highest slot a let, loop or catch of n's own frame binds.
static void bound_top(const clj_node *n, void *ctx) {
	uint32_t *top = ctx;
	switch (n->kind) {
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n; i++) {
			if (n->u.let.slots[i] + 1 > *top) *top = n->u.let.slots[i] + 1;
		}
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
			if (n->u.try_.catches[i].slot + 1 > *top) *top = n->u.try_.catches[i].slot + 1;
		}
		break;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) bound_top(n->u.fused.args[i], ctx);
		return;
	default: break;
	}
	clj_node_children(n, bound_top, ctx);
}

// Where a site's fresh slots start: past the analyzer's and past any its own subtree binds. Sites side by side share
// numbers, whose bindings never overlap; nested ones never do. Every backend takes a slot rebound in its turn, as a
// loop's let rebinds it.
static uint32_t fresh_base(const inline_frame *fr, const clj_node *site) {
	uint32_t top = fr->orig;
	bound_top(site, &top);
	return top;
}

static bool slots_fit(const inline_frame *fr, uint64_t after) {
	uint32_t before = fr->arity->nslots;
	if (after < before) after = before;
	return after <= INLINE_SLOTS_MAX && (before > INLINE_SLOTS_SMALL || after <= INLINE_SLOTS_SMALL);
}

typedef enum { ARG_BIND, ARG_SUBST, ARG_VAR, ARG_LITERAL, ARG_DIRECT } arg_how;

// A literal fn the calls of a parameter can become: a fixed arity for every count they pass, each small and pure and
// free of a fn, its own name and a recur to itself, none of which its copy in the calling frame has. Its nodes and
// slots, of the largest arity.
// @ai-generated(solo)
static bool literal_ok(const clj_node *lit, uint32_t counts, uint32_t *nodes, uint32_t *slots) {
	*nodes = *slots = 0;
	if (lit->kind != CLJ_NODE_FN || !counts) return false;
	for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
		if (!((counts >> k) & 1)) continue;
		const clj_fn_arity *a = lit->u.fn.fixed[k];
		if (!a) return false;
		// the site's tree is not numbered yet
		uint32_t size = nodes_in(a->body);
		if (size > INLINE_CALLEE_MAX) return false;
		callee_scan s = scan_arity(a, CLJ_NIL);
		if (s.arity_recur || s.self_read || s.has_fn) return false;
		if (clj_specialize_effects_of(a->body, ~(uint32_t)INLINE_EFFECTS) & ~(uint32_t)INLINE_EFFECTS) return false;
		if (size + k + 2 > *nodes) *nodes = size + k + 2;
		if (a->nslots > *slots) *slots = a->nslots;
	}
	return true;
}

// A literal fn too big to copy, called as a direct fn of the frame: its arity, no closure (try_direct decides).
static bool direct_ok(const clj_node *lit, uint32_t counts) {
	if (lit->kind != CLJ_NODE_FN || lit->u.fn.variadic || !counts) return false;
	for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
		if (((counts >> k) & 1) && !lit->u.fn.fixed[k]) return false;
	}
	return true;
}

// A var argument whose call can read the var where it calls, as a call of the var: an intrinsic on its builtin or a
// closure this pass would inline, both pure. The caller makes the read exact: once, and only pure code in between.
static bool var_head_ok(const clj_node *arg, uint32_t counts) {
	if (arg->kind != CLJ_NODE_VAR || !clj_is_var(arg->u.var.v) || clj_var_is_dynamic(arg->u.var.v) || __builtin_popcount(counts) != 1) return false;
	uint32_t             k = (uint32_t)__builtin_ctz(counts);
	clj_value            var = arg->u.var.v, root = clj_var_root(var);
	const clj_intrinsic *op = clj_intrinsic_find(var, k);
	if (op) return op->pure && root == clj_intrinsic_builtin(op);
	return clj_is_fn(root) && clj_fn_of(root)->kind == CLJ_FN_CLOSURE && !clj_fn_of(root)->nenv && clj_specialize_var_inlinable(var, k, INLINE_EFFECTS);
}

static bool trivial_from(const clj_node *const *args, uint32_t from, uint32_t n) {
	for (uint32_t i = from; i < n; i++) {
		if (!trivial_read(args[i])) return false;
	}
	return true;
}

static clj_node *guarded_call(clj_node *n, clj_node *then, clj_value var, clj_value root, const clj_fusion_var **guards, uint32_t nguards) {
	position  at = {n->line, n->col};
	clj_node *test = node_at(CLJ_NODE_ROOT_IS, at);
	clj_slot_store(&test->h, &test->u.root_is.var, clj_retain(var));
	clj_slot_store(&test->h, &test->u.root_is.expect, clj_retain(root));
	test->u.root_is.guards = guards;
	test->u.root_is.nguards = nguards;
	clj_node *call = node_at(CLJ_NODE_INVOKE, at);
	call->u.invoke = n->u.invoke;
	n->kind = CLJ_NODE_IF;
	n->u.if_.test = test;
	n->u.if_.then = then;
	n->u.if_.else_ = call;
	return n;
}

static bool try_inline(clj_node *n, inline_frame *fr, uint32_t depth, uint32_t base);

// A call a substituted var became: the intrinsic it names, or that closure's body in turn, one level deeper.
static void inline_heads(copy_ctx *c, inline_frame *fr, uint32_t depth, uint32_t base) {
	for (uint32_t i = 0; i < c->nheads; i++) {
		rewrite_invoke(c->heads[i]);
		if (c->heads[i]->kind == CLJ_NODE_INVOKE && try_inline(c->heads[i], fr, depth, base)) base = fr->arity->nslots;
	}
}

// depth: COPY and PATH bodies around the site within the frame; base: the site's first fresh slot.
// @ai-generated(solo)
static bool try_inline(clj_node *n, inline_frame *fr, uint32_t depth, uint32_t base) {
	const clj_node *head = n->u.invoke.fn;
	uint32_t        nargs = n->u.invoke.n;
	if (head->kind != CLJ_NODE_VAR || nargs > CLJ_FN_MAX_FIXED || depth + 1 > CLJ_INLINE_DEPTH_MAX) return false;
	clj_value var = head->u.var.v;
	if (!clj_is_var(var) || clj_var_is_dynamic(var) || var == fr->def_var) return false;
	clj_value root = clj_var_root(var);
	if (!clj_is_fn(root) || clj_fn_of(root)->kind != CLJ_FN_CLOSURE || clj_fn_of(root)->nenv) return false;
	const clj_node     *code = clj_fn_of(root)->u.node;
	const clj_fn_arity *a = code->u.fn.fixed[nargs];
	if (!a || a->body->nnodes > INLINE_CALLEE_MAX) return false;
	callee_scan s = scan_arity(a, fr->def_var);
	if (s.arity_recur || s.self_read || s.has_fn || s.calls_def) return false;
	const clj_node *const *args = n->u.invoke.args;
	// A parameter the body calls is called where the body calls it: only the copy's own code need be pure.
	bool calls = false;
	for (uint32_t i = 0; i < nargs; i++) calls = calls || s.heads[i];
	if (calls && clj_opt_off(CLJ_NO_OPT_BETA)) return false;
	arg_how  how[CLJ_FN_MAX_FIXED + 1];
	int64_t  added = (int64_t)a->nslots - (s.self != SLOT_NONE ? 1 : 0);
	uint32_t cost = a->body->nnodes + 4;
	for (uint32_t i = 0; i < nargs; i++) {
		uint32_t lnodes, lslots;
		bool     only = s.heads[i] && ((s.head_only >> i) & 1);
		how[i] = ARG_BIND;
		if (only && literal_ok(args[i], s.head_counts[i], &lnodes, &lslots)) {
			how[i] = ARG_LITERAL;
			added += (int64_t)s.heads[i] * lslots - 1;
			cost += s.heads[i] * lnodes;
		} else if (only && direct_ok(args[i], s.head_counts[i])) {
			how[i] = ARG_DIRECT;
		} else if (trivial_read(args[i]) && ((s.local_only >> i) & 1) && (!s.heads[i] || args[i]->kind == CLJ_NODE_LOCAL)) {
			how[i] = ARG_SUBST;
			added--;
		}
	}
	// A var argument is read at its one call, not at the argument, when only pure code runs between the two: no loop
	// around the call, trivial arguments after it, and no call of a value nobody knows.
	uint64_t vars = 0;
	bool     unknown = false;
	for (uint32_t i = 0; i < nargs; i++) {
		if (!s.heads[i] || how[i] == ARG_LITERAL) continue;
		if (((s.head_only >> i) & 1) && s.heads[i] == 1 && !((s.head_looped >> i) & 1) && trivial_from(args, i + 1, nargs) &&
		    var_head_ok(args[i], s.head_counts[i]))
			vars |= param_bit(i);
		else unknown = true;
	}
	for (uint32_t i = 0; i < nargs; i++) {
		if (!unknown && ((vars >> i) & 1)) {
			how[i] = ARG_VAR;
			added--;
			cost++;
		}
		if (how[i] == ARG_BIND || how[i] == ARG_DIRECT) cost += nodes_in(args[i]);
	}
	if (!slots_fit(fr, (uint64_t)base + (uint64_t)(added > 0 ? added : 0)) || fr->grown + cost > fr->budget) return false;
	// Last: the summary may walk the callee's tree. A call of a parameter leaves it open, so then only that it was walked.
	if (!clj_specialize_var_inlinable(var, nargs, calls ? UINT32_MAX : INLINE_EFFECTS)) return false;

	uint32_t        *map = zalloc(a->nslots, sizeof *map);
	const clj_node **sub = zalloc(a->nslots, sizeof *sub), **lit = zalloc(a->nslots, sizeof *lit);
	uint32_t         next = base;
	uint64_t         free_calls = 0; // the slots whose calls in the copy are the callee's calls of a parameter
	for (uint32_t k = 0; k < a->nslots; k++) {
		arg_how h = k < nargs ? how[k] : ARG_BIND;
		if (h == ARG_SUBST || h == ARG_VAR) {
			sub[k] = args[k];
			map[k] = SLOT_NONE;
			if (s.heads[k] && h == ARG_SUBST) free_calls |= param_bit(args[k]->u.local.index);
		} else if (h == ARG_LITERAL) {
			lit[k] = args[k];
			map[k] = SLOT_NONE;
		} else if (k == s.self) {
			map[k] = SLOT_NONE;
		} else {
			map[k] = next++;
			if (k < nargs && s.heads[k]) free_calls |= param_bit(map[k]);
		}
	}
	position  at = {n->line, n->col};
	copy_ctx  c = {.map = map, .subst = sub, .literal = lit, .nmap = a->nslots, .next = &next, .depth = depth + 1};
	clj_node *inl = inline_node(code, CLJ_INLINE_COPY, copy_node(a->body, &c, 0), at);
	if (calls && (clj_specialize_effects_calling(inl->u.inline_.body, ~(uint32_t)INLINE_EFFECTS, free_calls) & ~(uint32_t)INLINE_EFFECTS)) {
		clj_release(clj_from_ptr(inl));
		copy_ctx_free(&c);
		free(map);
		free(sub);
		free(lit);
		return false;
	}
	clj_node *then = inl;
	uint32_t  nbound = 0;
	for (uint32_t i = 0; i < nargs; i++) nbound += how[i] == ARG_BIND || how[i] == ARG_DIRECT;
	copy_ctx argc = {.depth = 0};
	if (nbound) {
		uint32_t        *slots = zalloc(nbound, sizeof *slots);
		const clj_node **inits = zalloc(nbound, sizeof *inits);
		for (uint32_t i = 0, k = 0; i < nargs; i++) {
			if (how[i] != ARG_BIND && how[i] != ARG_DIRECT) continue;
			slots[k] = map[i];
			inits[k++] = copy_node(args[i], &argc, VERBATIM);
		}
		then = let_node(slots, inits, nbound, inl, at);
		for (uint32_t i = 0, k = 0; i < nargs; i++) {
			if (how[i] != ARG_BIND && how[i] != ARG_DIRECT) continue;
			if (how[i] == ARG_DIRECT) try_direct(then, k);
			k++;
		}
	}
	guarded_call(n, then, var, root, NULL, 0);
	if (next > fr->arity->nslots) fr->arity->nslots = next;
	fr->grown += c.nodes + argc.nodes + 4;
	inline_heads(&c, fr, depth + 1, next);
	copy_ctx_free(&c);
	copy_ctx_free(&argc);
	free(map);
	free(sub);
	free(lit);
	return true;
}

// ---- known paths: (get-in m [k1 k2] nf), (assoc-in m [k1 k2] v), (update-in m [k1 k2] f a...) over a literal path
// vector of at most CLJ_INLINE_PATH_MAX keys become the gets and assocs core's own recursion makes, each level an
// INLINE of the fn whose frame it is, so a trace shows the frames the recursion would. Recursion is never unrolled in
// general: this is core's definition over a path whose length the site says. The guard: the var and each core var
// whose calls the unrolling took away (the destructuring's seq, first and next; apply; reduce) hold their boot roots;
// the gets and assocs stay calls of their vars, as in core's body. A redefinition by code the path itself runs (a
// get on a user's ILookup) is seen from the next call on, not from the next level.

typedef enum { PATH_GET_IN2, PATH_GET_IN3, PATH_ASSOC_IN, PATH_UPDATE_IN } path_kind;
// update-in's arguments past f; the vars a guard checks; the nodes a site may grow by, its argument copies included
enum { PATH_EXTRA_MAX = 4, PATH_GUARDS_MAX = 5, PATH_COST_MAX = 160 };

static const char *const path_guard_names[][PATH_GUARDS_MAX] = {
	[PATH_GET_IN2] = {"clojure.core/get-in", "clojure.core/reduce", "clojure.core/reduced?", "clojure.core/unreduced", NULL},
	[PATH_GET_IN3] = {"clojure.core/get-in", "clojure.core/seq", "clojure.core/first", "clojure.core/next", NULL},
	[PATH_ASSOC_IN] = {"clojure.core/assoc-in", "clojure.core/seq", "clojure.core/first", "clojure.core/next", NULL},
	[PATH_UPDATE_IN] = {"clojure.core/update-in", "clojure.core/seq", "clojure.core/first", "clojure.core/next", "clojure.core/apply"},
};

// An operand of the call: a trivial read copied at each use, or the slot its value was bound to where the call ran it.
typedef struct {
	const clj_node *read;
	uint32_t        slot;
} operand;

typedef struct {
	path_kind       kind;
	position        at;
	uint32_t       *next;
	copy_ctx        copies; // of the trivial operands, verbatim
	uint32_t        depth;  // COPY and PATH bodies around the levels, the PATH one included
	const clj_node *level_fn;
	position        level_at; // where core's recursion calls the next level: a trace shows a level's frame there
	uint32_t        nkeys, nextra;
	operand         m, keys[CLJ_INLINE_PATH_MAX], last, f, extra[PATH_EXTRA_MAX]; // last: get-in's not-found, assoc-in's v
	const clj_node *literal; // update-in's f, a literal fn copied where it is called
	clj_value       f_var;   // update-in's f, a var read where it is called; nil otherwise
	uint32_t        nodes;
	clj_value       get2, get3, assoc, identical, reduced_p, unreduced, not_found;
} path_ctx;

static clj_value table_var(const char *qualified, uint32_t arity) {
	clj_value            sym = clj_symbol_from_cstr(qualified);
	const clj_intrinsic *op = clj_intrinsic_find_named(sym, arity);
	const clj_fusion_var *fv = op ? NULL : clj_fusion_find_named(sym);
	clj_release(sym);
	return op ? clj_intrinsic_var(op) : fv ? clj_fusion_var_of(fv) : CLJ_NIL;
}

static clj_node *use(path_ctx *p, const operand *o) { return o->read ? copy_node(o->read, &p->copies, VERBATIM) : local_node(o->slot, p->at); }

// (var a b c) of the first n, an intrinsic as core's body has it.
static clj_node *core_call(path_ctx *p, clj_value var, uint32_t n, clj_node *a, clj_node *b, clj_node *c) {
	clj_node        *in[3] = {a, b, c};
	const clj_node **args = zalloc(n, sizeof *args);
	for (uint32_t i = 0; i < n; i++) args[i] = in[i];
	clj_node *call = invoke_node(var_node(var, p->at), args, n, p->at);
	rewrite_invoke(call);
	return call;
}

static clj_node *let1(uint32_t slot, clj_node *init, clj_node *body, position at) {
	uint32_t        *slots = zalloc(1, sizeof *slots);
	const clj_node **inits = zalloc(1, sizeof *inits);
	slots[0] = slot;
	inits[0] = init;
	return let_node(slots, inits, 1, body, at);
}

static clj_node *if_node(clj_node *test, clj_node *then, clj_node *else_, position at) {
	clj_node *n = node_at(CLJ_NODE_IF, at);
	n->u.if_.test = test;
	n->u.if_.then = then;
	n->u.if_.else_ = else_;
	return n;
}

static operand fresh(path_ctx *p) { return (operand){NULL, (*p->next)++}; }

// (reduce get m ks): a reduced value ends the walk unwrapped, as reduce's step does.
static clj_node *path_get2(path_ctx *p, uint32_t i, operand cur) {
	clj_node *g = core_call(p, p->get2, 2, use(p, &cur), use(p, &p->keys[i]), NULL);
	if (i + 1 == p->nkeys) return core_call(p, p->unreduced, 1, g, NULL, NULL);
	operand   a = fresh(p);
	clj_node *done = core_call(p, p->unreduced, 1, use(p, &a), NULL, NULL);
	return let1(a.slot, g, if_node(core_call(p, p->reduced_p, 1, use(p, &a), NULL, NULL), done, path_get2(p, i + 1, a), p->at), p->at);
}

// get-in's loop: (get m k ::not-found) per key, not-found at the first miss.
static clj_node *path_get3(path_ctx *p, uint32_t i, operand cur) {
	operand   v = fresh(p);
	clj_node *g = core_call(p, p->get3, 3, use(p, &cur), use(p, &p->keys[i]), const_node(p->not_found, p->at));
	clj_node *miss = core_call(p, p->identical, 2, use(p, &v), const_node(p->not_found, p->at), NULL);
	clj_node *hit = i + 1 == p->nkeys ? use(p, &v) : path_get3(p, i + 1, v);
	return let1(v.slot, g, if_node(miss, use(p, &p->last), hit, p->at), p->at);
}

// update-in's (apply f (get m k) args), the args spread at the site.
static clj_node *path_call_f(path_ctx *p, clj_node *x) {
	uint32_t         n = 1 + p->nextra;
	const clj_node **args = zalloc(n, sizeof *args);
	args[0] = x;
	for (uint32_t i = 0; i < p->nextra; i++) args[1 + i] = use(p, &p->extra[i]);
	// apply, a native, enters f with no call site of its own: the frame stands at the fn
	if (p->literal) return literal_call(p->literal, args, n, (position){p->literal->line, p->literal->col}, p->next, p->depth, &p->nodes);
	clj_node *call = invoke_node(clj_is_nil(p->f_var) ? use(p, &p->f) : var_node(p->f_var, p->at), args, n, (position){0, 0});
	if (!clj_is_nil(p->f_var)) head_add(&p->copies, call);
	return call;
}

// (assoc m k (assoc-in (get m k) ks v)) per level, (assoc m k v) at the last.
static clj_node *path_assoc(path_ctx *p, uint32_t i, operand cur) {
	clj_node *val;
	if (i + 1 == p->nkeys) {
		val = p->kind == PATH_ASSOC_IN ? use(p, &p->last) : path_call_f(p, core_call(p, p->get2, 2, use(p, &cur), use(p, &p->keys[i]), NULL));
	} else {
		operand   m = fresh(p);
		clj_node *get = core_call(p, p->get2, 2, use(p, &cur), use(p, &p->keys[i]), NULL);
		val = let1(m.slot, get, inline_node(p->level_fn, CLJ_INLINE_LEVEL, path_assoc(p, i + 1, m), p->level_at), p->at);
	}
	return core_call(p, p->assoc, 3, use(p, &cur), use(p, &p->keys[i]), val);
}

// update-in's (let [up (fn up [m ks f args] ...)] (up m ks f args)): the fn whose frames its levels are.
static const clj_node *update_in_level_fn(const clj_node *code) {
	const clj_fn_arity *a = code->u.fn.variadic;
	if (!a || a->nparams != 3 || a->body->kind != CLJ_NODE_LET || a->body->u.let.n != 1) return NULL;
	const clj_node *up = a->body->u.let.inits[0];
	return up->kind == CLJ_NODE_FN || up->kind == CLJ_NODE_DIRECT_FN ? up : NULL;
}

typedef struct {
	const clj_node *fn;  // a direct call of it
	clj_value       var; // or a call of the var
	position        at;
	bool            found;
} call_find;

static void find_call(const clj_node *n, void *ctx) {
	call_find *c = ctx;
	if (c->found) return;
	if ((n->kind == CLJ_NODE_DIRECT_CALL && n->u.direct.fn == c->fn) ||
	    (n->kind == CLJ_NODE_INVOKE && n->u.invoke.fn->kind == CLJ_NODE_VAR && n->u.invoke.fn->u.var.v == c->var)) {
		c->at = (position){n->line, n->col};
		c->found = true;
		return;
	}
	clj_node_children(n, find_call, ctx);
}

// The position of the first call of fn (direct) or var in body; dflt when there is none.
static position call_at(const clj_node *body, const clj_node *fn, clj_value var, position dflt) {
	call_find c = {fn, var, dflt, false};
	find_call(body, &c);
	return c.at;
}

// The operand at an argument, a binding of its own when it is not a trivial read: the let runs them in the call's order.
static operand bind_operand(path_ctx *p, const clj_node *arg, uint32_t *slots, const clj_node **inits, uint32_t *nbound) {
	if (trivial_read(arg)) return (operand){arg, 0};
	operand o = fresh(p);
	slots[*nbound] = o.slot;
	inits[(*nbound)++] = copy_node(arg, &p->copies, VERBATIM);
	return o;
}

// @ai-generated(solo)
static bool try_path(clj_node *n, inline_frame *fr) {
	const clj_node *head = n->u.invoke.fn;
	uint32_t        nargs = n->u.invoke.n;
	if (head->kind != CLJ_NODE_VAR || clj_opt_off(CLJ_NO_OPT_PATH)) return false;
	clj_value             var = head->u.var.v;
	const clj_fusion_var *self = clj_fusion_find(var);
	if (!self || self->role != CLJ_FUSION_GUARD || clj_var_is_dynamic(var)) return false;
	const char *name = strchr(self->name, '/') + 1;
	path_kind   kind;
	if (strcmp(name, "get-in") == 0 && (nargs == 2 || nargs == 3)) kind = nargs == 2 ? PATH_GET_IN2 : PATH_GET_IN3;
	else if (strcmp(name, "assoc-in") == 0 && nargs == 3) kind = PATH_ASSOC_IN;
	else if (strcmp(name, "update-in") == 0 && nargs >= 3 && nargs <= 3 + PATH_EXTRA_MAX) kind = PATH_UPDATE_IN;
	else return false;
	clj_value root = clj_var_root(var);
	if (root != clj_fusion_boot_root(self) || !clj_is_fn(root) || clj_fn_of(root)->kind != CLJ_FN_CLOSURE || clj_fn_of(root)->nenv) return false;
	const clj_node *code = clj_fn_of(root)->u.node, *level_fn = kind == PATH_UPDATE_IN ? update_in_level_fn(code) : code;
	if (!level_fn) return false;
	const clj_node *path = n->u.invoke.args[1];
	bool            literal = path->kind == CLJ_NODE_VECTOR;
	uint32_t        nkeys = literal ? path->u.seq.n : path->kind == CLJ_NODE_CONST && clj_is_vector(path->u.value.v) ? clj_vector_count(path->u.value.v) : 0;
	if (nkeys == 0 || nkeys > CLJ_INLINE_PATH_MAX) return false;
	const clj_fusion_var *guards[PATH_GUARDS_MAX];
	uint32_t              nguards = 0;
	for (uint32_t i = 0; i < PATH_GUARDS_MAX && path_guard_names[kind][i]; i++) {
		clj_value sym = clj_symbol_from_cstr(path_guard_names[kind][i]);
		guards[nguards] = clj_fusion_find_named(sym);
		clj_release(sym);
		if (!guards[nguards++]) return false;
	}

	uint32_t base = fresh_base(fr, n), next = base;
	path_ctx p = {.kind = kind, .at = {n->line, n->col}, .next = &next, .depth = 1, .level_fn = level_fn, .nkeys = nkeys, .f_var = CLJ_NIL,
	              .get2 = table_var("clojure.core/get", 2), .get3 = table_var("clojure.core/get", 3), .assoc = table_var("clojure.core/assoc", 3),
	              .identical = table_var("clojure.core/identical?", 2), .reduced_p = table_var("clojure.core/reduced?", 1),
	              .unreduced = table_var("clojure.core/unreduced", 1), .not_found = clj_keyword_from_cstr("clojure.core/not-found")};
	if (clj_is_nil(p.get2) || clj_is_nil(p.get3) || clj_is_nil(p.assoc) || clj_is_nil(p.identical) || clj_is_nil(p.reduced_p) || clj_is_nil(p.unreduced))
		return false;
	position entry = p.at;
	p.level_at = p.at;
	if (kind == PATH_ASSOC_IN && code->u.fn.fixed[3]) p.level_at = call_at(code->u.fn.fixed[3]->body, NULL, var, p.at);
	if (kind == PATH_UPDATE_IN) {
		entry = call_at(code->u.fn.variadic->body->u.let.body, level_fn, CLJ_NIL, p.at);
		// up's one arity, [m ks f args]
		if (level_fn->u.fn.fixed[4]) p.level_at = call_at(level_fn->u.fn.fixed[4]->body, level_fn, CLJ_NIL, p.at);
	}
	const clj_node *const *args = n->u.invoke.args;
	uint32_t               slots[2 + CLJ_INLINE_PATH_MAX + 1 + PATH_EXTRA_MAX], nbound = 0;
	const clj_node        *inits[2 + CLJ_INLINE_PATH_MAX + 1 + PATH_EXTRA_MAX];
	clj_node              *consts[CLJ_INLINE_PATH_MAX] = {0};
	// the operands in the order the call runs them: m, the path's items, then not-found, v, or f and its arguments
	p.m = bind_operand(&p, args[0], slots, inits, &nbound);
	for (uint32_t i = 0; i < nkeys; i++) {
		if (literal) {
			p.keys[i] = bind_operand(&p, path->u.seq.items[i], slots, inits, &nbound);
		} else {
			consts[i] = const_node(clj_vector_nth(path->u.value.v, i), p.at);
			p.keys[i] = (operand){consts[i], 0};
		}
	}
	if (kind == PATH_UPDATE_IN) {
		p.nextra = nargs - 3;
		uint32_t lnodes, lslots, counts = 1u << (1 + p.nextra);
		if (literal_ok(args[2], counts, &lnodes, &lslots)) p.literal = args[2];
		else if (var_head_ok(args[2], counts) && trivial_from(args, 3, nargs)) p.f_var = args[2]->u.var.v;
		else p.f = bind_operand(&p, args[2], slots, inits, &nbound);
		for (uint32_t i = 0; i < p.nextra; i++) p.extra[i] = bind_operand(&p, args[3 + i], slots, inits, &nbound);
	} else if (kind != PATH_GET_IN2) {
		p.last = bind_operand(&p, args[2], slots, inits, &nbound);
	}
	clj_node *body;
	switch (kind) {
	case PATH_GET_IN2: body = path_get2(&p, 0, p.m); break;
	case PATH_GET_IN3: body = path_get3(&p, 0, p.m); break;
	case PATH_ASSOC_IN: body = path_assoc(&p, 0, p.m); break;
	case PATH_UPDATE_IN: body = inline_node(level_fn, CLJ_INLINE_LEVEL, path_assoc(&p, 0, p.m), entry); break;
	}
	clj_node *then = inline_node(code, CLJ_INLINE_PATH, body, p.at);
	if (nbound) {
		uint32_t        *s = zalloc(nbound, sizeof *s);
		const clj_node **in = zalloc(nbound, sizeof *in);
		memcpy(s, slots, nbound * sizeof *s);
		memcpy(in, inits, nbound * sizeof *in);
		then = let_node(s, in, nbound, then, p.at);
	}
	for (uint32_t i = 0; i < nkeys; i++) {
		if (consts[i]) clj_release(clj_from_ptr(consts[i]));
	}
	uint32_t cost = nodes_in(then) + 4;
	// a site's own bound, not the frame's budget: the growth is linear in the paths the source writes, as fast arms are
	if (!slots_fit(fr, next) || cost > PATH_COST_MAX) {
		clj_release(clj_from_ptr(then));
		copy_ctx_free(&p.copies);
		return false;
	}
	const clj_fusion_var **g = zalloc(nguards, sizeof *g);
	memcpy(g, guards, nguards * sizeof *g);
	guarded_call(n, then, var, root, g, nguards);
	if (next > fr->arity->nslots) fr->arity->nslots = next;
	inline_heads(&p.copies, fr, 1, next);
	copy_ctx_free(&p.copies);
	return true;
}

// ---- lowered natives (design §10 step 7a′): a call of reduce, reduce-kv, swap! or swap-vals! whose fn is a literal
//   (if ROOT_IS[reduce, builtin] LOWERED[fn, init' coll'] (reduce (fn ...) init coll))
// The native's own C function runs over the literal's arity reached without a closure (eval_lowered, emit_lowered), so
// the walk, the lock and every other rule of the call are the native's. The else branch is the call, as for an inline.

enum { LOWER_COST_MAX = 64 }; // nodes of the operands a lowered site copies

// @ai-generated(solo)
static bool try_lower(clj_node *n) {
	const clj_node *head = n->u.invoke.fn;
	uint32_t        nargs = n->u.invoke.n;
	clj_lowered_how how;
	if (head->kind != CLJ_NODE_VAR || clj_opt_off(CLJ_NO_OPT_LOWER)) return false;
	clj_value var = head->u.var.v;
	if (!clj_is_var(var) || clj_var_is_dynamic(var) || !clj_lowered_find(var, nargs, &how)) return false;
	clj_value root = clj_var_root(var);
	if (root != clj_lowered_root(how)) return false;
	uint32_t at, arity;
	clj_lowered_shape(how, nargs, &at, &arity);
	const clj_node *lit = n->u.invoke.args[at];
	if (lit->kind != CLJ_NODE_FN || arity > CLJ_FN_MAX_FIXED || !lit->u.fn.fixed[arity]) return false;
	// no fn object stands behind the arity for its own name to read
	if (scan_arity(lit->u.fn.fixed[arity], CLJ_NIL).self_read) return false;
	// a site's own bound, as a known path's: it copies the operands and no body, so its growth is linear in the source
	uint32_t cost = 2;
	for (uint32_t i = 0; i < nargs; i++) cost += i == at ? 0 : nodes_in(n->u.invoke.args[i]);
	if (cost > LOWER_COST_MAX) return false;
	position         at_pos = {n->line, n->col};
	copy_ctx         argc = {.depth = 0};
	const clj_node **ops = zalloc(nargs - 1, sizeof *ops);
	for (uint32_t i = 0, k = 0; i < nargs; i++) {
		if (i != at) ops[k++] = copy_node(n->u.invoke.args[i], &argc, VERBATIM);
	}
	clj_node *low = node_at(CLJ_NODE_LOWERED, at_pos);
	low->u.lowered.fn = lit;
	low->u.lowered.args = ops;
	low->u.lowered.n = nargs - 1;
	low->u.lowered.how = (uint8_t)how;
	guarded_call(n, low, var, root, NULL, 0);
	copy_ctx_free(&argc);
	return true;
}

// Children first: an argument is inlined into before the call it is passed to, and the copy of it carries that.
// @ai-generated(solo)
static void inline_walk(const clj_node *n, void *ctx) {
	inline_frame *fr = ctx;
	clj_node     *m = (clj_node *)n;
	switch (m->kind) {
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN:
		for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED + 1; k++) {
			clj_fn_arity *a = k <= CLJ_FN_MAX_FIXED ? m->u.fn.fixed[k] : m->u.fn.variadic;
			if (!a) continue;
			uint32_t     size = nodes_in(a->body);
			inline_frame inner = {a, 0, size > INLINE_GROWTH_MIN ? size : INLINE_GROWTH_MIN, a->nslots, fr->def_var};
			inline_walk(a->body, &inner);
		}
		return;
	case CLJ_NODE_FUSED:
		// the programs are a frame of their own, and calls there are the fusion table's
		for (uint32_t i = 0; i < m->u.fused.nargs; i++) inline_walk(m->u.fused.args[i], ctx);
		return;
	case CLJ_NODE_DEF: {
		// a macro runs at expansion, where the copy is all cost
		if (m->u.def.macro) return;
		clj_value saved = fr->def_var;
		fr->def_var = m->u.def.var.v;
		clj_node_children(m, inline_walk, ctx);
		fr->def_var = saved;
		return;
	}
	default: break;
	}
	clj_node_children(m, inline_walk, ctx);
	if (m->kind == CLJ_NODE_INVOKE && fr->arity && !try_lower(m) && !try_path(m, fr)) try_inline(m, fr, 0, fresh_base(fr, m));
}

// ---- scalar replacement (design §10 step 7б; §4 «Известный shape + не убегает = мапы нет вообще»): a let-bound
// tuple literal (1 to 6 items), keyword-map literal, or an if of two such literals of one layout, is its fields in
// slots. A read the layout alone answers — (nth t 0) (:k m) (get m :k nf) (count t) (vector? t) (if t ...) — is the
// field or a constant, asked of the runtime on a sample whose fields are sentinels; (let [u t]) names the same fields;
// (assoc m :k v) of a key the map has derives a value; any other use is the literal again over the fields, the
// constructor the binding would have run. At most one such materialization on any path of the binding's extent, and
// none in a loop, so identical? sees one object per binding; a value materialized on every path keeps its allocation.
// An intrinsic's answer holds while its var holds the builtin: the value then binds g (ROOT_IS over those ops) once,
// each rewritten expression is (if g E' E), and E reads the object bound (if g nil MAT). A
// redefinition by the extent's own code is seen from the next binding on, as a known path's is.

enum {
	SR_TUPLE_MAX = 6,   // clj_vector_from_array lays 1 to 6 items out inline
	SR_KEYS_MAX = 32,   // a shape's keys
	SR_VALUES_MAX = 16, // the candidate and the values its assocs derive
	SR_OPS_MAX = 16,    // intrinsics one guard checks
	SR_GROWTH_MIN = 64, // nodes a frame may grow by, or by its own size when that is larger
	SR_PEND_MAX = 8,    // field bindings a derived value leaves for the let that binds it
};

typedef enum { SR_TUPLE, SR_MAP } sr_kind;

typedef struct {
	sr_kind   kind;
	uint32_t  n;
	clj_node *src[SR_KEYS_MAX];  // owned: what a read of the field copies (a LOCAL of its slot, a CONST, a trivial read)
	clj_value keys[SR_KEYS_MAX]; // SR_MAP: the literal's keys in its order, interned keywords
	position  at;
} sr_value;

typedef struct {
	clj_fn_arity *arity;
	uint32_t      grown, budget;
	uint32_t      size; // the arity body's nodes before the pass
} sr_frame;

typedef struct {
	sr_frame            *fr;
	uint32_t             next; // the frame's next fresh slot
	sr_value             vals[SR_VALUES_MAX];
	uint32_t             nvals;
	int8_t               alias[INLINE_SLOTS_MAX]; // slot -> the value it names, -1
	const clj_intrinsic *ops[SR_OPS_MAX];
	uint32_t             nops;
	uint32_t             loops;  // loop bodies entered within the expression
	uint32_t             guarded_args; // arguments of an inlined or lowered call's else branch being rewritten
	bool                 fail;
	clj_node           **mats; // every materialization made, for the path count
	uint8_t             *mat_of;
	uint32_t             nmats, cmats;
	uint32_t             pend_slots[SR_PEND_MAX]; // a derived value's field bindings, for the let that binds it
	clj_node            *pend_inits[SR_PEND_MAX];
	uint32_t             npend;
} sr_ctx;

static _Atomic uint64_t sr_counts[5]; // values replaced, of them guarded, materializations, if-joins, entry twins

static void scalar_walk(const clj_node *n, void *ctx);

void clj_debug_scalar_counts(uint64_t out[5]) {
	for (int i = 0; i < 5; i++) out[i] = atomic_load_explicit(&sr_counts[i], memory_order_relaxed);
}

static int sr_alias(const sr_ctx *c, const clj_node *n) {
	return n->kind == CLJ_NODE_LOCAL && n->u.local.index < INLINE_SLOTS_MAX ? c->alias[n->u.local.index] : -1;
}

static clj_node *sr_copy(const clj_node *n) {
	copy_ctx  cc = {.depth = 0};
	clj_node *m = copy_node(n, &cc, VERBATIM);
	copy_ctx_free(&cc);
	return m;
}

static void sr_free(const clj_node *n) {
	if (n) clj_release(clj_from_ptr((void *)n));
}

static void sr_replace(const clj_node **pp, clj_node *with) {
	sr_free(*pp);
	*pp = with;
}

// The node at *pp, out of its parent, which keeps a nil in its place.
static clj_node *sr_detach(const clj_node **pp) {
	clj_node *n = (clj_node *)*pp;
	*pp = const_node(CLJ_NIL, (position){n->line, n->col});
	return n;
}

static bool sr_trivial(const sr_ctx *c, const clj_node *n) { return trivial_read(n) && sr_alias(c, n) < 0; }

static uint32_t sr_fresh(sr_ctx *c) { return c->next++; }

static void sr_need(sr_ctx *c, const clj_intrinsic *op) {
	for (uint32_t i = 0; i < c->nops; i++) {
		if (c->ops[i] == op) return;
	}
	if (c->nops == SR_OPS_MAX) {
		c->fail = true;
		return;
	}
	c->ops[c->nops++] = op;
}

static clj_node *sr_materialize(sr_ctx *c, uint32_t vi) {
	const sr_value *v = &c->vals[vi];
	clj_node       *m;
	if (v->kind == SR_TUPLE) {
		const clj_node **items = zalloc(v->n, sizeof *items);
		for (uint32_t i = 0; i < v->n; i++) items[i] = sr_copy(v->src[i]);
		m = vector_node(items, v->n, v->at);
	} else {
		const clj_node **items = zalloc(2 * v->n, sizeof *items);
		for (uint32_t i = 0; i < v->n; i++) {
			items[2 * i] = const_node(v->keys[i], v->at);
			items[2 * i + 1] = sr_copy(v->src[i]);
		}
		m = node_at(CLJ_NODE_MAP, v->at);
		m->u.seq.items = items;
		m->u.seq.n = 2 * v->n;
	}
	if (c->loops) c->fail = true;
	if (c->nmats == c->cmats) {
		c->cmats = c->cmats ? c->cmats * 2 : 8;
		c->mats = realloc(c->mats, c->cmats * sizeof *c->mats);
		c->mat_of = realloc(c->mat_of, c->cmats * sizeof *c->mat_of);
		if (!c->mats || !c->mat_of) clj_fatal("out of memory");
	}
	c->mats[c->nmats] = m;
	c->mat_of[c->nmats++] = (uint8_t)vi;
	return m;
}

// ---- the oracle: the runtime's own answer on a sample of the layout, its fields sentinels

enum { SR_SENT_NF = SR_KEYS_MAX, SR_SENT_NEW };

// Immediates, so the samples of concurrent compiles share no count; no layout answer is a fixnum this large.
static clj_value sr_sentinel(uint32_t i) { return clj_fixnum((intptr_t)0x5ca1a5e0000 + (intptr_t)i); }

static clj_value sr_sample(const sr_value *v) {
	clj_value items[2 * SR_KEYS_MAX];
	if (v->kind == SR_TUPLE) {
		for (uint32_t i = 0; i < v->n; i++) items[i] = sr_sentinel(i);
		return clj_vector_from_array(items, v->n);
	}
	for (uint32_t i = 0; i < v->n; i++) {
		items[2 * i] = v->keys[i];
		items[2 * i + 1] = sr_sentinel(i);
	}
	return clj_map_from_items(items, 2 * v->n, NULL);
}

// What a read answered: a field, the not-found argument, a constant, or nothing the layout fixes.
typedef enum { SR_NONE, SR_FIELD, SR_NF, SR_CONST } sr_answer;

static sr_answer sr_classify(clj_value r, const sr_value *v, bool consts, uint32_t *field, clj_value *k) {
	if (r == CLJ_THROWN) {
		clj_release(clj_take_pending());
		return SR_NONE;
	}
	for (uint32_t i = 0; i < v->n; i++) {
		if (r == sr_sentinel(i)) {
			*field = i;
			clj_release(r);
			return SR_FIELD;
		}
	}
	if (r == sr_sentinel(SR_SENT_NF)) {
		clj_release(r);
		return SR_NF;
	}
	if (consts && clj_node_foldable(r) && (clj_is_nil(r) || clj_is_bool(r) || clj_is_fixnum(r))) {
		*k = r;
		return SR_CONST;
	}
	clj_release(r);
	return SR_NONE;
}

// Answers that depend on the layout alone: = or compare would answer for the sentinels, not the fields.
static bool sr_layout_op(const clj_intrinsic *op) {
	if (op->arity == 1) {
		clj_intrinsic_1 f = op->fn.f1;
		return f == clj_count || f == clj_first || f == clj_not || f == clj_nil_p || f == clj_empty_p || f == clj_number_p ||
		       f == clj_integer_p || f == clj_char_p || f == clj_string_p || f == clj_keyword_p || f == clj_symbol_p || f == clj_fn_p ||
		       f == clj_vector_p || f == clj_map_p || f == clj_set_p || f == clj_list_p || f == clj_seq_p || f == clj_seqable_p ||
		       f == clj_sequential_p || f == clj_coll_p || f == clj_counted_p || f == clj_ifn_p || f == clj_associative_p ||
		       f == clj_indexed_p;
	}
	if (op->arity == 2) return op->fn.f2 == clj_nth2 || op->fn.f2 == clj_get2 || op->fn.f2 == clj_contains_p;
	return op->fn.f3 == clj_nth3 || op->fn.f3 == clj_get;
}

static int sr_rw(const clj_node **pp, sr_ctx *c, bool bind);

// A not-found argument with effects still runs before the field is read, as the call's operand did.
static int sr_answered(const clj_node **pp, sr_ctx *c, bool bind, sr_answer a, uint32_t field, clj_value k, const clj_node **nf, uint32_t vi) {
	position at = {(*pp)->line, (*pp)->col};
	if (a == SR_NF) {
		clj_node *keep = sr_detach(nf);
		sr_replace(pp, keep);
		return sr_rw(pp, c, bind);
	}
	clj_node *with = a == SR_FIELD ? sr_copy(c->vals[vi].src[field]) : const_node(k, at);
	if (a == SR_CONST) clj_release(k);
	if (nf && !sr_trivial(c, *nf)) {
		clj_node        *effect = sr_detach(nf);
		const clj_node **items = zalloc(2, sizeof *items);
		items[0] = effect;
		items[1] = with;
		clj_node *seq = node_at(CLJ_NODE_DO, at);
		seq->u.seq.items = items;
		seq->u.seq.n = 2;
		sr_replace(pp, seq);
		sr_rw(&items[0], c, false);
		sr_rw(&items[1], c, false);
		return -1;
	}
	sr_replace(pp, with);
	return sr_rw(pp, c, bind);
}

// (op v args...) of an intrinsic over a replaced value: answered by the oracle, or -2 when the call needs the object.
static int sr_intrinsic(const clj_node **pp, sr_ctx *c, bool bind, uint32_t vi) {
	clj_node            *n = (clj_node *)*pp;
	const clj_intrinsic *op = n->u.intrinsic.op;
	uint32_t             nargs = n->u.intrinsic.n;
	clj_value            var = n->u.intrinsic.var.v;
	if (clj_var_is_dynamic(var) || clj_var_root(var) != clj_intrinsic_builtin(op)) return -2;
	const sr_value *v = &c->vals[vi];
	bool            is_assoc = nargs == 3 && op->fn.f3 == clj_assoc3;
	if (!is_assoc && !sr_layout_op(op)) return -2;
	// every operand past the value is a constant, but a not-found argument
	bool             has_nf = nargs == 3 && !is_assoc;
	const clj_node **nf = has_nf ? &n->u.intrinsic.args[2] : NULL;
	clj_value        args[3] = {0};
	for (uint32_t i = 1; i < nargs; i++) {
		if ((nf && i == 2) || (is_assoc && i == 2)) continue;
		if (n->u.intrinsic.args[i]->kind != CLJ_NODE_CONST) return -2;
		args[i] = n->u.intrinsic.args[i]->u.value.v;
	}
	clj_value sample = sr_sample(v);
	args[0] = sample;
	if (is_assoc) {
		if (!bind || c->nvals == SR_VALUES_MAX) {
			clj_release(sample);
			return -2;
		}
		// a key the value has, answered as the same layout with one field the new one
		args[2] = sr_sentinel(SR_SENT_NEW);
		clj_value r = clj_intrinsic_call(op, args);
		clj_release(sample);
		if (r == CLJ_THROWN) {
			clj_release(clj_take_pending());
			return -2;
		}
		int32_t at = -1;
		bool    same = (v->kind == SR_TUPLE ? clj_is_vector(r) && clj_vector_count(r) == v->n : clj_is_map(r) && clj_map_count(r) == v->n);
		for (uint32_t i = 0; same && i < v->n; i++) {
			clj_value key = v->kind == SR_TUPLE ? clj_fixnum((intptr_t)i) : v->keys[i];
			clj_value got = clj_get(r, key, CLJ_UNBOUND);
			if (got == sr_sentinel(SR_SENT_NEW) && at < 0) at = (int32_t)i;
			else if (got != sr_sentinel(i)) same = false;
			clj_release(got);
		}
		clj_release(r);
		if (!same || at < 0) return -2;
		sr_need(c, op);
		uint32_t  nv = c->nvals++;
		sr_value *d = &c->vals[nv];
		*d = *v;
		for (uint32_t i = 0; i < v->n; i++) d->src[i] = (int32_t)i == at ? NULL : sr_copy(v->src[i]);
		const clj_node **val = &n->u.intrinsic.args[2];
		if (sr_rw(val, c, false), c->fail) return -1;
		if (sr_trivial(c, *val)) {
			d->src[at] = sr_detach(val);
		} else {
			if (c->npend == SR_PEND_MAX || c->next >= INLINE_SLOTS_MAX) {
				c->fail = true;
				return -1;
			}
			uint32_t s = sr_fresh(c);
			c->pend_slots[c->npend] = s;
			c->pend_inits[c->npend++] = sr_detach(val);
			d->src[at] = local_node(s, d->at);
		}
		sr_replace(pp, const_node(CLJ_NIL, (position){n->line, n->col}));
		return (int)nv;
	}
	if (nf) args[2] = sr_sentinel(SR_SENT_NF);
	clj_value r = clj_intrinsic_call(op, args);
	clj_release(sample);
	uint32_t  field = 0;
	clj_value k = CLJ_NIL;
	sr_answer a = sr_classify(r, v, true, &field, &k);
	if (a == SR_NONE) return -2;
	sr_need(c, op);
	return sr_answered(pp, c, bind, a, field, k, nf, vi);
}

// (:k v), (:k v nf), (v i), (v k nf): a keyword's or the value's own lookup, which no var stands behind.
static int sr_lookup(const clj_node **pp, sr_ctx *c, bool bind, uint32_t vi, bool head_is_value) {
	clj_node       *n = (clj_node *)*pp;
	const sr_value *v = &c->vals[vi];
	uint32_t        nargs = n->u.invoke.n;
	const clj_node *keyn = head_is_value ? n->u.invoke.args[0] : n->u.invoke.fn;
	if (keyn->kind != CLJ_NODE_CONST) return -2;
	const clj_node **nf = head_is_value ? (nargs == 2 ? &n->u.invoke.args[1] : NULL) : (nargs == 2 ? &n->u.invoke.args[1] : NULL);
	clj_value        sample = sr_sample(v), r;
	clj_value        nfv = nf ? sr_sentinel(SR_SENT_NF) : CLJ_NIL;
	if (head_is_value) {
		clj_value args[2] = {keyn->u.value.v, nfv};
		r = clj_invoke(sample, args, nargs);
	} else {
		if (!clj_is_keyword(keyn->u.value.v)) {
			clj_release(sample);
			return -2;
		}
		r = clj_get(sample, keyn->u.value.v, nfv);
	}
	clj_release(sample);
	uint32_t  field = 0;
	clj_value k = CLJ_NIL;
	sr_answer a = sr_classify(r, v, true, &field, &k);
	if (a == SR_NONE) return -2;
	return sr_answered(pp, c, bind, a, field, k, nf, vi);
}

// A closure capturing a replaced value captures its materialization, bound just before the closure is made.
static void sr_capture(const clj_node **pp, sr_ctx *c) {
	clj_node *fn = (clj_node *)*pp;
	for (uint32_t i = 0; i < fn->u.fn.ncaptures; i++) {
		clj_capture *cap = &fn->u.fn.captures[i];
		if (cap->kind != CLJ_CAPTURE_LOCAL || cap->index >= INLINE_SLOTS_MAX || c->alias[cap->index] < 0) continue;
		// a literal fn an inlined or lowered body borrows from its call is that very node: it cannot move into a let
		if (c->guarded_args || c->next >= INLINE_SLOTS_MAX) {
			c->fail = true;
			return;
		}
		uint32_t  s = sr_fresh(c);
		clj_node *mat = sr_materialize(c, (uint32_t)c->alias[cap->index]);
		cap->index = s;
		position at = {fn->line, fn->col};
		*pp = let1(s, mat, fn, at);
		pp = &((clj_node *)*pp)->u.let.body;
	}
}

// Whether a direct fn's bodies read a replaced value through the static link, `depth` links up.
static bool sr_linked_read(const clj_node *n, const sr_ctx *c, uint32_t depth);

typedef struct {
	const sr_ctx *c;
	uint32_t      depth;
	bool          found;
} sr_link_scan;

static void sr_link_visit(const clj_node *n, void *ctx) {
	sr_link_scan *s = ctx;
	if (!s->found) s->found = sr_linked_read(n, s->c, s->depth);
}

static bool sr_hit(const sr_ctx *c, uint32_t slot) { return slot < INLINE_SLOTS_MAX && c->alias[slot] >= 0; }

static bool sr_linked_read(const clj_node *n, const sr_ctx *c, uint32_t depth) {
	switch (n->kind) {
	case CLJ_NODE_OUTER: return n->u.outer.depth == depth && sr_hit(c, n->u.outer.index);
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures && n->kind == CLJ_NODE_FN; i++) {
			const clj_capture *cap = &n->u.fn.captures[i];
			if (cap->kind == CLJ_CAPTURE_OUTER && cap->depth == depth && sr_hit(c, cap->index)) return true;
		}
		if (n->kind == CLJ_NODE_DIRECT_FN) {
			for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
				if (n->u.fn.fixed[k] && sr_linked_read(n->u.fn.fixed[k]->body, c, depth + 1)) return true;
			}
		}
		return false;
	case CLJ_NODE_FUSED: {
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) {
			if (sr_linked_read(n->u.fused.args[i], c, depth)) return true;
		}
		return false;
	}
	default: {
		sr_link_scan s = {c, depth, false};
		clj_node_children(n, sr_link_visit, &s);
		return s.found;
	}
	}
}

// Whether n, at this frame's level, reads a replaced value at all.
typedef struct {
	const sr_ctx *c;
	bool          found;
} sr_use_scan;

static void sr_use_visit(const clj_node *n, void *ctx) {
	sr_use_scan *s = ctx;
	if (s->found) return;
	switch (n->kind) {
	case CLJ_NODE_LOCAL: s->found = sr_hit(s->c, n->u.local.index); return;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			if (n->u.fn.captures[i].kind == CLJ_CAPTURE_LOCAL && sr_hit(s->c, n->u.fn.captures[i].index)) s->found = true;
		}
		return;
	case CLJ_NODE_DIRECT_FN:
		for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED && !s->found; k++) {
			if (n->u.fn.fixed[k] && sr_linked_read(n->u.fn.fixed[k]->body, s->c, 1)) s->found = true;
		}
		return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) sr_use_visit(n->u.fused.args[i], ctx);
		return;
	case CLJ_NODE_LOWERED: {
		const clj_node *fn = n->u.lowered.fn;
		for (uint32_t i = 0; i < fn->u.fn.ncaptures; i++) {
			if (fn->u.fn.captures[i].kind == CLJ_CAPTURE_LOCAL && sr_hit(s->c, fn->u.fn.captures[i].index)) s->found = true;
		}
		break;
	}
	default: break;
	}
	clj_node_children(n, sr_use_visit, ctx);
}

static bool sr_uses(const clj_node *n, const sr_ctx *c) {
	sr_use_scan s = {c, false};
	sr_use_visit(n, &s);
	return s.found;
}

static void sr_rw_all(const clj_node **nodes, uint32_t n, sr_ctx *c) {
	for (uint32_t i = 0; i < n && !c->fail; i++) sr_rw(&nodes[i], c, false);
}

// A binding that names or derives a replaced value leaves the let; a derived value's field bindings take its place.
static void sr_rw_let(clj_node *n, sr_ctx *c) {
	uint32_t         nb = n->u.let.n, k = 0, cap = nb + SR_PEND_MAX * nb;
	uint32_t        *slots = zalloc(cap, sizeof *slots);
	const clj_node **inits = zalloc(cap, sizeof *inits);
	uint32_t         named[64], nnamed = 0;
	for (uint32_t i = 0; i < nb && !c->fail; i++) {
		uint32_t s = n->u.let.slots[i];
		c->npend = 0;
		int v = sr_rw(&n->u.let.inits[i], c, true);
		if (c->fail) break;
		if (v < 0) {
			slots[k] = s;
			inits[k++] = n->u.let.inits[i];
			n->u.let.inits[i] = NULL;
			continue;
		}
		for (uint32_t p = 0; p < c->npend; p++) {
			slots[k] = c->pend_slots[p];
			inits[k++] = c->pend_inits[p];
		}
		c->npend = 0;
		if (s >= INLINE_SLOTS_MAX || nnamed == 64) {
			c->fail = true;
			break;
		}
		c->alias[s] = (int8_t)v;
		named[nnamed++] = s;
	}
	if (c->fail) {
		// the copy is thrown away whole: it only has to stay a tree its release can walk
		for (uint32_t i = 0; i < nb; i++) {
			if (!n->u.let.inits[i]) n->u.let.inits[i] = const_node(CLJ_NIL, (position){n->line, n->col});
		}
		for (uint32_t j = 0; j < k; j++) sr_free(inits[j]);
		free(slots);
		free(inits);
		for (uint32_t i = 0; i < nnamed; i++) c->alias[named[i]] = -1;
		return;
	}
	for (uint32_t i = 0; i < nb; i++) sr_free(n->u.let.inits[i]);
	free(n->u.let.slots);
	free(n->u.let.inits);
	n->u.let.slots = slots;
	n->u.let.inits = inits;
	n->u.let.n = k;
	sr_rw(&n->u.let.body, c, false);
	for (uint32_t i = 0; i < nnamed; i++) c->alias[named[i]] = -1;
}

// For the world where the guard holds. bind: *pp is a let init; a value it names or derives is returned, else -1.
// @ai-generated(solo)
static int sr_rw(const clj_node **pp, sr_ctx *c, bool bind) {
	if (c->fail) return -1;
	clj_node *n = (clj_node *)*pp;
	switch (n->kind) {
	case CLJ_NODE_LOCAL: {
		int v = sr_alias(c, n);
		if (v < 0) return -1;
		if (bind) return v;
		sr_replace(pp, sr_materialize(c, (uint32_t)v));
		return -1;
	}
	case CLJ_NODE_CONST:
	case CLJ_NODE_CAPTURED:
	case CLJ_NODE_OUTER:
	case CLJ_NODE_VAR:
	case CLJ_NODE_ROOT_IS: return -1;
	case CLJ_NODE_IF: {
		if (n->u.if_.test->kind == CLJ_NODE_ROOT_IS) {
			// an inlined or lowered call: the body reads what the call passes, and the call's literal fn is the body's
			sr_rw(&n->u.if_.then, c, false);
			c->guarded_args++;
			sr_rw(&n->u.if_.else_, c, false);
			c->guarded_args--;
			return -1;
		}
		if (sr_alias(c, n->u.if_.test) >= 0) sr_replace(&n->u.if_.test, const_node(CLJ_TRUE, (position){n->line, n->col}));
		else sr_rw(&n->u.if_.test, c, false);
		if (c->fail) return -1;
		if (n->u.if_.test->kind == CLJ_NODE_CONST) {
			fold_if(n);
			return sr_rw(pp, c, bind);
		}
		sr_rw(&n->u.if_.then, c, false);
		if (n->u.if_.else_) sr_rw(&n->u.if_.else_, c, false);
		return -1;
	}
	case CLJ_NODE_DO:
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET: sr_rw_all(n->u.seq.items, n->u.seq.n, c); return -1;
	case CLJ_NODE_LET: sr_rw_let(n, c); return -1;
	case CLJ_NODE_LOOP:
		sr_rw_all(n->u.let.inits, n->u.let.n, c);
		c->loops++;
		sr_rw(&n->u.let.body, c, false);
		c->loops--;
		return -1;
	case CLJ_NODE_RECUR: sr_rw_all(n->u.recur.args, n->u.recur.n, c); return -1;
	case CLJ_NODE_FN: sr_capture(pp, c); return -1;
	case CLJ_NODE_DIRECT_FN:
		for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
			if (n->u.fn.fixed[k] && sr_linked_read(n->u.fn.fixed[k]->body, c, 1)) c->fail = true;
		}
		return -1;
	case CLJ_NODE_INVOKE: {
		int vi;
		if (n->u.invoke.fn->kind == CLJ_NODE_CONST && (n->u.invoke.n == 1 || n->u.invoke.n == 2) && (vi = sr_alias(c, n->u.invoke.args[0])) >= 0) {
			int r = sr_lookup(pp, c, bind, (uint32_t)vi, false);
			if (r != -2) return r;
		}
		if ((vi = sr_alias(c, n->u.invoke.fn)) >= 0 && (n->u.invoke.n == 1 || n->u.invoke.n == 2)) {
			int r = sr_lookup(pp, c, bind, (uint32_t)vi, true);
			if (r != -2) return r;
		}
		sr_rw(&n->u.invoke.fn, c, false);
		sr_rw_all(n->u.invoke.args, n->u.invoke.n, c);
		return -1;
	}
	case CLJ_NODE_INTRINSIC: {
		int vi = n->u.intrinsic.n ? sr_alias(c, n->u.intrinsic.args[0]) : -1;
		if (vi >= 0) {
			int r = sr_intrinsic(pp, c, bind, (uint32_t)vi);
			if (r != -2) return r;
		}
		sr_rw_all(n->u.intrinsic.args, n->u.intrinsic.n, c);
		return -1;
	}
	case CLJ_NODE_DIRECT_CALL: sr_rw_all(n->u.direct.args, n->u.direct.n, c); return -1;
	case CLJ_NODE_OBJC_SEND:
		sr_rw(&n->u.objc.target, c, false);
		sr_rw_all(n->u.objc.args, n->u.objc.n, c);
		return -1;
	case CLJ_NODE_DEF:
		if (n->u.def.init) sr_rw(&n->u.def.init, c, false);
		sr_rw(&n->u.def.meta, c, false);
		return -1;
	case CLJ_NODE_TRY:
		sr_rw(&n->u.try_.body, c, false);
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) sr_rw(&n->u.try_.catches[i].handler, c, false);
		if (n->u.try_.finally_) sr_rw(&n->u.try_.finally_, c, false);
		return -1;
	case CLJ_NODE_THROW: sr_rw(&n->u.throw_, c, false); return -1;
	case CLJ_NODE_FUSED: sr_rw_all(n->u.fused.args, n->u.fused.nargs, c); return -1;
	case CLJ_NODE_INLINE: sr_rw(&n->u.inline_.body, c, false); return -1;
	case CLJ_NODE_LOWERED: {
		const clj_node *fn = n->u.lowered.fn;
		for (uint32_t i = 0; i < fn->u.fn.ncaptures; i++) {
			if (fn->u.fn.captures[i].kind == CLJ_CAPTURE_LOCAL && sr_hit(c, fn->u.fn.captures[i].index)) c->fail = true;
		}
		sr_rw_all(n->u.lowered.args, n->u.lowered.n, c);
		return -1;
	}
	}
	clj_ice("optimize/scalar-kind", n, "node kind %u", (unsigned)n->kind);
}

// ---- how often each value is materialized on a path

typedef struct {
	uint8_t hi[SR_VALUES_MAX], lo[SR_VALUES_MAX]; // most and fewest on any path, 2 meaning "2 or more"
} sr_paths;

static void sr_seq(sr_paths *a, const sr_paths *b) {
	for (uint32_t i = 0; i < SR_VALUES_MAX; i++) {
		a->hi[i] = (uint8_t)(a->hi[i] + b->hi[i] > 2 ? 2 : a->hi[i] + b->hi[i]);
		a->lo[i] = (uint8_t)(a->lo[i] + b->lo[i] > 2 ? 2 : a->lo[i] + b->lo[i]);
	}
}

static sr_paths sr_count(const clj_node *n, const sr_ctx *c);

typedef struct {
	const sr_ctx *c;
	sr_paths      p;
} sr_count_acc;

static void sr_count_visit(const clj_node *n, void *ctx) {
	sr_count_acc *a = ctx;
	sr_paths      q = sr_count(n, a->c);
	sr_seq(&a->p, &q);
}

static sr_paths sr_count(const clj_node *n, const sr_ctx *c) {
	sr_paths p = {{0}, {0}};
	switch (n->kind) {
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return p;
	case CLJ_NODE_FUSED: {
		sr_count_acc a = {c, p};
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) sr_count_visit(n->u.fused.args[i], &a);
		return a.p;
	}
	case CLJ_NODE_IF: {
		p = sr_count(n->u.if_.test, c);
		sr_paths t = sr_count(n->u.if_.then, c), e = {{0}, {0}};
		if (n->u.if_.else_) e = sr_count(n->u.if_.else_, c);
		sr_paths both;
		for (uint32_t i = 0; i < SR_VALUES_MAX; i++) {
			both.hi[i] = t.hi[i] > e.hi[i] ? t.hi[i] : e.hi[i];
			both.lo[i] = t.lo[i] < e.lo[i] ? t.lo[i] : e.lo[i];
		}
		sr_seq(&p, &both);
		return p;
	}
	case CLJ_NODE_TRY: {
		// a handler may follow any prefix of the body: its count adds to all of the body's
		sr_count_acc a = {c, p};
		clj_node_children(n, sr_count_visit, &a);
		sr_paths body = sr_count(n->u.try_.body, c);
		for (uint32_t i = 0; i < SR_VALUES_MAX; i++) a.p.lo[i] = body.lo[i];
		return a.p;
	}
	default: break;
	}
	sr_count_acc a = {c, p};
	clj_node_children(n, sr_count_visit, &a);
	for (uint32_t i = 0; i < c->nmats; i++) {
		if (c->mats[i] == n) {
			uint32_t v = c->mat_of[i];
			a.p.hi[v] = (uint8_t)(a.p.hi[v] + 1 > 2 ? 2 : a.p.hi[v] + 1);
			a.p.lo[v] = (uint8_t)(a.p.lo[v] + 1 > 2 ? 2 : a.p.lo[v] + 1);
		}
	}
	return a.p;
}

// ---- the candidate

// The layout a literal fixes: a tuple of 1 to 6 items, or a map of 1 to 32 distinct constant keywords.
static bool sr_layout(const clj_node *lit, sr_value *v) {
	if (lit->kind == CLJ_NODE_VECTOR) {
		if (!lit->u.seq.n || lit->u.seq.n > SR_TUPLE_MAX) return false;
		v->kind = SR_TUPLE;
		v->n = lit->u.seq.n;
		return true;
	}
	if (lit->kind != CLJ_NODE_MAP || !lit->u.seq.n || lit->u.seq.n > 2 * SR_KEYS_MAX || lit->u.seq.n % 2) return false;
	v->kind = SR_MAP;
	v->n = lit->u.seq.n / 2;
	for (uint32_t i = 0; i < v->n; i++) {
		const clj_node *k = lit->u.seq.items[2 * i];
		if (k->kind != CLJ_NODE_CONST || !clj_is_keyword(k->u.value.v)) return false;
		for (uint32_t j = 0; j < i; j++) {
			if (v->keys[j] == k->u.value.v) return false;
		}
		v->keys[i] = k->u.value.v;
	}
	return true;
}

static const clj_node *sr_item(const clj_node *lit, const sr_value *v, uint32_t i) {
	return lit->u.seq.items[v->kind == SR_TUPLE ? i : 2 * i + 1];
}

static void sr_ctx_free(sr_ctx *c) {
	for (uint32_t v = 0; v < c->nvals; v++) {
		for (uint32_t i = 0; i < c->vals[v].n; i++) sr_free(c->vals[v].src[i]);
	}
	for (uint32_t p = 0; p < c->npend; p++) sr_free(c->pend_inits[p]);
	free(c->mats);
	free(c->mat_of);
}

typedef enum { SR_KEEP, SR_DROP, SR_REWRITTEN } sr_how;

typedef struct {
	const clj_node **at;  // the expression's place: a binding's init, the body, or an inlined body inside it
	clj_node        *now; // the rewritten copy
	sr_how           how;
	int              value; // a binding that names or derives a value: which
	uint32_t         pend[SR_PEND_MAX], npend;
	clj_node        *pend_inits[SR_PEND_MAX];
} sr_expr;

static clj_node *sr_if(clj_node *test, clj_node *then, clj_node *else_, position at) { return if_node(test, then, else_, at); }

// Binding k of let `let`: its literal's items into slots, its uses rewritten; nothing changes unless all of it holds.
// @ai-generated(solo)
static void try_scalar(clj_node *let, uint32_t k, sr_frame *fr) {
	const clj_node *init = let->u.let.inits[k];
	uint32_t        t = let->u.let.slots[k];
	if (t >= INLINE_SLOTS_MAX || fr->arity->nslots >= INLINE_SLOTS_MAX) return;
	const clj_node *lits[2] = {init, NULL}, *test = NULL;
	sr_ctx          c = {.fr = fr, .next = fr->arity->nslots};
	memset(c.alias, -1, sizeof c.alias);
	sr_value *v = &c.vals[0];
	if (init->kind == CLJ_NODE_IF && init->u.if_.else_ && init->u.if_.test->kind != CLJ_NODE_ROOT_IS) {
		lits[0] = init->u.if_.then;
		lits[1] = init->u.if_.else_;
		test = init->u.if_.test;
	}
	if (!sr_layout(lits[0], v)) return;
	if (lits[1]) {
		sr_value w;
		if (!sr_layout(lits[1], &w) || w.kind != v->kind || w.n != v->n) return;
		for (uint32_t i = 0; v->kind == SR_MAP && i < v->n; i++) {
			if (w.keys[i] != v->keys[i]) return;
		}
	}
	v->at = (position){lits[0]->line, lits[0]->col};
	c.nvals = 1;
	c.alias[t] = 0;
	uint32_t nb = let->u.let.n;
	bool     any = sr_uses(let->u.let.body, &c);
	for (uint32_t j = k + 1; j < nb && !any; j++) any = sr_uses(let->u.let.inits[j], &c);
	if (!any) return;
	// the fields: a trivial item is read where it is, any other is bound in order; an if's arms each give theirs
	uint32_t   fslots[SR_KEYS_MAX + 1], nf = 0;
	clj_node  *finits[SR_KEYS_MAX + 1];
	clj_node  *ttest = NULL;
	if (test) {
		if (sr_trivial(&c, test)) ttest = sr_copy(test);
		else {
			fslots[nf] = sr_fresh(&c);
			finits[nf++] = sr_copy(test);
			ttest = local_node(fslots[nf - 1], (position){test->line, test->col});
		}
	}
	for (uint32_t i = 0; i < v->n; i++) {
		const clj_node *a = sr_item(lits[0], v, i);
		if (!lits[1] && sr_trivial(&c, a)) {
			v->src[i] = sr_copy(a);
			continue;
		}
		fslots[nf] = sr_fresh(&c);
		finits[nf++] = lits[1] ? sr_if(sr_copy(ttest), sr_copy(a), sr_copy(sr_item(lits[1], v, i)), (position){a->line, a->col}) : sr_copy(a);
		v->src[i] = local_node(fslots[nf - 1], (position){a->line, a->col});
	}
	sr_free(ttest);
	// the bindings after it, then the body (an inlined body's own, so the guard's then branch stays a let over INLINE)
	sr_expr  ex[nb - k];
	uint32_t nex = 0;
	for (uint32_t j = k + 1; j <= nb && !c.fail; j++) {
		const clj_node **at = j < nb ? &let->u.let.inits[j] : &let->u.let.body;
		if (j == nb && (*at)->kind == CLJ_NODE_INLINE) at = &((clj_node *)*at)->u.inline_.body;
		sr_expr *e = &ex[nex++];
		*e = (sr_expr){at, NULL, SR_KEEP, -1, {0}, 0, {0}};
		if (!sr_uses(*at, &c)) continue;
		e->now = sr_copy(*at);
		c.npend = 0;
		int named = sr_rw((const clj_node **)&e->now, &c, j < nb);
		if (c.fail) break;
		if (named >= 0) {
			e->value = named;
			e->how = SR_DROP;
			for (uint32_t p = 0; p < c.npend; p++) {
				e->pend[p] = c.pend_slots[p];
				e->pend_inits[p] = c.pend_inits[p];
			}
			e->npend = c.npend;
			c.npend = 0;
			c.alias[let->u.let.slots[j]] = (int8_t)named;
			continue;
		}
		e->how = SR_REWRITTEN;
	}
	// what the rewrite left must read no replaced value: an ICE, not a silently wrong field
	for (uint32_t i = 0; i < nex && !c.fail; i++) {
		if (ex[i].now && ex[i].how != SR_DROP && sr_uses(ex[i].now, &c))
			clj_ice("optimize/scalar-leftover", *ex[i].at, "a replaced value is still read after its rewrite");
		for (uint32_t p = 0; p < ex[i].npend; p++) {
			if (sr_uses(ex[i].pend_inits[p], &c)) clj_ice("optimize/scalar-leftover", *ex[i].at, "a derived field still reads a replaced value");
		}
	}
	// at most one materialization of a value on any path, and not on every path (no allocation would go)
	bool ok = !c.fail;
	if (ok) {
		sr_paths p = {{0}, {0}};
		for (uint32_t i = 0; i < nex; i++) {
			for (uint32_t q = 0; q < ex[i].npend; q++) {
				sr_paths r = sr_count(ex[i].pend_inits[q], &c);
				sr_seq(&p, &r);
			}
			if (!ex[i].now) continue;
			sr_paths q = sr_count(ex[i].now, &c);
			sr_seq(&p, &q);
		}
		for (uint32_t i = 0; i < c.nvals && ok; i++) ok = p.hi[i] <= 1;
		ok = ok && p.lo[0] == 0;
	}
	bool     guarded = c.nops > 0;
	uint32_t grown = nf + 4;
	for (uint32_t i = 0; ok && i < nex; i++) {
		if (ex[i].now) grown += nodes_in(ex[i].now);
	}
	uint32_t slots_after = c.next + (guarded ? 1u : 0u);
	if (ok && (fr->grown + grown > fr->budget || slots_after > INLINE_SLOTS_MAX ||
	           (fr->arity->nslots <= INLINE_SLOTS_SMALL && slots_after > INLINE_SLOTS_SMALL)))
		ok = false;
	// a body small enough to be inlined stays so: copied into its caller, it is replaced there, after the copy
	if (ok && fr->size <= INLINE_CALLEE_MAX && fr->size + fr->grown + grown > INLINE_CALLEE_MAX) ok = false;
	if (!ok) {
		for (uint32_t i = 0; i < nex; i++) {
			sr_free(ex[i].now);
			for (uint32_t p = 0; p < ex[i].npend; p++) sr_free(ex[i].pend_inits[p]);
		}
		for (uint32_t i = 0; i < nf; i++) sr_free(finits[i]);
		sr_ctx_free(&c);
		return;
	}
	// commit: the bindings up to k, the fields, the guard and the object it stands in for, then the rest
	uint32_t g = guarded ? sr_fresh(&c) : UINT32_MAX;
	position at = {init->line, init->col};
	uint32_t cap = nb + nf + 2 + nex * (SR_PEND_MAX + 1);
	uint32_t        *slots = zalloc(cap, sizeof *slots);
	const clj_node **inits = zalloc(cap, sizeof *inits);
	uint32_t         m = 0;
	for (uint32_t j = 0; j < k; j++) {
		slots[m] = let->u.let.slots[j];
		inits[m++] = let->u.let.inits[j];
	}
	for (uint32_t i = 0; i < nf; i++) {
		slots[m] = fslots[i];
		inits[m++] = finits[i];
	}
	if (guarded) {
		clj_node *gn = node_at(CLJ_NODE_ROOT_IS, at);
		clj_value var = clj_intrinsic_var(c.ops[0]);
		clj_slot_store(&gn->h, &gn->u.root_is.var, clj_retain(var));
		clj_slot_store(&gn->h, &gn->u.root_is.expect, clj_retain(clj_intrinsic_builtin(c.ops[0])));
		const clj_intrinsic **ops = zalloc(c.nops, sizeof *ops);
		memcpy(ops, c.ops, c.nops * sizeof *ops);
		gn->u.root_is.ops = ops;
		gn->u.root_is.nops = c.nops;
		slots[m] = g;
		inits[m++] = gn;
		slots[m] = t;
		inits[m++] = sr_if(local_node(g, at), const_node(CLJ_NIL, at), sr_materialize(&c, 0), at);
	}
	sr_free(init);
	for (uint32_t i = 0; i < nex; i++) {
		sr_expr *e = &ex[i];
		uint32_t j = k + 1 + i;
		if (e->how == SR_DROP) {
			for (uint32_t p = 0; p < e->npend; p++) {
				slots[m] = e->pend[p];
				inits[m++] = guarded ? sr_if(local_node(g, at), e->pend_inits[p], const_node(CLJ_NIL, at), at) : e->pend_inits[p];
			}
			sr_free(e->now);
			// in the world where the guard failed, the binding is the object the original code reads
			if (guarded) {
				slots[m] = let->u.let.slots[j];
				inits[m++] = sr_if(local_node(g, at), const_node(CLJ_NIL, at), (clj_node *)let->u.let.inits[j], at);
			} else sr_free(let->u.let.inits[j]);
			continue;
		}
		// under a guard every rewritten expression is versioned: a derived value's fields exist only where it holds
		if (e->how != SR_KEEP && guarded) *e->at = sr_if(local_node(g, at), e->now, (clj_node *)*e->at, at);
		else if (e->how != SR_KEEP) sr_replace(e->at, e->now);
		if (j < nb) {
			slots[m] = let->u.let.slots[j];
			inits[m++] = let->u.let.inits[j];
		}
	}
	free(let->u.let.slots);
	free(let->u.let.inits);
	let->u.let.slots = slots;
	let->u.let.inits = inits;
	let->u.let.n = m;
	if (c.next > fr->arity->nslots) fr->arity->nslots = c.next;
	fr->grown += grown;
	atomic_fetch_add_explicit(&sr_counts[0], 1, memory_order_relaxed);
	if (guarded) atomic_fetch_add_explicit(&sr_counts[1], 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&sr_counts[2], c.nmats - (guarded ? 1u : 0u), memory_order_relaxed);
	if (lits[1]) atomic_fetch_add_explicit(&sr_counts[3], 1, memory_order_relaxed);
	sr_ctx_free(&c);
}

// ---- the entry a reduce over a map hands its fn: a lowered reduce's literal (fn [acc e] body) gets a twin
// (fn [acc k v] (let [e [k v]] body)), the let then the candidate above, and a map's reduce runs the twin over the
// key and value it holds, so the entry is made only where the body materializes it. The twin is kept only when the
// entry was replaced; its slots are the literal's with k and v in at 1 and 2, e past them all.

typedef struct {
	uint32_t e, depth; // e: where slot 1 goes; depth: direct fn bodies entered below the frame
} sr_shift_ctx;

static uint32_t sr_shifted(const sr_shift_ctx *x, uint32_t s) { return s == 1 ? x->e : s >= 2 ? s + 1 : s; }

static void sr_shift(const clj_node *n, void *ctx);

static void sr_shift_slots(uint32_t *slots, uint32_t n, const sr_shift_ctx *x) {
	for (uint32_t i = 0; i < n; i++) slots[i] = sr_shifted(x, slots[i]);
}

// @ai-generated(solo)
static void sr_shift(const clj_node *n, void *ctx) {
	const sr_shift_ctx *x = ctx;
	clj_node           *m = (clj_node *)n;
	bool                own = x->depth == 0;
	switch (m->kind) {
	case CLJ_NODE_LOCAL:
		if (own) m->u.local.index = sr_shifted(x, m->u.local.index);
		return;
	case CLJ_NODE_OUTER:
		if (!own && m->u.outer.depth == x->depth) m->u.outer.index = sr_shifted(x, m->u.outer.index);
		return;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		if (own) sr_shift_slots(m->u.let.slots, m->u.let.n, x);
		break;
	case CLJ_NODE_RECUR:
		if (own) sr_shift_slots(m->u.recur.slots, m->u.recur.n, x);
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; own && i < m->u.try_.ncatches; i++) m->u.try_.catches[i].slot = sr_shifted(x, m->u.try_.catches[i].slot);
		break;
	case CLJ_NODE_DIRECT_CALL:
		if (m->u.direct.depth == x->depth) m->u.direct.slot = sr_shifted(x, m->u.direct.slot);
		break;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < m->u.fn.ncaptures; i++) {
			clj_capture *cap = &m->u.fn.captures[i];
			if ((cap->kind == CLJ_CAPTURE_LOCAL && own) || (cap->kind == CLJ_CAPTURE_OUTER && cap->depth == x->depth && !own))
				cap->index = sr_shifted(x, cap->index);
		}
		return; // a closure's arities are frames of their own
	case CLJ_NODE_DIRECT_FN: {
		sr_shift_ctx inner = {x->e, x->depth + 1};
		for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
			if (m->u.fn.fixed[k]) sr_shift(m->u.fn.fixed[k]->body, &inner);
		}
		return;
	}
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < m->u.fused.nargs; i++) sr_shift(m->u.fused.args[i], ctx);
		return;
	default: break;
	}
	clj_node_children(m, sr_shift, ctx);
}

static bool sr_binds_literal(const clj_node *let, uint32_t slot) {
	for (uint32_t i = 0; let->kind == CLJ_NODE_LET && i < let->u.let.n; i++) {
		if (let->u.let.slots[i] == slot && let->u.let.inits[i]->kind == CLJ_NODE_VECTOR) return true;
	}
	return false;
}

// @ai-generated(solo)
static void sr_split(clj_node *low) {
	if (low->u.lowered.how != CLJ_LOWER_REDUCE || low->u.lowered.split) return;
	const clj_node     *lit = low->u.lowered.fn;
	const clj_fn_arity *a = lit->u.fn.fixed[2];
	if (!a || a->nparams != 2 || a->variadic || a->nslots + 2 > INLINE_SLOTS_MAX) return;
	callee_scan cs = scan_arity(a, CLJ_NIL);
	// a recur goes back to two parameters, and the entry no parameter of the twin is
	if (cs.arity_recur || cs.self_read || !(cs.local_only & 2)) return;
	position      at = {lit->line, lit->col};
	sr_shift_ctx  x = {a->nslots + 1, 0};
	clj_node     *body = sr_copy(a->body);
	sr_shift(body, &x);
	const clj_node **items = zalloc(2, sizeof *items);
	items[0] = local_node(1, at);
	items[1] = local_node(2, at);
	clj_fn_arity *b = zalloc(1, sizeof *b);
	b->nparams = 3;
	b->self_slot = a->self_slot < 0 ? -1 : (int32_t)sr_shifted(&x, (uint32_t)a->self_slot);
	b->nslots = a->nslots + 2;
	b->body = let1(x.e, vector_node(items, 2, at), body, at);
	clj_node *twin = node_at(CLJ_NODE_FN, at);
	clj_slot_store(&twin->h, &twin->u.fn.name, clj_retain(lit->u.fn.name.v));
	if (lit->u.fn.ncaptures) {
		clj_capture *caps = zalloc(lit->u.fn.ncaptures, sizeof *caps);
		memcpy(caps, lit->u.fn.captures, lit->u.fn.ncaptures * sizeof *caps);
		twin->u.fn.captures = caps;
		twin->u.fn.ncaptures = lit->u.fn.ncaptures;
	}
	twin->u.fn.fixed[3] = b;
	uint32_t size = nodes_in(b->body);
	sr_frame inner = {b, 0, size > SR_GROWTH_MIN ? size : SR_GROWTH_MIN, UINT32_MAX};
	scalar_walk(b->body, &inner);
	// an entry read only for constants (a keyword lookup on what is a vector) says the source is no map: no twin
	sr_ctx probe = {.fr = NULL};
	memset(probe.alias, -1, sizeof probe.alias);
	probe.alias[1] = probe.alias[2] = 0;
	if (sr_binds_literal(b->body, x.e) || !sr_uses(b->body, &probe)) {
		sr_free(twin);
		return;
	}
	low->u.lowered.split = twin;
	atomic_fetch_add_explicit(&sr_counts[4], 1, memory_order_relaxed);
}

// Children first: an inner let is decided before the one around it, whose rewrite copies the result; a let's bindings
// from the last, so a value a later one holds as a field is decided before the earlier value it reads.
static void scalar_walk(const clj_node *n, void *ctx) {
	sr_frame *fr = ctx;
	clj_node *m = (clj_node *)n;
	switch (m->kind) {
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN:
		for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED + 1; k++) {
			clj_fn_arity *a = k <= CLJ_FN_MAX_FIXED ? m->u.fn.fixed[k] : m->u.fn.variadic;
			if (!a) continue;
			uint32_t size = nodes_in(a->body);
			sr_frame inner = {a, 0, size > SR_GROWTH_MIN ? size : SR_GROWTH_MIN, size};
			scalar_walk(a->body, &inner);
		}
		return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < m->u.fused.nargs; i++) scalar_walk(m->u.fused.args[i], ctx);
		return;
	case CLJ_NODE_DEF:
		if (m->u.def.macro) return;
		break;
	default: break;
	}
	clj_node_children(m, scalar_walk, ctx);
	if (m->kind == CLJ_NODE_LET && fr->arity) {
		for (uint32_t k = m->u.let.n; k-- > 0;) try_scalar(m, k, fr);
	}
	if (m->kind == CLJ_NODE_LOWERED) sr_split(m);
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
	case CLJ_NODE_INLINE: return live(n->u.inline_.body, out, c, use);
	case CLJ_NODE_LOWERED: // the literal's captures are read after the operands
		for (uint32_t i = 0; i < n->u.lowered.fn->u.fn.ncaptures; i++) {
			if (n->u.lowered.fn->u.fn.captures[i].kind == CLJ_CAPTURE_LOCAL) out |= slot_bit(n->u.lowered.fn->u.fn.captures[i].index);
		}
		return live_args(NULL, n->u.lowered.args, n->u.lowered.n, 0, out, c);
	case CLJ_NODE_CONST:
	case CLJ_NODE_CAPTURED:
	case CLJ_NODE_OUTER:
	case CLJ_NODE_VAR:
	case CLJ_NODE_ROOT_IS:
	case CLJ_NODE_DIRECT_FN: return out;
	}
	clj_ice("optimize/unknown-kind", n, "node kind %u", (unsigned)n->kind);
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
	if (clj_opt_off(CLJ_NO_OPT_PASSES)) return;
	clj_ice_pass("optimize/direct");
	direct_pass(root, NULL);
	CLJ_VERIFY_TREE(root, "optimize/direct", 0);
	clj_ice_pass("optimize/rewrite");
	optimize(root, NULL);
	CLJ_VERIFY_TREE(root, "optimize/rewrite", 0);
	if (!clj_opt_off(CLJ_NO_OPT_INLINE) && inline_enabled) {
		clj_ice_pass("optimize/inline");
		inline_frame top = {NULL, 0, 0, 0, CLJ_NIL};
		inline_walk(root, &top);
		CLJ_VERIFY_TREE(root, "optimize/inline", 0);
	}
	if (!clj_opt_off(CLJ_NO_OPT_SCALAR)) {
		clj_ice_pass("optimize/scalar");
		sr_frame top = {NULL, 0, 0, 0};
		scalar_walk(root, &top);
		CLJ_VERIFY_TREE(root, "optimize/scalar", 0);
	}
	clj_ice_pass("optimize/liveness");
	live_frame(root, 0);
	live_fns(root, NULL);
	CLJ_VERIFY_TREE(root, "optimize/liveness", CLJ_VERIFY_MARKS);
}

// @ai-generated(solo)
bool clj_lazy_seq_site(const clj_node *n) {
	if (n->kind != CLJ_NODE_INVOKE || n->u.invoke.n != 1 || n->u.invoke.fn->kind != CLJ_NODE_VAR) return false;
	const clj_node *fn = n->u.invoke.args[0];
	if (fn->kind != CLJ_NODE_FN || fn->u.fn.variadic || !fn->u.fn.fixed[0] || fn->u.fn.fixed[0]->self_slot >= 0) return false;
	for (uint32_t i = 1; i <= CLJ_FN_MAX_FIXED; i++) {
		if (fn->u.fn.fixed[i]) return false;
	}
	clj_value var = n->u.invoke.fn->u.var.v;
	return fn->u.fn.ncaptures <= UINT16_MAX && strcmp(CLJ_STR(clj_symbol_name(clj_var_ns(var))).bytes, "clojure.core") == 0 &&
	       strcmp(CLJ_STR(clj_symbol_name(clj_var_name(var))).bytes, "lazy-seq*") == 0;
}
