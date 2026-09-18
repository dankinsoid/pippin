// @ai-generated(solo)
// Function summaries of the facts lattice: pass 1 bottom-up over var roots, cached per var epoch (NOTES.md, "Facts").
#ifndef CLJ_SUMMARY_H
#define CLJ_SUMMARY_H

#include "facts.h"

// Effects as far as they fall out of the walk (design §3); an unknown callee has every bit.
enum {
	CLJ_EFFECT_ALLOC = 1u << 0,
	CLJ_EFFECT_THROW = 1u << 1,
	CLJ_EFFECT_IO    = 1u << 2,
	CLJ_EFFECT_ATOM  = 1u << 3, // atom-write
};
#define CLJ_EFFECT_ANY 0xfu

// One arity: requirements are the meet of a parameter's uses along a path, joined across branches; unknown is TOP.
typedef struct {
	uint32_t nparams;
	bool     variadic;  // params[nparams] is the rest parameter then
	bool     inferred;  // a body was walked
	bool     annotated; // the var's :clj/facts meta contributed
	bool     recursive; // the walk reached its own summary: a fixpoint was run
	uint32_t effects;
	clj_fact params[CLJ_FN_MAX_FIXED + 1];
	uint32_t param_line[CLJ_FN_MAX_FIXED + 1], param_col[CLJ_FN_MAX_FIXED + 1]; // the use that imposed it; 0 for an annotation
	clj_fact ret;
} clj_summary;

typedef struct clj_summaries clj_summaries;

// Runtime state: every entry is cached with the epoch of each var its walk read and recomputed when one moved. Not thread-safe.
clj_summaries *clj_summaries_new(void);
void           clj_summaries_free(clj_summaries *s);

// NULL for an unbound var, a native without an annotation or no arity for nargs. Borrowed until the next call into the store.
const clj_summary *clj_summary_of_var(clj_summaries *s, clj_value var, uint32_t nargs);
// A direct fn's arity, keyed by node identity.
const clj_summary *clj_summary_of_arity(clj_summaries *s, const clj_node *fn, const clj_fn_arity *arity);

// Kind and descriptor of the current root, never a singleton: the root may be rebound and only the epoch guards it. TOP when unbound.
clj_fact clj_summary_var_fact(clj_summaries *s, clj_value var);
// The var's epoch as the store last read it; UINT32_MAX when it never did.
uint32_t clj_summaries_epoch_seen(const clj_summaries *s, clj_value var);

// An annotation that contradicts the inferred summary: either the annotation or the body is wrong.
typedef struct {
	clj_value var;
	uint32_t  arg;               // UINT32_MAX for the result
	uint32_t  use_line, use_col; // the use in the body the inference rests on
	clj_fact  annotated, inferred;
} clj_annotation_conflict;

uint32_t                       clj_summaries_nannotation_conflicts(const clj_summaries *s);
const clj_annotation_conflict *clj_summaries_annotation_conflict(const clj_summaries *s, uint32_t i);
// "clojure.core/vec: annotation says vector, the body answers seq at 12:4" into buf; returns buf.
const char *clj_annotation_conflict_message(const clj_annotation_conflict *c, char *buf, size_t n);

// For the report: entries held, entries recomputed after a redefinition, fixpoint rounds run, summaries widened.
uint32_t clj_summaries_count(const clj_summaries *s);
uint32_t clj_summaries_invalidated(const clj_summaries *s);
uint32_t clj_summaries_rounds(const clj_summaries *s);
uint32_t clj_summaries_widenings(const clj_summaries *s);

// A kind keyword (the names of clj_fact_kind_name), an aggregate (:any :int :number :coll :maps :sets :seqable :ident :assoc :indexed) or a vector of them.
bool clj_fact_of_spec(clj_value spec, clj_fact *out);

#endif
