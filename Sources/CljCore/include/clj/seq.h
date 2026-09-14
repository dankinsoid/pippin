// @ai-generated(guided)
#ifndef CLJ_SEQ_H
#define CLJ_SEQ_H

#include <stdatomic.h>

#include "object.h"

// Seq types on the descriptor slots: views over a vector and a string, a fixnum range and the lazy seq.
// All carry the ASeq trait (coll.h): sequential equality and hash, conj as cons.

// (seq v) as an O(1) view; next is a new view one index on.
typedef struct {
	clj_header h;
	uint32_t   i;
	clj_value  vec;
} clj_vector_seq;

extern const clj_type clj_vector_seq_type;

// i < count. vec is retained.
clj_value clj_vector_seq_new(clj_value vec, uint32_t i);

// Code points of a string from byte offset pos.
typedef struct {
	clj_header h;
	uint32_t   pos;
	clj_value  str;
} clj_string_seq;

extern const clj_type clj_string_seq_type;

// pos < len, at a code point boundary. str is retained.
clj_value clj_string_seq_new(clj_value str, uint32_t pos);

// [start, end) by step, all fixnums. Never empty and step never 0: the constructor returns () instead.
typedef struct {
	clj_header h;
	intptr_t   start, end, step;
} clj_range;

extern const clj_type clj_range_type;

// () when the range is empty. Aborts on step 0; the caller maps that to Clojure's infinite repeat.
clj_value clj_range_new(intptr_t start, intptr_t end, intptr_t step);

// A thunk forced at most once; the realized seq is cached for the object's life, so a walk may borrow
// it. Nested lazy seqs are unwrapped iteratively (a thunk returning a lazy seq does not recurse).
// state: 0 unforced, 1 forcing, 2 forced. Forcing a shared object claims it with a CAS and other
// threads spin until the value is published; a thunk that forces its own object throws.
typedef struct {
	clj_header       h;
	_Atomic uint32_t state;
	clj_value        fn;    // thunk; nil once forced
	clj_value        value; // realized seq or nil; meaningful once forced
} clj_lazy_seq;

extern const clj_type clj_lazy_seq_type;

// fn is retained and called with no arguments; its result is seq'd.
clj_value clj_lazy_seq_new(clj_value fn);
// Borrowed realized seq (nil when empty), valid while ls is. CLJ_THROWN when the thunk throws; the
// object stays unforced and a later force runs the thunk again.
clj_value clj_lazy_seq_force(clj_value ls);
bool clj_lazy_seq_realized(clj_value ls);

static inline bool clj_is_lazy_seq(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_lazy_seq_type; }

static inline clj_vector_seq *clj_vector_seq_of(clj_value v) { return (clj_vector_seq *)clj_to_ptr(v); }
static inline clj_string_seq *clj_string_seq_of(clj_value v) { return (clj_string_seq *)clj_to_ptr(v); }
static inline clj_range      *clj_range_of(clj_value v) { return (clj_range *)clj_to_ptr(v); }
static inline clj_lazy_seq   *clj_lazy_seq_of(clj_value v) { return (clj_lazy_seq *)clj_to_ptr(v); }

#endif
