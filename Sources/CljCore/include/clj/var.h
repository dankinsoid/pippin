// @ai-generated(guided)
#ifndef CLJ_VAR_H
#define CLJ_VAR_H

#include "object.h"

// Immortal: a var lives for the process, like the name that reaches it. Made through clj_ns_intern.
typedef struct {
	clj_header        h;
	clj_value         ns;   // symbol
	clj_value         name; // symbol
	_Atomic clj_value root; // CLJ_UNBOUND until the first def
} clj_var;

extern const clj_type clj_var_type;

clj_value clj_var_new(clj_value ns, clj_value name);

static inline bool     clj_is_var(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_var_type; }
static inline clj_var *clj_var_of(clj_value v) { return (clj_var *)clj_to_ptr(v); }
// Borrowed: the var never dies.
static inline clj_value clj_var_ns(clj_value var) { return clj_var_of(var)->ns; }
static inline clj_value clj_var_name(clj_value var) { return clj_var_of(var)->name; }

// Borrowed root, CLJ_UNBOUND when unbound. Not safe against a concurrent def (NOTES.md).
clj_value clj_var_root(clj_value var);
static inline bool clj_var_is_bound(clj_value var) { return clj_var_root(var) != CLJ_UNBOUND; }
// Shares and retains val (a var is reachable from every thread), releases the previous root.
void clj_var_bind_root(clj_value var, clj_value val);
// Owned root; throws when unbound.
clj_value clj_var_deref(clj_value var);

#endif
