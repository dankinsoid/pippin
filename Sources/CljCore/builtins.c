// @ai-generated(guided)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/chan.h"
#include "clj/coll.h"
#include "clj/compare.h"
#include "clj/core.h"
#include "clj/fn.h"
#include "clj/fusion.h"
#include "clj/hosttype.h"
#include "clj/intrinsics.h"
#include "clj/record.h"
#include "clj/reduce.h"
#include "clj/regex.h"
#include "clj/runtime.h"
#include "clj/seq.h"
#include "clj/sorted.h"

// ---- numbers

typedef struct {
	bool     is_double;
	intptr_t i;
	double   d;
} num;

static bool to_num(clj_value v, num *out) {
	if (clj_is_fixnum(v)) {
		*out = (num){false, clj_fixnum_val(v), 0};
		return true;
	}
	if (clj_is_double(v)) {
		*out = (num){true, 0, clj_double_val(v)};
		return true;
	}
	return false;
}

static double as_double(const num *n) { return n->is_double ? n->d : (double)n->i; }

static clj_value not_a_number(clj_value v) { return clj_throw_msg("%s cannot be cast to a number", clj_type_name(v)); }

static clj_value arith2(clj_value a, clj_value b, clj_num_op op) {
	num x, y;
	if (!to_num(a, &x) || !to_num(b, &y)) return clj_num_arith(a, b, op);
	if (x.is_double || y.is_double) {
		double p = as_double(&x), q = as_double(&y), r = 0;
		switch (op) {
		case CLJ_OP_ADD: r = p + q; break;
		case CLJ_OP_SUB: r = p - q; break;
		case CLJ_OP_MUL: r = p * q; break;
		default: r = p / q; break;
		}
		return clj_double_new(r);
	}
	intptr_t r = 0;
	bool     overflow = false;
	switch (op) {
	case CLJ_OP_ADD: overflow = __builtin_add_overflow(x.i, y.i, &r); break;
	case CLJ_OP_SUB: overflow = __builtin_sub_overflow(x.i, y.i, &r); break;
	case CLJ_OP_MUL: overflow = __builtin_mul_overflow(x.i, y.i, &r); break;
	default:
		if (y.i == 0) return clj_throw_msg("Divide by zero");
		if (x.i % y.i != 0) return clj_num_arith(a, b, CLJ_OP_DIV);
		overflow = (x.i == INTPTR_MIN && y.i == -1);
		if (!overflow) r = x.i / y.i;
		break;
	}
	if (overflow) return clj_throw_msg("integer overflow");
	return clj_long_new(r);
}

clj_value clj_add(clj_value a, clj_value b) { return arith2(a, b, CLJ_OP_ADD); }
clj_value clj_sub(clj_value a, clj_value b) { return arith2(a, b, CLJ_OP_SUB); }
clj_value clj_mul(clj_value a, clj_value b) { return arith2(a, b, CLJ_OP_MUL); }
clj_value clj_div(clj_value a, clj_value b) { return arith2(a, b, CLJ_OP_DIV); }
clj_value clj_inc(clj_value v) { return clj_add(v, clj_fixnum(1)); }
clj_value clj_dec(clj_value v) { return clj_sub(v, clj_fixnum(1)); }

// (op a b c ...) as a left fold over the two-argument function; (op) is the identity, (op a) is a checked a
// for + and *, and (op identity a) for - and /.
static clj_value arith_fold(const clj_value *args, size_t n, clj_intrinsic_2 op, intptr_t identity, bool unary_is_self) {
	if (n == 0) return clj_fixnum(identity);
	if (n == 1) {
		if (!clj_is_number(args[0])) return not_a_number(args[0]);
		return unary_is_self ? clj_retain(args[0]) : op(clj_fixnum(identity), args[0]);
	}
	clj_value acc = op(args[0], args[1]);
	for (size_t i = 2; i < n && acc != CLJ_THROWN; i++) {
		clj_value next = op(acc, args[i]);
		clj_release(acc);
		acc = next;
	}
	return acc;
}

static clj_value b_add(const clj_value *args, size_t n) { return arith_fold(args, n, clj_add, 0, true); }
static clj_value b_sub(const clj_value *args, size_t n) { return arith_fold(args, n, clj_sub, 0, false); }
static clj_value b_mul(const clj_value *args, size_t n) { return arith_fold(args, n, clj_mul, 1, true); }
static clj_value b_div(const clj_value *args, size_t n) { return arith_fold(args, n, clj_div, 1, false); }

typedef enum { CMP_LT, CMP_LE, CMP_GT, CMP_GE } cmp_op;

static clj_value compare2(clj_value a, clj_value b, cmp_op op) {
	num x, y;
	if (!to_num(a, &x) || !to_num(b, &y)) {
		int c;
		if (clj_num_cmp(a, b, &c) == CLJ_THROWN) return CLJ_THROWN;
		// 2 is a NaN against a bigint or a decimal: unordered, so every operator is false.
		if (c == 2) return CLJ_FALSE;
		return clj_bool(op == CMP_LT ? c < 0 : op == CMP_LE ? c <= 0 : op == CMP_GT ? c > 0 : c >= 0);
	}
	bool ok;
	if (x.is_double || y.is_double) {
		double p = as_double(&x), q = as_double(&y);
		ok = op == CMP_LT ? p < q : op == CMP_LE ? p <= q : op == CMP_GT ? p > q : p >= q;
	} else {
		intptr_t p = x.i, q = y.i;
		ok = op == CMP_LT ? p < q : op == CMP_LE ? p <= q : op == CMP_GT ? p > q : p >= q;
	}
	return clj_bool(ok);
}

clj_value clj_lt(clj_value a, clj_value b) { return compare2(a, b, CMP_LT); }
clj_value clj_le(clj_value a, clj_value b) { return compare2(a, b, CMP_LE); }
clj_value clj_gt(clj_value a, clj_value b) { return compare2(a, b, CMP_GT); }
clj_value clj_ge(clj_value a, clj_value b) { return compare2(a, b, CMP_GE); }

// (op a b c ...) is true when every adjacent pair is; the 1-arity is true for anything, as on the JVM.
static clj_value compare_fold(const clj_value *args, size_t n, clj_intrinsic_2 op) {
	if (n == 1) return CLJ_TRUE;
	for (size_t i = 1; i < n; i++) {
		clj_value r = op(args[i - 1], args[i]);
		if (r != CLJ_TRUE) return r;
	}
	return CLJ_TRUE;
}

static clj_value b_lt(const clj_value *args, size_t n) { return compare_fold(args, n, clj_lt); }
static clj_value b_le(const clj_value *args, size_t n) { return compare_fold(args, n, clj_le); }
static clj_value b_gt(const clj_value *args, size_t n) { return compare_fold(args, n, clj_gt); }
static clj_value b_ge(const clj_value *args, size_t n) { return compare_fold(args, n, clj_ge); }

clj_value clj_eq(clj_value a, clj_value b) { return clj_bool(clj_equals(a, b)); }
clj_value clj_neq(clj_value a, clj_value b) { return clj_bool(!clj_equals(a, b)); }
clj_value clj_identical(clj_value a, clj_value b) { return clj_bool(a == b); }

static clj_value b_eq(const clj_value *args, size_t n) {
	for (size_t i = 1; i < n; i++) {
		if (clj_eq(args[0], args[i]) == CLJ_FALSE) return CLJ_FALSE;
	}
	return CLJ_TRUE;
}

static clj_value b_neq(const clj_value *args, size_t n) {
	for (size_t i = 1; i < n; i++) {
		if (clj_neq(args[0], args[i]) == CLJ_TRUE) return CLJ_TRUE;
	}
	return CLJ_FALSE;
}

static clj_value b_identical(const clj_value *args, size_t n) {
	(void)n;
	return clj_identical(args[0], args[1]);
}

static clj_value b_hash(const clj_value *args, size_t n) {
	(void)n;
	if (clj_is_ptr(args[0]) && !clj_type_of(args[0])->hash) return clj_throw_msg("%s cannot be hashed", clj_type_name(args[0]));
	return clj_fixnum((int32_t)clj_hash(args[0]));
}

static clj_value int_arg(clj_value v, intptr_t *out) {
	if (!clj_is_fixnum(v)) {
		clj_value text = clj_pr_str_max(v, CLJ_ERROR_PRINT_MAX);
		if (text == CLJ_THROWN) return CLJ_THROWN;
		clj_value r = clj_throw_msg("Argument must be an integer: %s", clj_string_bytes(text));
		clj_release(text);
		return r;
	}
	*out = clj_fixnum_val(v);
	return CLJ_NIL;
}

// NaN is neither zero, positive nor negative, which the double arm keeps and clj_num_sign cannot express.
static clj_value sign_test(clj_value v, cmp_op op) {
	num x;
	if (to_num(v, &x)) {
		double d = as_double(&x);
		return clj_bool(op == CMP_LT ? d < 0 : op == CMP_GT ? d > 0 : d == 0);
	}
	if (!clj_is_number(v)) return not_a_number(v);
	int s = clj_num_sign(v);
	return clj_bool(op == CMP_LT ? s < 0 : op == CMP_GT ? s > 0 : s == 0);
}

clj_value clj_zero_p(clj_value v) { return sign_test(v, CMP_LE); }
clj_value clj_pos_p(clj_value v) { return sign_test(v, CMP_GT); }
clj_value clj_neg_p(clj_value v) { return sign_test(v, CMP_LT); }

static clj_value parity(clj_value v, bool want_even) {
	bool    even;
	int64_t i;
	if (clj_int64_of(v, &i)) {
		even = i % 2 == 0;
	} else if (clj_is_bigint(v)) {
		clj_bigint *b = clj_bigint_of(v);
		even = b->n == 0 || (b->limbs[0] & 1) == 0;
	} else {
		intptr_t n;
		return int_arg(v, &n);
	}
	return clj_bool(even == want_even);
}

clj_value clj_even_p(clj_value v) { return parity(v, true); }
clj_value clj_odd_p(clj_value v) { return parity(v, false); }

// ---- predicates

// The one-argument function of intrinsics.h and the builtin that forwards to it.
#define PREDICATE(cname, bname, test) \
	clj_value cname(clj_value v) { return clj_bool(test(v)); } \
	FORWARD1(cname, bname)

#define FORWARD1(cname, bname) \
	static clj_value bname(const clj_value *args, size_t n) { \
		(void)n; \
		return cname(args[0]); \
	}

static bool is_not(clj_value v) { return !clj_truthy(v); }
static bool is_map_p(clj_value v) { return clj_has_core(v, CLJ_CORE_MAP); }
static bool is_set_p(clj_value v) { return clj_has_core(v, CLJ_CORE_SET); }
static bool is_vector_p(clj_value v) { return clj_has_core(v, CLJ_CORE_VECTOR); }
static bool is_list_p(clj_value v) { return clj_has_core(v, CLJ_CORE_LIST); }
static bool is_sequential(clj_value v) { return clj_has_core(v, CLJ_CORE_SEQUENTIAL); }
static bool is_coll(clj_value v) { return clj_has_core(v, CLJ_CORE_COLL); }
static bool is_counted(clj_value v) { return clj_has_core(v, CLJ_CORE_COUNTED); }
static bool is_ifn(clj_value v) { return clj_has_core(v, CLJ_CORE_FN); }
static bool is_associative(clj_value v) { return clj_has_core(v, CLJ_CORE_ASSOCIATIVE); }
static bool is_indexed(clj_value v) { return clj_has_core(v, CLJ_CORE_INDEXED); }

PREDICATE(clj_not, b_not, is_not)
PREDICATE(clj_nil_p, b_nil, clj_is_nil)
PREDICATE(clj_number_p, b_number, clj_is_number)
PREDICATE(clj_string_p, b_string, clj_is_string)
PREDICATE(clj_keyword_p, b_keyword, clj_is_keyword)
PREDICATE(clj_symbol_p, b_symbol, clj_is_symbol)
PREDICATE(clj_vector_p, b_vector_p, is_vector_p)
PREDICATE(clj_map_p, b_map, is_map_p)
PREDICATE(clj_set_p, b_set_p, is_set_p)
PREDICATE(clj_list_p, b_list_p, is_list_p)
PREDICATE(clj_fn_p, b_fn, clj_is_fn)
PREDICATE(clj_seq_p, b_seq_p, clj_is_seq)
PREDICATE(clj_seqable_p, b_seqable_p, clj_is_seqable)
PREDICATE(clj_sequential_p, b_sequential_p, is_sequential)
PREDICATE(clj_coll_p, b_coll_p, is_coll)
PREDICATE(clj_counted_p, b_counted_p, is_counted)
PREDICATE(clj_ifn_p, b_ifn_p, is_ifn)
PREDICATE(clj_associative_p, b_associative_p, is_associative)
PREDICATE(clj_indexed_p, b_indexed_p, is_indexed)
PREDICATE(clj_char_p, b_char_p, clj_is_char)
PREDICATE(clj_integer_p, b_integer_p, clj_is_integer)
FORWARD1(clj_inc, b_inc)
FORWARD1(clj_dec, b_dec)
FORWARD1(clj_zero_p, b_zero)
FORWARD1(clj_pos_p, b_pos)
FORWARD1(clj_neg_p, b_neg)
FORWARD1(clj_even_p, b_even)
FORWARD1(clj_odd_p, b_odd)

// ---- collections

clj_value clj_get2(clj_value coll, clj_value key) { return clj_get(coll, key, CLJ_NIL); }
clj_value clj_nth2(clj_value coll, clj_value index) { return clj_nth(coll, index, false, CLJ_NIL); }
clj_value clj_nth3(clj_value coll, clj_value index, clj_value not_found) { return clj_nth(coll, index, true, not_found); }
clj_value clj_conj2(clj_value coll, clj_value x) { return clj_conj(clj_retain(coll), x); }

static clj_value b_get(const clj_value *args, size_t n) { return n == 3 ? clj_get(args[0], args[1], args[2]) : clj_get2(args[0], args[1]); }

static clj_value b_nth(const clj_value *args, size_t n) { return n == 3 ? clj_nth3(args[0], args[1], args[2]) : clj_nth2(args[0], args[1]); }

// Consumes coll (+1 in), as clj_conj does.
static clj_value assoc_one(clj_value coll, clj_value key, clj_value val) {
	const clj_type *t = clj_is_ptr(coll) ? clj_type_of(coll) : NULL;
	if (t && t->assoc) return t->assoc(coll, key, val);
	if (clj_is_vector(coll)) {
		intptr_t idx;
		if (!clj_index_arg(key, &idx)) {
			clj_release(coll);
			return clj_throw_msg("Key must be integer");
		}
		if (idx < 0 || (uintptr_t)idx > clj_vector_count(coll)) {
			uint32_t count = clj_vector_count(coll);
			clj_release(coll);
			return clj_throw_msg("Index %lld out of bounds for length %u", (long long)idx, count);
		}
		return clj_vector_assoc(coll, (uint32_t)idx, val);
	}
	clj_value e = clj_throw_msg("assoc not supported on this type: %s", clj_type_name(coll));
	clj_release(coll);
	return e;
}

clj_value clj_assoc_owned(clj_value coll, clj_value key, clj_value val) {
	return assoc_one(clj_is_nil(coll) ? clj_map_empty() : coll, key, val);
}

clj_value clj_assoc3(clj_value coll, clj_value key, clj_value val) { return clj_assoc_owned(clj_retain(coll), key, val); }

static clj_value b_assoc(const clj_value *args, size_t n) {
	if (n % 2 == 0) return clj_throw_msg("assoc expects even number of arguments after map/vector, found odd number");
	clj_value coll = clj_assoc3(args[0], args[1], args[2]);
	for (size_t i = 3; i < n && coll != CLJ_THROWN; i += 2) coll = assoc_one(coll, args[i], args[i + 1]);
	return coll;
}

static clj_value dissoc_type_error(clj_value coll) { return clj_throw_msg("dissoc not supported on this type: %s", clj_type_name(coll)); }

clj_value clj_dissoc_owned(clj_value coll, clj_value key) {
	if (clj_is_nil(coll)) return CLJ_NIL;
	if (clj_has_core(coll, CLJ_CORE_MAP)) return clj_type_of(coll)->dissoc(coll, key);
	clj_value e = dissoc_type_error(coll);
	clj_release(coll);
	return e;
}

clj_value clj_dissoc2(clj_value coll, clj_value key) { return clj_dissoc_owned(clj_retain(coll), key); }

static clj_value b_dissoc(const clj_value *args, size_t n) {
	if (n == 1) return clj_is_nil(args[0]) || clj_has_core(args[0], CLJ_CORE_MAP) ? clj_retain(args[0]) : dissoc_type_error(args[0]);
	clj_value coll = clj_dissoc2(args[0], args[1]);
	for (size_t i = 2; i < n && coll != CLJ_THROWN; i++) coll = clj_dissoc_owned(coll, args[i]);
	return coll;
}

clj_value clj_contains_p(clj_value coll, clj_value key) {
	if (clj_is_nil(coll)) return CLJ_FALSE;
	if (clj_is_map(coll)) return clj_bool(clj_map_contains(coll, key));
	if (clj_is_set(coll)) return clj_bool(clj_set_contains(coll, key));
	intptr_t i;
	if (clj_is_vector(coll)) return clj_bool(clj_index_arg(key, &i) && i >= 0 && (uintptr_t)i < clj_vector_count(coll));
	// RT.contains indexes an array or a String and casts the key to a number first, so a nil key throws.
	if (clj_is_array(coll) || clj_is_string(coll)) {
		if (!clj_index_arg(key, &i)) return clj_throw_msg("%s cannot be cast to a number", clj_type_name(key));
		size_t n = clj_is_array(coll) ? clj_array_count(coll) : clj_string_count(coll);
		return clj_bool(i >= 0 && (size_t)i < n);
	}
	// Any other IPersistentMap/Set answers through its lookup; CLJ_UNBOUND is never a stored value.
	if (clj_has_core(coll, CLJ_CORE_MAP) || clj_has_core(coll, CLJ_CORE_SET)) {
		clj_value v = clj_get(coll, key, CLJ_UNBOUND);
		if (v == CLJ_THROWN) return CLJ_THROWN;
		clj_release(v);
		return clj_bool(v != CLJ_UNBOUND);
	}
	return clj_throw_msg("contains? not supported on type: %s", clj_type_name(coll));
}

static clj_value b_contains(const clj_value *args, size_t n) {
	(void)n;
	return clj_contains_p(args[0], args[1]);
}

static clj_value disj_type_error(clj_value coll) { return clj_throw_msg("disj not supported on this type: %s", clj_type_name(coll)); }

clj_value clj_disj_owned(clj_value coll, clj_value key) {
	if (clj_is_nil(coll)) return CLJ_NIL;
	if (clj_has_core(coll, CLJ_CORE_SET)) return clj_type_of(coll)->dissoc(coll, key);
	clj_value e = disj_type_error(coll);
	clj_release(coll);
	return e;
}

clj_value clj_disj2(clj_value coll, clj_value key) { return clj_disj_owned(clj_retain(coll), key); }

static clj_value b_disj(const clj_value *args, size_t n) {
	if (n == 1) return clj_is_nil(args[0]) || clj_has_core(args[0], CLJ_CORE_SET) ? clj_retain(args[0]) : disj_type_error(args[0]);
	clj_value coll = clj_disj2(args[0], args[1]);
	for (size_t i = 2; i < n && coll != CLJ_THROWN; i++) coll = clj_disj_owned(coll, args[i]);
	return coll;
}

static clj_value b_hash_set(const clj_value *args, size_t n) { return clj_set_from_array(args, n); }

// (set coll): a set comes back without its meta, as Clojure's does; anything else is conj'd through its seq.
static clj_value b_set(const clj_value *args, size_t n) {
	(void)n;
	if (clj_is_set(args[0])) return clj_with_meta(clj_retain(args[0]), CLJ_NIL);
	clj_value s = clj_seq(args[0]);
	if (s == CLJ_THROWN) return CLJ_THROWN;
	clj_seq_iter it = clj_seq_iter_start(s);
	clj_value    item, r = clj_set_empty();
	while (clj_seq_iter_next(&it, &item)) r = clj_set_conj(r, item);
	clj_release(s);
	if (it.thrown) {
		clj_release(r);
		return CLJ_THROWN;
	}
	return r;
}

// (empty coll): the empty collection of coll's kind with coll's meta; nil for anything that is no collection.
static clj_value b_empty_coll(const clj_value *args, size_t n) {
	(void)n;
	clj_value coll = args[0], e;
	// A record has no empty: its basis keys cannot be dropped, as on the JVM.
	if (clj_is_record(coll)) return clj_throw_msg("Can't create empty: %s", clj_type_name(coll));
	if (clj_is_vector(coll)) e = clj_vector_empty();
	else if (clj_is_map(coll)) e = clj_map_empty();
	else if (clj_is_set(coll)) e = clj_set_empty();
	else if (clj_is_sorted(coll)) e = clj_sorted_empty(coll);
	else if (clj_is_queue(coll)) e = clj_queue_empty();
	else if (clj_has_core(coll, CLJ_CORE_COLL)) e = clj_list_empty();
	else return CLJ_NIL;
	clj_value m = clj_meta(coll);
	if (clj_is_nil(m)) return e;
	clj_value r = clj_with_meta(e, m);
	clj_release(m);
	return r;
}

static clj_value b_conj(const clj_value *args, size_t n) {
	if (n == 0) return clj_vector_empty();
	if (n == 1) return clj_retain(args[0]);
	clj_value coll = clj_conj2(args[0], args[1]);
	for (size_t i = 2; i < n && coll != CLJ_THROWN; i++) coll = clj_conj(coll, args[i]);
	return coll;
}

FORWARD1(clj_count, b_count)
FORWARD1(clj_first, b_first)
FORWARD1(clj_rest, b_rest)
FORWARD1(clj_next, b_next)
FORWARD1(clj_seq, b_seq)

static clj_value b_cons(const clj_value *args, size_t n) {
	(void)n;
	return clj_seq_cons(args[0], args[1]);
}

static clj_value b_list(const clj_value *args, size_t n) { return clj_list_from_array(args, n); }

static clj_value b_vector(const clj_value *args, size_t n) {
	if (n > UINT32_MAX) return clj_throw_msg("vector too long");
	return clj_vector_from_array(args, (uint32_t)n);
}

static clj_value b_hash_map(const clj_value *args, size_t n) {
	if (n % 2) {
		clj_value text = clj_pr_str_max(args[n - 1], CLJ_ERROR_PRINT_MAX);
		if (text == CLJ_THROWN) return CLJ_THROWN;
		clj_value r = clj_throw_msg("No value supplied for key: %s", clj_string_bytes(text));
		clj_release(text);
		return r;
	}
	return clj_map_from_items(args, (uint32_t)n, NULL);
}

// ---- seqs

static clj_value b_lazy_seq_star(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_fn(args[0])) return clj_throw_msg("lazy-seq* expects a fn, got: %s", clj_type_name(args[0]));
	return clj_lazy_seq_new(args[0]);
}

static clj_value b_realized_p(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_lazy_seq(args[0])) return clj_throw_msg("realized? not supported on this type: %s", clj_type_name(args[0]));
	return clj_bool(clj_lazy_seq_realized(args[0]));
}

// (range* start end step) over the whole int64; core.clj's range handles the other arities and doubles.
static clj_value b_range_star(const clj_value *args, size_t n) {
	(void)n;
	int64_t v[3];
	for (size_t i = 0; i < 3; i++) {
		if (!clj_int64_of(args[i], &v[i])) {
			clj_value text = clj_pr_str_max(args[i], CLJ_ERROR_PRINT_MAX);
			if (text == CLJ_THROWN) return CLJ_THROWN;
			clj_value e = clj_throw_msg("range bound outside the 64-bit long: %s", clj_string_bytes(text));
			clj_release(text);
			return e;
		}
	}
	if (v[2] == 0) return clj_throw_msg("range* step must not be 0");
	return clj_range_new(v[0], v[1], v[2]);
}

// (cons a (cons b ... coll)): the last argument is the tail as a seq and stays unrealized.
static clj_value b_list_star(const clj_value *args, size_t n) {
	clj_value tail = args[n - 1];
	clj_value r = clj_is_seq(tail) ? clj_retain(tail) : clj_seq(tail);
	if (r == CLJ_THROWN) return CLJ_THROWN;
	for (size_t i = n - 1; i > 0; i--) {
		clj_value c = clj_seq_cons(args[i - 1], r);
		clj_release(r);
		r = c;
	}
	return r;
}

clj_value clj_empty_p(clj_value v) {
	clj_value s = clj_seq(v);
	if (s == CLJ_THROWN) return CLJ_THROWN;
	clj_release(s);
	return clj_bool(clj_is_nil(s));
}

FORWARD1(clj_empty_p, b_empty)

static clj_value b_second(const clj_value *args, size_t n) {
	(void)n;
	clj_value rest = clj_next(args[0]);
	if (rest == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_first(rest);
	clj_release(rest);
	return r;
}

static clj_value b_last(const clj_value *args, size_t n) {
	(void)n;
	clj_value s = clj_seq(args[0]);
	if (s == CLJ_THROWN) return CLJ_THROWN;
	clj_seq_iter it = clj_seq_iter_start(s);
	clj_value    item, last = CLJ_NIL;
	// An item the iterator owns dies at the next step, so each one is retained as it arrives.
	while (clj_seq_iter_next(&it, &item)) {
		clj_retain(item);
		clj_release(last);
		last = item;
	}
	clj_release(s);
	if (it.thrown) {
		clj_release(last);
		return CLJ_THROWN;
	}
	return last;
}

static clj_value b_butlast(const clj_value *args, size_t n) {
	(void)n;
	size_t     count;
	clj_value  keep;
	clj_value *items = clj_seq_items(args[0], &count, &keep);
	if (!items) return CLJ_THROWN;
	clj_value r = count > 1 ? clj_list_from_array(items, count - 1) : CLJ_NIL;
	free(items);
	clj_release(keep);
	return r;
}

static clj_value b_reverse(const clj_value *args, size_t n) {
	(void)n;
	clj_value s = clj_seq(args[0]);
	if (s == CLJ_THROWN) return CLJ_THROWN;
	clj_seq_iter it = clj_seq_iter_start(s);
	clj_value    item, r = clj_list_empty();
	while (clj_seq_iter_next(&it, &item)) {
		clj_value c = clj_list_new(item, r);
		clj_release(r);
		r = c;
	}
	clj_release(s);
	if (it.thrown) {
		clj_release(r);
		return CLJ_THROWN;
	}
	return r;
}

// (into to from) conj's every item in C; (into to xform from) runs the fused-into* driver under the xform, so the
// accumulator is the driver's own and a vector grows in place (fusion.c).
static clj_value b_into(const clj_value *args, size_t n) {
	if (n == 3) return clj_into_xform(args[0], args[1], args[2]);
	clj_value s = clj_seq(args[1]);
	if (s == CLJ_THROWN) return CLJ_THROWN;
	clj_seq_iter it = clj_seq_iter_start(s);
	clj_value    item, r = clj_retain(args[0]);
	while (r != CLJ_THROWN && clj_seq_iter_next(&it, &item)) r = clj_conj(r, item);
	clj_seq_iter_close(&it);
	clj_release(s);
	if (it.thrown) {
		clj_release(r);
		return CLJ_THROWN;
	}
	return r;
}

static clj_value b_reduce(const clj_value *args, size_t n) {
	return n == 3 ? clj_reduce(args[0], args[1], args[2]) : clj_reduce(args[0], CLJ_UNBOUND, args[1]);
}

static clj_value b_reduce_kv(const clj_value *args, size_t n) {
	(void)n;
	return clj_reduce_kv(args[0], args[1], args[2]);
}

static clj_value b_reduced(const clj_value *args, size_t n) {
	(void)n;
	return clj_reduced_new(args[0]);
}

static clj_value b_reduced_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_reduced(args[0]));
}

static clj_value b_unreduced(const clj_value *args, size_t n) {
	(void)n;
	return clj_retain(clj_is_reduced(args[0]) ? clj_reduced_value(args[0]) : args[0]);
}

static clj_value b_ensure_reduced(const clj_value *args, size_t n) {
	(void)n;
	return clj_is_reduced(args[0]) ? clj_retain(args[0]) : clj_reduced_new(args[0]);
}

static clj_value b_volatile(const clj_value *args, size_t n) {
	(void)n;
	return clj_volatile_new(args[0]);
}

static clj_value b_volatile_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_volatile(args[0]));
}

static clj_value b_vreset(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_volatile(args[0])) return clj_throw_msg("vreset! expects a volatile, got: %s", clj_type_name(args[0]));
	return clj_volatile_reset(args[0], args[1]);
}

// ---- atoms

static clj_value not_an_atom(const char *what, clj_value v) { return clj_throw_msg("%s expects an atom, got: %s", what, clj_type_name(v)); }

// (atom x & {:keys [meta validator]}): other keys are ignored, as Clojure's setup-reference does.
static clj_value b_atom(const clj_value *args, size_t n) {
	if (n % 2 == 0) {
		clj_value text = clj_pr_str_max(args[n - 1], CLJ_ERROR_PRINT_MAX);
		if (text == CLJ_THROWN) return CLJ_THROWN;
		clj_value r = clj_throw_msg("No value supplied for key: %s", clj_string_bytes(text));
		clj_release(text);
		return r;
	}
	clj_value meta = CLJ_NIL, validator = CLJ_NIL, affinity = CLJ_NIL;
	for (size_t i = 1; i < n; i += 2) {
		if (!clj_is_keyword(args[i])) continue;
		const char *name = clj_string_bytes(clj_keyword_name(args[i]));
		if (clj_is_nil(clj_keyword_ns(args[i])) && strcmp(name, "meta") == 0) meta = args[i + 1];
		else if (clj_is_nil(clj_keyword_ns(args[i])) && strcmp(name, "validator") == 0) validator = args[i + 1];
		else if (clj_is_nil(clj_keyword_ns(args[i])) && strcmp(name, "affinity") == 0) affinity = args[i + 1];
	}
	if (!clj_is_nil(meta) && !clj_has_core(meta, CLJ_CORE_MAP)) return clj_throw_msg("atom :meta must be a map, got: %s", clj_type_name(meta));
	if (!clj_is_nil(validator) && !clj_has_core(validator, CLJ_CORE_FN)) return clj_throw_msg("atom :validator must be a fn, got: %s", clj_type_name(validator));
	bool main = false;
	if (!clj_is_nil(affinity)) {
		if (!clj_is_keyword(affinity) || strcmp(clj_string_bytes(clj_keyword_name(affinity)), "main") != 0) return clj_throw_msg("atom :affinity must be :main");
		main = true;
	}
	clj_value a = clj_atom_new(args[0], meta, validator);
	if (a != CLJ_THROWN && main) clj_atom_set_affinity(a, CLJ_AFFINITY_MAIN);
	return a;
}

static clj_value b_monitor_enter(const clj_value *args, size_t n) {
	(void)n;
	return clj_monitor_enter(args[0]);
}

static clj_value b_monitor_exit(const clj_value *args, size_t n) {
	(void)n;
	return clj_monitor_exit(args[0]);
}

static clj_value b_atom_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_atom(args[0]));
}

static clj_value b_reset(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_atom(args[0])) return not_an_atom("reset!", args[0]);
	return clj_atom_reset(args[0], args[1]);
}

static clj_value b_reset_vals(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_atom(args[0])) return not_an_atom("reset-vals!", args[0]);
	return clj_atom_reset_vals(args[0], args[1]);
}

static clj_value b_swap(const clj_value *args, size_t n) {
	if (!clj_is_atom(args[0])) return not_an_atom("swap!", args[0]);
	return clj_atom_swap(args[0], args[1], args + 2, n - 2);
}

static clj_value b_swap_vals(const clj_value *args, size_t n) {
	if (!clj_is_atom(args[0])) return not_an_atom("swap-vals!", args[0]);
	return clj_atom_swap_vals(args[0], args[1], args + 2, n - 2);
}

static clj_value b_compare_and_set(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_atom(args[0])) return not_an_atom("compare-and-set!", args[0]);
	return clj_atom_compare_and_set(args[0], args[1], args[2]);
}

static clj_value b_add_watch(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_atom(args[0])) return not_an_atom("add-watch", args[0]);
	if (!clj_has_core(args[2], CLJ_CORE_FN)) return clj_throw_msg("add-watch expects a fn, got: %s", clj_type_name(args[2]));
	return clj_atom_add_watch(args[0], args[1], args[2]);
}

static clj_value b_remove_watch(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_atom(args[0])) return not_an_atom("remove-watch", args[0]);
	return clj_atom_remove_watch(args[0], args[1]);
}

static clj_value b_set_validator(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_atom(args[0])) return not_an_atom("set-validator!", args[0]);
	if (!clj_is_nil(args[1]) && !clj_has_core(args[1], CLJ_CORE_FN)) return clj_throw_msg("set-validator! expects a fn or nil, got: %s", clj_type_name(args[1]));
	return clj_atom_set_validator(args[0], args[1]);
}

static clj_value b_get_validator(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_atom(args[0])) return not_an_atom("get-validator", args[0]);
	return clj_atom_get_validator(args[0]);
}

// ---- names

// Splits "ns/name" at the first slash, as the reader does; "/" alone is a name.
static void split_name(clj_value text, clj_value *ns, clj_value *name) {
	const char *s = clj_string_bytes(text);
	size_t      len = clj_string_len(text);
	const char *slash = memchr(s, '/', len);
	if (!slash || len == 1) {
		*ns = CLJ_NIL;
		*name = clj_retain(text);
	} else {
		*ns = clj_string_new(s, (size_t)(slash - s));
		*name = clj_string_new(slash + 1, len - (size_t)(slash - s) - 1);
	}
}

// Owned ns (or nil) and name of what a symbol or keyword can be made from.
static clj_value name_parts(const clj_value *args, size_t n, clj_value *ns, clj_value *name) {
	if (n == 2) {
		if (!clj_is_nil(args[0]) && !clj_is_string(args[0])) return clj_throw_msg("namespace must be a string or nil, got: %s", clj_type_name(args[0]));
		if (!clj_is_string(args[1])) return clj_throw_msg("name must be a string, got: %s", clj_type_name(args[1]));
		*ns = clj_retain(args[0]);
		*name = clj_retain(args[1]);
		return CLJ_NIL;
	}
	clj_value v = args[0];
	if (clj_is_string(v)) {
		split_name(v, ns, name);
	} else if (clj_is_symbol(v)) {
		*ns = clj_retain(clj_symbol_ns(v));
		*name = clj_retain(clj_symbol_name(v));
	} else if (clj_is_keyword(v)) {
		*ns = clj_retain(clj_keyword_ns(v));
		*name = clj_retain(clj_keyword_name(v));
	} else if (clj_is_var(v)) {
		*ns = clj_retain(clj_symbol_name(clj_var_ns(v)));
		*name = clj_retain(clj_symbol_name(clj_var_name(v)));
	} else {
		return clj_throw_msg("no conversion to symbol from: %s", clj_type_name(v));
	}
	return CLJ_NIL;
}

static clj_value b_make_symbol(const clj_value *args, size_t n) {
	if (n == 1 && clj_is_symbol(args[0])) return clj_retain(args[0]);
	clj_value ns, name;
	if (name_parts(args, n, &ns, &name) == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_symbol_new(ns, name);
	clj_release(ns);
	clj_release(name);
	return r;
}

static clj_value b_make_keyword(const clj_value *args, size_t n) {
	if (n == 1 && clj_is_keyword(args[0])) return args[0];
	if (n == 1 && clj_is_nil(args[0])) return CLJ_NIL; // (keyword nil) is nil, as Clojure's is
	clj_value ns, name;
	if (name_parts(args, n, &ns, &name) == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_keyword_intern(ns, name);
	clj_release(ns);
	clj_release(name);
	return r;
}

static clj_value b_name(const clj_value *args, size_t n) {
	(void)n;
	clj_value v = args[0];
	if (clj_is_string(v)) return clj_retain(v);
	if (clj_is_symbol(v)) return clj_retain(clj_symbol_name(v));
	if (clj_is_keyword(v)) return clj_retain(clj_keyword_name(v));
	return clj_throw_msg("%s cannot be cast to a named value", clj_type_name(v));
}

static clj_value b_namespace(const clj_value *args, size_t n) {
	(void)n;
	clj_value v = args[0];
	if (clj_is_symbol(v)) return clj_retain(clj_symbol_ns(v));
	if (clj_is_keyword(v)) return clj_retain(clj_keyword_ns(v));
	return clj_throw_msg("%s cannot be cast to a named value", clj_type_name(v));
}

static clj_value b_gensym(const clj_value *args, size_t n) {
	const char *prefix = "G__";
	if (n == 1) {
		if (clj_is_string(args[0])) prefix = clj_string_bytes(args[0]);
		else if (clj_is_symbol(args[0])) prefix = clj_string_bytes(clj_symbol_name(args[0]));
		else return clj_throw_msg("gensym prefix must be a string or symbol, got: %s", clj_type_name(args[0]));
	}
	size_t cap = strlen(prefix) + 24;
	char  *text = malloc(cap);
	if (!text) clj_fatal("out of memory");
	snprintf(text, cap, "%s%llu", prefix, (unsigned long long)clj_next_id());
	clj_value name = clj_string_from_cstr(text);
	clj_value r = clj_symbol_new(CLJ_NIL, name);
	clj_release(name);
	free(text);
	return r;
}

// ---- macros and exceptions

static clj_value b_macroexpand_1(const clj_value *args, size_t n) {
	(void)n;
	return clj_macroexpand_1(args[0], NULL);
}

static clj_value b_macroexpand(const clj_value *args, size_t n) {
	(void)n;
	return clj_macroexpand(args[0], NULL);
}

static clj_value b_ex_info(const clj_value *args, size_t n) {
	if (!clj_is_string(args[0])) return clj_throw_msg("ex-info message must be a string, got: %s", clj_type_name(args[0]));
	if (!clj_is_nil(args[1]) && !clj_has_core(args[1], CLJ_CORE_MAP)) return clj_throw_msg("ex-info data must be a map, got: %s", clj_type_name(args[1]));
	clj_value cause = n == 3 ? args[2] : CLJ_NIL;
	if (!clj_is_nil(cause) && !clj_is_exception(cause)) return clj_throw_msg("ex-info cause must be an exception, got: %s", clj_type_name(cause));
	return clj_ex_info_cause(args[0], args[1], cause);
}

// nil for anything that is not an error, as Clojure's ex-message/ex-data/ex-cause do for non-Throwables.
static clj_value b_ex_message(const clj_value *args, size_t n) {
	(void)n;
	return clj_ex_message(args[0]);
}

static clj_value b_ex_data(const clj_value *args, size_t n) {
	(void)n;
	return clj_ex_data(args[0]);
}

static clj_value b_ex_cause(const clj_value *args, size_t n) {
	(void)n;
	return clj_ex_cause(args[0]);
}

static clj_value b_ex_trace(const clj_value *args, size_t n) {
	(void)n;
	return clj_ex_trace(args[0]);
}

// @ai-generated(solo)
static clj_value b_ex_type(const clj_value *args, size_t n) {
	(void)n;
	return clj_ex_type(args[0]);
}

// The value a qualified symbol naming a host type resolves to; interned, so no retain is owed (design §4).
// @ai-generated(solo)
static clj_value b_host_type(const clj_value *args, size_t n) {
	(void)n;
	clj_value name = args[0];
	if (clj_is_symbol(name)) {
		clj_value text = clj_pr_str(name);
		if (text == CLJ_THROWN) return CLJ_THROWN;
		clj_value r = b_host_type(&text, 1);
		clj_release(text);
		return r;
	}
	if (!clj_is_string(name)) return clj_throw_msg("host-type expects a string or a symbol, got: %s", clj_type_name(name));
	clj_value t = clj_host_type_named(clj_string_bytes(name), clj_string_len(name));
	if (clj_is_nil(t)) {
		return clj_throw_msg(clj_host_type_available() ? "Unable to resolve host type: %s" : "No host type resolver: %s",
		                     clj_string_bytes(name));
	}
	return t;
}

// @ai-generated(solo)
static clj_value b_coro_cancelled_p(const clj_value *args, size_t n) {
	(void)args, (void)n;
	return clj_bool(clj_coro_current_cancelled());
}

// ---- profiling

static clj_value b_profile_start(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	clj_profile_start();
	return CLJ_NIL;
}

static clj_value b_profile_stop(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_profile_stop();
}

// ---- strings and output

typedef struct {
	char  *data;
	size_t len, cap;
} buf;

static void buf_put(buf *b, const char *s, size_t n) {
	// memcpy from or to NULL is undefined even for 0 bytes, and an untouched buf has no data yet.
	if (n == 0) return;
	if (b->len + n > b->cap) {
		size_t cap = b->cap ? b->cap : 64;
		while (cap < b->len + n) cap *= 2;
		b->data = realloc(b->data, cap);
		if (!b->data) clj_fatal("out of memory");
		b->cap = cap;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
}

// Consumes an owned string, or CLJ_THROWN.
static bool put_text(buf *b, clj_value s) {
	if (s == CLJ_THROWN) return false;
	buf_put(b, clj_string_bytes(s), clj_string_len(s));
	clj_release(s);
	return true;
}

// Clojure `str`: strings raw, nil empty, everything else as pr-str (a char as its text).
static bool put_str(buf *b, clj_value v) {
	if (clj_is_nil(v)) return true;
	if (clj_is_string(v)) {
		buf_put(b, clj_string_bytes(v), clj_string_len(v));
		return true;
	}
	if (clj_is_double(v) && !isfinite(clj_double_val(v))) {
		double d = clj_double_val(v);
		const char *text = d != d ? "NaN" : d < 0 ? "-Infinity" : "Infinity";
		buf_put(b, text, strlen(text));
		return true;
	}
	// BigInt.toString and BigDecimal.toString carry no tag, where print-method appends N and M.
	if (clj_is_bigint(v) || clj_is_decimal(v)) {
		clj_value t = clj_is_bigint(v) ? clj_bigint_to_string(v) : clj_decimal_to_string(v);
		buf_put(b, clj_string_bytes(t), clj_string_len(t));
		clj_release(t);
		return true;
	}
	if (clj_is_char(v)) {
		uint32_t cp = clj_char_val(v);
		char     u[4];
		size_t   n;
		if (cp < 0x80) {
			u[0] = (char)cp;
			n = 1;
		} else if (cp < 0x800) {
			u[0] = (char)(0xC0 | (cp >> 6));
			u[1] = (char)(0x80 | (cp & 0x3F));
			n = 2;
		} else if (cp < 0x10000) {
			u[0] = (char)(0xE0 | (cp >> 12));
			u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
			u[2] = (char)(0x80 | (cp & 0x3F));
			n = 3;
		} else {
			u[0] = (char)(0xF0 | (cp >> 18));
			u[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
			u[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
			u[3] = (char)(0x80 | (cp & 0x3F));
			n = 4;
		}
		buf_put(b, u, n);
		return true;
	}
	if (clj_is_uuid(v)) {
		char text[37];
		clj_uuid_format(v, text);
		buf_put(b, text, 36);
		return true;
	}
	// Date.toString would be "Fri Apr 12 ..." in the host zone; the printed form is the useful one (docs/jvm-differences.md).
	if (clj_is_inst(v)) {
		char text[40];
		clj_inst_format(v, text, sizeof text);
		buf_put(b, text, strlen(text));
		return true;
	}
	// Pattern.toString is the pattern text, where print-method wraps it in #"".
	if (clj_is_regex(v)) {
		clj_value p = clj_regex_pattern(v);
		buf_put(b, clj_string_bytes(p), clj_string_len(p));
		return true;
	}
	return put_text(b, clj_pr_str(v));
}

// Printing realizes lazy seqs, so it can throw.
static bool put_print(buf *b, clj_value v) { return put_text(b, clj_pr_str_dynamic(v, false)); }

static bool put_pr(buf *b, clj_value v) { return put_text(b, clj_pr_str_dynamic(v, true)); }

static bool join(buf *b, const clj_value *args, size_t n, bool (*put)(buf *, clj_value), bool spaces) {
	for (size_t i = 0; i < n; i++) {
		if (spaces && i) buf_put(b, " ", 1);
		if (!put(b, args[i])) {
			free(b->data);
			return false;
		}
	}
	return true;
}

static clj_value b_str(const clj_value *args, size_t n) {
	buf b = {0};
	if (!join(&b, args, n, put_str, false)) return CLJ_THROWN;
	clj_value s = clj_string_new(b.data, b.len);
	free(b.data);
	return s;
}

clj_value clj_str_value(clj_value v) { return b_str(&v, 1); }

static clj_value b_pr_str(const clj_value *args, size_t n) {
	buf b = {0};
	if (!join(&b, args, n, put_pr, true)) return CLJ_THROWN;
	clj_value s = clj_string_new(b.data, b.len);
	free(b.data);
	return s;
}

static clj_value print_line(const clj_value *args, size_t n, bool (*put)(buf *, clj_value), bool newline) {
	buf b = {0};
	if (!join(&b, args, n, put, true)) return CLJ_THROWN;
	if (newline) buf_put(&b, "\n", 1);
	clj_output(b.data ? b.data : "", b.len);
	free(b.data);
	return CLJ_NIL;
}

static clj_value b_println(const clj_value *args, size_t n) { return print_line(args, n, put_print, true); }
static clj_value b_print(const clj_value *args, size_t n) { return print_line(args, n, put_print, false); }
static clj_value b_prn(const clj_value *args, size_t n) { return print_line(args, n, put_pr, true); }
static clj_value b_pr(const clj_value *args, size_t n) { return print_line(args, n, put_pr, false); }

// ---- functions

static clj_value b_identity(const clj_value *args, size_t n) {
	(void)n;
	return clj_retain(args[0]);
}

static clj_value b_apply(const clj_value *args, size_t n) { return clj_apply(args[0], args + 1, n - 1); }

// ---- vars

static clj_value b_resolve(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_symbol(args[0])) return clj_throw_msg("resolve expects a symbol, got: %s", clj_type_name(args[0]));
	return clj_ns_resolve(clj_ns_current(), args[0]);
}

// A core.clj protocol method (clojure.core/name) applied to v, for a builtin's fallback on user types.
static clj_value core_method(const char *name, clj_value v) {
	clj_value sym = clj_symbol_from_cstr(name);
	clj_value var = clj_ns_resolve(clj_ns_core(), sym);
	clj_release(sym);
	if (clj_is_nil(var) || !clj_var_is_bound(var)) return CLJ_UNBOUND;
	clj_value f = clj_var_deref(var);
	clj_value r = clj_invoke(f, &v, 1);
	clj_release(f);
	return r;
}

// (deref x), (deref x ms timeout-val): the timed form is for what can wait — a promise, a future, a channel.
static clj_value b_deref(const clj_value *args, size_t n) {
	if (clj_is_chan(args[0])) {
		if (n == 1) return clj_chan_deref(args[0]);
		if (n != 3) return clj_throw_msg("deref with a timeout expects (deref ref timeout-ms timeout-val)");
		if (!clj_is_fixnum(args[1])) return clj_throw_msg("deref expects a timeout in milliseconds, got: %s", clj_type_name(args[1]));
		return clj_chan_deref_timeout(args[0], clj_fixnum_val(args[1]), args[2]);
	}
	if (n != 1) {
		if (clj_is_instance(args[0])) {
			clj_value r = core_method("-deref", args[0]);
			if (r != CLJ_UNBOUND) return r;
		}
		return clj_throw_msg("deref with a timeout is not supported on this type: %s", clj_type_name(args[0]));
	}
	if (clj_is_atom(args[0])) return clj_atom_deref(args[0]);
	if (clj_is_var(args[0])) return clj_var_deref(args[0]);
	if (clj_is_reduced(args[0])) return clj_retain(clj_reduced_value(args[0]));
	if (clj_is_volatile(args[0])) return clj_volatile_deref(args[0]);
	if (clj_is_instance(args[0])) {
		clj_value r = core_method("-deref", args[0]);
		if (r != CLJ_UNBOUND) return r;
	}
	return clj_throw_msg("deref not supported on this type: %s", clj_type_name(args[0]));
}

// ---- metadata

static clj_value b_meta(const clj_value *args, size_t n) {
	(void)n;
	return clj_meta(args[0]);
}

clj_value clj_with_meta2(clj_value v, clj_value m) { return clj_with_meta(clj_retain(v), m); }

static clj_value b_with_meta(const clj_value *args, size_t n) {
	(void)n;
	return clj_with_meta2(args[0], args[1]);
}

static clj_value not_a_reference(const char *what, clj_value v) {
	return clj_throw_msg("%s expects a var, an atom or a namespace, got: %s", what, clj_type_name(v));
}

static clj_value b_reset_meta(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_nil(args[1]) && !clj_has_core(args[1], CLJ_CORE_MAP)) return clj_throw_msg("reset-meta! expects a map, got: %s", clj_type_name(args[1]));
	if (clj_is_atom(args[0])) return clj_atom_reset_meta(args[0], args[1]);
	if (clj_is_ns(args[0])) {
		clj_ns_set_meta(args[0], args[1]);
		return clj_retain(args[1]);
	}
	if (!clj_is_var(args[0])) return not_a_reference("reset-meta!", args[0]);
	clj_var_set_meta(args[0], args[1]);
	return clj_retain(args[1]);
}

// A namespace's meta is written under the namespace lock, so f runs once and a concurrent alter-meta! is lost.
static clj_value ns_alter_meta(clj_value ns, clj_value f, const clj_value *args, size_t n) {
	clj_value  small[8];
	clj_value *call = n + 1 <= sizeof small / sizeof *small ? small : malloc((n + 1) * sizeof *call);
	if (!call) clj_fatal("out of memory");
	call[0] = clj_ns_meta(ns);
	memcpy(call + 1, args, n * sizeof *call);
	clj_value m = clj_invoke(f, call, n + 1);
	if (call != small) free(call);
	if (m == CLJ_THROWN) return m;
	if (!clj_is_nil(m) && !clj_has_core(m, CLJ_CORE_MAP)) {
		clj_value e = clj_throw_msg("alter-meta! fn must return a map, got: %s", clj_type_name(m));
		clj_release(m);
		return e;
	}
	clj_ns_set_meta(ns, m);
	return m;
}

// (alter-meta! var f & args): a CAS loop, so f may run more than once under contention; on an atom f runs
// once under its lock.
// @ai-generated(guided)
static clj_value b_alter_meta(const clj_value *args, size_t n) {
	if (clj_is_atom(args[0])) return clj_atom_alter_meta(args[0], args[1], args + 2, n - 2);
	if (clj_is_ns(args[0])) return ns_alter_meta(args[0], args[1], args + 2, n - 2);
	if (!clj_is_var(args[0])) return not_a_reference("alter-meta!", args[0]);
	clj_value  small[8];
	clj_value *call = n <= sizeof small / sizeof *small ? small : malloc(n * sizeof *call);
	if (!call) clj_fatal("out of memory");
	memcpy(call + 1, args + 2, (n - 2) * sizeof *call);
	clj_value m;
	for (;;) {
		clj_value old = clj_var_meta(args[0]);
		call[0] = old;
		m = clj_invoke(args[1], call, n - 1);
		if (m == CLJ_THROWN) break;
		if (!clj_is_nil(m) && !clj_has_core(m, CLJ_CORE_MAP)) {
			clj_value e = clj_throw_msg("alter-meta! fn must return a map, got: %s", clj_type_name(m));
			clj_release(m);
			m = e;
			break;
		}
		if (clj_var_cas_meta(args[0], old, m)) break;
		clj_release(m);
	}
	if (call != small) free(call);
	return m;
}

// ---- registration

typedef struct {
	const char   *name;
	clj_native_fn fn;
	uint32_t      min, max;
} entry;

#define ANY CLJ_ARITY_ANY

static clj_value int_args(const char *what, const clj_value *args, size_t n, int64_t *out) {
	for (size_t i = 0; i < n; i++) {
		if (!clj_int64_of(args[i], &out[i])) return clj_throw_msg("%s: %s cannot be cast to an integer", what, clj_type_name(args[i]));
	}
	return CLJ_NIL;
}

clj_value clj_quot(clj_value a, clj_value b) {
	num x, y;
	if (!to_num(a, &x) || !to_num(b, &y)) return clj_num_arith(a, b, CLJ_OP_QUOT);
	if (x.is_double || y.is_double) {
		double q = as_double(&x) / as_double(&y);
		// Numbers.quotient rounds through BigDecimal past the long range, which rejects an infinity or a NaN.
		if (!isfinite(q)) return clj_throw_msg(as_double(&y) == 0 ? "Divide by zero" : "Infinite or NaN");
		return clj_double_new(q < 0 ? __builtin_ceil(q) : __builtin_floor(q));
	}
	if (y.i == 0) return clj_throw_msg("Divide by zero");
	return clj_long_new(x.i / y.i);
}

clj_value clj_rem(clj_value a, clj_value b) {
	num x, y;
	if (!to_num(a, &x) || !to_num(b, &y)) return clj_num_arith(a, b, CLJ_OP_REM);
	if (x.is_double || y.is_double) {
		double p = as_double(&x), d = as_double(&y), q = p / d;
		if (!isfinite(q)) return clj_throw_msg(d == 0 ? "Divide by zero" : "Infinite or NaN");
		// Numbers.remainder is n - trunc(n/d)*d, not fmod: (rem 1 ##Inf) is NaN, not 1.0.
		return clj_double_new(p - __builtin_trunc(q) * d);
	}
	if (y.i == 0) return clj_throw_msg("Divide by zero");
	if (y.i == -1) return clj_fixnum(0);
	return clj_fixnum(x.i % y.i);
}

static clj_value b_quot(const clj_value *args, size_t n) {
	(void)n;
	return clj_quot(args[0], args[1]);
}

static clj_value b_rem(const clj_value *args, size_t n) {
	(void)n;
	return clj_rem(args[0], args[1]);
}

typedef enum { BIT_AND, BIT_OR, BIT_XOR, BIT_AND_NOT } bit_op;

static clj_value bitwise(const char *what, const clj_value *args, size_t n, bit_op op) {
	int64_t v[2];
	if (int_args(what, args, 2, v) == CLJ_THROWN) return CLJ_THROWN;
	int64_t r = v[0];
	for (size_t i = 1; i < n; i++) {
		int64_t y;
		if (int_args(what, args + i, 1, &y) == CLJ_THROWN) return CLJ_THROWN;
		switch (op) {
		case BIT_AND: r &= y; break;
		case BIT_OR: r |= y; break;
		case BIT_XOR: r ^= y; break;
		case BIT_AND_NOT: r &= ~y; break;
		}
	}
	return clj_long_new(r);
}

static clj_value b_bit_and(const clj_value *args, size_t n) { return bitwise("bit-and", args, n, BIT_AND); }
static clj_value b_bit_or(const clj_value *args, size_t n) { return bitwise("bit-or", args, n, BIT_OR); }
static clj_value b_bit_xor(const clj_value *args, size_t n) { return bitwise("bit-xor", args, n, BIT_XOR); }
static clj_value b_bit_and_not(const clj_value *args, size_t n) { return bitwise("bit-and-not", args, n, BIT_AND_NOT); }

static clj_value b_bit_not(const clj_value *args, size_t n) {
	(void)n;
	int64_t v;
	if (int_args("bit-not", args, 1, &v) == CLJ_THROWN) return CLJ_THROWN;
	return clj_long_new(~v);
}

static clj_value shift(const char *what, const clj_value *args, int dir) {
	int64_t v[2];
	if (int_args(what, args, 2, v) == CLJ_THROWN) return CLJ_THROWN;
	int64_t  x = v[0];
	unsigned s = (unsigned)(v[1] & 63);
	int64_t  r;
	if (dir > 0) r = (int64_t)((uint64_t)x << s);
	else if (dir < 0) r = x >> s;
	else r = (int64_t)((uint64_t)x >> s);
	return clj_long_new(r);
}

static clj_value b_bit_shift_left(const clj_value *args, size_t n) { (void)n; return shift("bit-shift-left", args, 1); }
static clj_value b_bit_shift_right(const clj_value *args, size_t n) { (void)n; return shift("bit-shift-right", args, -1); }
static clj_value b_unsigned_bit_shift_right(const clj_value *args, size_t n) { (void)n; return shift("unsigned-bit-shift-right", args, 0); }

static clj_value b_bit_test(const clj_value *args, size_t n) {
	(void)n;
	int64_t v[2];
	if (int_args("bit-test", args, 2, v) == CLJ_THROWN) return CLJ_THROWN;
	return clj_bool(((uint64_t)v[0] >> (v[1] & 63)) & 1);
}

static clj_value bit_set_op(const char *what, const clj_value *args, int op) {
	int64_t v[2];
	if (int_args(what, args, 2, v) == CLJ_THROWN) return CLJ_THROWN;
	uint64_t mask = (uint64_t)1 << (v[1] & 63);
	uint64_t x = (uint64_t)v[0];
	uint64_t r = op > 0 ? x | mask : op < 0 ? x & ~mask : x ^ mask;
	return clj_long_new((int64_t)r);
}

static clj_value b_bit_set(const clj_value *args, size_t n) { (void)n; return bit_set_op("bit-set", args, 1); }
static clj_value b_bit_clear(const clj_value *args, size_t n) { (void)n; return bit_set_op("bit-clear", args, -1); }
static clj_value b_bit_flip(const clj_value *args, size_t n) { (void)n; return bit_set_op("bit-flip", args, 0); }

// SplitMix64 seeded from the clock once per thread: no cryptographic claims.
static clj_value b_rand_star(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	static _Thread_local uint64_t state;
	if (!state) state = (uint64_t)clj_next_id() * 0x9E3779B97F4A7C15ull ^ (uint64_t)(uintptr_t)&state;
	state += 0x9E3779B97F4A7C15ull;
	uint64_t z = state;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	z ^= z >> 31;
	return clj_double_new((double)(z >> 11) * (1.0 / 9007199254740992.0));
}

// ---- sorted collections

// cmp nil is the C comparator (compare.h), which is what sorted-map and sorted-set build with.
static clj_value sorted_from_args(bool map, clj_value cmp, const clj_value *args, size_t n) {
	if (map && n % 2) {
		clj_value text = clj_pr_str(args[n - 1]);
		clj_value e = clj_throw_msg("No value supplied for key: %s", clj_string_bytes(text));
		clj_release(text);
		return e;
	}
	clj_value c = map ? clj_sorted_map_new(cmp) : clj_sorted_set_new(cmp);
	for (size_t i = 0; i < n && c != CLJ_THROWN; i += map ? 2 : 1) c = clj_sorted_assoc(c, args[i], map ? args[i + 1] : CLJ_NIL);
	return c;
}

static clj_value sorted_by_args(bool map, const clj_value *args, size_t n) {
	if (!clj_has_core(args[0], CLJ_CORE_FN)) return clj_throw_msg("comparator must be a function, got: %s", clj_type_name(args[0]));
	return sorted_from_args(map, args[0], args + 1, n - 1);
}

static clj_value b_sorted_map_by(const clj_value *args, size_t n) { return sorted_by_args(true, args, n); }
static clj_value b_sorted_set_by(const clj_value *args, size_t n) { return sorted_by_args(false, args, n); }
static clj_value b_sorted_map(const clj_value *args, size_t n) { return sorted_from_args(true, CLJ_NIL, args, n); }
static clj_value b_sorted_set(const clj_value *args, size_t n) { return sorted_from_args(false, CLJ_NIL, args, n); }

// (sort-by keyfn coll) takes the C comparator; the 3-arity needs a real fn, as a nil comparator is a
// NullPointerException on the JVM and the suite tests for the throw.
static clj_value b_sort_by(const clj_value *args, size_t n) {
	if (n == 2) return clj_sort_by(args[1], args[0], CLJ_NIL);
	if (!clj_has_core(args[1], CLJ_CORE_FN)) return clj_throw_msg("comparator must be a function, got: %s", clj_type_name(args[1]));
	return clj_sort_by(args[2], args[0], args[1]);
}

static clj_value b_sorted_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_sorted(args[0]));
}

static clj_value sorted_arg(const char *name, clj_value v) {
	if (clj_is_sorted(v)) return CLJ_NIL;
	return clj_throw_msg("%s not supported on this type: %s", name, clj_type_name(v));
}

static clj_value b_sorted_seq(const clj_value *args, size_t n) {
	(void)n;
	if (sorted_arg("rseq", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	return clj_sorted_seq(args[0], clj_truthy(args[1]));
}

static clj_value b_sorted_seq_from(const clj_value *args, size_t n) {
	(void)n;
	if (sorted_arg("subseq", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	return clj_sorted_seq_from(args[0], args[1], clj_truthy(args[2]));
}

static clj_value b_sorted_compare(const clj_value *args, size_t n) {
	(void)n;
	if (sorted_arg("subseq", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	return clj_sorted_compare(args[0], args[1], args[2]);
}

static const entry entries[] = {
	{"quot", b_quot, 2, 2},        {"rem", b_rem, 2, 2},         {"bit-and", b_bit_and, 2, ANY}, {"bit-or", b_bit_or, 2, ANY},
	{"bit-xor", b_bit_xor, 2, ANY}, {"bit-and-not", b_bit_and_not, 2, ANY}, {"bit-not", b_bit_not, 1, 1},
	{"bit-shift-left", b_bit_shift_left, 2, 2}, {"bit-shift-right", b_bit_shift_right, 2, 2},
	{"unsigned-bit-shift-right", b_unsigned_bit_shift_right, 2, 2}, {"bit-test", b_bit_test, 2, 2}, {"bit-set", b_bit_set, 2, 2},
	{"bit-clear", b_bit_clear, 2, 2}, {"bit-flip", b_bit_flip, 2, 2}, {"rand*", b_rand_star, 0, 0},
	{"+", b_add, 0, ANY},          {"-", b_sub, 1, ANY},         {"*", b_mul, 0, ANY},          {"/", b_div, 1, ANY},
	{"<", b_lt, 1, ANY},           {"<=", b_le, 1, ANY},         {">", b_gt, 1, ANY},           {">=", b_ge, 1, ANY},
	{"=", b_eq, 1, ANY},           {"not=", b_neq, 1, ANY},      {"identical?", b_identical, 2, 2},      {"hash", b_hash, 1, 1},      {"inc", b_inc, 1, 1},          {"dec", b_dec, 1, 1},
	{"not", b_not, 1, 1},          {"nil?", b_nil, 1, 1},        {"zero?", b_zero, 1, 1},       {"pos?", b_pos, 1, 1},
	{"neg?", b_neg, 1, 1},         {"even?", b_even, 1, 1},      {"odd?", b_odd, 1, 1},         {"number?", b_number, 1, 1},
	{"string?", b_string, 1, 1},   {"keyword?", b_keyword, 1, 1}, {"symbol?", b_symbol, 1, 1},  {"vector?", b_vector_p, 1, 1},
	{"map?", b_map, 1, 1},         {"set?", b_set_p, 1, 1},      {"list?", b_list_p, 1, 1},    {"fn?", b_fn, 1, 1},           {"char?", b_char_p, 1, 1},
	{"integer?", b_integer_p, 1, 1},
	{"seq?", b_seq_p, 1, 1},       {"seqable?", b_seqable_p, 1, 1}, {"sequential?", b_sequential_p, 1, 1}, {"coll?", b_coll_p, 1, 1},
	{"counted?", b_counted_p, 1, 1}, {"ifn?", b_ifn_p, 1, 1},    {"associative?", b_associative_p, 1, 1}, {"indexed?", b_indexed_p, 1, 1},
	{"get", b_get, 2, 3},          {"assoc", b_assoc, 3, ANY},   {"dissoc", b_dissoc, 1, ANY},  {"contains?", b_contains, 2, 2},
	{"count", b_count, 1, 1},      {"conj", b_conj, 0, ANY},     {"nth", b_nth, 2, 3},          {"first", b_first, 1, 1},
	{"rest", b_rest, 1, 1},        {"next", b_next, 1, 1},       {"cons", b_cons, 2, 2},        {"list", b_list, 0, ANY},
	{"vector", b_vector, 0, ANY},  {"hash-map", b_hash_map, 0, ANY}, {"hash-set", b_hash_set, 0, ANY}, {"set", b_set, 1, 1},
	{"disj", b_disj, 1, ANY},      {"empty", b_empty_coll, 1, 1}, {"str", b_str, 0, ANY},    {"pr-str", b_pr_str, 0, ANY},
	{"sorted-map-by", b_sorted_map_by, 1, ANY}, {"sorted-set-by", b_sorted_set_by, 1, ANY}, {"sorted?", b_sorted_p, 1, 1},
	{"sorted-map*", b_sorted_map, 0, ANY}, {"sorted-set*", b_sorted_set, 0, ANY}, {"sort-by*", b_sort_by, 2, 3},
	{"sorted-seq*", b_sorted_seq, 2, 2}, {"sorted-seq-from*", b_sorted_seq_from, 3, 3}, {"sorted-compare*", b_sorted_compare, 3, 3},
	{"pr", b_pr, 0, ANY},          {"prn", b_prn, 0, ANY},       {"print", b_print, 0, ANY},    {"println", b_println, 0, ANY},
	{"identity", b_identity, 1, 1}, {"apply", b_apply, 2, ANY},  {"seq", b_seq, 1, 1},          {"lazy-seq*", b_lazy_seq_star, 1, 1},
	{"lazy-seq-realized?*", b_realized_p, 1, 1}, {"range*", b_range_star, 3, 3}, {"list*", b_list_star, 1, ANY}, {"empty?", b_empty, 1, 1},
	{"second", b_second, 1, 1},    {"last", b_last, 1, 1},       {"butlast", b_butlast, 1, 1},  {"reverse", b_reverse, 1, 1},
	{"into", b_into, 2, 3},        {"symbol", b_make_symbol, 1, 2}, {"keyword", b_make_keyword, 1, 2}, {"name", b_name, 1, 1},
	{"namespace", b_namespace, 1, 1}, {"gensym", b_gensym, 0, 1}, {"macroexpand-1", b_macroexpand_1, 1, 1}, {"macroexpand", b_macroexpand, 1, 1},
	{"ex-info", b_ex_info, 2, 3},  {"ex-message", b_ex_message, 1, 1}, {"ex-data", b_ex_data, 1, 1}, {"ex-cause", b_ex_cause, 1, 1},
	{"ex-trace", b_ex_trace, 1, 1}, {"ex-type", b_ex_type, 1, 1}, {"cancelled?*", b_coro_cancelled_p, 0, 0},
	{"host-type", b_host_type, 1, 1},
	{"profile-start!", b_profile_start, 0, 0}, {"profile-stop!", b_profile_stop, 0, 0},
	{"resolve", b_resolve, 1, 1},  {"deref", b_deref, 1, 3},     {"meta", b_meta, 1, 1},        {"with-meta", b_with_meta, 2, 2},
	{"reset-meta!", b_reset_meta, 2, 2}, {"alter-meta!", b_alter_meta, 2, ANY},
	{"reduce", b_reduce, 2, 3},    {"reduce-kv", b_reduce_kv, 3, 3}, {"reduced", b_reduced, 1, 1}, {"reduced?", b_reduced_p, 1, 1},
	{"fused-reduce*", clj_fused_reduce, 3, 4}, {"fused-into*", clj_fused_into, 3, 3}, {"fused-count*", clj_fused_count, 2, 2},
	{"unreduced", b_unreduced, 1, 1}, {"ensure-reduced", b_ensure_reduced, 1, 1}, {"volatile!", b_volatile, 1, 1},
	{"volatile?", b_volatile_p, 1, 1}, {"vreset!", b_vreset, 2, 2},
	{"atom", b_atom, 1, ANY},      {"atom?", b_atom_p, 1, 1},     {"reset!", b_reset, 2, 2},     {"reset-vals!", b_reset_vals, 2, 2},
	{"swap!", b_swap, 2, ANY},     {"swap-vals!", b_swap_vals, 2, ANY}, {"compare-and-set!", b_compare_and_set, 3, 3},
	{"add-watch", b_add_watch, 3, 3}, {"remove-watch", b_remove_watch, 2, 2}, {"set-validator!", b_set_validator, 2, 2},
	{"get-validator", b_get_validator, 1, 1}, {"monitor-enter*", b_monitor_enter, 1, 1}, {"monitor-exit*", b_monitor_exit, 1, 1},
};

void clj_builtin_bind(const char *name_text, clj_native_fn fn, uint32_t min, uint32_t max) {
	clj_value core = clj_ns_core();
	clj_value name = clj_string_from_cstr(name_text);
	clj_value sym = clj_symbol_new(CLJ_NIL, name);
	clj_value qualified = clj_symbol_new(clj_symbol_name(clj_ns_name(core)), name);
	clj_value f = clj_fn_native(qualified, fn, min, max);
	clj_var_bind_root(clj_ns_intern(core, sym), f);
	clj_release(f);
	clj_release(qualified);
	clj_release(sym);
	clj_release(name);
}

void clj_builtins_install(void) {
	for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) clj_builtin_bind(entries[i].name, entries[i].fn, entries[i].min, entries[i].max);
	clj_ns_builtins_install();
	clj_string_builtins_install();
	clj_number_builtins_install();
	clj_array_builtins_install();
	clj_regex_builtins_install();
	clj_uuid_builtins_install();
	clj_inst_builtins_install();
	clj_format_builtins_install();
	clj_objc_builtins_install();
}
