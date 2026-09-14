// @ai-generated(guided)
#ifndef CLJ_NS_H
#define CLJ_NS_H

#include "object.h"

// Immortal, like vars. Every lookup and mutation goes through one process-wide mutex.
typedef struct {
	clj_header h;
	clj_value  name;     // symbol
	clj_value  mappings; // map symbol → var interned here
	clj_value  refers;   // map symbol → var referred from other namespaces
} clj_ns;

extern const clj_type clj_ns_type;

static inline bool    clj_is_ns(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_ns_type; }
static inline clj_ns *clj_ns_of(clj_value v) { return (clj_ns *)clj_to_ptr(v); }
// Borrowed: the namespace never dies.
static inline clj_value clj_ns_name(clj_value ns) { return clj_ns_of(ns)->name; }

// name is an unqualified symbol. Borrowed results; nil when not found.
clj_value clj_ns_find(clj_value name);
clj_value clj_ns_find_or_create(clj_value name);
// The var mapped to sym in ns, created unbound when missing. sym must be unqualified.
clj_value clj_ns_intern(clj_value ns, clj_value sym);
void      clj_ns_refer(clj_value ns, clj_value sym, clj_value var);
// Unqualified: own mappings, then refers, then clojure.core minus its private vars. Qualified: the named
// namespace's own mappings, private or not (the analyzer refuses the private ones). nil when unresolved.
clj_value clj_ns_resolve(clj_value ns, clj_value sym);

// Per thread; `user` until set.
clj_value clj_ns_current(void);
void      clj_ns_set_current(clj_value ns);
clj_value clj_ns_core(void);
clj_value clj_ns_user(void);

#endif
