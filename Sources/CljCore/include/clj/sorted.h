// @ai-generated(solo)
#ifndef CLJ_SORTED_H
#define CLJ_SORTED_H

#include "object.h"

// A set stores its element as the key with a nil value, so both types share this wrapper (NOTES.md, "Sorted").
typedef struct {
	clj_header       h;
	uint32_t         count;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	clj_value        root;  // tree node or nil
	clj_value        cmp;   // comparator fn, never nil; kept by empty/assoc/dissoc/with-meta
	clj_value        meta;  // map or nil; ignored by equality and hash
} clj_sorted;

extern const clj_type clj_sorted_map_type;
extern const clj_type clj_sorted_set_type;

// cmp may answer a number or, as a predicate, logical true when a sorts first, as AFunction.compare reads a fn.
clj_value clj_sorted_map_new(clj_value cmp);
clj_value clj_sorted_set_new(clj_value cmp);

static inline bool        clj_is_sorted_map(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_sorted_map_type; }
static inline bool        clj_is_sorted_set(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_sorted_set_type; }
static inline bool        clj_is_sorted(clj_value v) { return clj_is_sorted_map(v) || clj_is_sorted_set(v); }
static inline clj_sorted *clj_sorted_of(clj_value v) { return (clj_sorted *)clj_to_ptr(v); }

static inline uint32_t clj_sorted_count(clj_value c) { return clj_sorted_of(c)->count; }
// Borrowed.
static inline clj_value clj_sorted_comparator(clj_value c) { return clj_sorted_of(c)->cmp; }

// -1, 0 or 1 as a is below, equal to or above b; CLJ_THROWN when the comparator throws.
clj_value clj_sorted_compare(clj_value c, clj_value a, clj_value b);

// Borrowed (+0); not_found when absent, CLJ_THROWN when the comparator throws.
clj_value clj_sorted_get(clj_value c, clj_value key, clj_value not_found);
clj_value clj_sorted_contains(clj_value c, clj_value key);

// Consume the collection (+1 in): a unique one is updated in place; CLJ_THROWN releases it.
clj_value clj_sorted_assoc(clj_value map, clj_value key, clj_value val);
// dissoc for a map, disj for a set: both remove the key.
clj_value clj_sorted_dissoc(clj_value c, clj_value key);
clj_value clj_sorted_conj(clj_value c, clj_value x);

// The empty collection of the same kind and comparator, without the meta.
clj_value clj_sorted_empty(clj_value c);

// Eager seq in key order, nil when empty; a map's entries are [k v] vectors.
clj_value clj_sorted_seq(clj_value c, bool ascending);
// The same from key on, inclusive; CLJ_THROWN when the comparator throws.
clj_value clj_sorted_seq_from(clj_value c, clj_value key, bool ascending);

// (reduce-kv f init map) over the tree in key order (reduce.h).
clj_value clj_sorted_map_reduce_kv(clj_value map, clj_value f, clj_value init);

// Return false to stop. Entries are borrowed and come in key order; a set's value is nil.
typedef bool (*clj_sorted_entry_fn)(clj_value key, clj_value val, void *ctx);
void clj_sorted_each(clj_value c, clj_sorted_entry_fn fn, void *ctx);

// Test hooks: the tree root, and the red-black plus ordering invariants of the whole tree.
clj_value clj_debug_sorted_root(clj_value c);
bool      clj_debug_sorted_valid(clj_value c);

#endif
