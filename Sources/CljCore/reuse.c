// @ai-generated(solo)
#include "reuse_internal.h"

#include "alloc.h"
#include "clj/coll.h"
#include "clj/cons.h"
#include "clj/list.h"
#include "clj/seq.h"
#include "clj/string.h"
#include "clj/vector.h"
#include "rc_internal.h"
#include "stats_internal.h"

#if CLJ_DEBUG
// Per thread: a test counts its own calls while other suites run compiled code beside it.
static _Thread_local int64_t debug_counts[3]; // taken, used, skipped
#define DEBUG_COUNT(i, n) (debug_counts[i] += (n))
#else
#define DEBUG_COUNT(i, n) ((void)0)
#endif

void clj_debug_reuse_counts(int64_t out[3]) {
	for (int i = 0; i < 3; i++) {
#if CLJ_DEBUG
		out[i] = debug_counts[i];
#else
		out[i] = -1;
#endif
	}
}

static void count_used(void) { DEBUG_COUNT(1, 1); }

static void count_skipped(uint32_t n) {
	CLJ_STAT_ADD(CLJ_STAT_TOKEN_SKIPPED, n);
	DEBUG_COUNT(2, (int64_t)n);
	(void)n;
}

clj_reuse_token clj_drop_reuse_slow(clj_header *h) {
	if (!clj_rc_token_ok(h)) {
		clj_release(clj_from_ptr(h));
		return NULL;
	}
	CLJ_STAT(CLJ_STAT_TOKEN_TAKEN);
	DEBUG_COUNT(0, 1);
	return h;
}

void *clj_alloc_at(clj_reuse_token tok, const clj_type *type, size_t size) {
	if (tok) {
		if (clj_cell_fits(tok, size)) {
			clj_rc_drop_children(tok);
			clj_cell_reborn(tok, type, size, true);
			count_used();
			return tok;
		}
		clj_reuse_free(tok);
	}
	return clj_alloc_uninit(type, size);
}

// Same layout and size, the old fields still held: a field the new cell holds again is neither released nor rewritten.
static clj_value cons_in_place(clj_cons *c, const clj_type *type, clj_value first, clj_value rest) {
	clj_value old_first = c->first.v, old_rest = c->rest.v;
	clj_cell_reborn(&c->h, type, sizeof *c, false);
	count_used();
	uint32_t skipped = 0;
	if (old_first == first) {
		clj_reach_from(&c->h, c->h.flags, first);
		skipped++;
	} else {
		clj_slot_init(&c->h, &c->first, clj_retain(first));
		clj_release(old_first);
	}
	if (old_rest == rest) {
		clj_reach_from(&c->h, c->h.flags, rest);
		skipped++;
	} else {
		clj_slot_init(&c->h, &c->rest, clj_retain(rest));
		clj_release(old_rest);
	}
	if (skipped) count_skipped(skipped);
	return clj_from_ptr(c);
}

static clj_value cons_at(clj_reuse_token tok, const clj_type *type, clj_value first, clj_value rest) {
	if ((tok->type == &clj_cons_type || tok->type == &clj_list_type) && !(tok->flags & CLJ_FLAG_META)) {
		return cons_in_place((clj_cons *)tok, type, first, rest);
	}
	clj_cons *c = clj_alloc_at(tok, type, sizeof *c);
	clj_slot_init(&c->h, &c->first, clj_retain(first));
	clj_slot_init(&c->h, &c->rest, clj_retain(rest));
	return clj_from_ptr(c);
}

// The type and tail clj_seq_cons picks: a nil tail makes a list, a seq tail a cons, anything else is seq'd.
clj_value clj_seq_cons_into(clj_reuse_token tok, clj_value x, clj_value coll) {
	if (clj_is_nil(coll)) return cons_at(tok, &clj_list_type, x, coll);
	if (clj_is_seq(coll)) return cons_at(tok, &clj_cons_type, x, coll);
	clj_value s = clj_seq(coll);
	if (s == CLJ_THROWN) {
		clj_reuse_free(tok);
		return s;
	}
	clj_value r = cons_at(tok, &clj_cons_type, x, s);
	clj_release(s);
	return r;
}

// One step of a unique view in its own cell; false at its end, the view then still the caller's to release.
static bool step_in_place(clj_header *h) {
	if (h->type == &clj_vector_seq_type) {
		clj_vector_seq *v = (clj_vector_seq *)h;
		if (v->i + 1 >= clj_vector_count(v->vec.v)) return false;
		v->i++;
		count_skipped(1);
		return true;
	}
	if (h->type == &clj_range_type) {
		clj_range *r = (clj_range *)h;
		int64_t    next;
		if (!clj_range_step(r->start, r->step, &next) || (r->step > 0 ? next >= r->end : next <= r->end)) return false;
		r->start = next;
		return true;
	}
	if (h->type == &clj_string_seq_type) {
		clj_string_seq *s = (clj_string_seq *)h;
		uint32_t        cp, len = clj_string_len(s->str.v);
		size_t          next = s->pos + clj_utf8_decode(clj_string_bytes(s->str.v), len, s->pos, &cp);
		if (next >= len) return false;
		s->pos = (uint32_t)next;
		count_skipped(1);
		return true;
	}
	return false;
}

static bool steps_in_place(clj_value s) {
	clj_header     *h = clj_header_of(s);
	const clj_type *t = h->type;
	// A view with meta is 8 bytes longer, and a vector-seq's or range's next drops the meta.
	if ((t != &clj_vector_seq_type && t != &clj_range_type && t != &clj_string_seq_type) || (h->flags & CLJ_FLAG_META)) return false;
	if (!clj_rc_token_ok(h)) return false;
	CLJ_STAT(CLJ_STAT_TOKEN_TAKEN);
	DEBUG_COUNT(0, 1);
	return true;
}

// The stepped view is a new object born where the old one died; its flags already describe the child it keeps.
static clj_value stepped(clj_header *h, size_t size) {
	uint32_t keep = h->flags & (CLJ_FLAG_REACH | CLJ_FLAG_REACH_LOCAL | CLJ_FLAG_LAZY);
	clj_cell_reborn(h, h->type, size, false);
	h->flags |= keep;
	count_used();
	return clj_from_ptr(h);
}

static size_t view_size(const clj_type *t) {
	return t == &clj_vector_seq_type ? sizeof(clj_vector_seq) : t == &clj_range_type ? sizeof(clj_range) : sizeof(clj_string_seq);
}

clj_value clj_next_owned_slow(clj_value s) {
	if (steps_in_place(s)) {
		clj_header *h = clj_header_of(s);
		if (step_in_place(h)) return stepped(h, view_size(h->type));
		clj_release(s);
		return CLJ_NIL;
	}
	clj_value r = clj_next(s);
	clj_release(s);
	return r;
}

// None of the three views has a rest of its own: rest is next, or () past the end.
clj_value clj_rest_owned_slow(clj_value s) {
	if (steps_in_place(s)) {
		clj_header *h = clj_header_of(s);
		if (step_in_place(h)) return stepped(h, view_size(h->type));
		clj_release(s);
		return clj_list_empty();
	}
	clj_value r = clj_rest(s);
	clj_release(s);
	return r;
}
