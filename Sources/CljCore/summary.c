// @ai-generated(solo)
// The summary store: bottom-up over var roots, a widened fixpoint on recursion, cached per var epoch (NOTES.md, "Facts").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/epoch.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/intrinsics.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/proto.h"
#include "clj/string.h"
#include "clj/summary.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "facts_internal.h"

// The same bound as a loop variable's fixpoint in facts.c, for the same reason (NOTES.md, "Facts").
#define WIDEN_ROUNDS 3
// Nested summary computations before a callee is taken as unknown: the budget per function of design §6b.
#define MAX_DEPTH 6
#define MAX_DEPS 24

#define T_INT  (CLJ_T_FIXNUM | CLJ_T_LONG | CLJ_T_BIGINT)
#define T_NUM  (T_INT | CLJ_T_RATIO | CLJ_T_DECIMAL | CLJ_T_DOUBLE)
#define T_MAPS (CLJ_T_MAP | CLJ_T_SORTED_MAP | CLJ_T_RECORD)
#define T_SETS (CLJ_T_SET | CLJ_T_SORTED_SET)
#define T_COLL (T_MAPS | T_SETS | CLJ_T_VECTOR | CLJ_T_LIST)
#define T_SEQABLE (T_COLL | CLJ_T_STRING | CLJ_T_ARRAY | CLJ_T_NIL)

typedef enum { STATE_EMPTY, STATE_RUNNING, STATE_DONE } state;

typedef struct {
	clj_value var;
	uint32_t  epoch;
} dep;

typedef struct {
	const void *key;   // clj_var, or the clj_fn_arity of a direct fn
	uint32_t    arity; // argument count the entry answers for
	bool        is_var;
	state       state;
	uint32_t    world;     // clj_epoch() at computation: the guard once deps overflow, and for a protocol's implementors
	bool        overflow;  // more than MAX_DEPS vars were read: validity falls back to world
	bool        by_world;  // the entry rests on the protocol tables, not on var roots
	bool        unknown;   // nothing could be said; holds while world stands
	bool        in_cycle;  // computed above a fixpoint in flight: its result is transient and recomputed each round
	dep         deps[MAX_DEPS];
	uint32_t    ndeps;
	clj_summary s;
} entry;

struct clj_summaries {
	entry  **slots;
	uint32_t cap, count;
	entry   *stack[MAX_DEPTH + 1]; // entries being computed, innermost last
	uint32_t depth;
	uint32_t invalidated, rounds, widenings;
	clj_annotation_conflict *conflicts;
	uint32_t                 nconflicts, cconflicts;
};

static void *xalloc(size_t n, size_t size) {
	void *p = calloc(n ? n : 1, size);
	if (!p) clj_fatal("out of memory");
	return p;
}

clj_summaries *clj_summaries_new(void) {
	clj_summaries *s = xalloc(1, sizeof *s);
	s->cap = 64;
	s->slots = xalloc(s->cap, sizeof(entry *));
	return s;
}

void clj_summaries_free(clj_summaries *s) {
	if (!s) return;
	for (uint32_t i = 0; i < s->cap; i++) free(s->slots[i]);
	free(s->slots);
	free(s->conflicts);
	free(s);
}

// ---- the table

static uint32_t hash_key(const void *key, uint32_t arity) {
	uint64_t h = (uint64_t)(uintptr_t)key >> 4;
	h ^= (uint64_t)arity * 0x9e3779b97f4a7c15ull;
	h *= 0xff51afd7ed558ccdull;
	return (uint32_t)(h >> 32);
}

static entry **slot_for(entry **slots, uint32_t cap, const void *key, uint32_t arity) {
	uint32_t mask = cap - 1;
	for (uint32_t i = hash_key(key, arity) & mask;; i = (i + 1) & mask) {
		if (!slots[i] || (slots[i]->key == key && slots[i]->arity == arity)) return &slots[i];
	}
}

static entry *entry_for(clj_summaries *s, const void *key, uint32_t arity) {
	entry **slot = slot_for(s->slots, s->cap, key, arity);
	if (*slot) return *slot;
	if ((s->count + 1) * 4 > s->cap * 3) {
		uint32_t cap = s->cap * 2;
		entry  **fresh = xalloc(cap, sizeof(entry *));
		for (uint32_t i = 0; i < s->cap; i++) {
			if (s->slots[i]) *slot_for(fresh, cap, s->slots[i]->key, s->slots[i]->arity) = s->slots[i];
		}
		free(s->slots);
		s->slots = fresh;
		s->cap = cap;
		slot = slot_for(s->slots, s->cap, key, arity);
	}
	entry *e = xalloc(1, sizeof *e);
	e->key = key;
	e->arity = arity;
	*slot = e;
	s->count++;
	return e;
}

// A var read on behalf of every computation in flight: a caller's summary rests on what its callees read.
static void note_dep(clj_summaries *s, clj_value var) {
	uint32_t epoch = clj_var_epoch(var);
	for (uint32_t d = 0; d < s->depth; d++) {
		entry *e = s->stack[d];
		bool   seen = false;
		for (uint32_t i = 0; i < e->ndeps && !seen; i++) seen = e->deps[i].var == var;
		if (seen) continue;
		if (e->ndeps == MAX_DEPS) e->overflow = true;
		else e->deps[e->ndeps++] = (dep){var, epoch};
	}
}

static bool entry_valid(const entry *e) {
	if (e->state != STATE_DONE) return true;
	if (e->overflow || e->by_world) return e->world == clj_epoch();
	for (uint32_t i = 0; i < e->ndeps; i++) {
		if (clj_var_epoch(e->deps[i].var) != e->deps[i].epoch) return false;
	}
	return true;
}

// ---- annotations: {:args [spec ...] :ret spec} under :clj/facts in the var's meta

static const struct {
	const char *name;
	uint32_t    types;
} aggregates[] = {
	{"any", CLJ_T_TOP},   {"int", T_INT},         {"number", T_NUM},   {"coll", T_COLL},
	{"maps", T_MAPS},     {"sets", T_SETS},       {"seqable", T_SEQABLE}, {"ident", CLJ_T_KEYWORD | CLJ_T_SYMBOL},
	{"assoc", T_MAPS | CLJ_T_VECTOR}, {"indexed", CLJ_T_VECTOR | CLJ_T_LIST | CLJ_T_STRING | CLJ_T_ARRAY},
};

static bool types_of_keyword(clj_value k, uint32_t *out) {
	if (!clj_is_keyword(k) || !clj_is_nil(clj_keyword_ns(k))) return false;
	const char *name = clj_string_bytes(clj_keyword_name(k));
	for (size_t i = 0; i < sizeof aggregates / sizeof *aggregates; i++) {
		if (strcmp(aggregates[i].name, name) == 0) {
			*out = aggregates[i].types;
			return true;
		}
	}
	for (uint32_t bit = 0; bit < 27; bit++) {
		if (strcmp(clj_fact_kind_name(1u << bit), name) == 0) {
			*out = 1u << bit;
			return true;
		}
	}
	return false;
}

bool clj_fact_of_spec(clj_value spec, clj_fact *out) {
	uint32_t types = 0;
	if (clj_is_keyword(spec)) {
		if (!types_of_keyword(spec, &types)) return false;
	}
	else if (clj_is_vector(spec)) {
		for (uint32_t i = 0; i < clj_vector_count(spec); i++) {
			uint32_t t = 0;
			if (!types_of_keyword(clj_vector_nth(spec, i), &t)) return false;
			types |= t;
		}
	}
	else return false;
	clj_fact f = clj_fact_top();
	f.types = types;
	f.null = (types & CLJ_T_NIL) ? (types == CLJ_T_NIL ? CLJ_NULL_ALWAYS : CLJ_NULL_MAYBE) : CLJ_NULL_NEVER;
	*out = clj_fact_meet_wide(f, f); // a requirement keeps every kind it names (facts.h)
	return true;
}

typedef struct {
	bool     present;
	uint32_t nargs;
	clj_fact args[CLJ_FN_MAX_FIXED + 1];
	bool     has_ret;
	clj_fact ret;
} annotation;

// Owned lookup on any map kind; nil when absent.
static clj_value lookup(clj_value m, const char *key) {
	if (!clj_has_core(m, CLJ_CORE_MAP)) return CLJ_NIL;
	clj_value r = clj_get2(m, clj_keyword_from_cstr(key));
	return r == CLJ_THROWN ? (clj_release(clj_take_pending()), CLJ_NIL) : r;
}

static annotation annotation_of(clj_value var) {
	annotation a = {0};
	clj_value  meta = clj_var_meta(var);
	if (clj_is_nil(meta)) return a;
	clj_value facts = lookup(meta, "clj/facts");
	if (clj_is_nil(facts)) return a;
	clj_value args = lookup(facts, "args"), ret = lookup(facts, "ret");
	clj_release(facts);
	a.present = true;
	if (clj_is_vector(args)) {
		for (uint32_t i = 0; i < clj_vector_count(args) && i <= CLJ_FN_MAX_FIXED; i++) {
			if (!clj_fact_of_spec(clj_vector_nth(args, i), &a.args[i])) a.args[i] = clj_fact_top();
			a.nargs = i + 1;
		}
	}
	if (!clj_is_nil(ret)) a.has_ret = clj_fact_of_spec(ret, &a.ret);
	clj_release(args);
	clj_release(ret);
	return a;
}

static void add_conflict(clj_summaries *s, clj_value var, uint32_t arg, uint32_t line, uint32_t col, clj_fact annotated, clj_fact inferred) {
	if (s->nconflicts == s->cconflicts) {
		s->cconflicts = s->cconflicts ? s->cconflicts * 2 : 8;
		clj_annotation_conflict *fresh = realloc(s->conflicts, s->cconflicts * sizeof *fresh);
		if (!fresh) clj_fatal("out of memory");
		s->conflicts = fresh;
	}
	s->conflicts[s->nconflicts++] = (clj_annotation_conflict){var, arg, line, col, annotated, inferred};
}

// The annotation meets the inferred summary; a meet down to BOTTOM means one of them is wrong and is reported, not stored.
static void apply_annotation(clj_summaries *s, clj_value var, const annotation *a, clj_summary *sum) {
	if (!a->present) return;
	sum->annotated = true;
	uint32_t dummy = 0;
	if (a->nargs > sum->nparams) {
		for (uint32_t i = sum->nparams; i < a->nargs; i++) sum->params[i] = clj_fact_top();
		sum->nparams = a->nargs;
	}
	for (uint32_t i = 0; i < a->nargs; i++) {
		clj_fact m = clj_fact_meet_wide(sum->params[i], a->args[i]);
		if (m.types == CLJ_T_BOTTOM && sum->params[i].types != CLJ_T_BOTTOM) {
			add_conflict(s, var, i, sum->param_line[i], sum->param_col[i], a->args[i], sum->params[i]);
			continue;
		}
		if (clj_fact_is_top(sum->params[i])) sum->param_line[i] = sum->param_col[i] = 0;
		sum->params[i] = m;
	}
	if (a->has_ret && sum->ret.types != CLJ_T_BOTTOM) {
		clj_fact m = clj_fact_meet(sum->ret, a->ret, &dummy);
		if (m.types == CLJ_T_BOTTOM) add_conflict(s, var, UINT32_MAX, 0, 0, a->ret, sum->ret);
		else sum->ret = m;
	}
}

// ---- computing an entry

static void summary_reset(clj_summary *sum, uint32_t nparams, bool variadic) {
	memset(sum, 0, sizeof *sum);
	sum->nparams = nparams;
	sum->variadic = variadic;
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) sum->params[i] = clj_fact_top();
	sum->ret = clj_fact_bottom(); // optimistic: the fixpoint climbs from here
}

static bool summary_eq(const clj_summary *a, const clj_summary *b) {
	if (a->nparams != b->nparams || a->effects != b->effects || !clj_fact_eq(a->ret, b->ret)) return false;
	for (uint32_t i = 0; i < a->nparams; i++) {
		if (!clj_fact_eq(a->params[i], b->params[i])) return false;
	}
	return true;
}

static void summary_widen(clj_summary *sum) {
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) sum->params[i] = clj_fact_top();
	sum->ret = clj_fact_top();
	sum->effects = CLJ_EFFECT_ANY;
}

static const clj_fn_arity *arity_for(const clj_node *fn, uint32_t nargs) {
	if (nargs <= CLJ_FN_MAX_FIXED && fn->u.fn.fixed[nargs]) return fn->u.fn.fixed[nargs];
	const clj_fn_arity *v = fn->u.fn.variadic;
	return v && nargs >= v->nparams ? v : NULL;
}

typedef struct {
	uint32_t types;
	bool     open; // Object or a core interface was extended: anything may satisfy
} implementors;

static void visit_implementor(const clj_type *t, void *ctx) {
	implementors *im = ctx;
	if (t == clj_object_type() || clj_proto_is_interface_type(t)) im->open = true;
	else im->types |= clj_facts_kind_of_type(t);
}

// A method call succeeds only on a receiver whose type is in the protocol's tables: the join of those kinds is the requirement.
static void protocol_summary(clj_summaries *s, entry *e, clj_value method, uint32_t nargs) {
	const clj_method_ctx *ctx = clj_method_ctx_of(method);
	clj_protocol         *proto = clj_protocol_of(ctx->proto);
	implementors          im = {0, false};
	clj_proto_each_immortal(ctx->proto, visit_implementor, &im);
	if (atomic_load_explicit(&proto->user_types, memory_order_relaxed)) im.types |= CLJ_T_HOST;
	if (atomic_load_explicit(&proto->user_records, memory_order_relaxed)) im.types |= CLJ_T_RECORD;
	summary_reset(&e->s, nargs, false);
	e->s.inferred = true;
	e->s.effects = CLJ_EFFECT_ANY;
	e->s.ret = clj_fact_top();
	e->by_world = true;
	if (!im.open && im.types) {
		clj_fact f = clj_fact_top();
		f.types = im.types;
		f.null = (im.types & CLJ_T_NIL) ? CLJ_NULL_MAYBE : CLJ_NULL_NEVER;
		e->s.params[0] = clj_fact_cap(f);
	}
	(void)s;
}

static void walk_fixpoint(clj_summaries *s, entry *e, const clj_node *fn, const clj_fn_arity *a) {
	summary_reset(&e->s, a->nparams, a->variadic);
	bool converged = false;
	for (uint32_t round = 0; round < WIDEN_ROUNDS; round++) {
		clj_summary next;
		summary_reset(&next, a->nparams, a->variadic);
		clj_facts_walk_arity(fn, a, s, &next);
		next.recursive = e->s.recursive;
		if (!e->s.recursive || summary_eq(&next, &e->s)) {
			e->s = next;
			converged = true;
			break;
		}
		e->s = next;
		s->rounds++;
	}
	if (!converged) {
		summary_widen(&e->s);
		s->widenings++;
	}
}

// The entry at depth k is running; everything computed above it rests on its optimistic value.
static void mark_cycle(clj_summaries *s, const entry *hit) {
	for (uint32_t d = 0; d < s->depth; d++) {
		if (s->stack[d] != hit) continue;
		for (uint32_t k = d + 1; k < s->depth; k++) s->stack[k]->in_cycle = true;
		return;
	}
}

static void finish(entry *e) {
	// a body that never returns normally has a BOTTOM result; a call node must not inherit it
	if (e->s.ret.types == CLJ_T_BOTTOM) e->s.ret = clj_fact_top();
	// a singleton borrows a constant of the callee's tree, which only the callee's root keeps alive
	e->s.ret.singleton = CLJ_UNBOUND;
	e->state = e->in_cycle ? STATE_EMPTY : STATE_DONE;
}

static uint32_t core_effects_of(clj_value var) {
	clj_value ns = clj_var_ns(var);
	if (!clj_is_symbol(ns) || strcmp(clj_string_bytes(clj_symbol_name(ns)), "clojure.core") != 0) return CLJ_EFFECT_ANY;
	return clj_facts_core_effects(clj_string_bytes(clj_symbol_name(clj_var_name(var))));
}

// A walk started this deep would be cut at once (facts.c); it is left for a caller with headroom.
static bool budget_left(const clj_summaries *s) { return s->depth <= MAX_DEPTH && clj_facts_walk_depth() + 8 < CLJ_FACTS_MAX_WALK_DEPTH; }

static const clj_summary *compute_var(clj_summaries *s, entry *e, clj_value var, uint32_t nargs) {
	if (!budget_left(s)) return NULL;
	if (e->state == STATE_DONE) s->invalidated++;
	e->state = STATE_RUNNING;
	e->ndeps = 0;
	e->overflow = false;
	e->by_world = false;
	e->in_cycle = false;
	e->world = clj_epoch();
	s->stack[s->depth++] = e;
	note_dep(s, var);
	summary_reset(&e->s, nargs, false);
	clj_value  root = clj_var_root(var);
	annotation a = annotation_of(var);
	bool       inferred = false;
	if (root != CLJ_UNBOUND && clj_is_fn(root)) {
		clj_fn *f = clj_fn_of(root);
		if (clj_is_protocol_method(root) && nargs >= 1) {
			protocol_summary(s, e, root, nargs);
			inferred = true;
		}
		else if (f->kind == CLJ_FN_CLOSURE) {
			const clj_fn_arity *ar = arity_for(f->u.node, nargs);
			if (ar) {
				walk_fixpoint(s, e, f->u.node, ar);
				inferred = true;
			}
		}
	}
	if (!inferred) {
		summary_reset(&e->s, nargs, false);
		e->s.ret = clj_fact_top();
		e->s.effects = core_effects_of(var);
	}
	apply_annotation(s, var, &a, &e->s);
	s->depth--;
	if (!inferred && !a.present) {
		e->state = STATE_EMPTY;
		return NULL;
	}
	finish(e);
	return &e->s;
}

const clj_summary *clj_summary_of_var(clj_summaries *s, clj_value var, uint32_t nargs) {
	if (!clj_is_var(var)) return NULL;
	entry *e = entry_for(s, clj_to_ptr(var), nargs);
	e->is_var = true;
	if (e->state == STATE_RUNNING) {
		e->s.recursive = true;
		mark_cycle(s, e);
		note_dep(s, var);
		return &e->s;
	}
	if (e->state == STATE_DONE && entry_valid(e)) {
		note_dep(s, var);
		return &e->s;
	}
	if (e->state == STATE_EMPTY && e->unknown && e->world == clj_epoch()) return NULL;
	if (!budget_left(s)) return NULL; // out of budget is not "unknown": asked again with headroom, it is computed
	const clj_summary *r = compute_var(s, e, var, nargs);
	e->unknown = !r;
	return r;
}

const clj_summary *clj_summary_of_arity(clj_summaries *s, const clj_node *fn, const clj_fn_arity *a) {
	if (!fn || !a) return NULL;
	entry *e = entry_for(s, a, a->nparams);
	if (e->state == STATE_RUNNING) {
		e->s.recursive = true;
		mark_cycle(s, e);
		return &e->s;
	}
	if (e->state == STATE_DONE && entry_valid(e)) return &e->s;
	if (!budget_left(s)) return NULL;
	if (e->state == STATE_DONE) s->invalidated++;
	e->state = STATE_RUNNING;
	e->ndeps = 0;
	e->overflow = false;
	e->by_world = false;
	e->in_cycle = false;
	e->world = clj_epoch();
	s->stack[s->depth++] = e;
	walk_fixpoint(s, e, fn, a);
	s->depth--;
	finish(e);
	return &e->s;
}

void clj_summaries_forget_arities(clj_summaries *s) {
	if (s->depth) return;
	entry **fresh = xalloc(s->cap, sizeof(entry *));
	uint32_t count = 0;
	for (uint32_t i = 0; i < s->cap; i++) {
		entry *e = s->slots[i];
		if (!e) continue;
		if (e->is_var) {
			*slot_for(fresh, s->cap, e->key, e->arity) = e;
			count++;
		}
		else free(e);
	}
	free(s->slots);
	s->slots = fresh;
	s->count = count;
}

clj_fact clj_summary_var_fact(clj_summaries *s, clj_value var) {
	if (!clj_is_var(var)) return clj_fact_top();
	note_dep(s, var);
	clj_value root = clj_var_root(var);
	if (root == CLJ_UNBOUND) return clj_fact_top();
	clj_fact f = clj_fact_of_value(root);
	f.singleton = CLJ_UNBOUND;
	return f;
}

uint32_t clj_summaries_epoch_seen(const clj_summaries *s, clj_value var) {
	for (uint32_t i = 0; i < s->cap; i++) {
		const entry *e = s->slots[i];
		if (!e || e->key != clj_to_ptr(var)) continue;
		for (uint32_t d = 0; d < e->ndeps; d++) {
			if (e->deps[d].var == var) return e->deps[d].epoch;
		}
	}
	return UINT32_MAX;
}

uint32_t clj_summaries_nannotation_conflicts(const clj_summaries *s) { return s->nconflicts; }
const clj_annotation_conflict *clj_summaries_annotation_conflict(const clj_summaries *s, uint32_t i) {
	return i < s->nconflicts ? &s->conflicts[i] : NULL;
}

static void fact_text(clj_fact f, char *buf, size_t n) {
	size_t k = 0;
	if (f.types == CLJ_T_TOP) {
		snprintf(buf, n, "anything");
		return;
	}
	for (uint32_t bit = 0; bit < 27 && k + 1 < n; bit++) {
		if (!(f.types & (1u << bit))) continue;
		k += (size_t)snprintf(buf + k, n - k, "%s%s", k ? "|" : "", clj_fact_kind_name(1u << bit));
	}
	if (k == 0) snprintf(buf, n, "nothing");
}

const char *clj_annotation_conflict_message(const clj_annotation_conflict *c, char *buf, size_t n) {
	char ann[256], inf[256];
	fact_text(c->annotated, ann, sizeof ann);
	fact_text(c->inferred, inf, sizeof inf);
	const char *ns = clj_string_bytes(clj_symbol_name(clj_var_ns(c->var)));
	const char *name = clj_string_bytes(clj_symbol_name(clj_var_name(c->var)));
	if (c->arg == UINT32_MAX) snprintf(buf, n, "%s/%s: annotation says the result is %s, the body answers %s", ns, name, ann, inf);
	else snprintf(buf, n, "%s/%s: annotation says argument %u is %s, the body uses it as %s at %u:%u", ns, name, c->arg, ann, inf, c->use_line, c->use_col);
	return buf;
}

uint32_t clj_summaries_count(const clj_summaries *s) { return s->count; }
uint32_t clj_summaries_invalidated(const clj_summaries *s) { return s->invalidated; }
uint32_t clj_summaries_rounds(const clj_summaries *s) { return s->rounds; }
uint32_t clj_summaries_widenings(const clj_summaries *s) { return s->widenings; }
