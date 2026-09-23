// @ai-generated(solo)
// Function summaries of the facts lattice: pass 1 bottom-up over var roots, cached per var epoch (NOTES.md, "Facts").
#ifndef CLJ_SUMMARY_H
#define CLJ_SUMMARY_H

#include "facts.h"

// Effects as far as they fall out of the walk (design §3); an unknown callee has every bit.
enum {
	CLJ_EFFECT_ALLOC  = 1u << 0,
	CLJ_EFFECT_THROW  = 1u << 1,
	CLJ_EFFECT_IO     = 1u << 2,
	CLJ_EFFECT_ATOM   = 1u << 3, // atom-write
	CLJ_EFFECT_PARK   = 1u << 4, // a wait that suspends the coroutine: <!, >!, alts!, a future or promise deref, sleep
	CLJ_EFFECT_OPAQUE = 1u << 5, // the walk did not see the whole body: anything may happen, a park included
	// PARK stays out: the :effects ladder needs "known to park" apart from the "may park" OPAQUE already carries.
	CLJ_EFFECT_ANY = CLJ_EFFECT_ALLOC | CLJ_EFFECT_THROW | CLJ_EFFECT_IO | CLJ_EFFECT_ATOM | CLJ_EFFECT_OPAQUE,
};

// What a parameter is allowed to do: the :effects of the properties map in the parameter's :=> schema (design §4).
#define CLJ_EFFECTS_FREE UINT32_MAX // no requirement declared
// The bits a requirement can forbid: the others describe a body, no site bans an allocation.
#define CLJ_EFFECTS_CHECKED CLJ_EFFECT_PARK
typedef struct {
	uint32_t allowed; // CLJ_EFFECTS_FREE, else the effect bits the parameter may have
	uint8_t  nargs;   // fixed arguments the requirement's :cat declares: the arity a var argument is summarized at
	bool     strict;  // the effect is impossible here (an error), not merely bad practice (a lint): :effects/severity :error
} clj_effects_req;

// One arity: requirements are the meet of a parameter's uses along a path, joined across branches; unknown is TOP.
typedef struct {
	uint32_t nparams;
	bool     variadic;  // params[nparams] is the rest parameter then
	bool     inferred;  // a body was walked
	bool     annotated; // the var's :clj/facts meta contributed
	bool     recursive; // the walk reached its own summary: a fixpoint was run
	uint32_t        effects;
	clj_fact        params[CLJ_FN_MAX_FIXED + 1];
	clj_effects_req param_effects[CLJ_FN_MAX_FIXED + 1];
	uint32_t param_line[CLJ_FN_MAX_FIXED + 1], param_col[CLJ_FN_MAX_FIXED + 1]; // the use that imposed it; 0 for an annotation
	clj_fact ret;
} clj_summary;

typedef struct clj_summaries clj_summaries;

// Runtime state: every entry is cached with the epoch of each var its walk read and recomputed when one moved. Not thread-safe.
clj_summaries *clj_summaries_new(void);
void           clj_summaries_free(clj_summaries *s);
// Off, the :=> metas are ignored: what inference alone gives, for the report. On by default.
void clj_summaries_use_annotations(clj_summaries *s, bool on);
// On, a table built with this store enters a def'd fn's parameters at the caller join below. Off by default.
void clj_summaries_use_callers(clj_summaries *s, bool on);
bool clj_summaries_callers_on(const clj_summaries *s);

// NULL for an unbound var, a native without an annotation or no arity for nargs. Borrowed until the next call into the store.
const clj_summary *clj_summary_of_var(clj_summaries *s, clj_value var, uint32_t nargs);
// A direct fn's arity, keyed by node identity.
const clj_summary *clj_summary_of_arity(clj_summaries *s, const clj_node *fn, const clj_fn_arity *arity);

// The numeric domain of an argument: what a summary specialized to a call site is keyed by (NOTES.md, "Facts").
typedef enum { CLJ_DOMAIN_ANY = 0, CLJ_DOMAIN_INT64 = 1, CLJ_DOMAIN_DOUBLE = 2 } clj_domain;
#define CLJ_SUMMARY_DOMAINS_MAX 8
clj_domain clj_domain_of(clj_fact f);
clj_fact   clj_fact_of_domain(clj_domain d);
// The arity's summary with the parameters entered at the domains instead of TOP: the result and effects of the body
// over such arguments. NULL as clj_summary_of_var is, for a native, a protocol method and past the domain count.
const clj_summary *clj_summary_of_var_at(clj_summaries *s, clj_value var, uint32_t nargs, const clj_domain *domains);

// Kind and descriptor of the current root, never a singleton: the root may be rebound and only the epoch guards it. TOP when unbound.
clj_fact clj_summary_var_fact(clj_summaries *s, clj_value var);
// The var's epoch as the store last read it; UINT32_MAX when it never did.
uint32_t clj_summaries_epoch_seen(const clj_summaries *s, clj_value var);

// The declaration diagnostics: a :=> meta the body contradicts (an error) or leaves at TOP (a warning).
uint32_t              clj_summaries_ndiagnostics(const clj_summaries *s);
const clj_diagnostic *clj_summaries_diagnostic(const clj_summaries *s, uint32_t i);
uint32_t              clj_summaries_nerrors(const clj_summaries *s);

// For the report: entries held, entries recomputed after a redefinition, fixpoint rounds run, summaries widened.
uint32_t clj_summaries_count(const clj_summaries *s);
uint32_t clj_summaries_invalidated(const clj_summaries *s);
uint32_t clj_summaries_rounds(const clj_summaries *s);
uint32_t clj_summaries_widenings(const clj_summaries *s);

// ---- the reverse index: who calls a var, and with what (design §3 "Проход 2", NOTES.md "Facts": the caller join)
// Process-wide, under its own lock. A site is what one call node of a live tree passes; it is keyed by an owner (the
// exec of the tree, the report tool's root) so that the owner's death takes its sites with it, and a value read is a
// use of the var as a value (an argument, a capture, #'f), after which the var may be called from anywhere. Every
// change bumps the var's callers epoch (var.h).
void clj_callers_add_site(const void *owner, clj_value var, uint32_t nargs, const clj_fact *args);
void clj_callers_add_value_read(const void *owner, clj_value var);
void clj_callers_forget(const void *owner);

// Why a join answered TOP at every position; CLJ_JOIN_TOP_ARG is per position and the others per var.
typedef enum { CLJ_JOIN_OK, CLJ_JOIN_NO_SITES, CLJ_JOIN_TOP_ARG, CLJ_JOIN_FIRST_CLASS, CLJ_JOIN_DYNAMIC } clj_join_reason;

// The join, per fixed parameter of the arity, of what every recorded site that resolves to it passes: TOP at a position
// some site passes TOP at, everywhere when no site is recorded, the var is read as a value or is dynamic. Kinds and
// nullability only: no singleton (it would borrow a caller's constant) and no descriptor.
clj_join_reason clj_callers_join(clj_value var, const clj_node *fn, const clj_fn_arity *arity, clj_fact *out);
// Sites and value reads recorded for the var, for tests and the report.
uint32_t clj_callers_nsites(clj_value var);
uint32_t clj_callers_nvalue_reads(clj_value var);
// How often each reason answered a join asked so far, indexed by clj_join_reason; joins counts the asks.
void clj_callers_stats(uint64_t counts[5]);

// The abstract interpretation of a schema of the design §3 vocabulary; false when a tag it does not know made a part TOP.
bool clj_fact_of_schema(clj_value schema, clj_fact *out);
// The total embedding back: owned data; a kind the vocabulary has no tag for comes out under its own name.
clj_value clj_fact_to_schema(clj_fact f);

#endif
