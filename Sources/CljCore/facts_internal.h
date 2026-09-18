// @ai-generated(solo)
// The walk facts.c lends summary.c: one arity with its parameters at TOP, requirements and result out (NOTES.md, "Facts").
#ifndef CLJ_FACTS_INTERNAL_H
#define CLJ_FACTS_INTERNAL_H

#include "clj/summary.h"

// Fills nparams, variadic, params with their positions, ret, effects and inferred; records nothing else.
void clj_facts_walk_arity(const clj_node *fn, const clj_fn_arity *arity, clj_summaries *sums, clj_summary *out);
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
