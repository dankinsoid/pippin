// @ai-generated(solo)
// The walk facts.c lends summary.c: one arity with its parameters at TOP, requirements and result out (NOTES.md, "Facts").
#ifndef CLJ_FACTS_INTERNAL_H
#define CLJ_FACTS_INTERNAL_H

#include "clj/summary.h"

// Which arity of fn a call with nargs arguments enters, as the evaluator resolves it; NULL for none.
static inline const clj_fn_arity *clj_facts_arity_for(const clj_node *fn, uint32_t nargs) {
	if (nargs <= CLJ_FN_MAX_FIXED && fn->u.fn.fixed[nargs]) return fn->u.fn.fixed[nargs];
	const clj_fn_arity *v = fn->u.fn.variadic;
	return v && nargs >= v->nparams ? v : NULL;
}

// Fills nparams, variadic, params with their positions, ret, effects and inferred; records nothing else. params enters
// the parameters at those facts instead of TOP (a specialized summary), NULL for TOP.
void clj_facts_walk_arity(const clj_node *fn, const clj_fn_arity *arity, clj_summaries *sums, clj_summary *out, const clj_fact *params);
// Effects of a clojure.core call by name: none for a predicate, IO and atom-write from short lists, else alloc|throw.
uint32_t clj_facts_core_effects(const char *name);
// The kind bit of a value of this descriptor, the dispatch pseudo-types included; HOST for anything unnamed.
uint32_t clj_facts_kind_of_type(const clj_type *t);
// Drops the direct-fn entries: keyed by a pointer into a tree, they must not outlive the table built over it.
void clj_summaries_forget_arities(clj_summaries *s);
// Nodes the walk on this thread is inside of, every nested summary walk included, and where a summary walk stops.
uint32_t clj_facts_walk_depth(void);
#define CLJ_FACTS_MAX_WALK_DEPTH 40

#endif
