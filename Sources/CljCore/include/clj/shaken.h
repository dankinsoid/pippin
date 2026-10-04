// @ai-generated(solo)
// The root a --closed tree-shaken def is left with (NOTES.md, "Compiler": tree shaking).
#ifndef CLJ_SHAKEN_H
#define CLJ_SHAKEN_H

#include "object.h"

// A type of its own, not a fn: fn? answers false by identity, ifn? stays true, and every use aborts named.
extern const clj_type clj_shaken_type;

// var is borrowed: vars are immortal, and so is the tripwire bound to one as a root.
clj_value clj_shaken_new(clj_value var);

static inline bool clj_is_shaken(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_shaken_type; }
// The var the tripwire names: what the printer and the fatal spell out.
clj_value clj_shaken_var(clj_value v);

#endif
