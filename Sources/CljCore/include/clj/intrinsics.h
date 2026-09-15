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
clj_value clj_contains_p(clj_value coll, clj_value key);

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
	// No effect and no identity the result exposes beyond what its arguments carry: the optimizer folds a call on
	// constant arguments at analysis (optimizer.c, on data the codec reads back as the same type, while the var
	// still holds the boot fn). A fold that throws leaves the call to throw at run time, so nth out of bounds
	// or a non-number qualifies; realizing a lazy seq would not, but a lazy seq is never such an argument.
	bool pure;
} clj_intrinsic;

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

// Resolves every entry's var and boot fn; clj_init calls it after the builtins are interned, before core.clj.
void clj_intrinsics_install(void);

#endif
