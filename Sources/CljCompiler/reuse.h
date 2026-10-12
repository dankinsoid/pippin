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
	// conj/assoc/dissoc/with-meta of the slot, which is read again by a later operand and dead once the call runs:
	// the operand is read borrowed and the slot taken at the call, so a unique collection is updated in place.
	CLJC_REUSE_TAKE = 3,
	// the slot as argument `arg` of a call to an owned parameter, dead once the call runs: taken at the call.
	CLJC_REUSE_HAND = 4,
	// (get s k) whose value an (assoc s k v) puts back, s read by nothing else in between: in the value operand of an
	// assoc that TAKEs s, or in a let's init with the assoc reading s last in its body. A unique child leaves its
	// unique parent at +1 (clj_get_take).
	CLJC_REUSE_TAKEOUT = 5,
} cljc_reuse_kind;

typedef struct {
	uint32_t        node; // the INTRINSIC, or the INVOKE of a HAND
	cljc_reuse_kind kind;
	uint32_t        slot;
	uint32_t        arg; // the argument position of a HAND, the assoc's node of a TAKEOUT
	// A TAKEOUT's: the node around the get and the assoc whose emission declares the flag they share (the assoc
	// itself or the let), and the assoc.
	uint32_t        decl;
	const clj_node *put;
} cljc_reuse_pair;

typedef struct cljc_reuse_plan cljc_reuse_plan;

// The argument positions of an INVOKE whose callee takes them owned (owned.c), a bit each; 0 for none.
typedef uint64_t (*cljc_owned_positions)(void *ctx, const clj_node *invoke);

// owned: slots holding nil or their own reference all frame long, the only ones a drop may move. positions: NULL
// for no HAND pair; takes: whether TAKE pairs are planned. NULL: no pair.
cljc_reuse_plan       *cljc_reuse_plan_frame(const clj_node *body, const clj_facts *facts, uint64_t owned, uint64_t recur_slots, bool takes,
                                             cljc_owned_positions positions, void *ctx);
// The TOKEN, CONSUME, TAKE or TAKEOUT pair at an INTRINSIC.
const cljc_reuse_pair *cljc_reuse_at(const cljc_reuse_plan *p, uint32_t node);
// The TAKEOUT pairs whose flag the node declares, one a call: *from starts at 0.
const cljc_reuse_pair *cljc_reuse_takeout_declared(const cljc_reuse_plan *p, uint32_t node, size_t *from);
// The HAND pair of an INVOKE's argument.
const cljc_reuse_pair *cljc_reuse_hand_at(const cljc_reuse_plan *p, uint32_t node, uint32_t arg);
size_t                 cljc_reuse_count(const cljc_reuse_plan *p);
const cljc_reuse_pair *cljc_reuse_pair_at(const cljc_reuse_plan *p, size_t i);
void                   cljc_reuse_plan_free(cljc_reuse_plan *p);

#endif
