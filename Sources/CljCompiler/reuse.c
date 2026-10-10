// @ai-generated(solo)
#include "reuse.h"

#include <stdlib.h>
#include <string.h>

// optimizer.c's liveness again: its marks name only reads a consumer owns, a pair needs the set after any node.

struct cljc_reuse_plan {
	cljc_reuse_pair *pairs;
	size_t           n, cap;
};

typedef struct {
	uint64_t slots;   // rebound by a recur of this target
	uint64_t body_in; // live at the target body's entry, from the fixpoint
} target;

typedef struct {
	uint32_t node, end; // the cons and one past its subtree
	uint64_t safe;      // owned, dead after it, borrowed by no pending operand and not its own operand
	uint64_t seqops;    // read inside it as the operand of first, next, rest or seq
	uint64_t reads;     // read inside it, maybe a seq by the facts
} cons_site;

typedef struct {
	uint32_t node, slot;
	bool     dropped; // the slot is left to a cons that takes its cell
} consume_site;

typedef struct {
	uint64_t         held; // borrowed by a pending operand or pinned by a direct fn body: never dropped early
	target          *recur;
	bool             mark;
	uint64_t         owned;
	const clj_facts *facts;
	cons_site       *conses;
	size_t           nconses, cconses;
	consume_site    *consumes;
	size_t           nconsumes, cconsumes;
} ctx;

static uint64_t slot_bit(uint32_t slot) { return slot < 64 ? (uint64_t)1 << slot : 0; }

static void *grow(void *items, size_t *cap, size_t n, size_t size) {
	if (n < *cap) return items;
	*cap = *cap ? *cap * 2 : 8;
	items = realloc(items, *cap * size);
	if (!items) clj_fatal("out of memory");
	return items;
}

static void plan_add(cljc_reuse_plan *p, uint32_t node, cljc_reuse_kind kind, uint32_t slot) {
	p->pairs = grow(p->pairs, &p->cap, p->n, sizeof *p->pairs);
	p->pairs[p->n++] = (cljc_reuse_pair){node, kind, slot};
}

static bool op_named(const clj_node *n, const char *name, uint32_t arity) {
	return n->u.intrinsic.op->arity == arity && strcmp(n->u.intrinsic.op->name, name) == 0;
}

static bool seq_op(const clj_node *n) {
	return op_named(n, "clojure.core/first", 1) || op_named(n, "clojure.core/next", 1) || op_named(n, "clojure.core/rest", 1) ||
	       op_named(n, "clojure.core/seq", 1);
}

typedef struct {
	const clj_facts *facts;
	uint64_t         reads, seqops;
} read_scan;

static bool kept_read(const clj_node *n) { return n->kind == CLJ_NODE_LOCAL && !n->u.local.last; }

// A last read moves the value out of its slot (clj_c_take), so it leaves nothing to drop. Captures and direct fn
// bodies read another frame's code: their slots are never owned.
static void read_walk(const clj_node *n, void *arg) {
	read_scan *r = arg;
	switch (n->kind) {
	case CLJ_NODE_LOCAL: {
		const clj_fact *f = r->facts ? clj_facts_node(r->facts, n->id) : NULL;
		if (kept_read(n) && f && (f->types & CLJ_T_LIST)) r->reads |= slot_bit(n->u.local.index);
		return;
	}
	case CLJ_NODE_INTRINSIC:
		if (seq_op(n) && kept_read(n->u.intrinsic.args[0])) r->seqops |= slot_bit(n->u.intrinsic.args[0]->u.local.index);
		clj_node_children(n, read_walk, arg);
		return;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) read_walk(n->u.fused.args[i], arg);
		return;
	default: clj_node_children(n, read_walk, arg);
	}
}

// A direct operand of the cons is borrowed until the cell is made, so it never lends the cell.
static void note_cons(ctx *c, const clj_node *n, uint64_t out) {
	read_scan r = {c->facts, 0, 0};
	uint64_t  operands = 0;
	for (uint32_t i = 0; i < n->u.intrinsic.n; i++) {
		const clj_node *a = n->u.intrinsic.args[i];
		if (a->kind == CLJ_NODE_LOCAL) operands |= slot_bit(a->u.local.index);
		else read_walk(a, &r);
	}
	uint64_t safe = c->owned & ~out & ~c->held & ~operands;
	if (!safe) return;
	c->conses = grow(c->conses, &c->cconses, c->nconses, sizeof *c->conses);
	c->conses[c->nconses++] = (cons_site){n->id, n->id + n->nnodes, safe, r.seqops, r.reads};
}

static void note_consume(ctx *c, const clj_node *n, uint64_t out) {
	const clj_node *a = n->u.intrinsic.args[0];
	if (a->kind != CLJ_NODE_LOCAL || !(slot_bit(a->u.local.index) & c->owned & ~out & ~c->held)) return;
	c->consumes = grow(c->consumes, &c->cconsumes, c->nconsumes, sizeof *c->consumes);
	c->consumes[c->nconsumes++] = (consume_site){n->id, a->u.local.index, false};
}

// A dying cons cell is worth more to a cons than to a next or rest before it: built in place it keeps the fields it
// holds again, where the step only frees it. A view (vector-seq, range) breaks even either way.
static void decide(ctx *c, cljc_reuse_plan *p) {
	for (size_t i = 0; i < c->nconses; i++) {
		const cons_site *k = &c->conses[i];
		uint64_t         stepped = 0;
		for (size_t j = 0; j < c->nconsumes; j++) {
			if (c->consumes[j].node < k->end) stepped |= slot_bit(c->consumes[j].slot);
		}
		uint64_t pick = k->safe & k->seqops;
		if (!pick) pick = k->safe & stepped;
		if (!pick) pick = k->safe & k->reads;
		if (!pick) continue;
		uint32_t slot = (uint32_t)__builtin_ctzll(pick);
		plan_add(p, k->node, CLJC_REUSE_TOKEN, slot);
		for (size_t j = 0; j < c->nconsumes; j++) {
			if (c->consumes[j].node < k->end && c->consumes[j].slot == slot) c->consumes[j].dropped = true;
		}
	}
	for (size_t j = 0; j < c->nconsumes; j++) {
		if (!c->consumes[j].dropped) plan_add(p, c->consumes[j].node, CLJC_REUSE_CONSUME, c->consumes[j].slot);
	}
}

static uint64_t live(const clj_node *n, uint64_t out, ctx *c);

static uint64_t live_seq(const clj_node *const *nodes, uint32_t n, uint64_t out, ctx *c) {
	while (n-- > 0) out = live(nodes[n], out, c);
	return out;
}

// A direct local operand is read at +0 and used only once every later operand has run: held until the call.
static uint64_t live_args(const clj_node *head, const clj_node *const *nodes, uint32_t n, uint64_t out, ctx *c) {
	uint64_t  small[16];
	uint64_t *before = small;
	if (n > 16) {
		before = calloc(n, sizeof *before);
		if (!before) clj_fatal("out of memory");
	}
	uint64_t borrowed = head && head->kind == CLJ_NODE_LOCAL ? slot_bit(head->u.local.index) : 0, held = c->held;
	for (uint32_t i = 0; i < n; i++) {
		before[i] = borrowed;
		if (nodes[i]->kind == CLJ_NODE_LOCAL) borrowed |= slot_bit(nodes[i]->u.local.index);
	}
	for (uint32_t i = n; i-- > 0;) {
		c->held = held | before[i];
		out = live(nodes[i], out, c);
	}
	c->held = held;
	if (before != small) free(before);
	return head ? live(head, out, c) : out;
}

static uint64_t live_fixpoint(const clj_node *body, uint64_t out, target *t, ctx *c) {
	target *saved = c->recur;
	bool    mark = c->mark;
	c->recur = t;
	c->mark = false;
	for (;;) {
		uint64_t in = live(body, out, c);
		if (in == t->body_in) break;
		t->body_in = in;
	}
	c->mark = mark;
	uint64_t in = mark ? live(body, out, c) : t->body_in;
	c->recur = saved;
	return in;
}

static uint64_t live_bindings(const clj_node *n, uint64_t in, ctx *c) {
	for (uint32_t i = n->u.let.n; i-- > 0;) {
		in &= ~slot_bit(n->u.let.slots[i]);
		in = live(n->u.let.inits[i], in, c);
	}
	return in;
}

static uint64_t live(const clj_node *n, uint64_t out, ctx *c) {
	switch (n->kind) {
	case CLJ_NODE_LOCAL: return out | slot_bit(n->u.local.index);
	case CLJ_NODE_IF: {
		uint64_t then = live(n->u.if_.then, out, c);
		uint64_t else_ = n->u.if_.else_ ? live(n->u.if_.else_, out, c) : out;
		return live(n->u.if_.test, then | else_, c);
	}
	case CLJ_NODE_DO: return live_seq(n->u.seq.items, n->u.seq.n, out, c);
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET: return live_args(NULL, n->u.seq.items, n->u.seq.n, out, c);
	case CLJ_NODE_LET: return live_bindings(n, live(n->u.let.body, out, c), c);
	case CLJ_NODE_LOOP: {
		target t = {0, 0};
		for (uint32_t i = 0; i < n->u.let.n; i++) t.slots |= slot_bit(n->u.let.slots[i]);
		return live_bindings(n, live_fixpoint(n->u.let.body, out, &t, c), c);
	}
	case CLJ_NODE_RECUR: return live_seq(n->u.recur.args, n->u.recur.n, c->recur->body_in & ~c->recur->slots, c);
	case CLJ_NODE_INVOKE: return live_args(n->u.invoke.fn, n->u.invoke.args, n->u.invoke.n, out, c);
	case CLJ_NODE_OBJC_SEND: return live_args(n->u.objc.target, n->u.objc.args, n->u.objc.n, out, c);
	case CLJ_NODE_INTRINSIC:
		if (c->mark && op_named(n, "clojure.core/cons", 2)) note_cons(c, n, out);
		else if (c->mark && (op_named(n, "clojure.core/next", 1) || op_named(n, "clojure.core/rest", 1))) note_consume(c, n, out);
		return live_args(NULL, n->u.intrinsic.args, n->u.intrinsic.n, out, c);
	case CLJ_NODE_DIRECT_CALL: return live_args(NULL, n->u.direct.args, n->u.direct.n, out, c);
	case CLJ_NODE_FUSED: return live_args(NULL, n->u.fused.args, n->u.fused.nargs, out, c);
	case CLJ_NODE_DEF: {
		out = live(n->u.def.meta, out, c);
		return n->u.def.init ? live(n->u.def.init, out, c) : out;
	}
	case CLJ_NODE_THROW: return live(n->u.throw_, out, c);
	case CLJ_NODE_TRY: {
		// any point of the body may throw: what the handlers and finally read is live all through it
		uint64_t after = n->u.try_.finally_ ? live(n->u.try_.finally_, out, c) : out;
		uint64_t handlers = 0;
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
			const clj_catch *k = &n->u.try_.catches[i];
			handlers |= live(k->handler, after, c) & ~slot_bit(k->slot);
		}
		return live(n->u.try_.body, after | handlers, c);
	}
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			if (n->u.fn.captures[i].kind == CLJ_CAPTURE_LOCAL) out |= slot_bit(n->u.fn.captures[i].index);
		}
		return out;
	case CLJ_NODE_CONST:
	case CLJ_NODE_CAPTURED:
	case CLJ_NODE_OUTER:
	case CLJ_NODE_VAR:
	case CLJ_NODE_DIRECT_FN: return out;
	}
	clj_fatal("compiler: unknown node kind");
}

typedef struct {
	uint32_t  depth;
	uint64_t *pinned;
} pin_ctx;

// Slots a direct fn body below reads through the static link whenever it is called.
static void pin_scan(const clj_node *n, void *arg) {
	pin_ctx *p = arg;
	switch (n->kind) {
	case CLJ_NODE_OUTER:
		if (n->u.outer.depth == p->depth) *p->pinned |= slot_bit(n->u.outer.index);
		return;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cap = &n->u.fn.captures[i];
			if (cap->kind == CLJ_CAPTURE_OUTER && cap->depth == p->depth) *p->pinned |= slot_bit(cap->index);
		}
		return;
	case CLJ_NODE_DIRECT_FN: {
		pin_ctx inner = {p->depth + 1, p->pinned};
		clj_node_children(n, pin_scan, &inner);
		return;
	}
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) pin_scan(n->u.fused.args[i], arg);
		return;
	default: clj_node_children(n, pin_scan, arg);
	}
}

cljc_reuse_plan *cljc_reuse_plan_frame(const clj_node *body, const clj_facts *facts, uint64_t owned, uint64_t recur_slots) {
	if (!owned || !facts) return NULL;
	uint64_t pinned = 0;
	pin_ctx  p = {0, &pinned};
	pin_scan(body, &p);
	target self = {recur_slots, 0};
	ctx    c = {.held = pinned, .recur = &self, .mark = true, .owned = owned & ~pinned, .facts = facts};
	live_fixpoint(body, 0, &self, &c);
	cljc_reuse_plan *plan = calloc(1, sizeof *plan);
	if (!plan) clj_fatal("out of memory");
	decide(&c, plan);
	free(c.conses);
	free(c.consumes);
	if (plan->n) return plan;
	free(plan);
	return NULL;
}

const cljc_reuse_pair *cljc_reuse_at(const cljc_reuse_plan *p, uint32_t node) {
	for (size_t i = 0; p && i < p->n; i++) {
		if (p->pairs[i].node == node) return &p->pairs[i];
	}
	return NULL;
}

void cljc_reuse_plan_free(cljc_reuse_plan *p) {
	if (!p) return;
	free(p->pairs);
	free(p);
}
