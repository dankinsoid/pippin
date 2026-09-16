// @ai-generated(solo)
#ifndef CLJ_NUMBER_H
#define CLJ_NUMBER_H

#include "decimal.h"
#include "object.h"
#include "ratio.h"

// Boxed IEEE double. Equality is type-strict like Clojure `=`: never equal to a fixnum.
typedef struct {
	clj_header h;
	double     val;
} clj_double;

extern const clj_type clj_double_type;

clj_value clj_double_new(double d);

static inline bool        clj_is_double(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_double_type; }
static inline clj_double *clj_double_of(clj_value v) { return (clj_double *)clj_to_ptr(v); }
static inline double      clj_double_val(clj_value v) { return clj_double_of(v)->val; }

// Ordered by Clojure's Ops.combine: the larger kind of a pair decides the result's kind.
typedef enum {
	CLJ_NUM_NONE = 0,
	CLJ_NUM_FIXNUM,
	CLJ_NUM_BIGINT,
	CLJ_NUM_RATIO,
	CLJ_NUM_DECIMAL,
	CLJ_NUM_DOUBLE,
} clj_num_kind;

static inline clj_num_kind clj_num_kind_of(clj_value v) {
	if (clj_is_fixnum(v)) return CLJ_NUM_FIXNUM;
	if (!clj_is_ptr(v)) return CLJ_NUM_NONE;
	const clj_type *t = clj_header_of(v)->type;
	if (t == &clj_double_type) return CLJ_NUM_DOUBLE;
	if (t == &clj_bigint_type) return CLJ_NUM_BIGINT;
	if (t == &clj_ratio_type) return CLJ_NUM_RATIO;
	if (t == &clj_decimal_type) return CLJ_NUM_DECIMAL;
	return CLJ_NUM_NONE;
}

static inline bool clj_is_number(clj_value v) { return clj_num_kind_of(v) != CLJ_NUM_NONE; }
static inline bool clj_is_integer(clj_value v) { return clj_is_fixnum(v) || clj_is_bigint(v); }

typedef enum { CLJ_OP_ADD, CLJ_OP_SUB, CLJ_OP_MUL, CLJ_OP_DIV, CLJ_OP_QUOT, CLJ_OP_REM } clj_num_op;

// The whole tower; the fixnum and double fast paths live in builtins.c and never reach here.
clj_value clj_num_arith(clj_value a, clj_value b, clj_num_op op);
// *out is -1, 0, 1, or 2 when a NaN makes the pair unordered; CLJ_THROWN on a non-number.
clj_value clj_num_cmp(clj_value a, clj_value b, int *out);
// v must be a number.
double clj_num_to_double(clj_value v);
int    clj_num_sign(clj_value v);
// Truncated toward zero as a bigint; v must be a number and, if a double, finite.
clj_value clj_num_truncate(clj_value v);
// v must be rational (an integer, a ratio or a decimal); both results are owned bigints.
void clj_num_as_fraction(clj_value v, clj_value *num, clj_value *den);

#endif
