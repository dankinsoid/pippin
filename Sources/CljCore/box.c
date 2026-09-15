// @ai-generated(guided)
#include "clj/box.h"

static void box_each_child(void *self, clj_visitor visit, void *ctx) { visit(((clj_reduced *)self)->value, ctx); }

static uint32_t box_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool box_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

const clj_type clj_reduced_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "reduced",
	.each_child = box_each_child,
	.hash = box_hash,
	.equals = box_equals,
};

clj_value clj_reduced_new(clj_value value) {
	clj_reduced *r = clj_alloc(&clj_reduced_type, sizeof *r);
	r->value = clj_retain(value);
	return clj_from_ptr(r);
}

const clj_type clj_volatile_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "volatile",
	.each_child = box_each_child,
	.hash = box_hash,
	.equals = box_equals,
};

clj_value clj_volatile_new(clj_value value) {
	clj_volatile *v = clj_alloc(&clj_volatile_type, sizeof *v);
	v->value = clj_retain(value);
	return clj_from_ptr(v);
}

clj_value clj_volatile_deref(clj_value vol) { return clj_retain(((clj_volatile *)clj_to_ptr(vol))->value); }

clj_value clj_volatile_reset(clj_value vol, clj_value value) {
	clj_volatile *v = clj_to_ptr(vol);
	if (v->h.flags & CLJ_FLAG_SHARED) clj_share(value);
	clj_value old = v->value;
	v->value = clj_retain(value);
	clj_release(old);
	return clj_retain(value);
}
