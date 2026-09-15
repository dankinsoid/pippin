// @ai-generated(guided)
#ifndef CLJ_REDUCE_H
#define CLJ_REDUCE_H

#include "box.h"
#include "coll.h"
#include "eval.h"

// The step every reduce slot shares: acc starts as init (CLJ_UNBOUND: the first item seeds it), each item
// goes through (f acc item) — or (f acc k v) for a reducer started with 3 — and a `reduced` result or a
// throw ends the walk with the result final. f is borrowed for the reducer's life.
typedef struct {
	clj_call  call;
	clj_value acc; // owned; CLJ_UNBOUND until seeded, CLJ_THROWN once a step threw
	clj_value args[3];
} clj_reducer;

clj_reducer clj_reducer_start(clj_value f, clj_value init, size_t nargs);
// false when the walk must stop: the result is final (reduced) or the step threw.
bool clj_reducer_step(clj_reducer *r, clj_value item);
bool clj_reducer_step_kv(clj_reducer *r, clj_value key, clj_value val);
// Owned result: the accumulator, or (f) when nothing seeded it; CLJ_THROWN after a throw.
clj_value clj_reducer_finish(clj_reducer *r);
// Releases the accumulator of a walk the caller abandons (its source threw).
void clj_reducer_drop(clj_reducer *r);

// (reduce f init coll): through the type's reduce slot, else over its seq with clj_seq_iter; a `reduced` init
// is its value at once. init CLJ_UNBOUND is the 2-arity: the first element seeds, (f) answers an empty coll.
// Owned result or CLJ_THROWN ("Don't know how to create ISeq from" for a non-seqable).
clj_value clj_reduce(clj_value f, clj_value init, clj_value coll);
// (reduce-kv f init coll) for a map (f acc k v) or a vector (f acc i x); nil yields init.
clj_value clj_reduce_kv(clj_value f, clj_value init, clj_value coll);
// The seq-walking reducer the slotless types and the cons/lazy-seq/string slots use: closes the iterator on an early stop.
clj_value clj_reduce_iter(clj_value coll, clj_value f, clj_value init);
// The reduce of nothing: init, or (f) for the 2-arity.
clj_value clj_reduce_empty(clj_value f, clj_value init);

#endif
