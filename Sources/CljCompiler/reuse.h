// @ai-generated(solo)
// Drop-guided reuse over one frame, decided before emission (design §7 «Perceus и Lean 4: что взято техникой»).
#ifndef CLJC_REUSE_H
#define CLJC_REUSE_H

#include "clj/analyzer.h"
#include "clj/facts.h"

typedef enum {
	// cons: the slot's value dies once the operands are evaluated and lends the new cell its own.
	CLJC_REUSE_TOKEN = 1,
	// next/rest of the slot, dead after the call: the operand is handed over and a unique view steps in place.
	CLJC_REUSE_CONSUME = 2,
} cljc_reuse_kind;

typedef struct {
	uint32_t        node; // the INTRINSIC
	cljc_reuse_kind kind;
	uint32_t        slot;
} cljc_reuse_pair;

typedef struct cljc_reuse_plan cljc_reuse_plan;

// owned: slots holding nil or their own reference all frame long, the only ones a drop may move. NULL: no pair.
cljc_reuse_plan       *cljc_reuse_plan_frame(const clj_node *body, const clj_facts *facts, uint64_t owned, uint64_t recur_slots);
const cljc_reuse_pair *cljc_reuse_at(const cljc_reuse_plan *p, uint32_t node);
void                   cljc_reuse_plan_free(cljc_reuse_plan *p);

#endif
