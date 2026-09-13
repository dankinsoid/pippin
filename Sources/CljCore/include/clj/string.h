// @ai-generated(solo)
#ifndef CLJ_STRING_H
#define CLJ_STRING_H

#include "object.h"

// Immutable UTF-8. bytes carry a trailing NUL for C callers; len counts embedded NULs too.
typedef struct {
	clj_header       h;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	uint32_t         len;
	char             bytes[];
} clj_string;

extern const clj_type clj_string_type;

// bytes may be NULL when len is 0.
clj_value clj_string_new(const char *bytes, size_t len);
clj_value clj_string_from_cstr(const char *s);

static inline bool        clj_is_string(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_string_type; }
static inline clj_string *clj_string_of(clj_value v) { return (clj_string *)clj_to_ptr(v); }
// Borrowed: valid while s is.
static inline const char *clj_string_bytes(clj_value s) { return clj_string_of(s)->bytes; }
static inline uint32_t    clj_string_len(clj_value s) { return clj_string_of(s)->len; }

#endif
