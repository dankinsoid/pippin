// @ai-generated(solo)
#ifndef CLJ_STRING_H
#define CLJ_STRING_H

#include "object.h"

// Immutable UTF-8. bytes carry a trailing NUL for C callers; len counts embedded NULs too. Indexes count code points
// (design §4 «Строки»). A non-ASCII string past CLJ_STRING_CRUMB_BYTES carries a tail after its NUL: its code point
// count and the byte offset of every CLJ_STRING_CRUMB_STRIDE-th code point, both built on the first indexed access.
typedef struct {
	clj_header       h;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	uint32_t         len;
	char             bytes[];
} clj_string;

extern const clj_type clj_string_type;

// Header bit, set at creation and never cleared: every byte is below 0x80, so an index is a byte offset.
#define CLJ_STRING_ASCII ((uint32_t)1 << 10)
#define CLJ_STRING_CRUMB_BYTES  64u
#define CLJ_STRING_CRUMB_STRIDE 64u

// bytes may be NULL when len is 0.
clj_value clj_string_new(const char *bytes, size_t len);
clj_value clj_string_from_cstr(const char *s);

static inline bool        clj_is_string(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_string_type; }
static inline clj_string *clj_string_of(clj_value v) { return (clj_string *)clj_to_ptr(v); }
// Borrowed: valid while s is.
static inline const char *clj_string_bytes(clj_value s) { return clj_string_of(s)->bytes; }
static inline uint32_t    clj_string_len(clj_value s) { return clj_string_of(s)->len; }
static inline bool        clj_string_is_ascii(clj_value s) { return clj_header_of(s)->flags & CLJ_STRING_ASCII; }

// Decodes the code point at byte offset pos (< len) into *out; returns its byte length. Malformed
// bytes decode to whatever they hold rather than failing (the reader does not validate UTF-8, NOTES.md).
size_t clj_utf8_decode(const char *bytes, size_t len, size_t pos, uint32_t *out);
size_t clj_string_count_slow(clj_value s);
// Code points in a string: the length when ASCII, else cached past CLJ_STRING_CRUMB_BYTES.
static inline size_t clj_string_count(clj_value s) { return clj_string_is_ascii(s) ? clj_string_len(s) : clj_string_count_slow(s); }

// A code point starts at every byte that is not 10xxxxxx; malformed bytes count by that rule too.
size_t clj_string_offset_slow(clj_value s, size_t i);
size_t clj_string_index_slow(clj_value s, size_t pos);
// Byte offset of code point i; i <= clj_string_count(s), the caller checks.
static inline size_t clj_string_offset(clj_value s, size_t i) { return clj_string_is_ascii(s) ? i : clj_string_offset_slow(s, i); }
// Code points before byte offset pos <= len.
static inline size_t clj_string_index_at(clj_value s, size_t pos) { return clj_string_is_ascii(s) ? pos : clj_string_index_slow(s, pos); }
// Code point i, or false past the end.
bool clj_string_char_at(clj_value s, size_t i, uint32_t *out);

// The first occurrence of nd in hay, NULL when none; an empty nd is found at hay. memmem is not in C11.
const char *clj_bytes_find(const char *hay, size_t hlen, const char *nd, size_t nlen);

#endif
