// @ai-generated(solo)
#ifndef CLJ_COMPARE_H
#define CLJ_COMPARE_H

#include "eval.h"
#include "object.h"

// Clojure `compare` in C: -1, 0 or 1 into *out, or CLJ_THROWN with the exception pending for a pair
// that has no order. nil is below everything, false below true, numbers compare numerically (a NaN is
// equal to any number, as Numbers.compare has it), chars and strings by code point, keywords and
// symbols by namespace (an unqualified one first) then name, vectors by count then item by item.
// Two values of any other type compare equal only as the same object.
clj_value clj_compare(clj_value a, clj_value b, int *out);

// A comparator prepared once (eval.h) for a driver that compares per element: a call whose f is nil is
// clj_compare, a fn answers a number or, as a predicate, logical true when its first argument sorts
// first, the way AFunction.compare reads a fn comparator. false leaves the exception pending.
bool clj_compare_with(const clj_call *call, clj_value a, clj_value b, int *out);

// (sort coll) and (sort cmp coll): the items of any seqable as a list, ascending and stable; cmp nil is
// clj_compare. Owned result or CLJ_THROWN.
clj_value clj_sort(clj_value coll, clj_value cmp);
// (sort-by keyfn coll) and (sort-by keyfn cmp coll): keyfn is applied per comparison, as Clojure does.
clj_value clj_sort_by(clj_value coll, clj_value keyfn, clj_value cmp);

#endif
