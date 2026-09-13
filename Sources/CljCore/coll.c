// @ai-generated(guided)
#include "clj/coll.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/list.h"
#include "clj/map.h"
#include "clj/string.h"
#include "clj/vector.h"

static bool is_map(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_map_type; }

// Code point at index i of a UTF-8 string, or false past the end.
static bool string_nth(clj_value s, size_t i, uint32_t *out) {
	const unsigned char *p = (const unsigned char *)clj_string_bytes(s);
	size_t               n = clj_string_len(s), pos = 0;
	for (size_t k = 0; pos < n; k++) {
		size_t len = p[pos] < 0x80 ? 1 : (p[pos] & 0xE0) == 0xC0 ? 2 : (p[pos] & 0xF0) == 0xE0 ? 3 : 4;
		if (k == i) {
			uint32_t cp = len == 1 ? p[pos] : p[pos] & (0xFF >> (len + 1));
			for (size_t j = 1; j < len && pos + j < n; j++) cp = (cp << 6) | (p[pos + j] & 0x3F);
			*out = cp;
			return true;
		}
		pos += len;
	}
	return false;
}

static size_t string_count(clj_value s) {
	const unsigned char *p = (const unsigned char *)clj_string_bytes(s);
	size_t               n = clj_string_len(s), count = 0;
	for (size_t i = 0; i < n; i++) count += (p[i] & 0xC0) != 0x80;
	return count;
}

clj_value clj_get(clj_value coll, clj_value key, clj_value not_found) {
	if (is_map(coll)) return clj_retain(clj_map_get(coll, key, not_found));
	if (clj_is_vector(coll) && clj_is_fixnum(key)) {
		intptr_t i = clj_fixnum_val(key);
		if (i >= 0 && (uintptr_t)i < clj_vector_count(coll)) return clj_retain(clj_vector_nth(coll, (uint32_t)i));
		return clj_retain(not_found);
	}
	if (clj_is_string(coll) && clj_is_fixnum(key)) {
		uint32_t cp;
		if (clj_fixnum_val(key) >= 0 && string_nth(coll, (size_t)clj_fixnum_val(key), &cp)) return clj_char(cp);
		return clj_retain(not_found);
	}
	return clj_retain(not_found);
}

clj_value clj_nth(clj_value coll, clj_value index, bool has_not_found, clj_value not_found) {
	if (!clj_is_fixnum(index)) return clj_throw_msg("Key must be integer");
	intptr_t i = clj_fixnum_val(index);
	size_t   count;
	if (clj_is_nil(coll)) return CLJ_NIL;
	if (clj_is_vector(coll)) {
		count = clj_vector_count(coll);
		if (i >= 0 && (size_t)i < count) return clj_retain(clj_vector_nth(coll, (uint32_t)i));
	} else if (clj_is_list(coll)) {
		clj_seq_iter it = clj_seq_iter_start(coll);
		clj_value    item;
		count = 0;
		while (clj_seq_iter_next(&it, &item)) {
			if ((intptr_t)count == i) return clj_retain(item);
			count++;
		}
	} else if (clj_is_string(coll)) {
		uint32_t cp;
		if (i >= 0 && string_nth(coll, (size_t)i, &cp)) return clj_char(cp);
		count = string_count(coll);
	} else {
		return clj_throw_msg("nth not supported on this type: %s", clj_type_name(coll));
	}
	if (has_not_found) return clj_retain(not_found);
	return clj_throw_msg("Index %lld out of bounds for length %zu", (long long)i, count);
}
