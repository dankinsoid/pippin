// @ai-generated(solo)
#include "reuse.h"

#include <stdlib.h>
#include <string.h>

#include "clj/ice.h"
#include "clj/intrinsics.h"
#include "clj/string.h"

// optimizer.c's liveness again: its marks name only reads a consumer owns, a pair needs the set after any node.

typedef struct {
	uint32_t node, at;
	uint64_t slots;
} drop_site;

struct cljc_reuse_plan {
	cljc_reuse_pair *pairs;
	size_t           n, cap;
	drop_site       *drops; // by node, then at
	size_t           ndrops;
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

// A TAKE or HAND pair: the slot leaves the frame at the call, so no cons around it may build in its cell.
typedef struct {
	uint32_t        node, slot, arg;
	cljc_reuse_kind kind;
} take_site;

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
	take_site       *takes;
	size_t           ntakes, ctakes;
	cljc_reuse_pair *takeouts;
	size_t           ntakeouts, ctakeouts;
	bool             late;      // TAKE pairs
	cljc_owned_positions positions; // HAND pairs, NULL for none
	void                *pctx;
	bool       drops;
	uint64_t   droppable; // read somewhere as a value clj_c_drop may let go, by the facts
	bool       tail;  // only the frame's exit runs after the node
	uint64_t   loose; // died under the node at a borrowing read, and no point inside it dropped them
	drop_site *dropv;
	size_t     ndropv, cdropv;
} ctx;

static uint64_t slot_bit(uint32_t slot) { return slot < 64 ? (uint64_t)1 << slot : 0; }

static uint64_t local_bit(const clj_node *n) { return n->kind == CLJ_NODE_LOCAL ? slot_bit(n->u.local.index) : 0; }

static void *grow(void *items, size_t *cap, size_t n, size_t size) {
	if (n < *cap) return items;
	*cap = *cap ? *cap * 2 : 8;
	items = realloc(items, *cap * size);
	if (!items) clj_fatal("out of memory");
	return items;
}

static void plan_add(cljc_reuse_plan *p, uint32_t node, cljc_reuse_kind kind, uint32_t slot, uint32_t arg) {
	p->pairs = grow(p->pairs, &p->cap, p->n, sizeof *p->pairs);
	p->pairs[p->n++] = (cljc_reuse_pair){node, kind, slot, arg, 0, NULL};
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

// The slots read directly as an operand other than `skip`: borrowed aliases the call receives beside the taken one,
// which an update in place would change under them ((assoc m m 1) must not store m in itself).
static uint64_t other_operands(const clj_node *const *args, uint32_t n, uint32_t skip) {
	uint64_t b = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (i != skip) b |= local_bit(args[i]);
	}
	return b;
}

static void take_add(ctx *c, uint32_t node, uint32_t slot, uint32_t arg, cljc_reuse_kind kind) {
	c->takes = grow(c->takes, &c->ctakes, c->ntakes, sizeof *c->takes);
	c->takes[c->ntakes++] = (take_site){node, slot, arg, kind};
}

typedef struct {
	uint32_t slot, reads;
} slot_reads;

// Every read of the frame's slot under n, a capture of it included. A direct fn's body reads it only through the
// static link, which pins the slot (no TAKE at all); a fused program's subtrees may over-count, never under.
static void count_reads(const clj_node *n, void *arg) {
	slot_reads *r = arg;
	switch (n->kind) {
	case CLJ_NODE_LOCAL: r->reads += n->u.local.index == r->slot; return;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) r->reads += n->u.fn.captures[i].kind == CLJ_CAPTURE_LOCAL && n->u.fn.captures[i].index == r->slot;
		return;
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_LOWERED: count_reads(n->u.lowered.fn, arg); break;
	default: break;
	}
	clj_node_children(n, count_reads, arg);
}

typedef struct {
	uint32_t        slot;
	const clj_node *g;
} once_scan;

static const clj_node *once_get(const clj_node *n, uint32_t slot);
static const clj_node *once_assoc(const clj_node *n);

static void once_child(const clj_node *n, void *arg) {
	once_scan *o = arg;
	if (!o->g) o->g = once_get(n, o->slot);
}

static void once_assoc_child(const clj_node *n, void *arg) {
	once_scan *o = arg;
	if (!o->g) o->g = once_assoc(n);
}

// The get of the slot that runs at most once per evaluation of n: not in a loop's body, a fn or a fused program.
static const clj_node *once_get(const clj_node *n, uint32_t slot) {
	const clj_node *g = NULL;
	switch (n->kind) {
	case CLJ_NODE_INTRINSIC:
		if (clj_intrinsic_is_get(n->u.intrinsic.op) && n->u.intrinsic.args[0]->kind == CLJ_NODE_LOCAL && n->u.intrinsic.args[0]->u.local.index == slot)
			return n;
		for (uint32_t i = 0; !g && i < n->u.intrinsic.n; i++) g = once_get(n->u.intrinsic.args[i], slot);
		return g;
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; !g && i < n->u.let.n; i++) g = once_get(n->u.let.inits[i], slot);
		return g;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN:
	case CLJ_NODE_FUSED: return NULL;
	case CLJ_NODE_LOWERED:
		for (uint32_t i = 0; !g && i < n->u.lowered.n; i++) g = once_get(n->u.lowered.args[i], slot);
		return g;
	default: {
		once_scan o = {slot, NULL};
		clj_node_children(n, once_child, &o);
		return o.g;
	}
	}
}

typedef struct {
	uint32_t slot;
	bool     bound;
} bind_scan;

// Whether n binds the frame's slot: a let or loop, or a catch.
static void binds_slot(const clj_node *n, void *arg) {
	bind_scan *b = arg;
	switch (n->kind) {
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n; i++) b->bound |= n->u.let.slots[i] == b->slot;
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) b->bound |= n->u.try_.catches[i].slot == b->slot;
		break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return;
	default: break;
	}
	clj_node_children(n, binds_slot, arg);
}

// The assoc's key and the get's are one key: equal constants, one capture, or one local nothing in v rebinds.
static bool same_key(const clj_node *a, const clj_node *b, const clj_node *v) {
	if (a->kind != b->kind) return false;
	switch (a->kind) {
	case CLJ_NODE_CONST:
		// a constant may be any reader value; strings are the equal literals that need not be one object
		return a->u.value.v == b->u.value.v || (clj_is_string(a->u.value.v) && clj_is_string(b->u.value.v) && clj_equals(a->u.value.v, b->u.value.v));
	case CLJ_NODE_CAPTURED: return a->u.index == b->u.index;
	case CLJ_NODE_LOCAL: {
		if (a->u.local.index != b->u.local.index) return false;
		bind_scan s = {a->u.local.index, false};
		binds_slot(v, &s);
		return !s.bound;
	}
	default: return false;
	}
}

// A get and the assoc that puts its value back. Nothing but the get reads s from the get on: the assoc's own read of s
// is held from before the get (a TAKE) or comes after it (a last use), so the entry the get empties is seen by no one
// until the assoc fills it again; s is dead past the assoc and the handlers read it nowhere (liveness), so a throw on
// the way, or a branch that skips the assoc, leaves the parent to a release.
static void takeout_add(ctx *c, const clj_node *get, const clj_node *assoc, const clj_node *decl, const clj_node *scope) {
	uint32_t slot = get->u.intrinsic.args[0]->u.local.index;
	if (get->u.intrinsic.args[0]->u.local.last || !same_key(assoc->u.intrinsic.args[1], get->u.intrinsic.args[1], scope)) return;
	bind_scan b = {slot, false};
	binds_slot(scope, &b);
	if (b.bound) return;
	c->takeouts = grow(c->takeouts, &c->ctakeouts, c->ntakeouts, sizeof *c->takeouts);
	c->takeouts[c->ntakeouts++] = (cljc_reuse_pair){get->id, CLJC_REUSE_TAKEOUT, slot, assoc->id, decl->id, assoc};
}

static bool is_assoc3(const clj_node *n) {
	return n->kind == CLJ_NODE_INTRINSIC && n->u.intrinsic.op->arity == 3 && strcmp(n->u.intrinsic.op->name, "clojure.core/assoc") == 0;
}

// (assoc s k (… (get s k) …)) whose s the assoc TAKEs: the get is s's one read in the value.
static void note_takeout(ctx *c, const clj_node *assoc, uint32_t slot) {
	if (!is_assoc3(assoc)) return;
	const clj_node *v = assoc->u.intrinsic.args[2];
	slot_reads      r = {slot, 0};
	count_reads(v, &r);
	const clj_node *g = r.reads == 1 ? once_get(v, slot) : NULL;
	if (g) takeout_add(c, g, assoc, assoc, v);
}

// The assoc of a local read last, which the let's body runs at most once: (assoc! m k (f (get m k))) inlined.
static const clj_node *once_assoc(const clj_node *n) {
	if (is_assoc3(n) && n->u.intrinsic.args[0]->kind == CLJ_NODE_LOCAL && n->u.intrinsic.args[0]->u.local.last) return n;
	switch (n->kind) {
	case CLJ_NODE_LET:
	case CLJ_NODE_DO:
	case CLJ_NODE_IF:
	case CLJ_NODE_INLINE:
	case CLJ_NODE_INTRINSIC: {
		once_scan o = {0, NULL};
		clj_node_children(n, once_assoc_child, &o);
		return o.g;
	}
	default: return NULL;
	}
}

// (let [… (get s k) …] … (assoc s k v) …) with the assoc's read of s its last: the get is in an init, so it runs
// first, and the two are s's only reads in the let.
static void note_takeout_let(ctx *c, const clj_node *let, uint64_t out) {
	const clj_node *a = once_assoc(let->u.let.body);
	if (!a) return;
	uint32_t slot = a->u.intrinsic.args[0]->u.local.index;
	if (!(slot_bit(slot) & c->owned & ~out & ~c->held)) return;
	slot_reads r = {slot, 0};
	count_reads(let, &r);
	if (r.reads != 2) return;
	const clj_node *g = NULL;
	for (uint32_t i = 0; !g && i < let->u.let.n; i++) g = once_get(let->u.let.inits[i], slot);
	if (g) takeout_add(c, g, a, let, let);
}

// A last-use read already hands the slot over where it is read (the optimizer's mark); this is the read that is not
// the last one only because a later operand reads the slot too.
static void note_take(ctx *c, const clj_node *n, uint64_t out) {
	const clj_node *a = n->u.intrinsic.args[0];
	if (a->kind != CLJ_NODE_LOCAL || a->u.local.last) return;
	uint64_t b = slot_bit(a->u.local.index);
	if (!(b & c->owned & ~out & ~c->held & ~other_operands(n->u.intrinsic.args, n->u.intrinsic.n, 0))) return;
	take_add(c, n->id, a->u.local.index, 0, CLJC_REUSE_TAKE);
	note_takeout(c, n, a->u.local.index);
}


// before: the direct operands read before each argument (live_args), the head among them.
static void note_hands(ctx *c, const clj_node *n, uint64_t out, const uint64_t *before) {
	uint64_t pos = c->positions(c->pctx, n);
	for (uint32_t i = 0; pos && i < n->u.invoke.n && i < 64; i++) {
		const clj_node *a = n->u.invoke.args[i];
		if (!((pos >> i) & 1) || a->kind != CLJ_NODE_LOCAL) continue;
		uint64_t b = slot_bit(a->u.local.index);
		if (!(b & c->owned & ~out & ~c->held & ~before[i] & ~other_operands(n->u.invoke.args, n->u.invoke.n, i))) continue;
		take_add(c, n->id, a->u.local.index, i, CLJC_REUSE_HAND);
	}
}

// A dying cons cell is worth more to a cons than to a next or rest before it: built in place it keeps the fields it
// holds again, where the step only frees it. A view (vector-seq, range) breaks even either way.
static void decide(ctx *c, cljc_reuse_plan *p) {
	for (size_t i = 0; i < c->nconses; i++) {
		const cons_site *k = &c->conses[i];
		uint64_t         stepped = 0, gone = 0;
		for (size_t j = 0; j < c->nconsumes; j++) {
			if (c->consumes[j].node < k->end) stepped |= slot_bit(c->consumes[j].slot);
		}
		// a value taken by a call inside the operands is the callee's: in place there, it lends no cell here
		for (size_t j = 0; j < c->ntakes; j++) {
			if (c->takes[j].node >= k->node && c->takes[j].node < k->end) gone |= slot_bit(c->takes[j].slot);
		}
		uint64_t pick = k->safe & ~gone & k->seqops;
		if (!pick) pick = k->safe & ~gone & stepped;
		if (!pick) pick = k->safe & ~gone & k->reads;
		if (!pick) continue;
		uint32_t slot = (uint32_t)__builtin_ctzll(pick);
		plan_add(p, k->node, CLJC_REUSE_TOKEN, slot, 0);
		for (size_t j = 0; j < c->nconsumes; j++) {
			if (c->consumes[j].node < k->end && c->consumes[j].slot == slot) c->consumes[j].dropped = true;
		}
	}
	for (size_t j = 0; j < c->nconsumes; j++) {
		if (!c->consumes[j].dropped) plan_add(p, c->consumes[j].node, CLJC_REUSE_CONSUME, c->consumes[j].slot, 0);
	}
	for (size_t j = 0; j < c->ntakes; j++) plan_add(p, c->takes[j].node, c->takes[j].kind, c->takes[j].slot, c->takes[j].arg);
	for (size_t j = 0; j < c->ntakeouts; j++) {
		bool seen = false;
		for (size_t k = 0; k < j; k++) seen = seen || c->takeouts[k].node == c->takeouts[j].node;
		if (seen) continue;
		p->pairs = grow(p->pairs, &p->cap, p->n, sizeof *p->pairs);
		p->pairs[p->n++] = c->takeouts[j];
	}
}

static uint64_t live(const clj_node *n, uint64_t out, ctx *c);
static uint64_t live_tail(const clj_node *n, uint64_t out, ctx *c, bool tail);

static bool dropping(const ctx *c) { return c->mark && c->drops; }

// What clj_c_drop may let go: a core collection or seq; HOST stands for every kind the facts do not name.
enum { DROPPABLE = CLJ_T_LIST | CLJ_T_VECTOR | CLJ_T_MAP | CLJ_T_SET | CLJ_T_SORTED_MAP | CLJ_T_SORTED_SET | CLJ_T_HOST };

// A read after which the slot is dead and which leaves the value in it; read: its LOCAL, NULL for a capture.
static void note_death(ctx *c, uint32_t slot, uint64_t out, const clj_node *read) {
	uint64_t b = slot_bit(slot);
	if (!dropping(c) || (out & b)) return;
	const clj_fact *f = read ? clj_facts_node(c->facts, read->id) : NULL;
	if (f && !(f->types & DROPPABLE)) return;
	c->loose |= b & c->owned;
}

// A pending operand's read of a slot lasts until its call: such a slot is left to a point after the call.
static uint64_t drop_at(ctx *c, const clj_node *n, uint32_t at, uint64_t dying) {
	uint64_t d = dying & c->owned & c->droppable & ~c->held;
	if (d) {
		c->dropv = grow(c->dropv, &c->cdropv, c->ndropv, sizeof *c->dropv);
		c->dropv[c->ndropv++] = (drop_site){n->id, at, d};
	}
	return dying & c->owned & c->droppable & ~d;
}

// A slot a call takes (a late take, a hand-over, a consumed next or rest) leaves nothing to drop.
static void taken_since(ctx *c, size_t takes, size_t consumes) {
	for (size_t i = takes; i < c->ntakes; i++) c->loose &= ~slot_bit(c->takes[i].slot);
	for (size_t i = consumes; i < c->nconsumes; i++) c->loose &= ~slot_bit(c->consumes[i].slot);
}

static uint64_t live_seq(const clj_node *const *nodes, uint32_t n, uint64_t out, ctx *c) {
	while (n-- > 0) out = live(nodes[n], out, c);
	return out;
}

// A direct local operand is read at +0 and used only once every later operand has run: held until the call. call:
// the INVOKE whose owned arguments may be handed over, NULL otherwise.
static uint64_t live_args(const clj_node *head, const clj_node *const *nodes, uint32_t n, uint64_t out, ctx *c, const clj_node *call) {
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
	if (call && c->mark && c->positions) note_hands(c, call, out, before);
	for (uint32_t i = n; i-- > 0;) {
		c->held = held | before[i];
		out = live(nodes[i], out, c);
	}
	c->held = held;
	if (before != small) free(before);
	return head ? live(head, out, c) : out;
}

static uint64_t live_fixpoint(const clj_node *body, uint64_t out, target *t, ctx *c, bool tail) {
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
	uint64_t in = mark ? live_tail(body, out, c, tail) : t->body_in;
	c->recur = saved;
	return in;
}

// dying: what died in the body and its end left. A binding's own slot that nothing after it reads dies at its store.
static uint64_t live_bindings(const clj_node *n, uint64_t in, ctx *c, uint64_t dying) {
	for (uint32_t i = n->u.let.n; i-- > 0;) {
		uint64_t b = slot_bit(n->u.let.slots[i]);
		const clj_node  *init = n->u.let.inits[i];
		const clj_fact  *f = clj_facts_node(c->facts, init->id);
		// a direct fn's slot holds nil: its arities are reached by name
		uint64_t self = dropping(c) && !(in & b) && init->kind != CLJ_NODE_DIRECT_FN && (!f || (f->types & DROPPABLE)) ? b & c->owned : 0;
		in &= ~b;
		uint64_t loose = c->loose;
		c->loose = 0;
		in = live(n->u.let.inits[i], in, c);
		uint64_t died = c->loose | self;
		c->loose = loose;
		dying |= dropping(c) ? drop_at(c, n, i, died) : died;
	}
	c->loose |= dying;
	return in;
}

static uint64_t live_tail(const clj_node *n, uint64_t out, ctx *c, bool tail) {
	switch (n->kind) {
	case CLJ_NODE_LOCAL:
		if (!n->u.local.last) note_death(c, n->u.local.index, out, n);
		return out | slot_bit(n->u.local.index);
	case CLJ_NODE_IF: {
		uint64_t loose = c->loose;
		c->loose = 0;
		uint64_t then = live_tail(n->u.if_.then, out, c, tail), lthen = c->loose;
		c->loose = 0;
		uint64_t else_ = n->u.if_.else_ ? live_tail(n->u.if_.else_, out, c, tail) : out, lelse = c->loose;
		c->loose = 0;
		uint64_t in = live(n->u.if_.test, then | else_, c), ltest = c->loose;
		// a branch drops what died in the test and what only the other branch reads
		if (dropping(c)) {
			lthen |= drop_at(c, n, 0, (else_ & ~then) | ltest);
			lelse |= drop_at(c, n, 1, (then & ~else_) | ltest);
		}
		c->loose = loose | lthen | lelse;
		return in;
	}
	case CLJ_NODE_DO: {
		if (n->u.seq.n == 0) return out;
		uint32_t last = n->u.seq.n - 1;
		out = live_tail(n->u.seq.items[last], out, c, tail);
		for (uint32_t i = last; i-- > 0;) {
			uint64_t loose = c->loose;
			c->loose = 0;
			out = live(n->u.seq.items[i], out, c);
			uint64_t died = c->loose;
			c->loose = loose | (dropping(c) ? drop_at(c, n, i, died) : died);
		}
		return out;
	}
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET: return live_args(NULL, n->u.seq.items, n->u.seq.n, out, c, NULL);
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP: {
		if (n->kind == CLJ_NODE_LET && c->mark && c->late) note_takeout_let(c, n, out);
		uint64_t loose = c->loose, in;
		c->loose = 0;
		if (n->kind == CLJ_NODE_LET) {
			in = live_tail(n->u.let.body, out, c, tail);
		} else {
			target t = {0, 0};
			for (uint32_t i = 0; i < n->u.let.n; i++) t.slots |= slot_bit(n->u.let.slots[i]);
			in = live_fixpoint(n->u.let.body, out, &t, c, tail);
		}
		// the end's drop would only come before the frame's own releases
		uint64_t dying = dropping(c) && !tail ? drop_at(c, n, n->u.let.n, c->loose) : c->loose;
		c->loose = 0;
		in = live_bindings(n, in, c, dying);
		c->loose |= loose;
		return in;
	}
	case CLJ_NODE_RECUR: {
		uint64_t loose = c->loose;
		c->loose = 0;
		uint64_t in = live_seq(n->u.recur.args, n->u.recur.n, c->recur->body_in & ~c->recur->slots, c);
		// the rebinding releases the target's own slots, and nothing runs after the jump
		if (dropping(c)) drop_at(c, n, 0, c->loose & ~c->recur->slots);
		c->loose = loose;
		return in;
	}
	case CLJ_NODE_INVOKE: {
		size_t   takes = c->ntakes;
		uint64_t in = live_args(n->u.invoke.fn, n->u.invoke.args, n->u.invoke.n, out, c, n);
		taken_since(c, takes, c->nconsumes);
		return in;
	}
	case CLJ_NODE_OBJC_SEND: return live_args(n->u.objc.target, n->u.objc.args, n->u.objc.n, out, c, NULL);
	case CLJ_NODE_INTRINSIC: {
		size_t takes = c->ntakes, consumes = c->nconsumes;
		if (c->mark && op_named(n, "clojure.core/cons", 2)) note_cons(c, n, out);
		else if (c->mark && (op_named(n, "clojure.core/next", 1) || op_named(n, "clojure.core/rest", 1))) note_consume(c, n, out);
		else if (c->mark && c->late && clj_intrinsic_consumes(n->u.intrinsic.op)) note_take(c, n, out);
		uint64_t in = live_args(NULL, n->u.intrinsic.args, n->u.intrinsic.n, out, c, NULL);
		taken_since(c, takes, consumes);
		return in;
	}
	case CLJ_NODE_DIRECT_CALL: return live_args(NULL, n->u.direct.args, n->u.direct.n, out, c, NULL);
	case CLJ_NODE_FUSED: return live_args(NULL, n->u.fused.args, n->u.fused.nargs, out, c, NULL);
	case CLJ_NODE_DEF: {
		out = live(n->u.def.meta, out, c);
		return n->u.def.init ? live(n->u.def.init, out, c) : out;
	}
	case CLJ_NODE_THROW: {
		// the unwind releases every slot
		uint64_t loose = c->loose, in = live(n->u.throw_, out, c);
		c->loose = loose;
		return in;
	}
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
			if (n->u.fn.captures[i].kind != CLJ_CAPTURE_LOCAL) continue;
			note_death(c, n->u.fn.captures[i].index, out, NULL);
			out |= slot_bit(n->u.fn.captures[i].index);
		}
		return out;
	case CLJ_NODE_INLINE: return live_tail(n->u.inline_.body, out, c, tail);
	case CLJ_NODE_LOWERED: // the literal's captures are read after the operands
		for (uint32_t i = 0; i < n->u.lowered.fn->u.fn.ncaptures; i++) {
			if (n->u.lowered.fn->u.fn.captures[i].kind != CLJ_CAPTURE_LOCAL) continue;
			note_death(c, n->u.lowered.fn->u.fn.captures[i].index, out, NULL);
			out |= slot_bit(n->u.lowered.fn->u.fn.captures[i].index);
		}
		return live_args(NULL, n->u.lowered.args, n->u.lowered.n, out, c, NULL);
	case CLJ_NODE_CONST:
	case CLJ_NODE_CAPTURED:
	case CLJ_NODE_OUTER:
	case CLJ_NODE_VAR:
	case CLJ_NODE_ROOT_IS:
	case CLJ_NODE_DIRECT_FN: return out;
	}
	clj_ice("emit/reuse-unknown-kind", n, "node kind %u", (unsigned)n->kind);
}

static uint64_t live(const clj_node *n, uint64_t out, ctx *c) { return live_tail(n, out, c, false); }

typedef struct {
	const clj_facts *facts;
	uint64_t         slots;
} droppable_scan;

static bool droppable_fact(const clj_facts *facts, const clj_node *n) {
	const clj_fact *f = clj_facts_node(facts, n->id);
	return !f || (f->types & DROPPABLE);
}

// The frame's slots read or bound as a value the facts do not rule out for clj_c_drop; a capture counts as such.
static void droppable_walk(const clj_node *n, void *arg) {
	droppable_scan *d = arg;
	switch (n->kind) {
	case CLJ_NODE_LOCAL:
		if (droppable_fact(d->facts, n)) d->slots |= slot_bit(n->u.local.index);
		return;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n; i++) {
			if (droppable_fact(d->facts, n->u.let.inits[i])) d->slots |= slot_bit(n->u.let.slots[i]);
		}
		break;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			if (n->u.fn.captures[i].kind == CLJ_CAPTURE_LOCAL) d->slots |= slot_bit(n->u.fn.captures[i].index);
		}
		return;
	case CLJ_NODE_LOWERED: droppable_walk(n->u.lowered.fn, arg); break;
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) droppable_walk(n->u.fused.args[i], arg);
		return;
	default: break;
	}
	clj_node_children(n, droppable_walk, arg);
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

static int drop_order(const void *a, const void *b) {
	const drop_site *x = a, *y = b;
	if (x->node != y->node) return x->node < y->node ? -1 : 1;
	return x->at < y->at ? -1 : x->at > y->at;
}

cljc_reuse_plan *cljc_reuse_plan_frame(const clj_node *body, const clj_facts *facts, uint64_t owned, uint64_t recur_slots, bool takes,
                                       bool drops, cljc_owned_positions positions, void *pctx) {
	if (!owned || !facts) return NULL;
	uint64_t pinned = 0;
	pin_ctx  p = {0, &pinned};
	pin_scan(body, &p);
	target self = {recur_slots, 0};
	ctx    c = {.held = pinned, .recur = &self, .mark = true, .owned = owned & ~pinned, .facts = facts, .late = takes, .positions = positions, .pctx = pctx,
	            .drops = drops};
	if (drops) {
		droppable_scan d = {facts, 0};
		droppable_walk(body, &d);
		c.droppable = d.slots;
	}
	live_fixpoint(body, 0, &self, &c, true);
	cljc_reuse_plan *plan = calloc(1, sizeof *plan);
	if (!plan) clj_fatal("out of memory");
	decide(&c, plan);
	free(c.conses);
	free(c.consumes);
	free(c.takes);
	free(c.takeouts);
	if (c.ndropv) qsort(c.dropv, c.ndropv, sizeof *c.dropv, drop_order);
	plan->drops = c.dropv;
	plan->ndrops = c.ndropv;
	if (plan->n || plan->ndrops) return plan;
	cljc_reuse_plan_free(plan);
	return NULL;
}

uint64_t cljc_reuse_drops(const cljc_reuse_plan *p, const clj_node *node, uint32_t at) {
	if (!p) return 0;
	size_t lo = 0, hi = p->ndrops;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		const drop_site *d = &p->drops[mid];
		if (d->node < node->id || (d->node == node->id && d->at < at)) lo = mid + 1;
		else hi = mid;
	}
	uint64_t slots = 0;
	for (; lo < p->ndrops && p->drops[lo].node == node->id && p->drops[lo].at == at; lo++) slots |= p->drops[lo].slots;
	return slots;
}

const cljc_reuse_pair *cljc_reuse_at(const cljc_reuse_plan *p, uint32_t node) {
	for (size_t i = 0; p && i < p->n; i++) {
		if (p->pairs[i].node == node && p->pairs[i].kind != CLJC_REUSE_HAND) return &p->pairs[i];
	}
	return NULL;
}

const cljc_reuse_pair *cljc_reuse_takeout_declared(const cljc_reuse_plan *p, uint32_t node, size_t *from) {
	for (size_t i = *from; p && i < p->n; i++) {
		if (p->pairs[i].kind == CLJC_REUSE_TAKEOUT && p->pairs[i].decl == node) {
			*from = i + 1;
			return &p->pairs[i];
		}
	}
	return NULL;
}

const cljc_reuse_pair *cljc_reuse_hand_at(const cljc_reuse_plan *p, uint32_t node, uint32_t arg) {
	for (size_t i = 0; p && i < p->n; i++) {
		if (p->pairs[i].node == node && p->pairs[i].kind == CLJC_REUSE_HAND && p->pairs[i].arg == arg) return &p->pairs[i];
	}
	return NULL;
}

size_t cljc_reuse_count(const cljc_reuse_plan *p) { return p ? p->n : 0; }

const cljc_reuse_pair *cljc_reuse_pair_at(const cljc_reuse_plan *p, size_t i) { return &p->pairs[i]; }

void cljc_reuse_plan_free(cljc_reuse_plan *p) {
	if (!p) return;
	free(p->pairs);
	free(p->drops);
	free(p);
}
