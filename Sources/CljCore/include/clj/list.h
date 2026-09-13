// @ai-generated(solo)
#ifndef CLJ_LIST_H
#define CLJ_LIST_H

#include "cons.h"

// A list is a cons chain ending in the empty list (or nil). `()` is a value distinct from nil.
extern const clj_type clj_empty_list_type;

// Immortal singleton.
clj_value clj_list_empty(void);
// Cons cell or the empty list.
static inline bool clj_is_list(clj_value v) {
	return clj_is_ptr(v) && (clj_header_of(v)->type == &clj_cons_type || clj_header_of(v)->type == &clj_empty_list_type);
}
static inline bool clj_is_empty_list(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_empty_list_type; }

// Items are borrowed; the array may be freed after the call.
clj_value clj_list_from_array(const clj_value *items, size_t n);
// O(n): walks the chain (NOTES.md).
size_t clj_list_count(clj_value list);

// Walks a cons chain, a vector, the empty list or nil; a cons tail must be one of those.
typedef struct {
	clj_value cur;
	uint32_t  idx; // position inside a vector tail
} clj_seq_iter;

static inline clj_seq_iter clj_seq_iter_start(clj_value seq) { return (clj_seq_iter){seq, 0}; }
// Items are borrowed: valid while the walked value is.
bool clj_seq_iter_next(clj_seq_iter *it, clj_value *out);

// Sequential equality and hash, shared by lists and vectors so `(= '(1 2) [1 2])` holds.
bool     clj_seq_equals(clj_value a, clj_value b);
uint32_t clj_seq_hash(clj_value seq);

#endif
