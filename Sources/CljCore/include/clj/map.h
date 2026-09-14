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

extern const clj_type clj_map_type;

// Immortal singleton.
clj_value clj_map_empty(void);
uint32_t  clj_map_count(clj_value map);
// Borrowed (+0): valid while map is.
clj_value clj_map_get(clj_value map, clj_value key, clj_value not_found);
bool      clj_map_contains(clj_value map, clj_value key);

// map is consumed (+1 in) and may be updated in place when unique; retain it first to keep the old version.
clj_value clj_map_assoc(clj_value map, clj_value key, clj_value val);
clj_value clj_map_dissoc(clj_value map, clj_value key);

// Return false to stop. Entries are borrowed. Order is unspecified.
typedef bool (*clj_map_entry_fn)(clj_value key, clj_value val, void *ctx);
void clj_map_each(clj_value map, clj_map_entry_fn fn, void *ctx);

static inline bool     clj_is_map(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_map_type; }
static inline clj_map *clj_map_of(clj_value v) { return (clj_map *)clj_to_ptr(v); }

// Structural comparison of the tries; collision nodes compare as sets.
bool clj_debug_map_same_shape(clj_value a, clj_value b);

#endif
