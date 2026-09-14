// @ai-generated(solo)
#ifndef CLJ_SYMBOL_H
#define CLJ_SYMBOL_H

#include "object.h"

// Not interned. No metadata slot yet (NOTES.md).
typedef struct {
	clj_header       h;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	clj_value        ns;   // string or nil
	clj_value        name; // string
} clj_symbol;

extern const clj_type clj_symbol_type;

clj_value clj_symbol_new(clj_value ns, clj_value name);
// Clojure's split: at the first '/', so "a/b/c" is ns "a" name "b/c"; "/" alone is the symbol `/`.
clj_value clj_symbol_from_cstr(const char *s);

static inline bool        clj_is_symbol(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_symbol_type; }
static inline clj_symbol *clj_symbol_of(clj_value v) { return (clj_symbol *)clj_to_ptr(v); }
// Borrowed: valid while s is.
static inline clj_value clj_symbol_ns(clj_value s) { return clj_symbol_of(s)->ns; }
static inline clj_value clj_symbol_name(clj_value s) { return clj_symbol_of(s)->name; }

// Process-wide counter behind gensym and the reader's auto-gensym; the first call returns 1.
uint64_t clj_next_id(void);

#endif
