// @ai-generated(solo)
#ifndef CLJ_RATIO_H
#define CLJ_RATIO_H

#include "bigint.h"

// Both components are bigints, as clojure.lang.Ratio holds BigIntegers: gcd-reduced, den > 1.
typedef struct {
	clj_header h;
	clj_value  num;
	clj_value  den;
} clj_ratio;

extern const clj_type clj_ratio_type;

static inline bool       clj_is_ratio(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_ratio_type; }
static inline clj_ratio *clj_ratio_of(clj_value v) { return (clj_ratio *)clj_to_ptr(v); }
// Borrowed: valid while v is.
static inline clj_value clj_ratio_num(clj_value v) { return clj_ratio_of(v)->num; }
static inline clj_value clj_ratio_den(clj_value v) { return clj_ratio_of(v)->den; }

// num and den are a fixnum or a bigint, den not zero; a denominator that reduces to 1 yields a bigint.
clj_value clj_ratio_new(clj_value num, clj_value den);
double    clj_ratio_to_double(clj_value v);
int       clj_ratio_cmp(clj_value a, clj_value b);
clj_value clj_ratio_to_string(clj_value v);

#endif
