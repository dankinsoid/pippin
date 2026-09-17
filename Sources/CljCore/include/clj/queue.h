// @ai-generated(solo)
#ifndef CLJ_QUEUE_H
#define CLJ_QUEUE_H

#include "object.h"

// clojure.lang.PersistentQueue. Invariant: count > 0 means front is non-empty, so peek and pop never read rear.
typedef struct {
	clj_header h;
	uint32_t   count;
	clj_value  front; // a seq, nil when empty
	clj_value  rear;  // a vector, the empty one when nothing is queued behind front
	clj_value  meta;
} clj_queue;

extern const clj_type clj_queue_type;

// Immortal singleton: clojure.lang.PersistentQueue/EMPTY.
clj_value clj_queue_empty(void);
// The front item, nil when empty.
clj_value clj_queue_peek(clj_value q);
// Without the front item; the empty queue pops to itself, as on the JVM. Consumes q (+1 in).
clj_value clj_queue_pop(clj_value q);

static inline bool       clj_is_queue(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_queue_type; }
static inline clj_queue *clj_queue_of(clj_value v) { return (clj_queue *)clj_to_ptr(v); }

void clj_queue_install(void);

#endif
