// @ai-generated(solo)
// (sequence xform coll): a realization feeds items until one emits, so an infinite source stays lazy.
#include <stdlib.h>

#include "clj/box.h"
#include "clj/coll.h"
#include "clj/cons.h"
#include "clj/core.h"
#include "clj/fn.h"
#include "clj/seq.h"
#include "shadow_internal.h"

// The outputs of the running step, owned. Empty between realizations but after a throw, which drops them.
typedef struct {
	clj_value *items;
	size_t     n, cap;
} outbuf;

static void drop_outputs(outbuf *b) {
	for (size_t i = 0; i < b->n; i++) clj_release(b->items[i]);
	b->n = 0;
}

static void outbuf_free(void *ctx) {
	outbuf *b = ctx;
	drop_outputs(b);
	free(b->items);
	free(b);
}

// The rf under xform: the result it answers is always nil, so a non-nil one a step sees is the transducer's own.
static clj_value bottom(void *ctx, const clj_value *args, size_t n) {
	outbuf *b = ctx;
	if (n == 0) return CLJ_NIL;
	if (n == 1) return clj_retain(args[0]);
	if (b->n == b->cap) {
		size_t     cap = b->cap ? 2 * b->cap : 4;
		clj_value *grown = realloc(b->items, cap * sizeof *grown);
		if (!grown) clj_fatal("out of memory");
		b->items = grown;
		b->cap = cap;
	}
	b->items[b->n++] = clj_retain(args[1]);
	return CLJ_NIL;
}

enum { CAP_XF, CAP_BOTTOM, CAP_SOURCE, NCAPTURED };

static outbuf *outbuf_of(clj_value bot) { return clj_fn_of(bot)->u.native_ctx.ctx; }

// The buffered outputs moved in front of tail (owned), the buffer left empty.
static clj_value drain_onto(outbuf *b, clj_value tail) {
	for (size_t i = b->n; i > 0; i--) {
		clj_value c = clj_cons_new(b->items[i - 1], tail);
		clj_release(b->items[i - 1]);
		clj_release(tail);
		tail = c;
	}
	b->n = 0;
	return tail;
}

static clj_value complete(clj_value xf, outbuf *b) {
	clj_value nil = CLJ_NIL;
	clj_value r = clj_invoke(xf, &nil, 1);
	if (r == CLJ_THROWN) {
		drop_outputs(b);
		return CLJ_THROWN;
	}
	clj_release(r);
	return drain_onto(b, CLJ_NIL);
}

static clj_value fail(outbuf *b, clj_value s) {
	drop_outputs(b);
	clj_release(s);
	return CLJ_THROWN;
}

// The source position is the cell's capture, so the rerun after a throw feeds the same item again.
static clj_value step(clj_value self, const clj_value *captured, const clj_value *args, size_t nargs) {
	(void)self;
	(void)args;
	(void)nargs;
	clj_value         xf = captured[CAP_XF];
	outbuf           *b = outbuf_of(captured[CAP_BOTTOM]);
	clj_shadow_stack *ring = NULL;
	clj_value         s = clj_seq(captured[CAP_SOURCE]);
	if (s == CLJ_THROWN) return s;
	for (;;) {
		if (clj_is_nil(s)) return complete(xf, b);
		clj_value x = clj_first(s);
		if (x == CLJ_THROWN) return fail(b, s);
		clj_value in[2] = {CLJ_NIL, x};
		clj_value r = clj_invoke(xf, in, 2);
		clj_release(x);
		if (r == CLJ_THROWN) return fail(b, s);
		bool halted = clj_is_reduced(r);
		clj_release(r);
		if (halted) {
			clj_release(s);
			return complete(xf, b);
		}
		if (b->n) {
			clj_value rest = clj_rest(s);
			if (rest == CLJ_THROWN) return fail(b, s);
			clj_release(s);
			const clj_value next_captured[NCAPTURED] = {xf, captured[CAP_BOTTOM], rest};
			clj_value       cell = clj_lazy_seq_code(step, next_captured, NCAPTURED);
			clj_release(rest);
			return drain_onto(b, cell);
		}
		clj_value nx = clj_next(s);
		clj_release(s);
		if (nx == CLJ_THROWN) return fail(b, CLJ_NIL);
		s = nx;
		// An item that emits nothing loops here, not through a cell, so the deadline is this loop's to check.
		if (!ring) ring = clj_shadow_stack_init();
		if (clj_deadline_tick_on(ring)) return fail(b, s);
	}
}

clj_value clj_sequence_xform(clj_value xform, clj_value coll) {
	outbuf *b = calloc(1, sizeof *b);
	if (!b) clj_fatal("out of memory");
	clj_value bot = clj_fn_native_ctx(CLJ_NIL, bottom, b, outbuf_free, 0, 2);
	clj_value xf = clj_invoke(xform, &bot, 1);
	if (xf == CLJ_THROWN) {
		clj_release(bot);
		return CLJ_THROWN;
	}
	const clj_value captured[NCAPTURED] = {xf, bot, coll};
	clj_value       cell = clj_lazy_seq_code(step, captured, NCAPTURED);
	clj_release(xf);
	clj_release(bot);
	return cell;
}
