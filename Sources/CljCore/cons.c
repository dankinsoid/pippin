// @ai-generated(solo)
#include "clj/cons.h"
#include "clj/list.h"

static void cons_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_cons *c = self;
	visit(c->first, ctx);
	visit(c->rest, ctx);
}

static clj_value cons_first(clj_value self) { return clj_retain(clj_cons_of(self)->first); }

static clj_value cons_next(clj_value self) { return clj_seq(clj_cons_of(self)->rest); }

// The tail as stored, so a lazy tail stays unrealized; a raw non-seq tail (C callers) is seq'd.
static clj_value cons_rest(clj_value self) {
	clj_value rest = clj_cons_of(self)->rest;
	if (clj_is_seq(rest)) return clj_retain(rest);
	clj_value s = clj_seq(rest);
	if (s == CLJ_THROWN || !clj_is_nil(s)) return s;
	return clj_list_empty();
}

// No hash cache: the cell stays 32 bytes, so hashing a list walks it every time.
// CLJ_CORE_LIST until a PersistentList wrapper exists (NOTES.md): reader lists are cons chains.
const clj_type clj_cons_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "cons",
	CLJ_ASEQ_TRAIT(CLJ_CORE_LIST),
	.each_child = cons_each_child,
	.seq = clj_aseq_seq,
	.first = cons_first,
	.next = cons_next,
	.rest = cons_rest,
};

clj_value clj_cons_new(clj_value first, clj_value rest) {
	clj_cons *c = clj_alloc(&clj_cons_type, sizeof *c);
	c->first = clj_retain(first);
	c->rest = clj_retain(rest);
	return clj_from_ptr(c);
}
