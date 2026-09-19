// @ai-generated(solo)
#ifndef CLJ_MAP_H
#define CLJ_MAP_H

#include "object.h"

// CHAMP trie; canonical shape, so equal key sets have equal tries.
typedef struct {
	clj_header       h;
	uint32_t         count;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	clj_value        root; // trie node, never nil
	clj_value        meta; // map or nil; kept across assoc/dissoc, ignored by equality and hash
} clj_map;

typedef struct clj_shape clj_shape;

// The other layout of the same type, told apart by CLJ_FLAG_SHAPE (shape.h): no meta, no hash cache.
typedef struct {
	clj_header       h;
	const clj_shape *shape;
	clj_value        slots[]; // nkeys values in the shape's key order
} clj_shape_map;

extern const clj_type clj_map_type;

static inline bool clj_is_map(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_map_type; }
static inline bool clj_is_shape_map(clj_value v) { return clj_is_map(v) && (clj_header_of(v)->flags & CLJ_FLAG_SHAPE); }
static inline clj_shape_map *clj_shape_map_of(clj_value v) { return (clj_shape_map *)clj_to_ptr(v); }
// The shape of a shape map, NULL for anything else.
static inline const clj_shape *clj_map_shape(clj_value v) { return clj_is_shape_map(v) ? clj_shape_map_of(v)->shape : NULL; }

// Immortal singleton; the object is named so another static initializer (the empty set) can point at it.
extern clj_map clj_map_empty_object;
clj_value clj_map_empty(void);
// A fresh empty map object: what a dissoc of the last key answers, so it is not identical to the singleton.
clj_value clj_map_empty_new(void);
uint32_t  clj_map_count(clj_value map);
// Borrowed (+0): valid while map is.
clj_value clj_map_get(clj_value map, clj_value key, clj_value not_found);
bool      clj_map_contains(clj_value map, clj_value key);

// map is consumed (+1 in) and may be updated in place when unique; retain it first to keep the old version.
// Either layout in, either out: an empty map takes a keyword key as a shape map, a shape map that cannot hold the
// key (shape.h) comes back as a hash map.
clj_value clj_map_assoc(clj_value map, clj_value key, clj_value val);
clj_value clj_map_dissoc(clj_value map, clj_value key);
// A map of n alternating keys and values, borrowed: through the key set's shape when every key is a keyword (the
// transition tree and its dictionary rule are for incremental building), else through the trie. A duplicate key
// answers CLJ_UNBOUND with *dup at its index when dup is given; with NULL the last value wins.
clj_value clj_map_from_items(const clj_value *items, uint32_t n, uint32_t *dup);
// The trie alone, for a caller that never wants a shape (a set's impl); map must be a hash map.
clj_value clj_hash_map_assoc(clj_value map, clj_value key, clj_value val);
clj_value clj_hash_map_dissoc(clj_value map, clj_value key);

// One entry's share of an unordered map hash; the sum of them mixed with the count is the map hash.
uint32_t clj_map_entry_hash(clj_value key, clj_value val);

// (reduce-kv f init map): (f acc k v) over the trie in place, stopping at a reduced result (reduce.h).
clj_value clj_map_reduce_kv(clj_value map, clj_value f, clj_value init);

// Return false to stop. Entries are borrowed. Order is unspecified for a hash map, the shape's for a shape map.
typedef bool (*clj_map_entry_fn)(clj_value key, clj_value val, void *ctx);
void clj_map_each(clj_value map, clj_map_entry_fn fn, void *ctx);

// Structural comparison of the tries; collision nodes compare as sets. Shape maps compare by shape.
bool clj_debug_map_same_shape(clj_value a, clj_value b);
// A hash map's trie root and cached hash (0 when none); a shape map has neither.
clj_value clj_debug_map_root(clj_value map);
uint32_t  clj_debug_map_cached_hash(clj_value map);

#endif
