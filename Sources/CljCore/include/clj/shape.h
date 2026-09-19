// @ai-generated(solo)
#ifndef CLJ_SHAPE_H
#define CLJ_SHAPE_H

#include "map.h"

// The map type's second layout: keyword keys in an interned immortal shape, values inline (NOTES.md "Shapes").

#define CLJ_SHAPE_MAX_KEYS 32
// Distinct assoc transitions a shape may take before it is dictionary-like (the root is exempt).
#define CLJ_SHAPE_MAX_CHILDREN 8
// Shapes per process; past it every new key set is a hash map.
#define CLJ_SHAPE_MAX_SHAPES 65536

uint32_t  clj_shape_nkeys(const clj_shape *s);
// Borrowed; the keyword is immortal.
clj_value clj_shape_key(const clj_shape *s, uint32_t i);
// Slot of key in the shape, -1 when absent. A pointer scan: keys are interned keywords.
int32_t   clj_shape_index(const clj_shape *s, clj_value key);
bool      clj_shape_is_dictionary(const clj_shape *s);
const clj_shape *clj_shape_root(void);

// NULL when the keys do not qualify (a non-keyword, a duplicate, more than the cap) or the shape cap is reached.
const clj_shape *clj_shape_for_keys(const clj_value *keys, uint32_t n);
// A shape map of the shape over nkeys values in slot order, retained.
clj_value clj_shape_map_new(const clj_shape *s, const clj_value *vals);
// The same with vals[i] the value of keys[i]: the shape's keys in any order (a canonical shape may reorder a literal).
clj_value clj_shape_map_from_keys(const clj_shape *s, const clj_value *keys, const clj_value *vals);
// The slot of each of the shape's keys given in any order, for a site that builds the same literal repeatedly.
void clj_shape_slots_of(const clj_shape *s, const clj_value *keys, uint8_t *slots);
// A zeroed shape map of the shape whose slots the caller fills with owned values.
clj_value clj_shape_map_alloc(const clj_shape *s);

// The shape map operations behind clj_map_assoc/dissoc: map is a shape map, consumed; the result may be a hash map.
clj_value clj_shape_map_assoc(clj_value map, clj_value key, clj_value val);
clj_value clj_shape_map_dissoc(clj_value map, clj_value key);
// A one-key shape map from the root, or CLJ_UNBOUND when shapes are off, key is no keyword or the cap is reached.
clj_value clj_shape_map_single(clj_value key, clj_value val);
// The same entries as a hash map; consumes map.
clj_value clj_shape_map_to_hash_map(clj_value map);

// Process-wide switch: off, no new shape map is made (existing ones keep working). On by default.
void clj_shapes_enable(bool on);
bool clj_shapes_enabled(void);

// Shapes made so far, a shape's distinct assoc transitions, and the bytes the shapes and transitions hold.
uint32_t clj_debug_shape_count(void);
uint32_t clj_debug_shape_children(const clj_shape *s);
size_t   clj_debug_shape_bytes(void);
// Where maps got their layout, counted in debug builds (-1 otherwise): shape maps from a key set (a literal, the
// reader, hash-map) or from the root's transition (assoc into an empty map), and tries by the reason a shape map
// was refused or given up. A trie's own descendants are not counted again.
enum {
	CLJ_MAPS_SHAPE_SET,
	CLJ_MAPS_SHAPE_EDGE,
	CLJ_MAPS_TRIE_KEY,  // a non-keyword or nil key
	CLJ_MAPS_TRIE_META, // with-meta on a shape map
	CLJ_MAPS_TRIE_FULL, // the 33rd key
	CLJ_MAPS_TRIE_DICT, // a transition through a dictionary-like shape
	CLJ_MAPS_TRIE_CAP,  // the shape cap
	CLJ_MAPS_TRIE_OFF,  // shapes switched off
	CLJ_MAPS_COUNTERS
};
void clj_debug_map_stats(int64_t out[CLJ_MAPS_COUNTERS]);
// Counts one of the trie reasons above (map.c's with-meta).
void clj_debug_map_generic(int reason);

#endif
