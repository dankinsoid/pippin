// @ai-generated(solo)
#include "clj/cons.h"
#include "clj/list.h"
#include "clj/vector.h"

static void cons_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_cons *c = self;
	visit(c->first, ctx);
	visit(c->rest, ctx);
}

// No cache slot: the cell stays 32 bytes, so hashing a list walks it every time.
static uint32_t cons_hash(void *self) { return clj_seq_hash(clj_from_ptr(self)); }

static bool cons_equals(void *self, clj_value other) {
	return (clj_is_list(other) || clj_is_vector(other)) && clj_seq_equals(clj_from_ptr(self), other);
}

const clj_type clj_cons_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "cons",
	.each_child = cons_each_child,
	.hash = cons_hash,
	.equals = cons_equals,
};

clj_value clj_cons_new(clj_value first, clj_value rest) {
	clj_cons *c = clj_alloc(&clj_cons_type, sizeof *c);
	c->first = clj_retain(first);
	c->rest = clj_retain(rest);
	return clj_from_ptr(c);
}
