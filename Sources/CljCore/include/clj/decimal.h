// @ai-generated(solo)
#ifndef CLJ_DECIMAL_H
#define CLJ_DECIMAL_H

#include "bigint.h"

// value = unscaled * 10^-scale, as java.math.BigDecimal; unscaled is always a bigint.
typedef struct {
	clj_header h;
	clj_value  unscaled;
	int32_t    scale;
} clj_decimal;

extern const clj_type clj_decimal_type;

static inline bool         clj_is_decimal(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_decimal_type; }
static inline clj_decimal *clj_decimal_of(clj_value v) { return (clj_decimal *)clj_to_ptr(v); }
// Borrowed: valid while v is.
static inline clj_value clj_decimal_unscaled(clj_value v) { return clj_decimal_of(v)->unscaled; }
static inline int32_t   clj_decimal_scale(clj_value v) { return clj_decimal_of(v)->scale; }

// unscaled is a fixnum or a bigint.
clj_value clj_decimal_new(clj_value unscaled, int32_t scale);
// CLJ_NIL when the text is not a decimal literal (the trailing M excluded).
clj_value clj_decimal_parse(const char *s, size_t n);
// The shortest decimal that round-trips, as BigDecimal.valueOf; d must be finite.
clj_value clj_decimal_from_double(double d);
double    clj_decimal_to_double(clj_value v);
// CLJ_NIL when p/q has no terminating decimal expansion.
clj_value clj_decimal_from_fraction(clj_value p, clj_value q);
clj_value clj_decimal_add(clj_value a, clj_value b);
clj_value clj_decimal_sub(clj_value a, clj_value b);
clj_value clj_decimal_mul(clj_value a, clj_value b);
// CLJ_NIL when the quotient has no terminating expansion: there is no *math-context* here.
clj_value clj_decimal_div(clj_value a, clj_value b);
clj_value clj_decimal_neg(clj_value a);
int       clj_decimal_cmp(clj_value a, clj_value b);
clj_value clj_decimal_to_string(clj_value v);
// Truncated toward zero, as a bigint.
clj_value clj_decimal_truncate(clj_value v);
// The exact value as two owned bigints.
void clj_decimal_as_fraction(clj_value v, clj_value *num, clj_value *den);

#endif
