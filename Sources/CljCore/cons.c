// @ai-generated(solo)
#include "clj/cons.h"

static void cons_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_cons *c = self;
	visit(c->first, ctx);
	visit(c->rest, ctx);
}

const clj_type clj_cons_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "cons",
	.each_child = cons_each_child,
};

clj_value clj_cons_new(clj_value first, clj_value rest) {
	clj_cons *c = clj_alloc(&clj_cons_type, sizeof *c);
	c->first = clj_retain(first);
	c->rest = clj_retain(rest);
	return clj_from_ptr(c);
}
