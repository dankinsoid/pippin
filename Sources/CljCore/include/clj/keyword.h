// @ai-generated(solo)
#ifndef CLJ_KEYWORD_H
#define CLJ_KEYWORD_H

#include "object.h"
#include "symbol.h"

struct clj_shape;

// Interned and immortal: equal keywords are the same pointer, so clj_equals never reaches the type.
typedef struct {
	clj_header       h;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	clj_value        sym;
	// The shape of {kw v}, the root's transition on this keyword (shape.c); NULL until first used. The root has
	// as many transitions as the program has first keys, so its table is the keywords themselves.
	_Atomic(const struct clj_shape *) shape1;
} clj_keyword;

extern const clj_type clj_keyword_type;

// ns is a string or nil, name a string. The result is immortal: retain/release are no-ops.
clj_value clj_keyword_intern(clj_value ns, clj_value name);
// Splits like clj_symbol_from_cstr.
clj_value clj_keyword_from_cstr(const char *s);

static inline bool         clj_is_keyword(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_keyword_type; }
static inline clj_keyword *clj_keyword_of(clj_value v) { return (clj_keyword *)clj_to_ptr(v); }
// Borrowed; the keyword never dies, so effectively forever.
static inline clj_value clj_keyword_ns(clj_value k) { return clj_symbol_ns(clj_keyword_of(k)->sym); }
static inline clj_value clj_keyword_name(clj_value k) { return clj_symbol_name(clj_keyword_of(k)->sym); }

#endif
