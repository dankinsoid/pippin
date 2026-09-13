// @ai-generated(solo)
#include "clj/core.h"

const char *clj_type_name(clj_value v) {
	if (clj_is_nil(v)) return "nil";
	if (clj_is_fixnum(v)) return "fixnum";
	if (clj_is_bool(v)) return "boolean";
	if (clj_is_char(v)) return "char";
	if (!clj_is_ptr(v)) return "special";
	return clj_header_of(v)->type->name;
}

uint32_t clj_debug_cached_hash(clj_value v) {
	if (clj_is_string(v)) return clj_hash_cache_load(&clj_string_of(v)->hash);
	if (clj_is_symbol(v)) return clj_hash_cache_load(&clj_symbol_of(v)->hash);
	if (clj_is_keyword(v)) return clj_hash_cache_load(&clj_keyword_of(v)->hash);
	if (clj_is_ptr(v) && clj_header_of(v)->type == &clj_map_type) return clj_hash_cache_load(&clj_map_of(v)->hash);
	if (clj_is_vector(v)) return clj_debug_vector_cached_hash(v);
	clj_fatal("value of a type without a hash cache");
}

uint32_t (*clj_debug_hash_override)(clj_value v);

void clj_debug_set_hash_override(uint32_t (*fn)(clj_value v)) {
	clj_debug_hash_override = fn;
}

uint32_t clj_mix_coll_hash(uint32_t hash, uint32_t count) {
	uint32_t k1 = hash * 0xcc9e2d51;
	k1 = (k1 << 15) | (k1 >> 17);
	k1 *= 0x1b873593;
	uint32_t h1 = k1;
	h1 = (h1 << 13) | (h1 >> 19);
	h1 = h1 * 5 + 0xe6546b64;
	return clj_fmix32(h1 ^ count);
}

uint32_t clj_hash_slow(clj_value v) {
	const clj_type *t = clj_header_of(v)->type;
	if (!t->hash) clj_fatal("value of a type without hash used as a key");
	return t->hash(clj_to_ptr(v));
}

bool clj_equals_slow(clj_value a, clj_value b) {
	clj_value self = clj_is_ptr(a) ? a : b;
	clj_value other = self == a ? b : a;
	const clj_type *t = clj_header_of(self)->type;
	if (!t->equals) clj_fatal("value of a type without equals compared");
	return t->equals(clj_to_ptr(self), other);
}
