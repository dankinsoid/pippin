// @ai-generated(solo)
#ifndef CLJ_VECTOR_H
#define CLJ_VECTOR_H

#include "object.h"

// Two private layouts of one type: Clojure's 32-way trie plus tail, and a tuple of up to six inline (NOTES "Vector").
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
// clj_map_get_take's vector: get's answer, or the item moved out when the vector, its path and the item are unique.
clj_value clj_vector_get_take(clj_value vec, clj_value key, clj_value not_found, bool *taken);
// Aborts on an empty vector.
clj_value clj_vector_pop(clj_value vec);

// Items are borrowed; the array may be freed after the call.
clj_value clj_vector_from_array(const clj_value *items, uint32_t n);

// The reduce slot from index `from` (reduce.h); the vector-seq view shares it. (f acc i x) for reduce-kv.
clj_value clj_vector_reduce_from(clj_value vec, uint32_t from, clj_value f, clj_value init);
clj_value clj_vector_reduce_kv(clj_value vec, clj_value f, clj_value init);

// The run of items holding item i, borrowed: items start..end-1, a trie's leaf or a tuple's whole, read without a call.
const clj_slot *clj_vector_run(clj_value vec, uint32_t i, uint32_t *start, uint32_t *end);

// Return false to stop. Items are borrowed and visited in order.
typedef bool (*clj_vector_item_fn)(clj_value item, void *ctx);
void clj_vector_each(clj_value vec, clj_vector_item_fn fn, void *ctx);

static inline bool clj_is_vector(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_vector_type; }

// The head both layouts share and a tuple's items right after it (vector.c): what a compiled site's fast arm of
// count, nth and first reads without a call (design §6 «Инлайнинг без JIT»).
typedef struct {
	clj_header       h;
	uint32_t         count;
	uint32_t         shift; // CLJ_VECTOR_TUPLE_SHIFT for a tuple, whose items follow meta
	_Atomic uint32_t hash;
	clj_slot         meta;
	clj_slot         items[];
} clj_vector_head;
enum { CLJ_VECTOR_TUPLE_SHIFT = 0 };

static inline const clj_vector_head *clj_vector_head_of(clj_value v) { return (const clj_vector_head *)clj_to_ptr(v); }

// Process-wide switch, the bench's control: off, clj_vector_from_array builds tries (existing tuples keep working).
void clj_tuples_enable(bool on);
bool clj_tuples_enabled(void);
bool clj_vector_is_tuple(clj_value vec);

// Where vectors got their layout, counted in debug builds (-1 otherwise).
enum {
	CLJ_VECTORS_TUPLE,     // clj_vector_from_array of 1 to 6 items: a literal, a map entry, `vector`
	CLJ_VECTORS_PROMOTED,  // a conj past a full tuple
	CLJ_VECTORS_TRIE_CONJ, // a conj onto the empty vector, which stays a trie
	CLJ_VECTORS_TRIE_OFF,  // tuples switched off
	CLJ_VECTORS_COUNTERS
};
void clj_debug_vector_stats(int64_t out[CLJ_VECTORS_COUNTERS]);

// Borrowed root and tail nodes, nil for a tuple: pointer identity shows in-place reuse in tests.
clj_value clj_debug_vector_root(clj_value vec);
clj_value clj_debug_vector_tail(clj_value vec);
// Raw cache slot, see clj_debug_cached_hash.
uint32_t clj_debug_vector_cached_hash(clj_value vec);

#endif
