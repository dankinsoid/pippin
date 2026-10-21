// @ai-generated(solo)
#ifndef CLJ_STRING_H
#define CLJ_STRING_H

#include "object.h"

// Immutable UTF-8. bytes carry a trailing NUL for C callers; len counts embedded NULs too. Indexes count code points
// (design §4 «Строки»). A non-ASCII string past CLJ_STRING_CRUMB_BYTES carries a tail after its NUL: its code point
// count and the byte offset of every CLJ_STRING_CRUMB_STRIDE-th code point, both built on the first indexed access.
// The layout is string.c's: a short string is to live in the value word (tag 100) with no bytes in memory, so every
// other reader borrows them (clj_string_borrow). Past the inline accessors below the struct's name is poisoned.
typedef struct {
	clj_header       h;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	uint32_t         len;
	char             bytes[];
} clj_string;

extern const clj_type clj_string_type;

// Header bit, set at creation, cleared only by an append that brings a byte past 0x7F: an index is a byte offset.
#define CLJ_STRING_ASCII ((uint32_t)1 << 10)
#define CLJ_STRING_CRUMB_BYTES  64u
#define CLJ_STRING_CRUMB_STRIDE 64u

// bytes may be NULL when len is 0.
CLJ_NO_CYCLE clj_value clj_string_new(const char *bytes, size_t len);
CLJ_NO_CYCLE clj_value clj_string_from_cstr(const char *s);
// s with n more bytes, s consumed: unique and unshared (clj_is_unique_unshared), so it grows in place, moving only
// when its cell is full; bytes must not point into s.
clj_value clj_string_append(clj_value s, const char *bytes, size_t n);

static inline bool     clj_is_string(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_string_type; }
static inline uint32_t clj_string_len(clj_value s) { return ((const clj_string *)clj_to_ptr(s))->len; }
static inline bool     clj_string_is_ascii(clj_value s) { return clj_header_of(s)->flags & CLJ_STRING_ASCII; }

// A string's bytes, borrowed; bytes[len] is NUL.
typedef struct {
	const char *bytes;
	size_t      len;
} clj_str;

// Where a string kept in the value word unpacks: 7 bytes and the NUL.
typedef struct {
	char b[8];
} clj_str_buf;

// Valid while s lives and buf is in scope; never across a park, never to another thread (clj_string_materialize).
static inline clj_str clj_string_borrow(clj_value s, clj_str_buf *buf) {
	(void)buf;
	const clj_string *o = (const clj_string *)clj_to_ptr(s);
	return (clj_str){o->bytes, o->len};
}

// clj_string_borrow into a buffer that lives to the end of the enclosing block, not of the statement: a pointer
// taken from it must not be kept past that block, nor returned.
#define CLJ_STR(s) clj_string_borrow((s), &(clj_str_buf){{0}})

// A heap string equal to s, +1: the one form whose bytes outlive a borrow (across a park, on a pool thread, in a
// struct that keeps a C string). Every string is on the heap so far, so this is s retained.
static inline clj_value clj_string_materialize(clj_value s) { return clj_retain(s); }
// The bytes of a heap string, NUL-terminated, for as long as it lives: one clj_string_materialize returned, or any
// string where a `char *` leaves for C with no owner (objc.c's '*', which must materialize once strings can be small).
static inline const char *clj_string_heap_bytes(clj_value h) { return ((const clj_string *)clj_to_ptr(h))->bytes; }

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

// clj_hash_cache_load of the string's hash: 0 until computed.
uint32_t clj_string_cached_hash(clj_value s);

// The first occurrence of nd in hay, NULL when none; an empty nd is found at hay. memmem is not in C11.
const char *clj_bytes_find(const char *hay, size_t hlen, const char *nd, size_t nlen);

#ifndef CLJ_STRING_LAYOUT
#define clj_string       clj_string_layout_is_private_to_string_c
#undef CLJ_STRING_ASCII
#endif

#endif
