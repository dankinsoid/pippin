// @ai-generated(solo)
// The keyword-lookup inline cache (NOTES.md "Shapes", the cache): what eval.c and the compiled helpers share.
#ifndef CLJ_SHAPE_INTERNAL_H
#define CLJ_SHAPE_INTERNAL_H

#include "clj/record.h"
#include "clj/shape.h"

#define CLJ_KW_IC_ENTRIES 4
#define CLJ_KW_IC_ABSENT UINT32_MAX

// One receiver layout the site saw and where its slot for the key is.
typedef struct {
	const void *key;    // the shape, or the record descriptor
	uint32_t    offset; // byte offset of the slot, or CLJ_KW_IC_ABSENT for a shape without the key
	uint32_t    index;  // the basis index of a record entry
	bool        record;
} clj_kw_entry;

// The cache key of a receiver: its shape, its record descriptor, or NULL for anything the cache does not cover.
static inline const void *clj_kw_key_of(clj_value m, bool *record) {
	*record = false;
	if (!clj_is_ptr(m)) return NULL;
	const clj_header *h = clj_header_of(m);
	if (h->flags & CLJ_FLAG_SHAPE) return ((const clj_shape_map *)h)->shape;
	if (h->type->core_bits & CLJ_CORE_RECORD) {
		*record = true;
		return h->type;
	}
	return NULL;
}

// A shape is immortal; a record descriptor's address may be reused after its death, so its basis is re-read.
static inline bool clj_kw_entry_hit(const clj_kw_entry *e, const void *key, clj_value kw) {
	if (e->key != key) return false;
	if (!e->record) return true;
	const clj_record_type *rt = key;
	return e->index < rt->ut.nfields && rt->basis[e->index] == kw;
}

// The slot value a hit answers, borrowed; not_found for an absent key.
static inline clj_value clj_kw_entry_read(const clj_kw_entry *e, clj_value m, clj_value not_found) {
	if (e->offset == CLJ_KW_IC_ABSENT) return not_found;
	return *(const clj_value *)((const char *)clj_to_ptr(m) + e->offset);
}

// False when m's layout is not cacheable: a hash map, nil, a record whose basis lacks the key (its extmap decides).
static inline bool clj_kw_entry_make(clj_kw_entry *e, clj_value m, clj_value key) {
	bool        record;
	const void *k = clj_kw_key_of(m, &record);
	if (!k) return false;
	if (record) {
		int32_t i = clj_record_basis_index((const clj_type *)k, key);
		if (i < 0) return false;
		*e = (clj_kw_entry){k, (uint32_t)(offsetof(clj_record, slots) + (size_t)i * sizeof(clj_value)), (uint32_t)i, true};
		return true;
	}
	int32_t i = clj_shape_index((const clj_shape *)k, key);
	*e = (clj_kw_entry){k, i < 0 ? CLJ_KW_IC_ABSENT : (uint32_t)(offsetof(clj_shape_map, slots) + (size_t)i * sizeof(clj_value)), 0, false};
	return true;
}

#endif
