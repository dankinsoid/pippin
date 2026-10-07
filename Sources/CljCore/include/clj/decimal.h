// @ai-generated(solo)
#ifndef CLJ_DECIMAL_H
#define CLJ_DECIMAL_H

#include "bigint.h"

// value = unscaled * 10^-scale, as java.math.BigDecimal; unscaled is always a bigint.
typedef struct {
	clj_header h;
	clj_slot   unscaled;
	int32_t    scale;
} clj_decimal;

extern const clj_type clj_decimal_type;

static inline bool         clj_is_decimal(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_decimal_type; }
static inline clj_decimal *clj_decimal_of(clj_value v) { return (clj_decimal *)clj_to_ptr(v); }
// Borrowed: valid while v is.
static inline clj_value clj_decimal_unscaled(clj_value v) { return clj_decimal_of(v)->unscaled.v; }
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
// CLJ_NIL when the quotient has no terminating expansion.
clj_value clj_decimal_div(clj_value a, clj_value b);
clj_value clj_decimal_neg(clj_value a);
int       clj_decimal_cmp(clj_value a, clj_value b);
clj_value clj_decimal_to_string(clj_value v);
// Truncated toward zero, as a bigint.
clj_value clj_decimal_truncate(clj_value v);
// The exact value as two owned bigints.
void clj_decimal_as_fraction(clj_value v, clj_value *num, clj_value *den);

// java.math.RoundingMode, in its order.
typedef enum {
	CLJ_ROUND_UP,
	CLJ_ROUND_DOWN,
	CLJ_ROUND_CEILING,
	CLJ_ROUND_FLOOR,
	CLJ_ROUND_HALF_UP,
	CLJ_ROUND_HALF_DOWN,
	CLJ_ROUND_HALF_EVEN,
	CLJ_ROUND_UNNECESSARY,
} clj_rounding;

// java.math.MathContext: significant digits, 0 for exact arithmetic.
typedef struct {
	uint32_t     precision;
	clj_rounding rounding;
} clj_math_context;

// v to mc's precision, as BigDecimal.round; CLJ_THROWN when CLJ_ROUND_UNNECESSARY would lose a digit.
clj_value clj_decimal_round(clj_value v, const clj_math_context *mc);
// BigDecimal.divide(b, mc): the exact quotient when it fits the precision, else rounded to it; b must not be
// zero. Under precision 0 a quotient that does not terminate throws.
clj_value clj_decimal_div_mc(clj_value a, clj_value b, const clj_math_context *mc);
// Whether the bigint whole has at most mc's precision digits: divideToIntegralValue's "Division impossible".
bool clj_decimal_integral_fits(clj_value whole, const clj_math_context *mc);

#endif
