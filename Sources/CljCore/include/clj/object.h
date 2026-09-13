// @ai-generated(guided)
#ifndef CLJ_OBJECT_H
#define CLJ_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#include "value.h"

typedef struct clj_type clj_type;

// Every heap object starts with this. Kept to 16 bytes so a cons cell is 32.
typedef struct {
	uint32_t        rc;
	uint32_t        flags;
	const clj_type *type;
} clj_header;

// Set once the object has crossed a thread or been exported to the host; RC becomes atomic.
#define CLJ_FLAG_SHARED ((uint32_t)1 << 0)

// Type descriptors are heap objects themselves: deftype creates them at runtime
// and builtin types must be indistinguishable from user ones.
struct clj_type {
	clj_header  h;
	const char *name;
	size_t      size;
	void      (*drop)(void *self);
};

static inline clj_header *clj_header_of(clj_value v) { return (clj_header *)clj_to_ptr(v); }

#endif
