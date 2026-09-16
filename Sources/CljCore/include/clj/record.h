// @ai-generated(solo)
#ifndef CLJ_RECORD_H
#define CLJ_RECORD_H

#include "map.h"
#include "proto.h"

// The design's named shape (§4): the hash map's bits and slots over a basis kept inline.
typedef struct {
	clj_user_type ut;
	clj_value     basis[]; // interned keywords, one per field, declaration order; immortal, so never visited
} clj_record_type;

// A deftype instance with two trailing slots, so field* and the deftype method bodies read the basis unchanged.
typedef struct {
	clj_header h;
	clj_value  slots[]; // nfields basis values, then extmap (nil or a non-empty hash map), then meta
} clj_record;

// impls as deftype* takes them, minus the core interfaces: the map ones are the record's own.
clj_value clj_record_type_new(clj_value name, clj_value fields, const clj_value *impls, size_t nimpls);
// n must equal the type's nfields. Values are retained.
clj_value clj_record_new(clj_value type, const clj_value *vals, size_t n);
// map->Name: basis keys take their slot, the rest go to the extmap, a missing basis key is nil.
clj_value clj_record_from_map(clj_value type, clj_value m);

static inline bool clj_is_record(clj_value v) { return clj_has_core(v, CLJ_CORE_RECORD); }
static inline bool clj_is_record_type(clj_value type) {
	return clj_is_user_type(type) && (((const clj_user_type *)clj_to_ptr(type))->t.core_bits & CLJ_CORE_RECORD);
}

// -1 when key is not one of the basis keywords.
int32_t clj_record_basis_index(const clj_type *t, clj_value key);
// Borrowed; aborts out of range.
clj_value clj_record_field(clj_value r, uint32_t i);
uint32_t  clj_record_count(clj_value r);
// A plain hash map of every entry, keeping the record's meta: what (dissoc r basis-key) answers.
clj_value clj_record_to_map(clj_value r);
clj_value clj_record_reduce_kv(clj_value r, clj_value f, clj_value init);
// Return false to stop. Entries are borrowed; basis fields in declaration order, then the extmap.
void clj_record_each(clj_value r, clj_map_entry_fn fn, void *ctx);

void clj_record_install(void);

#endif
