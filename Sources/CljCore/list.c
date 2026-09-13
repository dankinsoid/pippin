// @ai-generated(solo)
#include "clj/list.h"
#include "clj/vector.h"

static uint32_t empty_list_hash(void *self) {
	(void)self;
	return clj_mix_coll_hash(1, 0);
}

static bool empty_list_equals(void *self, clj_value other) {
	(void)self;
	if (!clj_is_list(other) && !clj_is_vector(other)) return false;
	clj_seq_iter it = clj_seq_iter_start(other);
	clj_value item;
	return !clj_seq_iter_next(&it, &item);
}

const clj_type clj_empty_list_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "empty-list",
	.hash = empty_list_hash,
	.equals = empty_list_equals,
};

static clj_header empty_list = {1, CLJ_FLAG_IMMORTAL, &clj_empty_list_type};

clj_value clj_list_empty(void) { return clj_from_ptr(&empty_list); }

clj_value clj_list_from_array(const clj_value *items, size_t n) {
	clj_value rest = clj_list_empty();
	for (size_t i = n; i > 0; i--) {
		clj_cons *c = clj_alloc(&clj_cons_type, sizeof *c);
		c->first = clj_retain(items[i - 1]);
		c->rest = rest;
		rest = clj_from_ptr(c);
	}
	return rest;
}

size_t clj_list_count(clj_value list) {
	size_t n = 0;
	clj_seq_iter it = clj_seq_iter_start(list);
	clj_value item;
	while (clj_seq_iter_next(&it, &item)) n++;
	return n;
}

bool clj_seq_iter_next(clj_seq_iter *it, clj_value *out) {
	clj_value cur = it->cur;
	if (clj_is_nil(cur) || clj_is_empty_list(cur)) return false;
	if (clj_is_vector(cur)) {
		if (it->idx >= clj_vector_count(cur)) return false;
		*out = clj_vector_nth(cur, it->idx++);
		return true;
	}
	if (clj_header_of(cur)->type != &clj_cons_type) clj_fatal("improper list tail");
	clj_cons *c = clj_cons_of(cur);
	*out = c->first;
	it->cur = c->rest;
	return true;
}

bool clj_seq_equals(clj_value a, clj_value b) {
	clj_seq_iter ia = clj_seq_iter_start(a), ib = clj_seq_iter_start(b);
	clj_value x, y;
	for (;;) {
		bool ma = clj_seq_iter_next(&ia, &x), mb = clj_seq_iter_next(&ib, &y);
		if (ma != mb) return false;
		if (!ma) return true;
		if (!clj_equals(x, y)) return false;
	}
}

uint32_t clj_seq_hash(clj_value seq) {
	uint32_t h = 1, n = 0;
	clj_seq_iter it = clj_seq_iter_start(seq);
	clj_value item;
	while (clj_seq_iter_next(&it, &item)) {
		h = 31 * h + clj_hash(item);
		n++;
	}
	return clj_mix_coll_hash(h, n);
}
