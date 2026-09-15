// @ai-generated(solo)
#ifndef CLJ_VECTOR_H
#define CLJ_VECTOR_H

#include "object.h"

// 32-way radix trie plus a tail node, as in Clojure. The layout is private: tuples and unboxed
// representations will sit behind these same functions.
extern const clj_type clj_vector_type;

// Immortal singleton.
clj_value clj_vector_empty(void);
uint32_t  clj_vector_count(clj_value vec);
// Borrowed (+0): valid while vec is. Aborts out of range; the bounds check that throws lives higher up.
clj_value clj_vector_nth(clj_value vec, uint32_t i);
// nil when empty.
clj_value clj_vector_peek(clj_value vec);

// Consumes vec (+1 in); a unique vec is rewritten in place, see clj_map_assoc.
clj_value clj_vector_conj(clj_value vec, clj_value val);
// i == count appends; i > count aborts.
clj_value clj_vector_assoc(clj_value vec, uint32_t i, clj_value val);
// Aborts on an empty vector.
clj_value clj_vector_pop(clj_value vec);

// Items are borrowed; the array may be freed after the call.
clj_value clj_vector_from_array(const clj_value *items, uint32_t n);

// The reduce slot from index `from` (reduce.h); the vector-seq view shares it. (f acc i x) for reduce-kv.
clj_value clj_vector_reduce_from(clj_value vec, uint32_t from, clj_value f, clj_value init);
clj_value clj_vector_reduce_kv(clj_value vec, clj_value f, clj_value init);

// Return false to stop. Items are borrowed and visited in order.
typedef bool (*clj_vector_item_fn)(clj_value item, void *ctx);
void clj_vector_each(clj_value vec, clj_vector_item_fn fn, void *ctx);

static inline bool clj_is_vector(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_vector_type; }

// Borrowed root and tail nodes: pointer identity shows in-place reuse in tests.
clj_value clj_debug_vector_root(clj_value vec);
clj_value clj_debug_vector_tail(clj_value vec);
// Raw cache slot, see clj_debug_cached_hash.
uint32_t clj_debug_vector_cached_hash(clj_value vec);

#endif
