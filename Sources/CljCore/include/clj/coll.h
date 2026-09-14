// @ai-generated(guided)
#ifndef CLJ_COLL_H
#define CLJ_COLL_H

#include "object.h"

// Polymorphic collection operations with Clojure semantics, dispatched through the descriptor slots:
// arguments borrowed, results owned or CLJ_THROWN.

static inline bool clj_is_seq(clj_value v) { return clj_has_core(v, CLJ_CORE_SEQ); }
static inline bool clj_is_seqable(clj_value v) { return clj_is_nil(v) || clj_has_core(v, CLJ_CORE_SEQABLE); }

// nil for nil or an empty collection; "Don't know how to create ISeq from" for anything without a seq slot.
clj_value clj_seq(clj_value coll);
clj_value clj_first(clj_value coll);
clj_value clj_next(clj_value coll);
// () when nothing remains. A cons hands out its tail unrealized, as Clojure's more().
clj_value clj_rest(clj_value coll);
// (cons x coll): a seq is kept as the tail as is, anything else is seq'd first.
clj_value clj_seq_cons(clj_value x, clj_value coll);
// Fixnum. Without a count slot the seq is walked, which realizes a lazy seq.
clj_value clj_count(clj_value coll);
// Consumes coll (+1 in): a unique collection may be updated in place. nil starts a list.
clj_value clj_conj(clj_value coll, clj_value x);
// not_found for nil and for types without lookup.
clj_value clj_get(clj_value coll, clj_value key, clj_value not_found);
// Indexed types, strings (code points) and sequential seqs (walked). Out of range throws, or yields not_found when has_not_found.
clj_value clj_nth(clj_value coll, clj_value index, bool has_not_found, clj_value not_found);

// Walks any seqable: nil, (), a cons chain, a vector, a string and the seq types of seq.h inline, and any
// other type (a deftype/reify seq, a map) through its slots. Items are borrowed and stay valid while the
// walked value is — a cons chain and a realized lazy seq keep their elements alive, a view keeps its backing
// collection — except that an item a first slot yielded lives only until the next step or close (`slots`).
typedef struct {
	clj_value cur;    // borrowed
	uintptr_t pos;    // index into a vector, byte offset into a string, or the current value of a range
	bool      thrown; // a lazy seq's thunk or a slot threw; the exception is pending and the walk is over
	bool      slots;  // set once a first/next slot was used: from then on items are owned by the iterator
	bool      yielded; // cur's first is out; the next step calls its next slot
	clj_value held;   // owned: the seq cur borrows from when a slot produced it, else nil
	clj_value item;   // owned: the last item a first slot yielded, else nil
} clj_seq_iter;

clj_seq_iter clj_seq_iter_start(clj_value seq);
bool         clj_seq_iter_next(clj_seq_iter *it, clj_value *out);
// Releases what the iterator holds; needed only when a walk stops before next returned false. Idempotent.
void clj_seq_iter_close(clj_seq_iter *it);

// Borrowed items of any seqable in a malloc'd array; *keep holds them alive and is owned by the caller.
// NULL with the exception pending when coll is not seqable or realizing it throws.
clj_value *clj_seq_items(clj_value coll, size_t *n, clj_value *keep);

// ---- ASeq trait: the slots every seq type shares. Designated initializers cannot be overridden, so the
// trait carries the common slots and each descriptor adds its own seq/first/next/count.
bool     clj_seq_equals(clj_value a, clj_value b);
uint32_t clj_seq_hash(clj_value seq);

uint32_t  clj_aseq_hash(void *self);
bool      clj_aseq_equals(void *self, clj_value other);
clj_value clj_aseq_conj(clj_value self, clj_value x);
// seq of a seq is itself.
clj_value clj_aseq_seq(clj_value self);

#define CLJ_ASEQ_BITS (CLJ_CORE_SEQABLE | CLJ_CORE_SEQ | CLJ_CORE_SEQUENTIAL | CLJ_CORE_COLL)
#define CLJ_ASEQ_TRAIT(bits) .core_bits = CLJ_ASEQ_BITS | (bits), .hash = clj_aseq_hash, .equals = clj_aseq_equals, .conj = clj_aseq_conj

// Test hooks: first/next taken through the seq slot, bypassing a type's fast path.
clj_value clj_debug_first_via_seq(clj_value coll);
clj_value clj_debug_next_via_seq(clj_value coll);

#endif
