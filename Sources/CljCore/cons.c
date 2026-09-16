// @ai-generated(solo)
#include "clj/cons.h"
#include "clj/list.h"
#include "clj/reduce.h"

static clj_value *meta_slot(clj_cons *c) { return clj_meta_slot_at(c, sizeof *c); }

static void cons_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_cons *c = self;
	visit(c->first, ctx);
	visit(c->rest, ctx);
	if (c->h.flags & CLJ_FLAG_META) visit(*meta_slot(c), ctx);
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

static clj_value cons_meta(clj_value self) {
	clj_cons *c = clj_cons_of(self);
	return c->h.flags & CLJ_FLAG_META ? clj_retain(*meta_slot(c)) : CLJ_NIL;
}

// @ai-generated(guided)
static clj_value with_meta_of(const clj_type *type, clj_value self, clj_value m) {
	clj_cons *c = clj_cons_of(self);
	if ((c->h.flags & CLJ_FLAG_META) && clj_is_unique(self)) {
		if (c->h.flags & CLJ_FLAG_SHARED) clj_share(m);
		clj_value old = *meta_slot(c);
		*meta_slot(c) = clj_retain(m);
		clj_release(old);
		return self;
	}
	if (!(c->h.flags & CLJ_FLAG_META) && clj_is_nil(m)) return self;
	clj_value r = clj_cons_alloc(type, c->first, c->rest, m, true);
	clj_release(self);
	return r;
}

static clj_value cons_with_meta(clj_value self, clj_value m) { return with_meta_of(&clj_cons_type, self, m); }

static clj_value list_with_meta(clj_value self, clj_value m) { return with_meta_of(&clj_list_type, self, m); }

// PersistentList.cons keeps the list's own meta on the new head; ASeq.cons drops down to a Cons.
static clj_value list_conj(clj_value self, clj_value x) {
	clj_cons *c = clj_cons_of(self);
	clj_value m = c->h.flags & CLJ_FLAG_META ? *meta_slot(c) : CLJ_NIL;
	clj_value r = clj_cons_alloc(&clj_list_type, x, self, m, !clj_is_nil(m));
	clj_release(self);
	return r;
}

// No hash cache: the cell stays 32 bytes, so hashing a list walks it every time.
const clj_type clj_cons_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "cons",
	CLJ_ASEQ_TRAIT(CLJ_CORE_META | CLJ_CORE_OBJ),
	.each_child = cons_each_child,
	.seq = clj_aseq_seq,
	.first = cons_first,
	.next = cons_next,
	.rest = cons_rest,
	.reduce = clj_reduce_iter,
	.meta = cons_meta,
	.with_meta = cons_with_meta,
};

// Same cell as a Cons; only IPersistentList, conj and the type name separate them, as on the JVM.
const clj_type clj_list_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "list",
	CLJ_ASEQ_TRAIT_BASE(CLJ_CORE_LIST | CLJ_CORE_META | CLJ_CORE_OBJ),
	.conj = list_conj,
	.each_child = cons_each_child,
	.seq = clj_aseq_seq,
	.first = cons_first,
	.next = cons_next,
	.rest = cons_rest,
	.reduce = clj_reduce_iter,
	.meta = cons_meta,
	.with_meta = list_with_meta,
};

clj_value clj_cons_alloc(const clj_type *type, clj_value first, clj_value rest, clj_value m, bool with_meta) {
	clj_cons *c = clj_alloc(type, sizeof *c + (with_meta ? sizeof(clj_value) : 0));
	c->first = clj_retain(first);
	c->rest = clj_retain(rest);
	if (with_meta) {
		c->h.flags |= CLJ_FLAG_META;
		*meta_slot(c) = clj_retain(m);
	}
	return clj_from_ptr(c);
}

clj_value clj_cons_new(clj_value first, clj_value rest) { return clj_cons_alloc(&clj_cons_type, first, rest, CLJ_NIL, false); }

clj_value clj_cons_new_meta(clj_value first, clj_value rest, clj_value m) { return clj_cons_alloc(&clj_cons_type, first, rest, m, true); }

clj_value clj_list_new(clj_value first, clj_value rest) { return clj_cons_alloc(&clj_list_type, first, rest, CLJ_NIL, false); }

clj_value clj_list_new_meta(clj_value first, clj_value rest, clj_value m) { return clj_cons_alloc(&clj_list_type, first, rest, m, true); }
