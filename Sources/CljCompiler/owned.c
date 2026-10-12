// @ai-generated(solo)
#include "owned.h"

#include <stdlib.h>
#include <string.h>

#include "clj/ice.h"
#include "clj/summary.h"
#include "ice_internal.h"
#include "reuse.h"

typedef struct {
	clj_value       var;
	uint32_t        order, ndefs;
	// The fixed arities of a def whose init is a FN without captures, 0 for any other init. Not the node: a compiled
	// eval's tree dies with its form, and the entry outlives it.
	uint32_t        fixed;
	bool            scanned;
	uint64_t        cand[CLJ_FN_MAX_FIXED + 1];
	uint64_t        local[CLJ_FN_MAX_FIXED + 1];
	uint64_t        paired[CLJ_FN_MAX_FIXED + 1]; // of local, by a reuse pair alone
	uint64_t        mask[CLJ_FN_MAX_FIXED + 1];
} entry;

// Parameter `param` of (from, arity) dies as argument `pos` of a call to (to, to_arity).
typedef struct {
	uint32_t from, arity, param, to, to_arity, pos;
} edge;

struct cljc_owned {
	entry   *entries;
	size_t   n, cap;
	uint32_t *index; // open addressing over entries by var, UINT32_MAX empty
	size_t    icap;
	edge     *edges;
	size_t    nedges, edges_cap;
	uint64_t  rounds;
};

static void *grow(void *items, size_t *cap, size_t n, size_t size) {
	if (n < *cap) return items;
	*cap = *cap ? *cap * 2 : 16;
	items = realloc(items, *cap * size);
	if (!items) clj_fatal("out of memory");
	return items;
}

static size_t hash_var(clj_value var, size_t cap) { return (size_t)((var >> 4) * 0x9E3779B97F4A7C15ull) & (cap - 1); }

static void rehash(cljc_owned *o) {
	free(o->index);
	o->icap = o->icap ? o->icap * 2 : 64;
	o->index = malloc(o->icap * sizeof *o->index);
	if (!o->index) clj_fatal("out of memory");
	memset(o->index, 0xff, o->icap * sizeof *o->index);
	for (size_t i = 0; i < o->n; i++) {
		size_t h = hash_var(o->entries[i].var, o->icap);
		while (o->index[h] != UINT32_MAX) h = (h + 1) & (o->icap - 1);
		o->index[h] = (uint32_t)i;
	}
}

static entry *find(const cljc_owned *o, clj_value var) {
	if (!o->icap) return NULL;
	for (size_t h = hash_var(var, o->icap); o->index[h] != UINT32_MAX; h = (h + 1) & (o->icap - 1)) {
		if (o->entries[o->index[h]].var == var) return &o->entries[o->index[h]];
	}
	return NULL;
}

static entry *intern(cljc_owned *o, clj_value var) {
	entry *e = find(o, var);
	if (e) return e;
	if (2 * (o->n + 1) > o->icap) rehash(o);
	o->entries = grow(o->entries, &o->cap, o->n, sizeof *o->entries);
	e = &o->entries[o->n];
	memset(e, 0, sizeof *e);
	e->var = var;
	size_t h = hash_var(var, o->icap);
	while (o->index[h] != UINT32_MAX) h = (h + 1) & (o->icap - 1);
	o->index[h] = (uint32_t)o->n++;
	return e;
}

cljc_owned *cljc_owned_new(void) {
	cljc_owned *o = calloc(1, sizeof *o);
	if (!o) clj_fatal("out of memory");
	return o;
}

void cljc_owned_free(cljc_owned *o) {
	if (!o) return;
	free(o->entries);
	free(o->index);
	free(o->edges);
	free(o);
}

static bool enabled(void) { return !clj_opt_off(CLJ_NO_OPT_OWNED) && !clj_opt_off(CLJ_NO_OPT_EMIT); }

void cljc_owned_declare(cljc_owned *o, const clj_node *root, uint32_t order) {
	// a (declare f) is no definition: record_direct counts only a def with an init
	if (!enabled() || root->kind != CLJ_NODE_DEF || !root->u.def.init) return;
	entry          *e = intern(o, root->u.def.var.v);
	const clj_node *init = root->u.def.init;
	bool fn = init->kind == CLJ_NODE_FN && !init->u.fn.ncaptures && !root->u.def.dynamic && !root->u.def.macro;
	if (e->ndefs++ == 0) e->order = order;
	e->fixed = 0;
	for (uint32_t k = 0; e->ndefs == 1 && fn && k <= CLJ_FN_MAX_FIXED; k++) {
		if (init->u.fn.fixed[k]) e->fixed |= (uint32_t)1 << k;
	}
}

static uint64_t bit(uint32_t i) { return i < 64 ? (uint64_t)1 << i : 0; }

typedef struct {
	uint32_t level;
	uint64_t pinned;
} pins;

// compiler.c's pin_walk: the slots of the frame read by address, by a capture or through the static link.
static void pin_walk(const clj_node *n, void *ctx) {
	pins *p = ctx;
	switch (n->kind) {
	case CLJ_NODE_OUTER:
		if (n->u.outer.depth == p->level) p->pinned |= bit(n->u.outer.index);
		return;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cp = &n->u.fn.captures[i];
			if ((cp->kind == CLJ_CAPTURE_LOCAL && p->level == 0) || (cp->kind == CLJ_CAPTURE_OUTER && cp->depth == p->level)) p->pinned |= bit(cp->index);
		}
		return;
	case CLJ_NODE_DIRECT_FN:
		p->level++;
		clj_node_children(n, pin_walk, p);
		p->level--;
		return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) pin_walk(n->u.fused.args[i], p);
		p->level++;
		pin_walk(n->u.fused.fused, p);
		pin_walk(n->u.fused.original, p);
		p->level--;
		return;
	default: clj_node_children(n, pin_walk, p);
	}
}

static uint32_t frame_of(const clj_facts *facts, const clj_node *fn, const clj_node *body) {
	for (uint32_t i = 0; i < clj_facts_nframes(facts); i++) {
		const clj_facts_frame *fr = clj_facts_frame_at(facts, i);
		if (fr->owner == fn->id && fr->first == body->id) return i;
	}
	return UINT32_MAX;
}

uint64_t cljc_owned_candidates(const clj_node *fn, const clj_fn_arity *a, const clj_facts *facts) {
	if (!facts || a->variadic || a->nslots > 64 || a->nparams == 0) return 0;
	uint32_t fi = frame_of(facts, fn, a->body);
	if (fi == UINT32_MAX) return 0;
	pins p = {0, 0};
	pin_walk(a->body, &p);
	const clj_facts_join *j = NULL;
	for (uint32_t i = 0; i < clj_facts_njoins(facts) && !j; i++) {
		const clj_facts_join *cand = clj_facts_join_at(facts, i);
		if (cand->fn == fn->id && cand->arity == a->nparams) j = cand;
	}
	uint64_t c = 0;
	for (uint32_t i = 0; i < a->nparams; i++) {
		if (p.pinned & bit(i) || clj_facts_escape(facts, fi, i) == CLJ_ESCAPE_CAPTURED) continue;
		// a number has no cell to reuse, and an all-numeric arity keeps its primitive worker
		if (j && clj_domain_of(j->params[i]) != CLJ_DOMAIN_ANY) continue;
		c |= bit(i);
	}
	return c;
}

typedef struct {
	uint32_t node, callee, arity;
} site;

typedef struct {
	const cljc_owned *o;
	uint32_t          order;
	site             *sites;
	size_t            nsites, sites_cap;
} scan_ctx;

// Any argument of a direct call to a def of the set emitted before the caller: the masks are not settled yet.
static uint64_t scan_positions(void *arg, const clj_node *n) {
	scan_ctx       *s = arg;
	const clj_node *head = n->u.invoke.fn;
	if (head->kind != CLJ_NODE_VAR || n->u.invoke.n > CLJ_FN_MAX_FIXED) return 0;
	const entry *e = find(s->o, head->u.var.v);
	if (!e || e->ndefs != 1 || e->order > s->order || !((e->fixed >> n->u.invoke.n) & 1)) return 0;
	s->sites = grow(s->sites, &s->sites_cap, s->nsites, sizeof *s->sites);
	s->sites[s->nsites++] = (site){n->id, (uint32_t)(e - s->o->entries), n->u.invoke.n};
	return bit(n->u.invoke.n) - 1;
}

// Reads the optimizer marked as last uses: each hands the slot to an owning consumer, which a +0 slot pays a retain for.
static void last_reads(const clj_node *n, void *ctx) {
	uint64_t *reads = ctx;
	switch (n->kind) {
	case CLJ_NODE_LOCAL:
		if (n->u.local.last) *reads |= bit(n->u.local.index);
		return;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) last_reads(n->u.fused.args[i], ctx);
		return;
	default: clj_node_children(n, last_reads, ctx);
	}
}

static uint64_t let_slots(const clj_fn_arity *a) {
	uint64_t lets = 0;
	for (uint32_t i = a->nparams; i < a->nslots && i < 64; i++) {
		if ((int32_t)i != a->self_slot) lets |= bit(i);
	}
	return lets;
}

static uint64_t param_slots(const clj_fn_arity *a) { return a->nparams >= 64 ? UINT64_MAX : bit(a->nparams) - 1; }

// The slots a recur of this frame rebinds; a direct fn's recur rebinds its own frame's.
static void recur_slots(const clj_node *n, void *ctx) {
	uint64_t *out = ctx;
	switch (n->kind) {
	case CLJ_NODE_RECUR:
		for (uint32_t i = 0; i < n->u.recur.n; i++) *out |= bit(n->u.recur.slots[i]);
		break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) recur_slots(n->u.fused.args[i], ctx);
		return;
	default: break;
	}
	clj_node_children(n, recur_slots, ctx);
}

uint64_t cljc_owned_literal(const clj_node *fn, const clj_fn_arity *a, const clj_facts *facts, uint64_t offer, cljc_owned_positions positions, void *ctx) {
	if (!enabled() || !offer) return 0;
	uint64_t rebound = 0;
	recur_slots(a->body, &rebound);
	uint64_t cand = cljc_owned_candidates(fn, a, facts) & offer & ~rebound;
	if (!cand) return 0;
	uint64_t reads = 0;
	last_reads(a->body, &reads);
	cljc_reuse_plan *plan = cljc_reuse_plan_frame(a->body, facts, cand | let_slots(a), param_slots(a), true, false, positions, ctx);
	uint64_t         paired = 0;
	for (size_t i = 0; i < cljc_reuse_count(plan); i++) paired |= bit(cljc_reuse_pair_at(plan, i)->slot);
	cljc_reuse_plan_free(plan);
	return (reads | paired) & cand;
}

void cljc_owned_scan(cljc_owned *o, const clj_node *root, const clj_facts *facts) {
	if (!enabled() || !facts || root->kind != CLJ_NODE_DEF) return;
	entry *e = find(o, root->u.def.var.v);
	// a var defined once: root is that very def
	if (!e || e->ndefs != 1 || !e->fixed || e->scanned) return;
	const clj_node *fn = root->u.def.init;
	e->scanned = true;
	uint32_t self = (uint32_t)(e - o->entries);
	for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
		const clj_fn_arity *a = fn->u.fn.fixed[k];
		if (!a) continue;
		uint64_t cand = cljc_owned_candidates(fn, a, facts);
		e->cand[k] = cand;
		if (!cand) continue;
		uint64_t reads = 0;
		last_reads(a->body, &reads);
		scan_ctx         sc = {o, e->order, NULL, 0, 0};
		cljc_reuse_plan *plan = cljc_reuse_plan_frame(a->body, facts, cand | let_slots(a), param_slots(a), true, false, scan_positions, &sc);
		uint64_t         paired = 0;
		for (size_t i = 0; i < cljc_reuse_count(plan); i++) {
			const cljc_reuse_pair *p = cljc_reuse_pair_at(plan, i);
			if (!(cand & bit(p->slot))) continue;
			if (p->kind != CLJC_REUSE_HAND) {
				paired |= bit(p->slot);
				continue;
			}
			for (size_t j = 0; j < sc.nsites; j++) {
				if (sc.sites[j].node != p->node) continue;
				o->edges = grow(o->edges, &o->edges_cap, o->nedges, sizeof *o->edges);
				o->edges[o->nedges++] = (edge){self, k, p->slot, sc.sites[j].callee, sc.sites[j].arity, p->arg};
				break;
			}
		}
		free(sc.sites);
		e->local[k] = (reads | paired) & cand;
		e->paired[k] = paired & ~reads & cand;
		cljc_reuse_plan_free(plan);
	}
}

void cljc_owned_settle(cljc_owned *o) {
	for (size_t i = 0; i < o->n; i++) {
		entry *e = &o->entries[i];
		for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) e->mask[k] = e->fixed ? e->local[k] : 0;
	}
	// The masks only grow, a bit per parameter, so the closure ends; no widening is needed.
	for (bool changed = true; changed;) {
		changed = false;
		o->rounds++;
		for (size_t i = 0; i < o->nedges; i++) {
			const edge *g = &o->edges[i];
			entry      *from = &o->entries[g->from];
			const entry *to = &o->entries[g->to];
			if (!to->fixed || !((to->mask[g->to_arity] >> g->pos) & 1) || (from->mask[g->arity] & bit(g->param))) continue;
			from->mask[g->arity] |= bit(g->param);
			changed = true;
		}
	}
}

uint64_t cljc_owned_mask(const cljc_owned *o, clj_value var, uint32_t arity) {
	if (!o || !enabled() || arity > CLJ_FN_MAX_FIXED) return 0;
	const entry *e = find(o, var);
	return e && e->fixed && e->ndefs == 1 ? e->mask[arity] : 0;
}

void cljc_owned_stats_of(const cljc_owned *o, cljc_owned_stats *out) {
	memset(out, 0, sizeof *out);
	if (!o) return;
	out->rounds = o->rounds;
	for (size_t i = 0; i < o->n; i++) {
		const entry *e = &o->entries[i];
		if (!e->fixed || e->ndefs != 1) continue;
		for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
			uint64_t m = e->mask[k];
			if (!m) continue;
			out->arities++;
			out->params += (uint64_t)__builtin_popcountll(m);
			out->paired += (uint64_t)__builtin_popcountll(m & e->paired[k]);
			out->returned += (uint64_t)__builtin_popcountll(m & e->local[k] & ~e->paired[k]);
			out->handed += (uint64_t)__builtin_popcountll(m & ~e->local[k]);
		}
	}
}
