// @ai-generated(solo)
#ifndef CLJ_CONS_H
#define CLJ_CONS_H

#include "object.h"

// 32 bytes. With CLJ_FLAG_META set, one more clj_value follows the cell: its metadata (clj_cons_meta_slot).
typedef struct {
	clj_header h;
	clj_value  first;
	clj_value  rest;
} clj_cons;

extern const clj_type clj_cons_type;

clj_value clj_cons_new(clj_value first, clj_value rest);
// The 40-byte variant with meta stored; m is a map or nil, borrowed.
clj_value clj_cons_new_meta(clj_value first, clj_value rest, clj_value m);

static inline clj_cons *clj_cons_of(clj_value v) { return (clj_cons *)clj_to_ptr(v); }
// The trailing meta word of an object whose header has CLJ_FLAG_META; obj_size is the size without it.
static inline clj_value *clj_meta_slot_at(void *obj, size_t obj_size) { return (clj_value *)((char *)obj + obj_size); }

#endif
