// @ai-generated(solo)
#ifndef CLJ_BIGINT_H
#define CLJ_BIGINT_H

#include "object.h"

// Sign and magnitude, little-endian base 2^32 with no leading zero limb, so sign == 0 iff n == 0.
typedef struct {
	clj_header h;
	int32_t    sign; // -1, 0, +1
	uint32_t   n;
	uint32_t   limbs[];
} clj_bigint;

extern const clj_type clj_bigint_type;

static inline bool        clj_is_bigint(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_bigint_type; }
static inline clj_bigint *clj_bigint_of(clj_value v) { return (clj_bigint *)clj_to_ptr(v); }
static inline int         clj_bigint_sign(clj_value v) { return clj_bigint_of(v)->sign; }
static inline bool        clj_bigint_is_zero(clj_value v) { return clj_bigint_of(v)->sign == 0; }

clj_value clj_bigint_from_i64(int64_t v);
// Truncates toward zero; d must be finite, the caller reports the range error.
clj_value clj_bigint_from_double(double d);
// CLJ_NIL when the text is not `n` digits of `radix` with an optional leading sign.
clj_value clj_bigint_parse(const char *s, size_t n, unsigned radix);
bool      clj_bigint_to_i64(clj_value v, int64_t *out);
// ±HUGE_VAL past the double range.
double    clj_bigint_to_double(clj_value v);
// The canonical integer: a fixnum, else a boxed long, else v retained.
clj_value clj_bigint_demote(clj_value v);

clj_value clj_bigint_add(clj_value a, clj_value b);
clj_value clj_bigint_sub(clj_value a, clj_value b);
clj_value clj_bigint_mul(clj_value a, clj_value b);
// Truncated toward zero, the remainder taking the sign of a; b must not be zero, *rem may be NULL.
clj_value clj_bigint_quot(clj_value a, clj_value b, clj_value *rem);
clj_value clj_bigint_neg(clj_value a);
clj_value clj_bigint_abs(clj_value a);
// Non-negative; gcd(0, 0) is 0.
clj_value clj_bigint_gcd(clj_value a, clj_value b);
clj_value clj_bigint_pow10(uint32_t e);
int       clj_bigint_cmp(clj_value a, clj_value b);
clj_value clj_bigint_to_string(clj_value v);
// Magnitude digits into buf, NUL-terminated; cap must exceed clj_bigint_digits10(v).
size_t    clj_bigint_digits(clj_value v, char *buf, size_t cap);
size_t    clj_bigint_digits10(clj_value v);

#endif
