// @ai-generated(guided)
#ifndef CLJ_COLL_H
#define CLJ_COLL_H

#include "object.h"

// Polymorphic collection operations with Clojure semantics: arguments borrowed, results owned or CLJ_THROWN.

// map, vector (by index), nil; any other collection yields not_found.
clj_value clj_get(clj_value coll, clj_value key, clj_value not_found);
// vector, list, string (code points). Out of range throws, or yields not_found when has_not_found.
clj_value clj_nth(clj_value coll, clj_value index, bool has_not_found, clj_value not_found);

#endif
