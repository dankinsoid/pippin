// @ai-generated(solo)
#include "clj/bigint.h"
#include "clj/long.h"
#include "clj/proto.h"

// The fixnum hash widened to 64 bits, so a box, a fixnum and a bigint of one value hash alike.
static uint32_t long_hash(void *self) {
	uint64_t n = (uint64_t)((clj_long *)self)->val;
	return clj_fmix32((uint32_t)(n ^ (n >> 32)));
}

// A fixnum is never equal to a box by the canonical-form invariant; a bigint holding the same value is.
static bool long_equals(void *self, clj_value other) {
	int64_t x = ((clj_long *)self)->val, y;
	if (clj_is_long(other)) return x == clj_long_val(other);
	return clj_is_bigint(other) && clj_bigint_to_i64(other, &y) && x == y;
}

const clj_type clj_long_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "long",
	.hash = long_hash,
	.equals = long_equals,
};

clj_value clj_long_box(int64_t v) {
	CLJ_ASSERT(v < CLJ_FIXNUM_MIN || v > CLJ_FIXNUM_MAX, "boxed long inside the fixnum range");
	clj_long *b = clj_alloc(&clj_long_type, sizeof *b);
	b->val = v;
	return clj_from_ptr(b);
}
