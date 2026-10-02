// @ai-generated(solo)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/error.h"
#include "clj/hostbox.h"

typedef struct {
	clj_type         t;
	clj_host_box_ops ops;
	void            *ctx;
	char            *hash_refusal, *equals_refusal;
} host_box_type;

static const host_box_type *box_type(const void *self) { return (const host_box_type *)((const clj_header *)self)->type; }

static void box_finalize(void *self) {
	const host_box_type *t = box_type(self);
	t->ops.release(t->ctx, ((clj_host_box *)self)->payload);
}

static uint32_t box_hash(void *self) {
	const host_box_type *t = box_type(self);
	if (!t->ops.hash) {
		clj_refuse(t->hash_refusal);
		return 0;
	}
	return t->ops.hash(t->ctx, ((clj_host_box *)self)->payload);
}

// Boxes of two host types are never equal, as two Swift types are not comparable at all.
static bool box_equals(void *self, clj_value other) {
	if (!clj_is_ptr(other) || clj_type_of(other) != ((const clj_header *)self)->type) return false;
	const host_box_type *t = box_type(self);
	if (!t->ops.equals) {
		clj_refuse(t->equals_refusal);
		return false;
	}
	return t->ops.equals(t->ctx, ((clj_host_box *)self)->payload, clj_host_box_payload(other));
}

static char *format(const char *fmt, const char *name) {
	int   n = snprintf(NULL, 0, fmt, name);
	char *out = malloc((size_t)n + 1);
	if (!out) clj_fatal("out of memory");
	snprintf(out, (size_t)n + 1, fmt, name);
	return out;
}

// Immortal and out of the live count, as an interned host type is: boxes reach it through their header.
const clj_type *clj_host_box_type_new(const char *name, const clj_host_box_ops *ops, void *ctx) {
	int64_t        before = clj_debug_live_objects();
	host_box_type *t = clj_alloc(&clj_type_type, sizeof *t);
	t->t.h.flags |= CLJ_FLAG_IMMORTAL | CLJ_FLAG_SHARED;
	t->t.name = format("%s", name);
	t->t.finalize = box_finalize;
	t->t.hash = box_hash;
	t->t.equals = box_equals;
	t->ops = *ops;
	t->ctx = ctx;
	t->hash_refusal = format("%s is not Hashable: a boxed Swift value without it cannot be a map key or a set member (design §5)", name);
	t->equals_refusal = format("%s is not Equatable: two boxed Swift values without it have no answer to = (design §5)", name);
	if (before >= 0) clj_debug_live_objects_exclude(clj_debug_live_objects() - before);
	return &t->t;
}

clj_value clj_host_box_new(const clj_type *type, void *payload) {
	clj_host_box *b = clj_alloc(type, sizeof *b);
	b->payload = payload;
	return clj_from_ptr(b);
}

bool clj_is_host_box(clj_value v) { return clj_is_ptr(v) && clj_type_of(v)->finalize == box_finalize; }

void *clj_host_box_type_ctx(const clj_type *type) { return ((const host_box_type *)type)->ctx; }

clj_value clj_host_box_describe(clj_value box) {
	const host_box_type *t = box_type(clj_to_ptr(box));
	return t->ops.describe ? t->ops.describe(t->ctx, clj_host_box_payload(box)) : CLJ_NIL;
}
