// @ai-generated(solo)
// Owned parameters (design §7 «Владеющие параметры»), inferred bottom-up over the call graph.
#ifndef CLJC_OWNED_H
#define CLJC_OWNED_H

#include "clj/analyzer.h"
#include "clj/facts.h"
#include "reuse.h"

typedef struct cljc_owned cljc_owned;

cljc_owned *cljc_owned_new(void);
void        cljc_owned_free(cljc_owned *o);

// order: the load order; a call emitted before its callee's def is not direct, so it hands nothing over.
void cljc_owned_declare(cljc_owned *o, const clj_node *root, uint32_t order);
// The local consumption and the call edges of the def's fixed arities, over the table the emitter reads too.
void cljc_owned_scan(cljc_owned *o, const clj_node *root, const clj_facts *facts);
// Closes the masks over the edges: a parameter handed at its death to an owned parameter is owned.
void cljc_owned_settle(cljc_owned *o);

// The owned parameters of var's fixed arity, a bit each; 0 for a var the set does not define once.
uint64_t cljc_owned_mask(const cljc_owned *o, clj_value var, uint32_t arity);
// The parameters of a fixed arity of a def'd fn that can be owned slots: what both the scan and the emitter test.
uint64_t cljc_owned_candidates(const clj_node *fn, const clj_fn_arity *a, const clj_facts *facts);

// The parameters among `offer` of a literal fn's fixed arity that its body consumes, as the scan counts it, a hand-over
// to a settled owned parameter included (positions: the emitter's). For a lowered call's step, whose driver offers
// its own reference; a fn-body recur's parameters are left out, since a typed slot cannot arrive owned.
uint64_t cljc_owned_literal(const clj_node *fn, const clj_fn_arity *a, const clj_facts *facts, uint64_t offer, cljc_owned_positions positions, void *ctx);

typedef struct {
	uint64_t arities;   // fixed arities with an owned parameter
	uint64_t params;    // their owned parameters
	uint64_t returned;  // of those, consumed by a last-use read: returned, bound, recurred, a consuming intrinsic's own
	uint64_t paired;    // consumed by a reuse pair: a cons token, a next/rest step, a late take
	uint64_t handed;    // owned only through a call that hands them on
	uint64_t rounds;    // passes of the fixpoint
} cljc_owned_stats;

void cljc_owned_stats_of(const cljc_owned *o, cljc_owned_stats *out);

#endif
