// @ai-generated(solo)
#include "clj/list.h"
#include "clj/reduce.h"

static clj_value *meta_slot(clj_header *h) { return clj_meta_slot_at(h, sizeof *h); }

static clj_value empty_list_nil(clj_value self) {
	(void)self;
	return CLJ_NIL;
}

static clj_value empty_list_count(clj_value self) {
	(void)self;
	return clj_fixnum(0);
}

static clj_value empty_list_reduce(clj_value self, clj_value f, clj_value init) {
	(void)self;
	return clj_reduce_empty(f, init);
}

// Only the 24-byte with-meta variant reaches each_child: the singleton is immortal.
static void empty_list_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_header *h = self;
	if (h->flags & CLJ_FLAG_META) visit(*meta_slot(h), ctx);
}

static clj_value empty_list_meta(clj_value self) {
	clj_header *h = clj_header_of(self);
	return h->flags & CLJ_FLAG_META ? clj_retain(*meta_slot(h)) : CLJ_NIL;
}

// @ai-generated(guided)
static clj_value empty_list_with_meta(clj_value self, clj_value m) {
	clj_header *h = clj_header_of(self);
	if ((h->flags & CLJ_FLAG_META) && clj_is_unique(self)) {
		if (h->flags & CLJ_FLAG_SHARED) clj_share(m);
		clj_value old = *meta_slot(h);
		*meta_slot(h) = clj_retain(m);
		clj_release(old);
		return self;
	}
	clj_release(self);
	if (clj_is_nil(m)) return clj_list_empty();
	clj_header *e = clj_alloc(&clj_empty_list_type, sizeof *e + sizeof(clj_value));
	e->flags |= CLJ_FLAG_META;
	*meta_slot(e) = clj_retain(m);
	return clj_from_ptr(e);
}

// EmptyList.cons: a PersistentList carrying the empty list's own meta.
static clj_value empty_list_conj(clj_value self, clj_value x) {
	clj_header *h = clj_header_of(self);
	clj_value   m = h->flags & CLJ_FLAG_META ? *meta_slot(h) : CLJ_NIL;
	clj_value   r = clj_cons_alloc(&clj_list_type, x, CLJ_NIL, m, !clj_is_nil(m));
	clj_release(self);
	return r;
}

const clj_type clj_empty_list_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "empty-list",
	CLJ_ASEQ_TRAIT_BASE(CLJ_CORE_LIST | CLJ_CORE_COUNTED | CLJ_CORE_META | CLJ_CORE_OBJ),
	.conj = empty_list_conj,
	.each_child = empty_list_each_child,
	.seq = empty_list_nil,
	.first = empty_list_nil,
	.next = empty_list_nil,
	.count = empty_list_count,
	.reduce = empty_list_reduce,
	.meta = empty_list_meta,
	.with_meta = empty_list_with_meta,
};

static clj_header empty_list = {1, CLJ_FLAG_IMMORTAL, &clj_empty_list_type};

clj_value clj_list_empty(void) { return clj_from_ptr(&empty_list); }

clj_value clj_list_from_array(const clj_value *items, size_t n) {
	clj_value rest = clj_list_empty();
	for (size_t i = n; i > 0; i--) {
		clj_cons *c = clj_alloc(&clj_list_type, sizeof *c);
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
