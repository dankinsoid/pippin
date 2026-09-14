// @ai-generated(guided)
#include <stdlib.h>

#include "clj/coll.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/list.h"
#include "clj/seq.h"
#include "clj/string.h"
#include "clj/vector.h"

static clj_value not_a_seq(clj_value v) { return clj_throw_msg("Don't know how to create ISeq from: %s", clj_type_name(v)); }

static const clj_type *type_or_null(clj_value v) { return clj_is_ptr(v) ? clj_type_of(v) : NULL; }

clj_value clj_seq(clj_value coll) {
	if (clj_is_nil(coll)) return CLJ_NIL;
	const clj_type *t = type_or_null(coll);
	if (!t || !t->seq) return not_a_seq(coll);
	return t->seq(coll);
}

// Applies a seq type's first or next slot to (seq coll).
static clj_value via_seq(clj_value coll, bool first) {
	clj_value s = clj_seq(coll);
	if (s == CLJ_THROWN || clj_is_nil(s)) return s;
	const clj_type *t = clj_type_of(s);
	CLJ_ASSERT(t->first && t->next, "a seq type must implement first and next");
	clj_value r = first ? t->first(s) : t->next(s);
	clj_release(s);
	return r;
}

clj_value clj_first(clj_value coll) {
	if (clj_is_nil(coll)) return CLJ_NIL;
	const clj_type *t = type_or_null(coll);
	if (t && t->first) return t->first(coll);
	return via_seq(coll, true);
}

clj_value clj_next(clj_value coll) {
	if (clj_is_nil(coll)) return CLJ_NIL;
	const clj_type *t = type_or_null(coll);
	if (t && t->next) return t->next(coll);
	return via_seq(coll, false);
}

clj_value clj_rest(clj_value coll) {
	const clj_type *t = type_or_null(coll);
	if (t && t->rest) return t->rest(coll);
	clj_value s = clj_seq(coll);
	if (s == CLJ_THROWN) return s;
	if (clj_is_nil(s)) return clj_list_empty();
	clj_value r;
	if (clj_type_of(s)->rest) {
		r = clj_type_of(s)->rest(s);
	} else {
		r = clj_type_of(s)->next(s);
		if (clj_is_nil(r)) r = clj_list_empty();
	}
	clj_release(s);
	return r;
}

clj_value clj_seq_cons(clj_value x, clj_value coll) {
	if (clj_is_nil(coll) || clj_is_seq(coll)) return clj_cons_new(x, coll);
	clj_value s = clj_seq(coll);
	if (s == CLJ_THROWN) return s;
	clj_value r = clj_cons_new(x, s);
	clj_release(s);
	return r;
}

clj_value clj_count(clj_value coll) {
	if (clj_is_nil(coll)) return clj_fixnum(0);
	const clj_type *t = type_or_null(coll);
	if (t && t->count) return clj_fixnum((intptr_t)t->count(coll));
	if (!t || !t->seq) return clj_throw_msg("count not supported on this type: %s", clj_type_name(coll));
	clj_value s = t->seq(coll);
	if (s == CLJ_THROWN) return s;
	clj_seq_iter it = clj_seq_iter_start(s);
	clj_value    item;
	intptr_t     n = 0;
	while (clj_seq_iter_next(&it, &item)) n++;
	clj_release(s);
	return it.thrown ? CLJ_THROWN : clj_fixnum(n);
}

clj_value clj_conj(clj_value coll, clj_value x) {
	if (clj_is_nil(coll)) coll = clj_list_empty();
	const clj_type *t = type_or_null(coll);
	if (!t || !t->conj) {
		clj_value r = clj_throw_msg("conj not supported on this type: %s", clj_type_name(coll));
		clj_release(coll);
		return r;
	}
	return t->conj(coll, x);
}

clj_value clj_get(clj_value coll, clj_value key, clj_value not_found) {
	const clj_type *t = type_or_null(coll);
	if (t && t->lookup) return t->lookup(coll, key, not_found);
	return clj_retain(not_found);
}

// Code point at index i, or false past the end.
static bool string_nth(clj_value s, size_t i, uint32_t *out) {
	const char *p = clj_string_bytes(s);
	size_t      n = clj_string_len(s), pos = 0;
	for (size_t k = 0; pos < n; k++) {
		size_t len = clj_utf8_decode(p, n, pos, out);
		if (k == i) return true;
		pos += len;
	}
	return false;
}

clj_value clj_nth(clj_value coll, clj_value index, bool has_not_found, clj_value not_found) {
	if (!clj_is_fixnum(index)) return clj_throw_msg("Key must be integer");
	intptr_t i = clj_fixnum_val(index);
	size_t   count;
	if (clj_is_nil(coll)) return CLJ_NIL;
	const clj_type *t = type_or_null(coll);
	if (t && (t->core_bits & CLJ_CORE_INDEXED)) {
		// CLJ_UNBOUND is never an element, so it tells a miss from a stored value.
		clj_value r = t->lookup(coll, index, CLJ_UNBOUND);
		if (r != CLJ_UNBOUND) return r;
		count = t->count(coll);
	} else if (t == &clj_string_type) {
		uint32_t cp;
		if (i >= 0 && string_nth(coll, (size_t)i, &cp)) return clj_char(cp);
		count = clj_string_count(coll);
	} else if (t && (t->core_bits & CLJ_CORE_SEQUENTIAL) && t->seq) {
		clj_value s = t->seq(coll);
		if (s == CLJ_THROWN) return s;
		clj_seq_iter it = clj_seq_iter_start(s);
		clj_value    item;
		count = 0;
		while (clj_seq_iter_next(&it, &item)) {
			if ((intptr_t)count == i) {
				clj_retain(item);
				clj_release(s);
				return item;
			}
			count++;
		}
		clj_release(s);
		if (it.thrown) return CLJ_THROWN;
	} else {
		return clj_throw_msg("nth not supported on this type: %s", clj_type_name(coll));
	}
	if (has_not_found) return clj_retain(not_found);
	return clj_throw_msg("Index %lld out of bounds for length %zu", (long long)i, count);
}

// ---- iterator

// Positions the walk at v, whatever it is.
static void iter_enter(clj_seq_iter *it, clj_value v) {
	it->cur = v;
	it->pos = 0;
	if (!clj_is_ptr(v)) return;
	const clj_type *t = clj_type_of(v);
	if (t == &clj_vector_seq_type) it->pos = clj_vector_seq_of(v)->i;
	else if (t == &clj_string_seq_type) it->pos = clj_string_seq_of(v)->pos;
	else if (t == &clj_range_type) it->pos = (uintptr_t)clj_range_of(v)->start;
}

clj_seq_iter clj_seq_iter_start(clj_value seq) {
	clj_seq_iter it = {0};
	iter_enter(&it, seq);
	return it;
}

bool clj_seq_iter_next(clj_seq_iter *it, clj_value *out) {
	for (;;) {
		clj_value cur = it->cur;
		if (!clj_is_ptr(cur)) return false;
		const clj_type *t = clj_type_of(cur);
		if (t == &clj_cons_type) {
			const clj_cons *c = clj_cons_of(cur);
			*out = c->first;
			iter_enter(it, c->rest);
			return true;
		}
		if (t == &clj_empty_list_type) return false;
		if (t == &clj_vector_type || t == &clj_vector_seq_type) {
			clj_value vec = t == &clj_vector_type ? cur : clj_vector_seq_of(cur)->vec;
			if (it->pos >= clj_vector_count(vec)) return false;
			*out = clj_vector_nth(vec, (uint32_t)it->pos++);
			return true;
		}
		if (t == &clj_string_type || t == &clj_string_seq_type) {
			clj_value s = t == &clj_string_type ? cur : clj_string_seq_of(cur)->str;
			size_t    len = clj_string_len(s);
			if (it->pos >= len) return false;
			uint32_t cp;
			it->pos += clj_utf8_decode(clj_string_bytes(s), len, it->pos, &cp);
			*out = clj_char(cp);
			return true;
		}
		if (t == &clj_range_type) {
			const clj_range *r = clj_range_of(cur);
			intptr_t         at = (intptr_t)it->pos;
			if (r->step > 0 ? at >= r->end : at <= r->end) return false;
			*out = clj_fixnum(at);
			it->pos = (uintptr_t)(at + r->step);
			return true;
		}
		if (t == &clj_lazy_seq_type) {
			clj_value v = clj_lazy_seq_force(cur);
			if (v == CLJ_THROWN) {
				it->thrown = true;
				it->cur = CLJ_NIL;
				return false;
			}
			iter_enter(it, v);
			continue;
		}
		// A deftype seq would go through the first/next slots with owned intermediates (NOTES.md).
		clj_fatal("seq iterator: not a builtin seq");
	}
}

clj_value *clj_seq_items(clj_value coll, size_t *n, clj_value *keep) {
	clj_value seq = clj_seq(coll);
	if (seq == CLJ_THROWN) return NULL;
	size_t     cap = 8, count = 0;
	clj_value *items = malloc(cap * sizeof *items);
	if (!items) clj_fatal("out of memory");
	clj_seq_iter it = clj_seq_iter_start(seq);
	clj_value    item;
	while (clj_seq_iter_next(&it, &item)) {
		if (count == cap) {
			cap *= 2;
			items = realloc(items, cap * sizeof *items);
			if (!items) clj_fatal("out of memory");
		}
		items[count++] = item;
	}
	if (it.thrown) {
		free(items);
		clj_release(seq);
		return NULL;
	}
	*n = count;
	*keep = seq;
	return items;
}

// ---- ASeq trait

// equals/hash cannot throw, so a lazy seq whose thunk throws compares unequal and hashes what it
// yielded; the exception is dropped rather than left pending (NOTES.md).
static void drop_thrown(const clj_seq_iter *it) {
	if (it->thrown) clj_release(clj_take_pending());
}

bool clj_seq_equals(clj_value a, clj_value b) {
	clj_seq_iter ia = clj_seq_iter_start(a), ib = clj_seq_iter_start(b);
	clj_value    x, y;
	for (;;) {
		bool ma = clj_seq_iter_next(&ia, &x), mb = clj_seq_iter_next(&ib, &y);
		if (ia.thrown || ib.thrown) {
			drop_thrown(&ia);
			drop_thrown(&ib);
			return false;
		}
		if (ma != mb) return false;
		if (!ma) return true;
		if (!clj_equals(x, y)) return false;
	}
}

uint32_t clj_seq_hash(clj_value seq) {
	uint32_t     h = 1, n = 0;
	clj_seq_iter it = clj_seq_iter_start(seq);
	clj_value    item;
	while (clj_seq_iter_next(&it, &item)) {
		h = 31 * h + clj_hash(item);
		n++;
	}
	drop_thrown(&it);
	return clj_mix_coll_hash(h, n);
}

uint32_t clj_aseq_hash(void *self) { return clj_seq_hash(clj_from_ptr(self)); }

bool clj_aseq_equals(void *self, clj_value other) {
	return clj_has_core(other, CLJ_CORE_SEQUENTIAL) && clj_seq_equals(clj_from_ptr(self), other);
}

clj_value clj_aseq_conj(clj_value self, clj_value x) {
	clj_value c = clj_cons_new(x, self);
	clj_release(self);
	return c;
}

clj_value clj_aseq_seq(clj_value self) { return clj_retain(self); }

clj_value clj_debug_first_via_seq(clj_value coll) { return via_seq(coll, true); }
clj_value clj_debug_next_via_seq(clj_value coll) { return via_seq(coll, false); }
