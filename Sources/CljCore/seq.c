// @ai-generated(guided)
#include <sched.h>
#include <stdlib.h>

#include "clj/coll.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/list.h"
#include "clj/seq.h"
#include "clj/string.h"
#include "clj/vector.h"

// ---- vector-seq

static void vector_seq_each_child(void *self, clj_visitor visit, void *ctx) { visit(((clj_vector_seq *)self)->vec, ctx); }

static clj_value vector_seq_first(clj_value self) {
	const clj_vector_seq *s = clj_vector_seq_of(self);
	return clj_retain(clj_vector_nth(s->vec, s->i));
}

static clj_value vector_seq_next(clj_value self) {
	const clj_vector_seq *s = clj_vector_seq_of(self);
	return s->i + 1 < clj_vector_count(s->vec) ? clj_vector_seq_new(s->vec, s->i + 1) : CLJ_NIL;
}

static size_t vector_seq_count(clj_value self) {
	const clj_vector_seq *s = clj_vector_seq_of(self);
	return clj_vector_count(s->vec) - s->i;
}

const clj_type clj_vector_seq_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "vector-seq",
	CLJ_ASEQ_TRAIT(CLJ_CORE_COUNTED),
	.each_child = vector_seq_each_child,
	.seq = clj_aseq_seq,
	.first = vector_seq_first,
	.next = vector_seq_next,
	.count = vector_seq_count,
};

clj_value clj_vector_seq_new(clj_value vec, uint32_t i) {
	CLJ_ASSERT(i < clj_vector_count(vec), "vector-seq past the end");
	clj_vector_seq *s = clj_alloc(&clj_vector_seq_type, sizeof *s);
	s->i = i;
	s->vec = clj_retain(vec);
	return clj_from_ptr(s);
}

// ---- string-seq

static void string_seq_each_child(void *self, clj_visitor visit, void *ctx) { visit(((clj_string_seq *)self)->str, ctx); }

static clj_value string_seq_first(clj_value self) {
	const clj_string_seq *s = clj_string_seq_of(self);
	uint32_t              cp;
	clj_utf8_decode(clj_string_bytes(s->str), clj_string_len(s->str), s->pos, &cp);
	return clj_char(cp);
}

static clj_value string_seq_next(clj_value self) {
	const clj_string_seq *s = clj_string_seq_of(self);
	uint32_t              cp, len = clj_string_len(s->str);
	size_t                next = s->pos + clj_utf8_decode(clj_string_bytes(s->str), len, s->pos, &cp);
	return next < len ? clj_string_seq_new(s->str, (uint32_t)next) : CLJ_NIL;
}

static size_t string_seq_count(clj_value self) {
	const clj_string_seq *s = clj_string_seq_of(self);
	const unsigned char  *p = (const unsigned char *)clj_string_bytes(s->str);
	size_t                n = clj_string_len(s->str), count = 0;
	for (size_t i = s->pos; i < n; i++) count += (p[i] & 0xC0) != 0x80;
	return count;
}

const clj_type clj_string_seq_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "string-seq",
	CLJ_ASEQ_TRAIT(0),
	.each_child = string_seq_each_child,
	.seq = clj_aseq_seq,
	.first = string_seq_first,
	.next = string_seq_next,
	.count = string_seq_count,
};

clj_value clj_string_seq_new(clj_value str, uint32_t pos) {
	CLJ_ASSERT(pos < clj_string_len(str), "string-seq past the end");
	clj_string_seq *s = clj_alloc(&clj_string_seq_type, sizeof *s);
	s->pos = pos;
	s->str = clj_retain(str);
	return clj_from_ptr(s);
}

// ---- range

static clj_value range_first(clj_value self) { return clj_fixnum(clj_range_of(self)->start); }

static clj_value range_next(clj_value self) {
	const clj_range *r = clj_range_of(self);
	intptr_t         next = r->start + r->step;
	if (r->step > 0 ? next >= r->end : next <= r->end) return CLJ_NIL;
	return clj_range_new(next, r->end, r->step);
}

static size_t range_count(clj_value self) {
	const clj_range *r = clj_range_of(self);
	intptr_t         span = r->end - r->start, step = r->step;
	return (size_t)(span / step + (span % step != 0));
}

const clj_type clj_range_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "range",
	CLJ_ASEQ_TRAIT(CLJ_CORE_COUNTED),
	.seq = clj_aseq_seq,
	.first = range_first,
	.next = range_next,
	.count = range_count,
};

clj_value clj_range_new(intptr_t start, intptr_t end, intptr_t step) {
	if (step == 0) clj_fatal("range with step 0");
	if (step > 0 ? start >= end : start <= end) return clj_list_empty();
	clj_range *r = clj_alloc(&clj_range_type, sizeof *r);
	r->start = start;
	r->end = end;
	r->step = step;
	return clj_from_ptr(r);
}

// ---- lazy-seq

enum { UNFORCED = 0, FORCING = 1, FORCED = 2 };

static void lazy_seq_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_lazy_seq *s = self;
	visit(s->fn, ctx);
	visit(s->value, ctx);
}

static clj_value lazy_seq_seq(clj_value self) {
	clj_value v = clj_lazy_seq_force(self);
	return v == CLJ_THROWN ? v : clj_retain(v);
}

const clj_type clj_lazy_seq_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "lazy-seq",
	CLJ_ASEQ_TRAIT(0),
	.each_child = lazy_seq_each_child,
	.seq = lazy_seq_seq,
};

clj_value clj_lazy_seq_new(clj_value fn) {
	CLJ_ASSERT(clj_is_fn(fn), "lazy-seq thunk must be a fn");
	clj_lazy_seq *s = clj_alloc(&clj_lazy_seq_type, sizeof *s);
	s->fn = clj_retain(fn);
	return clj_from_ptr(s);
}

bool clj_lazy_seq_realized(clj_value ls) {
	return atomic_load_explicit(&clj_lazy_seq_of(ls)->state, memory_order_acquire) == FORCED;
}

// Objects this thread is forcing, innermost first: a thunk reaching its own object throws, not spins.
typedef struct forcing {
	clj_value       obj;
	struct forcing *prev;
} forcing;

static _Thread_local forcing *forcing_top;

static bool forcing_here(clj_value obj) {
	for (const forcing *f = forcing_top; f; f = f->prev) {
		if (f->obj == obj) return true;
	}
	return false;
}

// UNFORCED -> FORCING, or false with the object FORCED (its value is now readable) or CLJ_THROWN pending.
static bool claim(clj_value v, bool *thrown) {
	clj_lazy_seq *s = clj_lazy_seq_of(v);
	*thrown = false;
	if (!(s->h.flags & CLJ_FLAG_SHARED)) {
		uint32_t st = atomic_load_explicit(&s->state, memory_order_relaxed);
		if (st == FORCED) return false;
		if (st == FORCING) {
			*thrown = clj_throw_msg("Recursive realization of a lazy seq") == CLJ_THROWN;
			return false;
		}
		atomic_store_explicit(&s->state, FORCING, memory_order_relaxed);
		return true;
	}
	for (;;) {
		uint32_t expected = UNFORCED;
		if (atomic_compare_exchange_weak_explicit(&s->state, &expected, FORCING, memory_order_acq_rel, memory_order_acquire)) return true;
		if (expected == FORCED) return false;
		if (forcing_here(v)) {
			*thrown = clj_throw_msg("Recursive realization of a lazy seq") == CLJ_THROWN;
			return false;
		}
		sched_yield();
	}
}

static void publish(clj_value v, clj_value value) {
	clj_lazy_seq *s = clj_lazy_seq_of(v);
	if (s->h.flags & CLJ_FLAG_SHARED) clj_share(value);
	s->value = value;
	clj_value fn = s->fn;
	s->fn = CLJ_NIL;
	atomic_store_explicit(&s->state, FORCED, memory_order_release);
	clj_release(fn);
}

static void unclaim(clj_value v) { atomic_store_explicit(&clj_lazy_seq_of(v)->state, UNFORCED, memory_order_release); }

// Runs the thunk of a claimed object. Owned result.
static clj_value run_thunk(clj_value v) {
	forcing frame = {v, forcing_top};
	forcing_top = &frame;
	clj_value r = clj_invoke(clj_lazy_seq_of(v)->fn, NULL, 0);
	forcing_top = frame.prev;
	return r;
}

typedef struct {
	clj_value *items;
	size_t     n, cap;
	clj_value  small[16];
} chain;

static void chain_push(chain *c, clj_value v) {
	if (c->n == c->cap) {
		size_t     cap = c->cap * 2;
		clj_value *grown = malloc(cap * sizeof *grown);
		if (!grown) clj_fatal("out of memory");
		for (size_t i = 0; i < c->n; i++) grown[i] = c->items[i];
		if (c->items != c->small) free(c->items);
		c->items = grown;
		c->cap = cap;
	}
	c->items[c->n++] = v;
}

clj_value clj_lazy_seq_force(clj_value ls) {
	clj_lazy_seq *s = clj_lazy_seq_of(ls);
	if (atomic_load_explicit(&s->state, memory_order_acquire) == FORCED) return s->value;
	bool thrown;
	if (!claim(ls, &thrown)) return thrown ? CLJ_THROWN : s->value;

	// Nested lazy seqs are unwrapped in a loop, so nesting depth costs no C stack (LazySeq.seq()).
	chain inner = {.n = 0, .cap = 16};
	inner.items = inner.small;
	clj_value r = run_thunk(ls);
	while (r != CLJ_THROWN && clj_is_lazy_seq(r)) {
		if (!claim(r, &thrown)) {
			clj_value v = thrown ? CLJ_THROWN : clj_retain(clj_lazy_seq_of(r)->value);
			clj_release(r);
			r = v;
			break;
		}
		chain_push(&inner, r);
		r = run_thunk(r);
	}
	// A thunk may return any seqable; the cache holds its seq.
	if (r != CLJ_THROWN && !clj_is_nil(r) && !clj_is_seq(r)) {
		clj_value v = clj_seq(r);
		clj_release(r);
		r = v;
	}
	if (r == CLJ_THROWN) {
		for (size_t i = inner.n; i > 0; i--) {
			unclaim(inner.items[i - 1]);
			clj_release(inner.items[i - 1]);
		}
		unclaim(ls);
	} else {
		for (size_t i = inner.n; i > 0; i--) {
			publish(inner.items[i - 1], clj_retain(r));
			clj_release(inner.items[i - 1]);
		}
		publish(ls, r);
	}
	if (inner.items != inner.small) free(inner.items);
	return r == CLJ_THROWN ? CLJ_THROWN : s->value;
}
