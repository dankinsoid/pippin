// @ai-generated(solo)
#ifndef CLJ_LONG_H
#define CLJ_LONG_H

#include "object.h"

// The part of the JVM's 64-bit long that the 63-bit fixnum tag cannot hold.
typedef struct {
	clj_header h;
	int64_t    val;
} clj_long;

// Also the dispatch key of a fixnum: (type 1) and (type Long/MAX_VALUE) are one descriptor.
extern const clj_type clj_long_type;

static inline bool      clj_is_long(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_long_type; }
static inline clj_long *clj_long_of(clj_value v) { return (clj_long *)clj_to_ptr(v); }
static inline int64_t   clj_long_val(clj_value v) { return clj_long_of(v)->val; }

// v must lie outside the fixnum range; clj_long_new is the constructor that guarantees it.
clj_value clj_long_box(int64_t v);

// Canonical form: a box never holds what a fixnum could, so = and hash never cross-check the two.
static inline clj_value clj_long_new(int64_t v) {
	return v >= CLJ_FIXNUM_MIN && v <= CLJ_FIXNUM_MAX ? clj_fixnum((intptr_t)v) : clj_long_box(v);
}

// True for a fixnum or a box, the two representations of a long.
static inline bool clj_int64_of(clj_value v, int64_t *out) {
	if (clj_is_fixnum(v)) {
		*out = clj_fixnum_val(v);
		return true;
	}
	if (clj_is_long(v)) {
		*out = clj_long_val(v);
		return true;
	}
	return false;
}

#endif
