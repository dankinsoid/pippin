// @ai-generated(guided)
#ifndef CLJ_PROTO_INTERNAL_H
#define CLJ_PROTO_INTERNAL_H

#include <stdatomic.h>

#include "clj/long.h"
#include "clj/proto.h"

// A thread's dispatch window (proto.c); a writer frees a retired snapshot only after every window has closed.
typedef struct clj_proto_reader {
	_Atomic uint32_t         active;
	struct clj_proto_reader *next;
	struct clj_proto_reader *spare_next; // on the spare list once its thread exited
} clj_proto_reader;

// Not in a public header: Swift cannot import a _Thread_local.
extern _Thread_local clj_proto_reader *clj_proto_reader_tls;

clj_proto_reader *clj_proto_reader_init(void);
// Spins until every window on every thread is closed: a writer retires what readers may still borrow.
void clj_proto_wait_readers(void);

// seq_cst on both sides (Dekker): a reader either shows up to the writer's scan or sees the new snapshot.
static inline clj_proto_reader *clj_proto_window_open_inline(void) {
	clj_proto_reader *r = clj_proto_reader_tls;
	if (__builtin_expect(!r, 0)) r = clj_proto_reader_init();
	atomic_store_explicit(&r->active, 1, memory_order_seq_cst);
	return r;
}

static inline void clj_proto_window_close_inline(clj_proto_reader *r) { atomic_store_explicit(&r->active, 0, memory_order_seq_cst); }

// field* and new*, the builtins a compiled site checks for before reading or building an instance in place.
clj_value clj_b_field_star(const clj_value *args, size_t n);
clj_value clj_b_new_star(const clj_value *args, size_t n);

// A deftype instance of ut over n == ut->nfields fields, each retained.
static inline clj_value clj_instance_make(const clj_user_type *ut, const clj_value *fields, size_t n) {
	clj_instance *o = clj_alloc(&ut->t, sizeof *o + n * sizeof *o->fields);
	for (size_t i = 0; i < n; i++) clj_slot_init(&o->h, &o->fields[i], clj_retain(fields[i]));
	clj_retain(clj_from_ptr((void *)ut));
	return clj_from_ptr(o);
}

// The slot that hands its fn to the type's IReduceInit method, Clojure code that may keep it.
bool clj_proto_reduce_is_user(const clj_type *t);

// Pseudo-descriptors for the immediates (proto.c): what clj_dispatch_type answers for them.
extern const clj_type clj_nil_dispatch_type, clj_boolean_dispatch_type, clj_char_dispatch_type;

static inline const clj_type *clj_dispatch_type_inline(clj_value v) {
	if (__builtin_expect(clj_is_ptr(v), 1)) return clj_type_of(v);
	if (clj_is_fixnum(v)) return &clj_long_type;
	if (clj_is_nil(v)) return &clj_nil_dispatch_type;
	if (clj_is_bool(v)) return &clj_boolean_dispatch_type;
	if (clj_is_char(v)) return &clj_char_dispatch_type;
	clj_fatal("protocol dispatch on a non-value");
}

#endif
