// @ai-generated(guided)
#ifndef CLJ_BOX_H
#define CLJ_BOX_H

#include "object.h"

// (reduced x): the box a reducing fn returns to stop the reduction early. Identity equality, as Clojure's.
typedef struct {
	clj_header h;
	clj_value  value;
} clj_reduced;

extern const clj_type clj_reduced_type;

// value is retained.
clj_value clj_reduced_new(clj_value value);

static inline bool      clj_is_reduced(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_reduced_type; }
// Borrowed: valid while r is.
static inline clj_value clj_reduced_value(clj_value r) { return ((clj_reduced *)clj_to_ptr(r))->value; }

// (volatile! x): a mutable cell for one thread at a time (NOTES.md, "Volatile"). A volatile that reached a
// shared graph shares what is stored into it, so the RC invariant holds; the read/write race is the
// caller's, as with a var and a concurrent def.
typedef struct {
	clj_header h;
	clj_value  value;
} clj_volatile;

extern const clj_type clj_volatile_type;

clj_value clj_volatile_new(clj_value value);

static inline bool clj_is_volatile(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_volatile_type; }
// Owned current value.
clj_value clj_volatile_deref(clj_value vol);
// Borrowed: valid until the next reset.
static inline clj_value clj_volatile_value(clj_value vol) { return ((clj_volatile *)clj_to_ptr(vol))->value; }
// Stores value (retained, shared when the cell is) and returns it owned; the previous value is released.
clj_value clj_volatile_reset(clj_value vol, clj_value value);

#endif
