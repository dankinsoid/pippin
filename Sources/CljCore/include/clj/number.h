// @ai-generated(solo)
#ifndef CLJ_NUMBER_H
#define CLJ_NUMBER_H

#include "object.h"

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

#endif
