// @ai-generated(solo)
// Design §3 item 6, NOTES "Tree verifier": a structural walk, then the last-use marks forward per frame.
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/fn.h"
#include "clj/fusion.h"
#include "clj/ice.h"
#include "clj/intrinsics.h"
#include "clj/keyword.h"
#include "clj/lowered.h"
#include "clj/string.h"
#include "clj/var.h"
#include "clj/verify.h"
#include "guard_internal.h"
#include "node.h"

static _Atomic int enabled = -1;

bool clj_verify_enabled(void) {
	int on = atomic_load_explicit(&enabled, memory_order_relaxed);
	if (on < 0) {
		const char *e = getenv("CLJ_VERIFY");
		on = !(e && strcmp(e, "0") == 0);
		atomic_store_explicit(&enabled, on, memory_order_relaxed);
	}
	return on;
}

static uint64_t bit(uint32_t slot) { return slot < 64 ? (uint64_t)1 << slot : 0; }

typedef struct {
	uint32_t              flags;
	clj_verify_violation *out;
	bool                  failed;
	uint32_t              next_id, next_site;
	char                 *stack_limit; // past it the rest goes unchecked: a fault under the specialize lock is fatal
	bool                  gave_up;
	const clj_node       *root_is, *inline_; // the guard and the body of the inlined call being walked: nowhere else
	const clj_node       *lowered; // the then branch of the lowered call being walked: nowhere else
	const clj_node       *path;   // the PATH body being walked, whose levels sit inside it
	uint32_t              levels; // LEVEL bodies around the walk within that PATH body
	// The guarded calls of this frame whose then branch is being walked: their literal fn arguments are what a LITERAL
	// body may be a copy of.
	const clj_node *calls[2 * CLJ_INLINE_DEPTH_MAX];
	uint32_t        ncalls;
	const clj_node *guard_test; // the if test being walked: the one place a scalar guard's slot is read
} vctx;

static bool stack_low(vctx *c) {
	char here;
	if (&here < c->stack_limit) c->gave_up = true;
	return c->gave_up;
}

static bool bad(vctx *c, const char *kind, const clj_node *at, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static bool bad(vctx *c, const char *kind, const clj_node *at, const char *fmt, ...) {
	if (c->failed) return false;
	c->failed = true;
	c->out->kind = kind;
	c->out->at = at;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(c->out->message, sizeof c->out->message, fmt, ap);
	va_end(ap);
	return false;
}

// ---- the structural walk

typedef struct vframe {
	const struct vframe *up; // where a direct fn arity's static link goes; NULL elsewhere
	uint32_t             nslots, ncaptured;
	bool                 env;   // CAPTURED reads have an environment
	const clj_node      *fused; // a fused program's frame: the FUSED node
	uint8_t             *bound;
	const clj_node     **direct; // the DIRECT_FN bound at a slot while it is in scope
	uint8_t             *guard;  // a scalar replacement's guard is bound at the slot
	uint32_t             inlines; // INLINE bodies the walk is inside of, in this frame
} vframe;

typedef struct {
	uint32_t        n;
	const uint32_t *slots;
} vtarget;

static vframe frame_new(uint32_t nslots) {
	vframe f = {NULL, nslots, 0, false, NULL, calloc(nslots ? nslots : 1, 1), calloc(nslots ? nslots : 1, sizeof(clj_node *)),
	            calloc(nslots ? nslots : 1, 1), 0};
	if (!f.bound || !f.direct || !f.guard) clj_fatal("out of memory");
	return f;
}

static void frame_free(vframe *f) {
	free(f->bound);
	free(f->direct);
	free(f->guard);
}

static const vframe *linked(const vframe *f, uint32_t depth) {
	while (depth-- && f) f = f->up;
	return f;
}

static bool optimizer_kind(clj_node_kind k) {
	return k == CLJ_NODE_INTRINSIC || k == CLJ_NODE_FUSED || k == CLJ_NODE_DIRECT_FN || k == CLJ_NODE_DIRECT_CALL || k == CLJ_NODE_ROOT_IS ||
	       k == CLJ_NODE_INLINE || k == CLJ_NODE_LOWERED;
}

// What eval_fused's frame can hold: the arguments as locals, the calls over them and the transducer vector.
static bool fused_kind(clj_node_kind k) {
	return k == CLJ_NODE_LOCAL || k == CLJ_NODE_CONST || k == CLJ_NODE_VAR || k == CLJ_NODE_INVOKE || k == CLJ_NODE_INTRINSIC ||
	       k == CLJ_NODE_VECTOR;
}

static bool guarded(const clj_node *fused, clj_value var) {
	for (uint32_t i = 0; i < fused->u.fused.nguards; i++) {
		if (clj_fusion_var_of(fused->u.fused.guards[i]) == var) return true;
	}
	return false;
}

static bool slot_read_ok(vctx *c, const vframe *g, const clj_node *at, uint32_t i) {
	if (i >= g->nslots) return bad(c, "verify/slot-outside-frame", at, "slot %u of a frame of %u", i, g->nslots);
	if (!g->bound[i]) return bad(c, "verify/slot-not-in-scope", at, "slot %u is read where no binding of it is in scope", i);
	if (g->direct[i]) return bad(c, "verify/direct-fn-slot-read", at, "slot %u holds a direct fn, which only a direct call may name", i);
	return true;
}

static bool outer_ok(vctx *c, const vframe *f, const clj_node *at, uint32_t depth, uint32_t i) {
	if (depth == 0) return bad(c, "verify/outer-depth", at, "a static-link read of depth 0");
	const vframe *g = linked(f, depth);
	if (!g) return bad(c, "verify/outer-past-static-chain", at, "depth %u passes the frames direct fns link", depth);
	return slot_read_ok(c, g, at, i);
}

static bool walk(vctx *c, vframe *f, const vtarget *t, const clj_node *n, bool tail, bool init);

static bool walk_all(vctx *c, vframe *f, const clj_node *const *nodes, uint32_t n, const clj_node *parent) {
	if (n && !nodes) return bad(c, "verify/missing-child", parent, "%u children and no array", n);
	for (uint32_t i = 0; i < n; i++) {
		if (!walk(c, f, NULL, nodes[i], false, false)) return false;
	}
	return true;
}

static bool arity_ok(vctx *c, const clj_node *fn, const clj_fn_arity *a, uint32_t k) {
	if (k <= CLJ_FN_MAX_FIXED ? (a->nparams != k || a->variadic) : (!a->variadic || a->nparams > CLJ_FN_MAX_FIXED))
		return bad(c, "verify/arity-table", fn, "an arity of %u params%s at entry %u", a->nparams, a->variadic ? " and a rest" : "", k);
	uint32_t np = a->nparams + (a->variadic ? 1u : 0u);
	if (a->nslots < np) return bad(c, "verify/arity-slots", fn, "%u slots for %u params", a->nslots, np);
	if (a->self_slot >= 0 && ((uint32_t)a->self_slot < np || (uint32_t)a->self_slot >= a->nslots))
		return bad(c, "verify/arity-self-slot", fn, "self slot %d beside %u params in %u slots", a->self_slot, np, a->nslots);
	return true;
}

static bool walk_arities(vctx *c, vframe *f, const clj_node *n, bool direct);

static bool walk_fn(vctx *c, vframe *f, const clj_node *n, bool init) {
	bool direct = n->kind == CLJ_NODE_DIRECT_FN;
	if (direct) {
		if (!init) return bad(c, "verify/direct-fn-outside-binding", n, "a direct fn that is no let or loop init");
		if (n->u.fn.variadic || n->u.fn.ncaptures) return bad(c, "verify/direct-fn-shape", n, "a direct fn with a rest arity or captures");
	} else {
		if (n->u.fn.ncaptures && !n->u.fn.captures) return bad(c, "verify/missing-child", n, "captures without an array");
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cp = &n->u.fn.captures[i];
			switch (cp->kind) {
			case CLJ_CAPTURE_LOCAL:
				if (!slot_read_ok(c, f, n, cp->index)) return false;
				break;
			case CLJ_CAPTURE_CAPTURED:
				if (!f->env || cp->index >= f->ncaptured) return bad(c, "verify/capture-outside-environment", n, "capture of entry %u of an environment of %u", cp->index, f->env ? f->ncaptured : 0);
				break;
			case CLJ_CAPTURE_OUTER:
				if (!outer_ok(c, f, n, cp->depth, cp->index)) return false;
				break;
			default: return bad(c, "verify/capture-kind", n, "capture kind %u", (unsigned)cp->kind);
			}
		}
	}
	// an arity is a frame of its own: no inlined body of the enclosing one reaches into it
	const clj_node *path = c->path;
	uint32_t        levels = c->levels, ncalls = c->ncalls;
	c->path = NULL;
	c->levels = c->ncalls = 0;
	bool ok = walk_arities(c, f, n, direct);
	c->path = path;
	c->levels = levels;
	c->ncalls = ncalls;
	return ok;
}

static bool walk_arities(vctx *c, vframe *f, const clj_node *n, bool direct) {
	bool any = false;
	for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED + 1; k++) {
		const clj_fn_arity *a = k <= CLJ_FN_MAX_FIXED ? n->u.fn.fixed[k] : n->u.fn.variadic;
		if (!a) continue;
		any = true;
		if (!arity_ok(c, n, a, k)) return false;
		vframe g = frame_new(a->nslots);
		g.up = direct ? f : NULL;
		g.env = direct ? f->env : true;
		g.ncaptured = direct ? f->ncaptured : n->u.fn.ncaptures;
		uint32_t  np = a->nparams + (a->variadic ? 1u : 0u);
		uint32_t *slots = calloc(np ? np : 1, sizeof *slots);
		if (!slots) clj_fatal("out of memory");
		for (uint32_t i = 0; i < np; i++) {
			slots[i] = i;
			g.bound[i] = 1;
		}
		// eval_direct_call leaves a direct arity's self slot nil: a call through the name is a direct call
		if (a->self_slot >= 0 && !direct) g.bound[a->self_slot] = 1;
		vtarget target = {np, slots};
		bool    ok = walk(c, &g, &target, a->body, true, false);
		free(slots);
		frame_free(&g);
		if (!ok) return false;
	}
	if (!any) return bad(c, "verify/fn-without-arity", n, "a fn with no arity");
	return true;
}

// A scalar replacement's guard (optimizer.c), bound by a let: the intrinsic guard of each op it answered statically, the
// var and expect its first's, so what it checks is exactly what those intrinsic nodes would.
static bool scalar_guard_ok(vctx *c, const clj_node *g) {
	if (g->u.root_is.nguards || !g->u.root_is.nops || !g->u.root_is.ops)
		return bad(c, "verify/scalar-guard", g, "a let-bound guard that checks no intrinsic, or fusion guards");
	for (uint32_t i = 0; i < g->u.root_is.nops; i++) {
		const clj_intrinsic *op = g->u.root_is.ops[i];
		if (!op || clj_intrinsic_find(clj_intrinsic_var(op), op->arity) != op) return bad(c, "verify/scalar-guard", g, "guard op %u is no intrinsic of the table", i);
	}
	const clj_intrinsic *first = g->u.root_is.ops[0];
	if (g->u.root_is.var.v != clj_intrinsic_var(first) || g->u.root_is.expect.v != clj_intrinsic_builtin(first))
		return bad(c, "verify/scalar-guard", g, "a guard whose var and root are not its first op's");
	return true;
}

static bool walk_let(vctx *c, vframe *f, const vtarget *t, const clj_node *n, bool tail) {
	uint32_t nb = n->u.let.n;
	if (nb && (!n->u.let.slots || !n->u.let.inits)) return bad(c, "verify/missing-child", n, "%u bindings and no arrays", nb);
	for (uint32_t i = 0; i < nb; i++) {
		uint32_t        s = n->u.let.slots[i];
		const clj_node *init = n->u.let.inits[i];
		if (s >= f->nslots) return bad(c, "verify/slot-outside-frame", n, "binding of slot %u in a frame of %u", s, f->nslots);
		// The evaluator keeps one value per slot: a nested binding of a bound slot would overwrite the outer one.
		if (f->bound[s]) return bad(c, "verify/slot-rebound-in-scope", n, "slot %u is bound again while its binding is in scope", s);
		bool direct = init && init->kind == CLJ_NODE_DIRECT_FN;
		// A direct fn's body may call itself through the binding it is the init of.
		if (direct) {
			f->bound[s] = 1;
			f->direct[s] = init;
		}
		if (!walk(c, f, NULL, init, false, true)) return false;
		if (init && init->kind == CLJ_NODE_ROOT_IS) f->guard[s] = 1;
		f->bound[s] = 1;
	}
	bool ok;
	if (n->kind == CLJ_NODE_LOOP) {
		vtarget target = {nb, n->u.let.slots};
		ok = walk(c, f, &target, n->u.let.body, true, false);
	} else {
		ok = walk(c, f, t, n->u.let.body, tail, false);
	}
	for (uint32_t i = 0; i < nb; i++) {
		f->bound[n->u.let.slots[i]] = 0;
		f->direct[n->u.let.slots[i]] = NULL;
		f->guard[n->u.let.slots[i]] = 0;
	}
	return ok;
}

static bool walk_try(vctx *c, vframe *f, const clj_node *n) {
	if (!walk(c, f, NULL, n->u.try_.body, false, false)) return false;
	if (n->u.try_.ncatches && !n->u.try_.catches) return bad(c, "verify/missing-child", n, "catches without an array");
	for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
		const clj_catch *k = &n->u.try_.catches[i];
		bool             sel;
		switch (k->kind) {
		case CLJ_CATCH_ALL:
		case CLJ_CATCH_ERROR: sel = true; break;
		case CLJ_CATCH_KEYWORD: sel = clj_is_keyword(k->selector); break;
		case CLJ_CATCH_TYPE: sel = clj_is_var(k->selector); break;
		case CLJ_CATCH_HOST: sel = clj_is_string(k->selector); break;
		default: return bad(c, "verify/catch-kind", n, "catch kind %u", (unsigned)k->kind);
		}
		if (!sel) return bad(c, "verify/catch-selector", n, "catch %u: a selector of the wrong type for its kind", i);
		if (k->slot >= f->nslots) return bad(c, "verify/slot-outside-frame", n, "catch binding of slot %u in a frame of %u", k->slot, f->nslots);
		if (f->bound[k->slot]) return bad(c, "verify/slot-rebound-in-scope", n, "catch slot %u is bound while its binding is in scope", k->slot);
		f->bound[k->slot] = 1;
		bool ok = walk(c, f, NULL, k->handler, false, false);
		f->bound[k->slot] = 0;
		if (!ok) return false;
	}
	return !n->u.try_.finally_ || walk(c, f, NULL, n->u.try_.finally_, false, false);
}

static bool walk_fused(vctx *c, vframe *f, const clj_node *n) {
	if (!n->u.fused.nguards || !n->u.fused.guards) return bad(c, "verify/fused-guard", n, "a fused node without guards");
	for (uint32_t i = 0; i < n->u.fused.nguards; i++) {
		const clj_fusion_var *g = n->u.fused.guards[i];
		if (!g || g->role == CLJ_FUSION_GUARD || clj_fusion_find(clj_fusion_var_of(g)) != g) return bad(c, "verify/fused-guard", n, "guard %u is no stage, consumer or driver of the fusion table", i);
	}
	if (!n->u.fused.nargs) return bad(c, "verify/fused-args", n, "a fused node without arguments");
	if (!walk_all(c, f, n->u.fused.args, n->u.fused.nargs, n)) return false;
	vframe g = frame_new(n->u.fused.nargs);
	g.fused = n;
	memset(g.bound, 1, n->u.fused.nargs);
	bool ok = walk(c, &g, NULL, n->u.fused.fused, false, false) && walk(c, &g, NULL, n->u.fused.original, false, false);
	frame_free(&g);
	return ok;
}

// (if ROOT_IS[var, closure] (let [fresh...] INLINE[fn, body]) (var args...)): the guard compares the root with the closure
// whose arity the body is a copy of, and the else branch is that very call, so a failed guard runs what the program
// said. A guard or a body anywhere else would run a copy no guard checks.
static bool path_guard_ok(vctx *c, const clj_node *g, const clj_node *at);

// (if ROOT_IS[native var, builtin] LOWERED[fn, args] (var args...)): the guard checks the root the lowered call runs,
// the else branch is the call whose literal fn LOWERED borrows, and LOWERED's args are the call's others, in order.
// @ai-generated(solo)
static bool walk_lowered(vctx *c, vframe *f, const vtarget *t, const clj_node *n, bool tail) {
	const clj_node *g = n->u.if_.test, *low = n->u.if_.then, *call = n->u.if_.else_;
	if (!call || call->kind != CLJ_NODE_INVOKE || call->u.invoke.fn->kind != CLJ_NODE_VAR || call->u.invoke.fn->u.var.v != g->u.root_is.var.v)
		return bad(c, "verify/lowered-shape", n, "a lowered call whose else branch is no call of its guard's var");
	clj_lowered_how how;
	uint32_t        nargs = call->u.invoke.n, at, arity;
	if (!clj_lowered_find(g->u.root_is.var.v, nargs, &how) || how != low->u.lowered.how)
		return bad(c, "verify/lowered-shape", n, "a lowered call of a var at %u args that has no such lowering", nargs);
	if (g->u.root_is.nguards || g->u.root_is.expect.v != clj_lowered_root(how))
		return bad(c, "verify/lowered-guard", n, "a lowered call whose guard does not check its var at the builtin it runs");
	clj_lowered_shape(how, nargs, &at, &arity);
	const clj_node *fn = low->u.lowered.fn;
	if (low->u.lowered.n + 1 != nargs || fn != call->u.invoke.args[at])
		return bad(c, "verify/lowered-shape", n, "a lowered call whose fn or operands are not its call's");
	if (fn->kind != CLJ_NODE_FN || arity > CLJ_FN_MAX_FIXED || !fn->u.fn.fixed[arity])
		return bad(c, "verify/lowered-shape", n, "a lowered call over a literal without the arity %u its native calls", arity);
	if (low->u.lowered.n && !low->u.lowered.args) return bad(c, "verify/missing-child", n, "lowered operands without an array");
	const clj_node *outer_root_is = c->root_is, *outer_lowered = c->lowered;
	c->root_is = g;
	c->lowered = low;
	bool ok = walk(c, f, NULL, g, false, false) && walk(c, f, t, low, tail, false) && walk(c, f, t, call, tail, false);
	c->root_is = outer_root_is;
	c->lowered = outer_lowered;
	return ok;
}

// A lowered reduce's entry twin: one arity of three parameters over the literal's own captures, which the step reads
// from the same environment; its body a frame of its own.
static bool walk_split(vctx *c, vframe *f, const clj_node *n) {
	const clj_node *s = n->u.lowered.split, *fn = n->u.lowered.fn;
	if (n->u.lowered.how != CLJ_LOWER_REDUCE || s->h.type != &clj_node_type || s->kind != CLJ_NODE_FN || s->u.fn.variadic || !s->u.fn.fixed[3])
		return bad(c, "verify/lowered-split", n, "an entry twin that is no fn of one 3-arity under a reduce");
	for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
		if (k != 3 && s->u.fn.fixed[k]) return bad(c, "verify/lowered-split", n, "an entry twin with an arity of %u", k);
	}
	if (s->u.fn.ncaptures != fn->u.fn.ncaptures || (s->u.fn.ncaptures && memcmp(s->u.fn.captures, fn->u.fn.captures, s->u.fn.ncaptures * sizeof *s->u.fn.captures)))
		return bad(c, "verify/lowered-split", n, "an entry twin whose captures are not its literal's");
	return walk(c, f, NULL, s, false, false);
}

// @ai-generated(solo)
static bool walk_inlined(vctx *c, vframe *f, const vtarget *t, const clj_node *n, bool tail) {
	const clj_node *g = n->u.if_.test, *call = n->u.if_.else_, *inl = n->u.if_.then;
	if (inl && inl->kind == CLJ_NODE_LOWERED) return walk_lowered(c, f, t, n, tail);
	if (inl && inl->kind == CLJ_NODE_LET) inl = inl->u.let.body;
	if (!inl || inl->kind != CLJ_NODE_INLINE) return bad(c, "verify/inline-shape", n, "an inline guard whose then branch is no inlined body");
	if (!call || call->kind != CLJ_NODE_INVOKE || call->u.invoke.fn->kind != CLJ_NODE_VAR || call->u.invoke.fn->u.var.v != g->u.root_is.var.v)
		return bad(c, "verify/inline-shape", n, "an inline guard whose else branch is no call of its var");
	clj_value expect = g->u.root_is.expect.v;
	if (!clj_is_var(g->u.root_is.var.v) || !clj_is_fn(expect) || clj_fn_of(expect)->kind != CLJ_FN_CLOSURE || clj_fn_of(expect)->nenv)
		return bad(c, "verify/inline-guard", n, "an inline guard that holds no closure without an environment");
	const clj_node *code = clj_fn_of(expect)->u.node;
	uint32_t        nargs = call->u.invoke.n;
	if (inl->u.inline_.fn != code) return bad(c, "verify/inline-guard", n, "an inlined body of another fn than its guard checks");
	if (g->u.root_is.nguards) {
		if (!path_guard_ok(c, g, n)) return false;
		if (inl->u.inline_.how != CLJ_INLINE_PATH) return bad(c, "verify/inline-shape", n, "a path guard whose then branch is no path body");
		if (!(nargs <= CLJ_FN_MAX_FIXED && code->u.fn.fixed[nargs]) && !(code->u.fn.variadic && code->u.fn.variadic->nparams <= nargs))
			return bad(c, "verify/inline-guard", n, "a path specialization of %u args to a fn without that arity", nargs);
	} else {
		if (inl->u.inline_.how != CLJ_INLINE_COPY) return bad(c, "verify/inline-shape", n, "a guard whose then branch is no copied body");
		if (nargs > CLJ_FN_MAX_FIXED || !code->u.fn.fixed[nargs])
			return bad(c, "verify/inline-guard", n, "an inlined call of %u args to a fn without that fixed arity", nargs);
	}
	if (f->inlines >= CLJ_INLINE_DEPTH_MAX) return bad(c, "verify/inline-depth", n, "an inlined body %u deep, past the budget of %u", f->inlines + 1, CLJ_INLINE_DEPTH_MAX);
	if (c->ncalls == sizeof c->calls / sizeof *c->calls) return bad(c, "verify/inline-depth", n, "guards nested %u deep", c->ncalls + 1);
	const clj_node *outer_root_is = c->root_is, *outer_inline = c->inline_;
	c->root_is = g;
	c->inline_ = inl;
	bool ok = walk(c, f, NULL, g, false, false);
	// a literal fn argument is copied into the then branch, never into the call that passes it
	c->calls[c->ncalls++] = call;
	ok = ok && walk(c, f, t, n->u.if_.then, tail, false);
	c->ncalls--;
	ok = ok && walk(c, f, t, call, tail, false);
	c->root_is = outer_root_is;
	c->inline_ = outer_inline;
	return ok;
}

// A known-path guard: entries of the fusion table, among them the var's own at the closure the guard expects, and a
// var a specialization exists for.
static bool path_guard_ok(vctx *c, const clj_node *g, const clj_node *at) {
	if (!g->u.root_is.guards) return bad(c, "verify/missing-child", at, "%u path guards and no array", g->u.root_is.nguards);
	bool own = false;
	for (uint32_t i = 0; i < g->u.root_is.nguards; i++) {
		const clj_fusion_var *gv = g->u.root_is.guards[i];
		if (!gv || clj_fusion_find(clj_fusion_var_of(gv)) != gv) return bad(c, "verify/inline-guard", at, "path guard %u is no entry of the fusion table", i);
		if (clj_fusion_var_of(gv) == g->u.root_is.var.v) own = gv->role == CLJ_FUSION_GUARD && clj_fusion_boot_root(gv) == g->u.root_is.expect.v;
	}
	if (!own) return bad(c, "verify/inline-guard", at, "a path guard that does not check its own var at its boot root");
	const char *name = strchr(clj_fusion_find(g->u.root_is.var.v)->name, '/') + 1;
	if (strcmp(name, "get-in") != 0 && strcmp(name, "assoc-in") != 0 && strcmp(name, "update-in") != 0)
		return bad(c, "verify/inline-guard", at, "a path specialization of %s, which has none", name);
	return true;
}

static bool literal_arg(const vctx *c, const clj_node *fn) {
	for (uint32_t k = 0; k < c->ncalls; k++) {
		for (uint32_t i = 0; i < c->calls[k]->u.invoke.n; i++) {
			if (c->calls[k]->u.invoke.args[i] == fn) return true;
		}
	}
	return false;
}

// The bodies a guard's body holds: a level of a path, a copy of a literal fn argument.
static bool walk_nested_inline(vctx *c, vframe *f, const clj_node *n) {
	const clj_node *fn = n->u.inline_.fn;
	if (!fn || fn->h.type != &clj_node_type || (fn->kind != CLJ_NODE_FN && fn->kind != CLJ_NODE_DIRECT_FN))
		return bad(c, "verify/inline-shape", n, "an inlined body of no fn node");
	if (n->u.inline_.how == CLJ_INLINE_LEVEL) {
		if (!c->path) return bad(c, "verify/inline-shape", n, "a path level outside a path body");
		if (c->levels >= CLJ_INLINE_PATH_MAX) return bad(c, "verify/inline-depth", n, "a path level %u deep, past %u", c->levels + 1, CLJ_INLINE_PATH_MAX);
		c->levels++;
		bool ok = walk(c, f, NULL, n->u.inline_.body, false, false);
		c->levels--;
		return ok;
	}
	if (fn->kind != CLJ_NODE_FN || !literal_arg(c, fn)) return bad(c, "verify/inline-literal", n, "a copied fn that no guarded call around it passes");
	return walk(c, f, NULL, n->u.inline_.body, false, false);
}

// @ai-generated(solo)
static bool walk_kind(vctx *c, vframe *f, const vtarget *t, const clj_node *n, bool tail, bool init) {
	switch (n->kind) {
	case CLJ_NODE_CONST: return true;
	case CLJ_NODE_LOCAL: {
		uint32_t i = n->u.local.index;
		if (i >= f->nslots) return bad(c, "verify/slot-outside-frame", n, "slot %u of a frame of %u", i, f->nslots);
		if (f->guard[i] && n != c->guard_test) return bad(c, "verify/scalar-guard-read", n, "slot %u holds a scalar guard, read here as a value", i);
		if (n->u.local.last) {
			if (!(c->flags & CLJ_VERIFY_MARKS)) return bad(c, "verify/last-use-before-liveness", n, "a last-use mark on slot %u before the liveness pass", i);
			if (f->fused) return bad(c, "verify/last-use-in-fused-program", n, "a last-use mark in a fused program, whose frame is the argument array at +0");
			if (i >= 64) return bad(c, "verify/last-use-past-64", n, "a last-use mark on slot %u, past the 64 the owned mask covers", i);
		}
		return f->fused || slot_read_ok(c, f, n, i);
	}
	case CLJ_NODE_CAPTURED:
		if (!f->env || n->u.index >= f->ncaptured) return bad(c, "verify/capture-outside-environment", n, "entry %u of an environment of %u", n->u.index, f->env ? f->ncaptured : 0);
		return true;
	case CLJ_NODE_OUTER: return outer_ok(c, f, n, n->u.outer.depth, n->u.outer.index);
	case CLJ_NODE_VAR:
		if (!clj_is_var(n->u.var.v)) return bad(c, "verify/var-node", n, "a var node without a var");
		if (f->fused && !guarded(f->fused, n->u.var.v)) return bad(c, "verify/fused-unguarded-var", n, "a fused program names a var none of its guards checks");
		return true;
	case CLJ_NODE_IF:
		if (n->u.if_.test && n->u.if_.test->kind == CLJ_NODE_ROOT_IS) return walk_inlined(c, f, t, n, tail);
		c->guard_test = n->u.if_.test;
		bool test_ok = walk(c, f, NULL, n->u.if_.test, false, false);
		c->guard_test = NULL;
		return test_ok && walk(c, f, t, n->u.if_.then, tail, false) && (!n->u.if_.else_ || walk(c, f, t, n->u.if_.else_, tail, false));
	case CLJ_NODE_DO: {
		uint32_t k = n->u.seq.n;
		if (!k) return bad(c, "verify/empty-do", n, "a do of no items, whose value eval_do reads from item -1");
		if (!n->u.seq.items) return bad(c, "verify/missing-child", n, "items without an array");
		for (uint32_t i = 0; i + 1 < k; i++) {
			if (!walk(c, f, NULL, n->u.seq.items[i], false, false)) return false;
		}
		return walk(c, f, t, n->u.seq.items[k - 1], tail, false);
	}
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP: return walk_let(c, f, t, n, tail);
	case CLJ_NODE_RECUR: {
		if (!tail) return bad(c, "verify/recur-not-in-tail", n, "recur outside a tail position of its loop or fn");
		if (!t) return bad(c, "verify/recur-without-target", n, "recur with no loop or fn arity to go back to");
		if (n->u.recur.n != t->n) return bad(c, "verify/recur-slots", n, "recur of %u values to a target of %u", n->u.recur.n, t->n);
		if (n->u.recur.n && !n->u.recur.slots) return bad(c, "verify/missing-child", n, "recur slots without an array");
		for (uint32_t i = 0; i < n->u.recur.n; i++) {
			uint32_t s = n->u.recur.slots[i];
			if (s != t->slots[i]) return bad(c, "verify/recur-slots", n, "recur value %u to slot %u where its target binds slot %u", i, s, t->slots[i]);
			if (s < f->nslots && f->direct[s]) return bad(c, "verify/direct-fn-slot-rebound", n, "recur rebinds slot %u, which holds a direct fn", s);
		}
		return walk_all(c, f, n->u.recur.args, n->u.recur.n, n);
	}
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return walk_fn(c, f, n, init);
	case CLJ_NODE_INVOKE: return walk(c, f, NULL, n->u.invoke.fn, false, false) && walk_all(c, f, n->u.invoke.args, n->u.invoke.n, n);
	case CLJ_NODE_DIRECT_CALL: {
		const clj_node *fn = n->u.direct.fn;
		if (!fn || fn->h.type != &clj_node_type || fn->kind != CLJ_NODE_DIRECT_FN) return bad(c, "verify/direct-call-target", n, "a direct call whose fn is no direct fn node");
		const vframe *g = linked(f, n->u.direct.depth);
		if (!g) return bad(c, "verify/outer-past-static-chain", n, "a direct call %u links up, past the frames direct fns link", n->u.direct.depth);
		if (n->u.direct.slot >= g->nslots || g->direct[n->u.direct.slot] != fn)
			return bad(c, "verify/direct-call-unbound", n, "its fn is not the direct fn bound at slot %u %u links up", n->u.direct.slot, n->u.direct.depth);
		if (n->u.direct.n > CLJ_FN_MAX_FIXED || !n->u.direct.arity || fn->u.fn.fixed[n->u.direct.n] != n->u.direct.arity)
			return bad(c, "verify/direct-call-arity", n, "a direct call of %u args to an arity the fn does not have there", n->u.direct.n);
		return walk_all(c, f, n->u.direct.args, n->u.direct.n, n);
	}
	case CLJ_NODE_DEF:
		if (!clj_is_var(n->u.def.var.v)) return bad(c, "verify/def-var", n, "a def without a var");
		if (!n->u.def.meta) return bad(c, "verify/missing-child", n, "a def without its meta node");
		if (n->u.def.lazy > CLJ_DEF_LAZY_EXPLICIT || n->u.def.asked > CLJ_DEF_ASKED_EAGER) return bad(c, "verify/def-lazy", n, "lazy %u, asked %u", n->u.def.lazy, n->u.def.asked);
		return (!n->u.def.init || walk(c, f, NULL, n->u.def.init, false, false)) && walk(c, f, NULL, n->u.def.meta, false, false);
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_SET: return walk_all(c, f, n->u.seq.items, n->u.seq.n, n);
	case CLJ_NODE_MAP:
		if (n->u.seq.n % 2) return bad(c, "verify/map-odd", n, "a map literal of %u items", n->u.seq.n);
		return walk_all(c, f, n->u.seq.items, n->u.seq.n, n);
	case CLJ_NODE_TRY: return walk_try(c, f, n);
	case CLJ_NODE_THROW: return walk(c, f, NULL, n->u.throw_, false, false);
	case CLJ_NODE_INTRINSIC: {
		const clj_intrinsic *op = n->u.intrinsic.op;
		if (!op || n->u.intrinsic.n != op->arity) return bad(c, "verify/intrinsic-arity", n, "an intrinsic of %u args", n->u.intrinsic.n);
		// The guard compares this var's root with op's builtin: another var would run op where the program meant it.
		if (clj_intrinsic_find(n->u.intrinsic.var.v, n->u.intrinsic.n) != op) return bad(c, "verify/intrinsic-var", n, "%s under a var the table does not name for it", op->name);
		if (f->fused && !guarded(f->fused, n->u.intrinsic.var.v)) return bad(c, "verify/fused-unguarded-var", n, "a fused program calls %s, which none of its guards checks", op->name);
		return walk_all(c, f, n->u.intrinsic.args, n->u.intrinsic.n, n);
	}
	case CLJ_NODE_FUSED: return walk_fused(c, f, n);
	case CLJ_NODE_OBJC_SEND:
		if (!clj_is_string(n->u.objc.selector.v)) return bad(c, "verify/objc-selector", n, "a send without a selector string");
		return walk(c, f, NULL, n->u.objc.target, false, false) && walk_all(c, f, n->u.objc.args, n->u.objc.n, n);
	case CLJ_NODE_ROOT_IS:
		if (n->u.root_is.nops) return init ? scalar_guard_ok(c, n) : bad(c, "verify/scalar-guard", n, "a scalar guard that is no let init");
		if (n != c->root_is) return bad(c, "verify/inline-shape", n, "an inline guard that is no test of an inlined call");
		c->root_is = NULL;
		return true;
	case CLJ_NODE_LOWERED:
		if (n != c->lowered) return bad(c, "verify/lowered-shape", n, "a lowered call outside the then branch of its guard");
		c->lowered = NULL;
		return walk_all(c, f, n->u.lowered.args, n->u.lowered.n, n) && (!n->u.lowered.split || walk_split(c, f, n));
	case CLJ_NODE_INLINE: {
		uint8_t how = n->u.inline_.how;
		if (how == CLJ_INLINE_LEVEL || how == CLJ_INLINE_LITERAL) return walk_nested_inline(c, f, n);
		if (how != CLJ_INLINE_COPY && how != CLJ_INLINE_PATH) return bad(c, "verify/inline-shape", n, "an inlined body of kind %u", how);
		if (n != c->inline_) return bad(c, "verify/inline-shape", n, "an inlined body outside the then branch of its guard");
		c->inline_ = NULL;
		// the body is the callee's arity body or core's path over it: no recur of the caller reaches through it
		const clj_node *path = c->path;
		uint32_t        levels = c->levels;
		c->path = how == CLJ_INLINE_PATH ? n : NULL;
		c->levels = 0;
		f->inlines++;
		bool ok = walk(c, f, NULL, n->u.inline_.body, false, false);
		f->inlines--;
		c->path = path;
		c->levels = levels;
		return ok;
	}
	}
	return bad(c, "verify/unknown-kind", n, "node kind %u", (unsigned)n->kind);
}

static bool walk(vctx *c, vframe *f, const vtarget *t, const clj_node *n, bool tail, bool init) {
	if (stack_low(c)) return true;
	if (!n) return bad(c, "verify/missing-child", NULL, "a node is missing where its parent needs one");
	if (n->h.type != &clj_node_type) return bad(c, "verify/not-a-node", NULL, "a child pointer that reaches no node");
	if ((uint32_t)n->kind > CLJ_NODE_KIND_LAST) return bad(c, "verify/unknown-kind", NULL, "node kind %u", (unsigned)n->kind);
	uint32_t id = c->next_id++;
	if (c->flags & CLJ_VERIFY_NUMBERED) {
		if (n->id != id) return bad(c, "verify/numbering", n, "id %u where the pre-order says %u: a node shared or moved after numbering", n->id, id);
		if (n->kind == CLJ_NODE_INVOKE && n->site != c->next_site++) return bad(c, "verify/numbering", n, "invoke site %u out of order", n->site);
	}
	if ((c->flags & CLJ_VERIFY_ANALYZED) && optimizer_kind(n->kind)) return bad(c, "verify/optimizer-kind", n, "a %s node before the optimizer ran", clj_node_kind_name((int)n->kind));
	if (f->fused && !fused_kind(n->kind)) return bad(c, "verify/fused-program-kind", n, "a %s node in a fused program, which holds local reads, calls and literals only", clj_node_kind_name((int)n->kind));
	if (!walk_kind(c, f, t, n, tail, init)) return false;
	if ((c->flags & CLJ_VERIFY_NUMBERED) && !c->gave_up && n->nnodes != c->next_id - id) return bad(c, "verify/numbering", n, "a subtree size of %u over %u nodes", n->nnodes, c->next_id - id);
	return true;
}

// The top-level frame as clj_exec_new sizes it: the let, loop and catch slots outside every fn.
static void top_slots(const clj_node *n, void *ctx) {
	uint32_t *m = ctx;
	switch (n->kind) {
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n && n->u.let.slots; i++) {
			if (n->u.let.slots[i] + 1 > *m) *m = n->u.let.slots[i] + 1;
		}
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; i < n->u.try_.ncatches && n->u.try_.catches; i++) {
			if (n->u.try_.catches[i].slot + 1 > *m) *m = n->u.try_.catches[i].slot + 1;
		}
		break;
	default: break;
	}
	clj_node_children(n, top_slots, ctx);
}

// ---- frames: what a body's own frame reads by address and rebinds

typedef struct {
	uint32_t level; // direct fn bodies entered below the frame
	uint64_t linked, captured, written;
} fs_ctx;

static void fs_walk(const clj_node *n, void *ctx) {
	fs_ctx *p = ctx;
	switch (n->kind) {
	case CLJ_NODE_OUTER:
		if (n->u.outer.depth == p->level) p->linked |= bit(n->u.outer.index);
		return;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cp = &n->u.fn.captures[i];
			if (cp->kind == CLJ_CAPTURE_LOCAL && p->level == 0) p->captured |= bit(cp->index);
			else if (cp->kind == CLJ_CAPTURE_OUTER && cp->depth == p->level) p->linked |= bit(cp->index);
		}
		return;
	case CLJ_NODE_DIRECT_FN:
		p->level++;
		clj_node_children(n, fs_walk, ctx);
		p->level--;
		return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) fs_walk(n->u.fused.args[i], ctx);
		return;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; p->level == 0 && i < n->u.let.n; i++) p->written |= bit(n->u.let.slots[i]);
		break;
	case CLJ_NODE_RECUR:
		for (uint32_t i = 0; p->level == 0 && i < n->u.recur.n; i++) p->written |= bit(n->u.recur.slots[i]);
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; p->level == 0 && i < n->u.try_.ncatches; i++) p->written |= bit(n->u.try_.catches[i].slot);
		break;
	default: break;
	}
	clj_node_children(n, fs_walk, ctx);
}

void clj_verify_frame_slots(const clj_node *body, uint64_t *linked, uint64_t *captured, uint64_t *written) {
	fs_ctx p = {0, 0, 0, 0};
	fs_walk(body, &p);
	*linked = p.linked;
	*captured = p.captured;
	*written = p.written;
}

// ---- last-use marks: forward over each frame, a slot handed over stays dead until it is bound again

typedef struct {
	uint64_t dead;
	bool     reach;
} lstate;

typedef struct {
	uint64_t slots; // what a recur to the target rebinds
	uint64_t acc;   // dead slots at the recurs, carried back to the target's entry
} ltarget;

typedef struct {
	vctx     *c;
	uint64_t  held;   // borrowed by an earlier operand of the call or literal being evaluated
	uint64_t  pinned; // read through the static link by a direct fn body
	ltarget  *t;
	uint64_t *any; // slots handed over anywhere in the innermost try body or handler
	clj_verify_drop_fn drops; // NULL for the marks alone
	void              *dctx;
} lctx;

static lstate join(lstate a, lstate b) {
	if (!a.reach) return b;
	if (!b.reach) return a;
	return (lstate){a.dead | b.dead, true};
}

static const lstate no_way_out = {0, false};

static lstate lv(lctx *x, const clj_node *n, lstate s, bool owned);

static lstate dropped(lctx *x, const clj_node *n, uint32_t at, lstate s) {
	if (!x->drops || !s.reach) return s;
	uint64_t d = x->drops(x->dctx, n, at);
	if (d & (x->held | x->pinned)) {
		bad(x->c, "verify/drop-of-held-slot", n, "a drop at point %u releases slots %#llx, which an earlier operand borrows or a direct fn body reads", at,
		    (unsigned long long)(d & (x->held | x->pinned)));
	}
	s.dead |= d;
	if (x->any) *x->any |= d;
	return s;
}

// eval_all with borrowing: an operand read at +0 is used only once every later operand has run (optimizer.c, live_args).
static lstate lv_operands(lctx *x, const clj_node *head, const clj_node *const *ops, uint32_t n, uint32_t owned, lstate s) {
	uint64_t held = x->held, borrowed = 0;
	if (head) {
		s = lv(x, head, s, false);
		if (head->kind == CLJ_NODE_LOCAL) borrowed |= bit(head->u.local.index);
	}
	for (uint32_t i = 0; i < n; i++) {
		x->held = held | borrowed;
		s = lv(x, ops[i], s, i < owned);
		if (ops[i]->kind == CLJ_NODE_LOCAL) borrowed |= bit(ops[i]->u.local.index);
	}
	x->held = held;
	return s;
}

static lstate lv_bindings(lctx *x, const clj_node *n, lstate s) {
	for (uint32_t i = 0; i < n->u.let.n; i++) {
		s = lv(x, n->u.let.inits[i], s, true);
		s.dead &= ~bit(n->u.let.slots[i]);
		s = dropped(x, n, i, s);
	}
	return s;
}

// The body until the dead sets carried back by its recurs stop growing.
static lstate lv_target(lctx *x, const clj_node *body, uint64_t slots, uint64_t entry) {
	ltarget *saved = x->t;
	for (;;) {
		ltarget t = {slots, 0};
		x->t = &t;
		lstate r = lv(x, body, (lstate){entry, true}, true);
		x->t = saved;
		uint64_t next = entry | t.acc;
		if (next == entry || x->c->failed) return r;
		entry = next;
	}
}

static lstate lv_try(lctx *x, const clj_node *n, lstate s, bool owned) {
	uint64_t  entry = s.dead, body_any = 0, *saved = x->any;
	x->any = &body_any;
	lstate out = lv(x, n->u.try_.body, s, owned);
	uint64_t all = body_any;
	// a handler starts from any point of the body
	for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
		const clj_catch *k = &n->u.try_.catches[i];
		uint64_t         h_any = 0;
		x->any = &h_any;
		out = join(out, lv(x, k->handler, (lstate){(entry | body_any) & ~bit(k->slot), true}, owned));
		all |= h_any;
	}
	x->any = saved;
	if (saved) *saved |= all;
	if (n->u.try_.finally_) {
		// every way into finally at once for the checks; the normal way for what follows
		lv(x, n->u.try_.finally_, (lstate){entry | all | out.dead, true}, false);
		if (out.reach) out = lv(x, n->u.try_.finally_, out, false);
	}
	return out;
}

// @ai-generated(solo)
static lstate lv(lctx *x, const clj_node *n, lstate s, bool owned) {
	vctx *c = x->c;
	if (c->failed || !s.reach || stack_low(c)) return s;
	switch (n->kind) {
	case CLJ_NODE_LOCAL: {
		uint32_t i = n->u.local.index;
		uint64_t b = bit(i);
		if (s.dead & b) {
			if (x->drops) bad(c, "verify/read-after-drop", n, "slot %u is read after a drop released it or a last use handed it over", i);
			else bad(c, "verify/read-after-last-use", n, "slot %u is read after a last use handed its value over", i);
			return s;
		}
		if (!n->u.local.last) return s;
		if (!owned) bad(c, "verify/last-use-on-borrowed-read", n, "a last-use mark on slot %u where the consumer only borrows", i);
		else if (x->held & b) bad(c, "verify/last-use-of-held-slot", n, "slot %u is handed over while an earlier operand borrows it", i);
		else if (x->pinned & b) bad(c, "verify/last-use-of-pinned-slot", n, "slot %u is handed over though a direct fn body reads it through the static link", i);
		s.dead |= b;
		if (x->any) *x->any |= b;
		return s;
	}
	case CLJ_NODE_CONST:
	case CLJ_NODE_CAPTURED:
	case CLJ_NODE_OUTER:
	case CLJ_NODE_VAR:
	case CLJ_NODE_DIRECT_FN: return s;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cp = &n->u.fn.captures[i];
			if (cp->kind == CLJ_CAPTURE_LOCAL && (s.dead & bit(cp->index)))
				bad(c, "verify/read-after-last-use", n, "a closure captures slot %u after a last use handed its value over", cp->index);
		}
		return s;
	case CLJ_NODE_IF: {
		s = lv(x, n->u.if_.test, s, false);
		lstate a = lv(x, n->u.if_.then, dropped(x, n, 0, s), owned);
		lstate e = dropped(x, n, 1, s);
		return join(a, n->u.if_.else_ ? lv(x, n->u.if_.else_, e, owned) : e);
	}
	case CLJ_NODE_DO: {
		uint32_t last = n->u.seq.n - 1;
		for (uint32_t i = 0; i < last; i++) s = dropped(x, n, i, lv(x, n->u.seq.items[i], s, false));
		return lv(x, n->u.seq.items[last], s, owned);
	}
	case CLJ_NODE_LET: return dropped(x, n, n->u.let.n, lv(x, n->u.let.body, lv_bindings(x, n, s), owned));
	case CLJ_NODE_LOOP: {
		s = lv_bindings(x, n, s);
		uint64_t slots = 0;
		for (uint32_t i = 0; i < n->u.let.n; i++) slots |= bit(n->u.let.slots[i]);
		return dropped(x, n, n->u.let.n, lv_target(x, n->u.let.body, slots, s.dead));
	}
	case CLJ_NODE_RECUR:
		for (uint32_t i = 0; i < n->u.recur.n; i++) s = lv(x, n->u.recur.args[i], s, true);
		s = dropped(x, n, 0, s);
		if (x->t) x->t->acc |= s.dead & ~x->t->slots;
		return no_way_out;
	case CLJ_NODE_INVOKE: return lv_operands(x, n->u.invoke.fn, n->u.invoke.args, n->u.invoke.n, 0, s);
	case CLJ_NODE_OBJC_SEND: return lv_operands(x, n->u.objc.target, n->u.objc.args, n->u.objc.n, 0, s);
	case CLJ_NODE_INTRINSIC:
		return lv_operands(x, NULL, n->u.intrinsic.args, n->u.intrinsic.n, clj_intrinsic_consumes(n->u.intrinsic.op) ? 1 : 0, s);
	case CLJ_NODE_DIRECT_CALL: return lv_operands(x, NULL, n->u.direct.args, n->u.direct.n, 0, s);
	case CLJ_NODE_FUSED: return lv_operands(x, NULL, n->u.fused.args, n->u.fused.nargs, 0, s);
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET: return lv_operands(x, NULL, n->u.seq.items, n->u.seq.n, 0, s);
	case CLJ_NODE_DEF:
		if (n->u.def.init) s = lv(x, n->u.def.init, s, true);
		return lv(x, n->u.def.meta, s, true);
	case CLJ_NODE_THROW:
		lv(x, n->u.throw_, s, true);
		return no_way_out;
	case CLJ_NODE_TRY: return lv_try(x, n, s, owned);
	case CLJ_NODE_INLINE: return lv(x, n->u.inline_.body, s, owned);
	case CLJ_NODE_LOWERED: {
		s = lv_operands(x, NULL, n->u.lowered.args, n->u.lowered.n, 0, s);
		const clj_node *fn = n->u.lowered.fn;
		for (uint32_t i = 0; i < fn->u.fn.ncaptures; i++) {
			const clj_capture *cp = &fn->u.fn.captures[i];
			if (cp->kind == CLJ_CAPTURE_LOCAL && (s.dead & bit(cp->index)))
				bad(c, "verify/read-after-last-use", n, "a lowered call reads captured slot %u after a last use handed its value over", cp->index);
		}
		return s;
	}
	case CLJ_NODE_ROOT_IS: return s;
	}
	return s;
}

static void lv_frame(vctx *c, const clj_node *body, uint64_t recur_slots) {
	uint64_t linked, captured, written;
	clj_verify_frame_slots(body, &linked, &captured, &written);
	lctx x = {c, 0, linked, NULL, NULL, NULL, NULL};
	lv_target(&x, body, recur_slots, 0);
}

bool clj_verify_drops(const clj_node *body, uint64_t recur_slots, clj_verify_drop_fn drops, void *ctx, clj_verify_violation *out) {
	vctx c = {.out = out, .stack_limit = clj_stack_limit()};
	out->kind = NULL;
	out->at = NULL;
	out->message[0] = '\0';
	uint64_t linked, captured, written;
	clj_verify_frame_slots(body, &linked, &captured, &written);
	lctx x = {&c, 0, linked, NULL, NULL, drops, ctx};
	lv_target(&x, body, recur_slots, 0);
	return !c.failed;
}

static void lv_frames(const clj_node *n, void *ctx) {
	vctx *c = ctx;
	if (c->failed) return;
	if (n->kind == CLJ_NODE_FN || n->kind == CLJ_NODE_DIRECT_FN) {
		for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED + 1; k++) {
			const clj_fn_arity *a = k <= CLJ_FN_MAX_FIXED ? n->u.fn.fixed[k] : n->u.fn.variadic;
			if (!a) continue;
			uint32_t np = a->nparams + (a->variadic ? 1u : 0u);
			lv_frame(c, a->body, np >= 64 ? UINT64_MAX : ((uint64_t)1 << np) - 1);
		}
	}
	clj_node_children(n, lv_frames, ctx);
}

bool clj_verify_tree_check(const clj_node *root, uint32_t flags, clj_verify_violation *out) {
	vctx c = {.flags = flags, .out = out, .next_id = root->id, .stack_limit = clj_stack_limit()};
	out->kind = NULL;
	out->at = NULL;
	out->message[0] = '\0';
	uint32_t nslots = 0;
	top_slots(root, &nslots);
	vframe f = frame_new(nslots);
	bool   ok = walk(&c, &f, NULL, root, true, false);
	frame_free(&f);
	if (ok && !c.gave_up && (flags & CLJ_VERIFY_MARKS)) {
		lv_frame(&c, root, 0);
		lv_frames(root, &c);
	}
	return !c.failed;
}

void clj_verify_tree(const clj_node *root, const char *pass, uint32_t flags) {
	if (!clj_verify_enabled()) return;
	clj_verify_violation v;
	if (clj_verify_tree_check(root, flags, &v)) return;
	clj_ice_frame f = {.pass = pass};
	clj_ice_enter(&f);
	clj_ice(v.kind, v.at, "%s; the tree as %s left it", v.message, pass);
}

// ---- immutability: a digest of everything a backend reads off the tree

static uint64_t mix(uint64_t h, uint64_t v) {
	h ^= v;
	h *= 0x100000001b3ULL;
	return h ^ (h >> 29);
}

// @ai-generated(solo)
static void digest(const clj_node *n, void *ctx) {
	uint64_t *h = ctx;
	uint64_t  x = *h;
	x = mix(x, n->kind);
	x = mix(x, ((uint64_t)n->id << 32) | n->nnodes);
	x = mix(x, ((uint64_t)n->line << 32) | n->col);
	x = mix(x, n->site);
	switch (n->kind) {
	case CLJ_NODE_CONST: x = mix(x, n->u.value.v); break;
	case CLJ_NODE_LOCAL: x = mix(x, ((uint64_t)n->u.local.index << 1) | n->u.local.last); break;
	case CLJ_NODE_CAPTURED: x = mix(x, n->u.index); break;
	case CLJ_NODE_OUTER: x = mix(x, ((uint64_t)n->u.outer.depth << 32) | n->u.outer.index); break;
	case CLJ_NODE_VAR: x = mix(x, n->u.var.v); break;
	case CLJ_NODE_IF: x = mix(x, n->u.if_.else_ != NULL); break;
	case CLJ_NODE_DO:
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET: x = mix(x, n->u.seq.n); break;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		x = mix(x, n->u.let.n);
		for (uint32_t i = 0; i < n->u.let.n; i++) x = mix(x, n->u.let.slots[i]);
		break;
	case CLJ_NODE_RECUR:
		x = mix(x, n->u.recur.n);
		for (uint32_t i = 0; i < n->u.recur.n; i++) x = mix(x, n->u.recur.slots[i]);
		break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN:
		x = mix(x, n->u.fn.name.v);
		x = mix(x, n->u.fn.ncaptures);
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cp = &n->u.fn.captures[i];
			x = mix(x, ((uint64_t)cp->kind << 48) ^ ((uint64_t)cp->depth << 32) ^ cp->index);
		}
		for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED + 1; k++) {
			const clj_fn_arity *a = k <= CLJ_FN_MAX_FIXED ? n->u.fn.fixed[k] : n->u.fn.variadic;
			if (!a) continue;
			x = mix(x, ((uint64_t)k << 48) ^ ((uint64_t)a->nparams << 32) ^ ((uint64_t)a->variadic << 31) ^ a->nslots);
			x = mix(x, (uint64_t)(int64_t)a->self_slot);
		}
		break;
	case CLJ_NODE_INVOKE: x = mix(x, n->u.invoke.n); break;
	case CLJ_NODE_DIRECT_CALL:
		x = mix(x, (uintptr_t)n->u.direct.fn);
		x = mix(x, (uintptr_t)n->u.direct.arity);
		x = mix(x, ((uint64_t)n->u.direct.slot << 32) ^ ((uint64_t)n->u.direct.depth << 16) ^ n->u.direct.n);
		break;
	case CLJ_NODE_DEF:
		x = mix(x, n->u.def.var.v);
		x = mix(x, ((uint64_t)(n->u.def.init != NULL) << 4) ^ ((uint64_t)n->u.def.macro << 3) ^ ((uint64_t)n->u.def.dynamic << 2));
		x = mix(x, ((uint64_t)n->u.def.lazy << 8) | n->u.def.asked);
		break;
	case CLJ_NODE_TRY:
		x = mix(x, ((uint64_t)n->u.try_.ncatches << 1) | (n->u.try_.finally_ != NULL));
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
			const clj_catch *k = &n->u.try_.catches[i];
			x = mix(x, ((uint64_t)k->kind << 32) | k->slot);
			x = mix(x, k->selector);
		}
		break;
	case CLJ_NODE_THROW: break;
	case CLJ_NODE_INTRINSIC:
		x = mix(x, (uintptr_t)n->u.intrinsic.op);
		x = mix(x, n->u.intrinsic.var.v);
		x = mix(x, n->u.intrinsic.n);
		break;
	case CLJ_NODE_FUSED:
		x = mix(x, ((uint64_t)n->u.fused.nguards << 32) | n->u.fused.nargs);
		for (uint32_t i = 0; i < n->u.fused.nguards; i++) x = mix(x, (uintptr_t)n->u.fused.guards[i]);
		break;
	case CLJ_NODE_OBJC_SEND:
		x = mix(x, n->u.objc.selector.v);
		x = mix(x, n->u.objc.n);
		break;
	case CLJ_NODE_ROOT_IS:
		x = mix(x, n->u.root_is.var.v);
		x = mix(x, n->u.root_is.expect.v);
		x = mix(x, n->u.root_is.nguards);
		for (uint32_t i = 0; i < n->u.root_is.nguards; i++) x = mix(x, (uintptr_t)n->u.root_is.guards[i]);
		for (uint32_t i = 0; i < n->u.root_is.nops; i++) x = mix(x, (uintptr_t)n->u.root_is.ops[i]);
		break;
	case CLJ_NODE_INLINE:
		x = mix(x, (uintptr_t)n->u.inline_.fn);
		x = mix(x, n->u.inline_.how);
		break;
	case CLJ_NODE_LOWERED:
		x = mix(x, (uintptr_t)n->u.lowered.fn);
		x = mix(x, ((uint64_t)n->u.lowered.how << 32) | n->u.lowered.n);
		break;
	}
	*h = x;
	clj_node_children(n, digest, ctx);
}

uint64_t clj_verify_digest(const clj_node *root) {
	uint64_t h = 0xcbf29ce484222325ULL;
	digest(root, &h);
	return h;
}

void clj_verify_unchanged(const clj_node *root, uint64_t before, const char *pass) {
	if (!clj_verify_enabled() || clj_verify_digest(root) == before) return;
	clj_ice_frame f = {.pass = pass};
	clj_ice_enter(&f);
	clj_ice("verify/tree-written", root, "the tree changed across %s, which only reads it", pass);
}
