// @ai-generated(solo)
#include <string.h>

#include "clj/number.h"

// -0.0 is folded into 0.0 first: they are equal, so they must hash alike.
static uint32_t double_hash(void *self) {
	double d = clj_double_of(clj_from_ptr(self))->val;
	if (d == 0.0) d = 0.0;
	uint64_t bits;
	memcpy(&bits, &d, sizeof bits);
	return clj_fmix32((uint32_t)(bits ^ (bits >> 32)));
}

// IEEE ==, so NaN differs from everything; only pointer identity in clj_equals makes a NaN equal itself.
static bool double_equals(void *self, clj_value other) {
	return clj_is_double(other) && clj_double_of(clj_from_ptr(self))->val == clj_double_val(other);
}

const clj_type clj_double_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "double",
	.hash = double_hash,
	.equals = double_equals,
};

clj_value clj_double_new(double d) {
	clj_double *b = clj_alloc(&clj_double_type, sizeof *b);
	b->val = d;
	return clj_from_ptr(b);
}
