// @ai-generated(guided)
#ifndef CLJ_INTRINSICS_H
#define CLJ_INTRINSICS_H

#include "object.h"

// Core operations with a fixed-arity C signature: what the optimizer calls directly from an INTRINSIC node
// and what a compiler emits. Arguments are borrowed, the result is owned or CLJ_THROWN (the slot
// convention). The one correctness rule, kept by structure rather than by test alone: the builtin bound to
// the same core var calls the same function — a single-arity builtin forwards to it, a variadic one folds
// over it — so the two paths cannot drift. `conj` and `assoc` consume their collection at the core; their
// table entry is that call after one retain, and the builtin's fold passes ownership along instead.
// The differential test (IntrinsicsTests) crosses every entry with sample values against clj_invoke.

// ---- arithmetic and comparison (numbers only; anything else throws "cannot be cast to a number")
clj_value clj_add(clj_value a, clj_value b);
clj_value clj_sub(clj_value a, clj_value b);
clj_value clj_mul(clj_value a, clj_value b);
clj_value clj_div(clj_value a, clj_value b);
clj_value clj_inc(clj_value v);
clj_value clj_dec(clj_value v);
clj_value clj_lt(clj_value a, clj_value b);
clj_value clj_le(clj_value a, clj_value b);
clj_value clj_gt(clj_value a, clj_value b);
clj_value clj_ge(clj_value a, clj_value b);
clj_value clj_eq(clj_value a, clj_value b);
clj_value clj_neq(clj_value a, clj_value b);
clj_value clj_identical(clj_value a, clj_value b);

// ---- predicates: a boolean, never thrown, except zero?/pos?/neg? on a non-number and even?/odd? on a non-integer
clj_value clj_not(clj_value v);
clj_value clj_nil_p(clj_value v);
clj_value clj_zero_p(clj_value v);
clj_value clj_pos_p(clj_value v);
clj_value clj_neg_p(clj_value v);
clj_value clj_even_p(clj_value v);
clj_value clj_odd_p(clj_value v);
clj_value clj_number_p(clj_value v);
clj_value clj_integer_p(clj_value v);
clj_value clj_char_p(clj_value v);
clj_value clj_string_p(clj_value v);
clj_value clj_keyword_p(clj_value v);
clj_value clj_symbol_p(clj_value v);
clj_value clj_fn_p(clj_value v);
clj_value clj_vector_p(clj_value v);
clj_value clj_map_p(clj_value v);
clj_value clj_set_p(clj_value v);
clj_value clj_list_p(clj_value v);
clj_value clj_seq_p(clj_value v);
clj_value clj_seqable_p(clj_value v);
clj_value clj_sequential_p(clj_value v);
clj_value clj_coll_p(clj_value v);
clj_value clj_counted_p(clj_value v);
clj_value clj_ifn_p(clj_value v);
clj_value clj_associative_p(clj_value v);
clj_value clj_indexed_p(clj_value v);
// seq's the argument: realizes a lazy seq, throws on a non-seqable.
clj_value clj_empty_p(clj_value v);

// ---- collections (first, rest, next, seq, count and cons are clj_first ... clj_seq_cons of coll.h)
clj_value clj_get2(clj_value coll, clj_value key);
clj_value clj_nth2(clj_value coll, clj_value index);
clj_value clj_nth3(clj_value coll, clj_value index, clj_value not_found);
clj_value clj_conj2(clj_value coll, clj_value x);
clj_value clj_assoc3(clj_value coll, clj_value key, clj_value val);
clj_value clj_dissoc2(clj_value coll, clj_value key);
clj_value clj_disj2(clj_value coll, clj_value key);
clj_value clj_with_meta2(clj_value v, clj_value m);
clj_value clj_contains_p(clj_value coll, clj_value key);

// ---- arrays (array.h): a read and a write of one element, and the length
clj_value clj_aget(clj_value arr, clj_value index);
clj_value clj_aset(clj_value arr, clj_value index, clj_value val);
clj_value clj_alength(clj_value arr);
// The consuming forms of the five above: the collection at +1 (nil included), the rest borrowed; clj_conj and
// clj_with_meta of coll.h are the other two. What a site that owns the collection calls instead.
clj_value clj_assoc_owned(clj_value coll, clj_value key, clj_value val);
clj_value clj_dissoc_owned(clj_value coll, clj_value key);
clj_value clj_disj_owned(clj_value coll, clj_value key);

// ---- the table

typedef clj_value (*clj_intrinsic_1)(clj_value);
typedef clj_value (*clj_intrinsic_2)(clj_value, clj_value);
typedef clj_value (*clj_intrinsic_3)(clj_value, clj_value, clj_value);

// The C signature; the enum value is the argument count.
typedef enum { CLJ_INTRINSIC_1 = 1, CLJ_INTRINSIC_2 = 2, CLJ_INTRINSIC_3 = 3 } clj_intrinsic_kind;

typedef struct {
	const char        *name; // qualified, clojure.core/+
	uint32_t           arity;
	clj_intrinsic_kind kind;
	union {
		clj_intrinsic_1 f1;
		clj_intrinsic_2 f2;
		clj_intrinsic_3 f3;
	} fn;
	const char *cname;    // the C name of fn, what a compiler emits as a direct call
	const char *cconsume; // the same for the consuming form, NULL when none
	// No effect and no identity the result exposes beyond what its arguments carry: the optimizer folds a call on
	// constant arguments at analysis (optimizer.c, on data the codec reads back as the same type, while the var
	// still holds the boot fn). A fold that throws leaves the call to throw at run time, so nth out of bounds
	// or a non-number qualifies; realizing a lazy seq would not, but a lazy seq is never such an argument.
	bool pure;
	// The same operation with its first argument at +1, for a caller that owns it: conj, assoc, dissoc and
	// with-meta consume their collection at the core, and a unique one is updated in place. Zero otherwise.
	union {
		clj_intrinsic_2 f2;
		clj_intrinsic_3 f3;
	} consume;
} clj_intrinsic;

static inline bool clj_intrinsic_consumes(const clj_intrinsic *op) { return op->consume.f2 != NULL; }

// The whole table, in its own order; the array is what clj_intrinsic_builtin indexes by pointer.
const clj_intrinsic *clj_intrinsic_table(size_t *n);
// The entry for a core var called with `arity` arguments, or NULL. Keyed by the var, so a shadowing local
// or another namespace's var of the same name never matches.
const clj_intrinsic *clj_intrinsic_find(clj_value var, uint32_t arity);
// The same by qualified name (the codec reads names); the var it stands for is clj_intrinsic_var.
const clj_intrinsic *clj_intrinsic_find_named(clj_value qualified_sym, uint32_t arity);
// Borrowed: the core var of an entry, and the native fn bound to it at boot. An INTRINSIC node runs the
// table's function only while the var's root is still that fn; anything else falls back to clj_invoke.
clj_value clj_intrinsic_var(const clj_intrinsic *op);
clj_value clj_intrinsic_builtin(const clj_intrinsic *op);
// Calls op with exactly op->arity borrowed arguments.
clj_value clj_intrinsic_call(const clj_intrinsic *op, const clj_value *args);
// The consuming form: args[0] at +1, the rest borrowed. Only for an entry that consumes.
clj_value clj_intrinsic_call_consuming(const clj_intrinsic *op, const clj_value *args);
// Consuming calls made so far (a site that owned its collection, a driver's step), counted in debug builds only (-1 otherwise).
int64_t clj_debug_consuming_calls(void);
// The consuming entry whose boot builtin is fn at this arity, or NULL: what a driver that owns its accumulator
// hands it to instead of calling fn.
const clj_intrinsic *clj_intrinsic_consuming(clj_value fn, uint32_t arity);

// Resolves every entry's var and boot fn; clj_init calls it after the builtins are interned, before core.clj.
void clj_intrinsics_install(void);

#endif
