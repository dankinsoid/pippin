// @ai-generated(solo)
#ifndef CLJ_CONS_H
#define CLJ_CONS_H

#include "object.h"

typedef struct {
	clj_header h;
	clj_value  first;
	clj_value  rest;
} clj_cons;

extern const clj_type clj_cons_type;

clj_value clj_cons_new(clj_value first, clj_value rest);

static inline clj_cons *clj_cons_of(clj_value v) { return (clj_cons *)clj_to_ptr(v); }

#endif
