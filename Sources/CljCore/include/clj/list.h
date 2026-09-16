// @ai-generated(solo)
#ifndef CLJ_LIST_H
#define CLJ_LIST_H

#include "coll.h"
#include "cons.h"

// A list is a cons chain ending in the empty list (or nil). `()` is a value distinct from nil.
extern const clj_type clj_empty_list_type;

// Immortal singleton.
clj_value clj_list_empty(void);
// List cell or the empty list; a Cons is neither, as on the JVM.
static inline bool clj_is_list(clj_value v) {
	return clj_is_ptr(v) && (clj_header_of(v)->type == &clj_list_type || clj_header_of(v)->type == &clj_empty_list_type);
}
static inline bool clj_is_empty_list(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_empty_list_type; }

// Items are borrowed; the array may be freed after the call.
clj_value clj_list_from_array(const clj_value *items, size_t n);
// O(n): walks the chain with clj_seq_iter, so it counts anything the iterator walks (NOTES.md).
size_t clj_list_count(clj_value list);

#endif
