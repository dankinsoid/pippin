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
#include "clj/ns.h"
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
	bool     no_annotations;
	clj_diagnostic *diags;
	uint32_t        ndiags, cdiags, nerrors;
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

void clj_summaries_use_annotations(clj_summaries *s, bool on) { s->no_annotations = !on; }

void clj_summaries_free(clj_summaries *s) {
	if (!s) return;
	for (uint32_t i = 0; i < s->cap; i++) free(s->slots[i]);
	free(s->slots);
	free(s->diags);
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

// ---- schemas: the fixed vocabulary of design §3, each tag's abstract interpretation (schema → fact)

typedef struct {
	const char *tag;
	uint32_t    types;
} tag_types;

// The value tags; :nil goes through the null lattice, :any is TOP.
static const tag_types value_tags[] = {
	{"int", T_INT},      {"double", CLJ_T_DOUBLE},  {"number", T_NUM},     {"string", CLJ_T_STRING}, {"keyword", CLJ_T_KEYWORD},
	{"symbol", CLJ_T_SYMBOL}, {"boolean", CLJ_T_BOOL}, {"map", T_MAPS},    {"vector", CLJ_T_VECTOR}, {"set", T_SETS},
	{"seq", CLJ_T_LIST}, {"fn", CLJ_T_FN},          {"tuple", CLJ_T_VECTOR}, {"=>", CLJ_T_FN},
};

static bool tag_is(clj_value v, const char *name) {
	return clj_is_keyword(v) && clj_is_nil(clj_keyword_ns(v)) && strcmp(clj_string_bytes(clj_keyword_name(v)), name) == 0;
}

static clj_fact fact_of_types(uint32_t types) {
	clj_fact f = clj_fact_top();
	f.types = types;
	f.null = (types & CLJ_T_NIL) ? (types == CLJ_T_NIL ? CLJ_NULL_ALWAYS : CLJ_NULL_MAYBE) : CLJ_NULL_NEVER;
	return clj_fact_meet_wide(f, f);
}

// A singleton is kept only for a value that no tree owns: an immediate or an interned keyword.
static clj_fact fact_of_constant(clj_value v) {
	clj_fact f = clj_fact_of_value(v);
	if (clj_is_ptr(v) && !clj_is_keyword(v)) f.singleton = CLJ_UNBOUND;
	return f;
}

// The children of a vector schema past the tag and an optional properties map.
static uint32_t schema_children(clj_value v, uint32_t *first) {
	uint32_t n = clj_vector_count(v);
	*first = n > 1 && clj_has_core(clj_vector_nth(v, 1), CLJ_CORE_MAP) ? 2 : 1;
	return n;
}

bool clj_fact_of_schema(clj_value schema, clj_fact *out) {
	*out = clj_fact_top();
	if (clj_is_keyword(schema)) {
		if (tag_is(schema, "any")) return true;
		if (tag_is(schema, "nil")) {
			*out = fact_of_types(CLJ_T_NIL);
			return true;
		}
		for (size_t i = 0; i < sizeof value_tags / sizeof *value_tags; i++) {
			if (tag_is(schema, value_tags[i].tag)) {
				*out = fact_of_types(value_tags[i].types);
				return true;
			}
		}
		return false; // an unknown tag is TOP, never an error (design §3)
	}
	if (!clj_is_vector(schema) || clj_vector_count(schema) == 0) return false;
	clj_value tag = clj_vector_nth(schema, 0);
	uint32_t  first = 0, n = schema_children(schema, &first);
	bool      whole = true;
	if (tag_is(tag, "maybe") || tag_is(tag, "?") || tag_is(tag, "*")) {
		clj_fact x;
		if (n <= first) return false;
		whole = clj_fact_of_schema(clj_vector_nth(schema, first), &x);
		*out = tag_is(tag, "maybe") ? clj_fact_join(x, fact_of_types(CLJ_T_NIL)) : x;
		return whole;
	}
	if (tag_is(tag, "or") || tag_is(tag, "enum")) {
		bool     is_enum = tag_is(tag, "enum");
		clj_fact r = clj_fact_bottom();
		for (uint32_t i = first; i < n; i++) {
			clj_fact x;
			if (is_enum) x = fact_of_constant(clj_vector_nth(schema, i));
			else whole = clj_fact_of_schema(clj_vector_nth(schema, i), &x) && whole;
			r = clj_fact_join(r, x);
		}
		*out = r;
		return whole;
	}
	if (tag_is(tag, "and")) {
		clj_fact r = clj_fact_top();
		for (uint32_t i = first; i < n; i++) {
			clj_fact x;
			whole = clj_fact_of_schema(clj_vector_nth(schema, i), &x) && whole;
			r = clj_fact_meet_wide(r, x);
		}
		*out = r;
		return whole;
	}
	if (tag_is(tag, "=")) {
		if (n <= first) return false;
		*out = fact_of_constant(clj_vector_nth(schema, first));
		return true;
	}
	if (tag_is(tag, "fn")) return true; // [:fn pred]: whatever the predicate accepts
	// the structural tags name their kind; their children describe elements, which no fact holds yet
	for (size_t i = 0; i < sizeof value_tags / sizeof *value_tags; i++) {
		if (tag_is(tag, value_tags[i].tag)) {
			*out = fact_of_types(value_tags[i].types);
			return true;
		}
	}
	return false;
}

static clj_value kw(const char *name) { return clj_keyword_from_cstr(name); }

// conj retains the item, so an immortal keyword and a borrowed singleton go in as they are.
static clj_value vec_push(clj_value v, clj_value item) { return clj_vector_conj(v, item); }

// The total embedding fact → schema: a kind set as [:or …] of the widest tags that fit, then whatever is
// left as a tag of its own name (which the vocabulary does not read back: the gap the round trip reports).
clj_value clj_fact_to_schema(clj_fact f) {
	if (f.types == CLJ_T_TOP) return kw("any");
	if (f.singleton != CLJ_UNBOUND && clj_fact_union_size(f) == 1) {
		clj_value v = vec_push(clj_vector_empty(), kw("="));
		return vec_push(v, f.singleton);
	}
	uint32_t  types = f.types & ~(uint32_t)CLJ_T_NIL;
	clj_value members = clj_vector_empty();
	static const tag_types by_width[] = {
		{"number", T_NUM},        {"int", T_INT},          {"double", CLJ_T_DOUBLE}, {"map", T_MAPS},          {"set", T_SETS},
		{"string", CLJ_T_STRING}, {"keyword", CLJ_T_KEYWORD}, {"symbol", CLJ_T_SYMBOL}, {"boolean", CLJ_T_BOOL}, {"vector", CLJ_T_VECTOR},
		{"seq", CLJ_T_LIST},      {"fn", CLJ_T_FN},
	};
	for (size_t i = 0; i < sizeof by_width / sizeof *by_width; i++) {
		if ((types & by_width[i].types) == by_width[i].types) {
			members = vec_push(members, kw(by_width[i].tag));
			types &= ~by_width[i].types;
		}
	}
	for (uint32_t bit = 0; bit < 27; bit++) {
		if (types & (1u << bit)) members = vec_push(members, kw(clj_fact_kind_name(1u << bit)));
	}
	clj_value r;
	if (clj_vector_count(members) == 1) {
		r = clj_retain(clj_vector_nth(members, 0));
		clj_release(members);
	}
	else {
		r = vec_push(clj_vector_empty(), kw("or"));
		for (uint32_t i = 0; i < clj_vector_count(members); i++) r = vec_push(r, clj_vector_nth(members, i));
		clj_release(members);
	}
	if (f.types == CLJ_T_NIL) {
		clj_release(r);
		return kw("nil");
	}
	if (f.types & CLJ_T_NIL) {
		clj_value m = vec_push(clj_vector_empty(), kw("maybe"));
		return vec_push(m, r);
	}
	return r;
}

// ---- annotations: [:=> [:cat arg-schema …] ret-schema] under :=> in the var's meta (design §3, anchor 1)

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
	clj_value schema = lookup(meta, "=>");
	if (clj_is_nil(schema)) return a;
	if (clj_is_vector(schema) && clj_vector_count(schema) >= 3 && tag_is(clj_vector_nth(schema, 0), "=>")) {
		clj_value in = clj_vector_nth(schema, 1), out = clj_vector_nth(schema, 2);
		a.present = true;
		if (clj_is_vector(in) && clj_vector_count(in) > 0 && tag_is(clj_vector_nth(in, 0), "cat")) {
			uint32_t first = 0, n = schema_children(in, &first);
			for (uint32_t i = first; i < n && a.nargs <= CLJ_FN_MAX_FIXED; i++) {
				clj_value item = clj_vector_nth(in, i);
				// the rest of the arguments: no requirement on any one element
				if (clj_is_vector(item) && clj_vector_count(item) > 0 && tag_is(clj_vector_nth(item, 0), "*")) break;
				clj_fact_of_schema(item, &a.args[a.nargs++]);
			}
		}
		a.has_ret = true;
		clj_fact_of_schema(out, &a.ret);
	}
	clj_release(schema);
	return a;
}

static void add_diagnostic(clj_summaries *s, clj_diag_kind kind, clj_value var, uint32_t arg, uint32_t line, uint32_t col, clj_fact declared,
                           clj_fact inferred) {
	if (s->ndiags == s->cdiags) {
		s->cdiags = s->cdiags ? s->cdiags * 2 : 8;
		clj_diagnostic *fresh = realloc(s->diags, s->cdiags * sizeof *fresh);
		if (!fresh) clj_fatal("out of memory");
		s->diags = fresh;
	}
	clj_diag_severity severity = kind == CLJ_DIAG_DECL_CONFLICT ? CLJ_DIAG_ERROR : CLJ_DIAG_WARNING;
	if (severity == CLJ_DIAG_ERROR) s->nerrors++;
	s->diags[s->ndiags++] = (clj_diagnostic){kind, severity, 0, 0, line, col, var, arg, inferred, declared, false};
}

static bool warnings_on(clj_value var) {
	clj_value ns = clj_ns_find(clj_var_ns(var));
	return clj_is_nil(ns) || clj_facts_warnings_enabled(ns);
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
			add_diagnostic(s, CLJ_DIAG_DECL_CONFLICT, var, i, sum->param_line[i], sum->param_col[i], a->args[i], sum->params[i]);
			continue;
		}
		// a position the declaration decides is reported as declared (a TOP argument there warns at the site)
		if (!clj_fact_is_top(a->args[i])) sum->param_line[i] = sum->param_col[i] = 0;
		sum->params[i] = m;
	}
	if (a->has_ret && sum->ret.types != CLJ_T_BOTTOM) {
		clj_fact m = clj_fact_meet(sum->ret, a->ret, &dummy);
		if (m.types == CLJ_T_BOTTOM) add_diagnostic(s, CLJ_DIAG_DECL_CONFLICT, var, UINT32_MAX, 0, 0, a->ret, sum->ret);
		else {
			if (sum->inferred && clj_fact_is_top(sum->ret) && !clj_fact_is_top(a->ret) && warnings_on(var))
				add_diagnostic(s, CLJ_DIAG_TOP_RESULT, var, UINT32_MAX, 0, 0, a->ret, sum->ret);
			sum->ret = m;
		}
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
	annotation a = s->no_annotations ? (annotation){0} : annotation_of(var);
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

uint32_t              clj_summaries_ndiagnostics(const clj_summaries *s) { return s->ndiags; }
const clj_diagnostic *clj_summaries_diagnostic(const clj_summaries *s, uint32_t i) { return i < s->ndiags ? &s->diags[i] : NULL; }
uint32_t              clj_summaries_nerrors(const clj_summaries *s) { return s->nerrors; }

uint32_t clj_summaries_count(const clj_summaries *s) { return s->count; }
uint32_t clj_summaries_invalidated(const clj_summaries *s) { return s->invalidated; }
uint32_t clj_summaries_rounds(const clj_summaries *s) { return s->rounds; }
uint32_t clj_summaries_widenings(const clj_summaries *s) { return s->widenings; }
