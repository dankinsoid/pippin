// @ai-generated(guided)
#ifndef CLJ_SET_H
#define CLJ_SET_H

#include "map.h"

// A hash set over the map's CHAMP trie (element -> element), as Clojure's PersistentHashSet over its map.
typedef struct {
	clj_header       h;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	clj_value        impl; // map, never nil
	clj_value        meta; // map or nil; kept across conj/disj, ignored by equality and hash
} clj_set;

extern const clj_type clj_set_type;

// Immortal singleton.
clj_value clj_set_empty(void);
uint32_t  clj_set_count(clj_value set);
bool      clj_set_contains(clj_value set, clj_value x);
// Borrowed (+0): the element equal to x as the set holds it, or not_found.
clj_value clj_set_get(clj_value set, clj_value x, clj_value not_found);

// Consume set (+1 in): a unique one is updated in place. A present element stays as it is.
clj_value clj_set_conj(clj_value set, clj_value x);
clj_value clj_set_disj(clj_value set, clj_value x);

// (hash-set items...): duplicates collapse onto the first.
clj_value clj_set_from_array(const clj_value *items, size_t n);

// Return false to stop. Items are borrowed. Order is unspecified.
typedef bool (*clj_set_item_fn)(clj_value item, void *ctx);
void clj_set_each(clj_value set, clj_set_item_fn fn, void *ctx);

static inline bool     clj_is_set(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_set_type; }
static inline clj_set *clj_set_of(clj_value v) { return (clj_set *)clj_to_ptr(v); }

#endif
