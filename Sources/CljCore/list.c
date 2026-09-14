// @ai-generated(solo)
#include "clj/list.h"

static clj_value empty_list_nil(clj_value self) {
	(void)self;
	return CLJ_NIL;
}

static clj_value empty_list_count(clj_value self) {
	(void)self;
	return clj_fixnum(0);
}

const clj_type clj_empty_list_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "empty-list",
	CLJ_ASEQ_TRAIT(CLJ_CORE_LIST | CLJ_CORE_COUNTED),
	.seq = empty_list_nil,
	.first = empty_list_nil,
	.next = empty_list_nil,
	.count = empty_list_count,
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
	size_t       n = 0;
	clj_seq_iter it = clj_seq_iter_start(list);
	clj_value    item;
	while (clj_seq_iter_next(&it, &item)) n++;
	return n;
}
