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
} clj_proto_reader;

// Not in a public header: Swift cannot import a _Thread_local.
extern _Thread_local clj_proto_reader *clj_proto_reader_tls;

clj_proto_reader *clj_proto_reader_init(void);

// seq_cst on both sides (Dekker): a reader either shows up to the writer's scan or sees the new snapshot.
static inline clj_proto_reader *clj_proto_window_open_inline(void) {
	clj_proto_reader *r = clj_proto_reader_tls;
	if (__builtin_expect(!r, 0)) r = clj_proto_reader_init();
	atomic_store_explicit(&r->active, 1, memory_order_seq_cst);
	return r;
}

static inline void clj_proto_window_close_inline(clj_proto_reader *r) { atomic_store_explicit(&r->active, 0, memory_order_seq_cst); }

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
