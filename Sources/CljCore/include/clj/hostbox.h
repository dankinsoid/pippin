// @ai-generated(solo)
#ifndef CLJ_HOSTBOX_H
#define CLJ_HOSTBOX_H

#include "object.h"

// A boxed copy of a Swift struct, opaque to the core (design §5 «Равенство и хэш бокса»).
typedef struct {
	// NULL when the host type has no equality: `=` of two distinct boxes of it refuses (error.h, clj_refuse).
	bool (*equals)(void *ctx, void *a, void *b);
	// NULL when the host type has no hash: a box of it refuses to be a map key or a set member.
	uint32_t (*hash)(void *ctx, void *payload);
	// Runs once, when the box dies, on whatever thread drops the last reference.
	void (*release)(void *ctx, void *payload);
	// The printed form after the type's name, an owned string; NULL prints the name alone.
	clj_value (*describe)(void *ctx, void *payload);
} clj_host_box_ops;

typedef struct {
	clj_header  h;
	void       *payload;
} clj_host_box;

// One descriptor per host type, immortal: a box points at it for as long as any box lives. name is copied.
const clj_type *clj_host_box_type_new(const char *name, const clj_host_box_ops *ops, void *ctx);
// An owned box of the type; it owns payload from here on.
clj_value clj_host_box_new(const clj_type *type, void *payload);
bool      clj_is_host_box(clj_value v);
// The host's state for the box's type, as given to clj_host_box_type_new.
void *clj_host_box_type_ctx(const clj_type *type);
// The describe text of a box, owned, or nil when the type prints its name alone.
clj_value clj_host_box_describe(clj_value box);

static inline void           *clj_host_box_payload(clj_value v) { return ((clj_host_box *)clj_to_ptr(v))->payload; }
static inline const clj_type *clj_host_box_type(clj_value v) { return clj_type_of(v); }

#endif
