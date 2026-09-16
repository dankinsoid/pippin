// @ai-generated(solo)
#include <math.h>
#include <string.h>

#include "clj/core.h"
#include "clj/error.h"
#include "clj/number.h"

// -0.0 is folded into 0.0 first: they are equal, so they must hash alike.
static uint32_t double_hash(void *self) {
	double d = clj_double_of(clj_from_ptr(self))->val;
	if (d == 0.0) d = 0.0;
	uint64_t bits;
	memcpy(&bits, &d, sizeof bits);
	return clj_fmix32((uint32_t)(bits ^ (bits >> 32)));
}

// IEEE ==, so NaN differs from everything; only pointer identity in clj_equals makes a NaN equal itself.
static bool double_equals(void *self, clj_value other) {
	return clj_is_double(other) && clj_double_of(clj_from_ptr(self))->val == clj_double_val(other);
}

const clj_type clj_double_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "double",
	.hash = double_hash,
	.equals = double_equals,
};

clj_value clj_double_new(double d) {
	clj_double *b = clj_alloc(&clj_double_type, sizeof *b);
	b->val = d;
	return clj_from_ptr(b);
}

// ---- the tower

double clj_num_to_double(clj_value v) {
	switch (clj_num_kind_of(v)) {
	case CLJ_NUM_FIXNUM: return (double)clj_fixnum_val(v);
	case CLJ_NUM_LONG: return (double)clj_long_val(v);
	case CLJ_NUM_DOUBLE: return clj_double_val(v);
	case CLJ_NUM_BIGINT: return clj_bigint_to_double(v);
	case CLJ_NUM_RATIO: return clj_ratio_to_double(v);
	case CLJ_NUM_DECIMAL: return clj_decimal_to_double(v);
	case CLJ_NUM_NONE: break;
	}
	clj_fatal("clj_num_to_double on a non-number");
}

int clj_num_sign(clj_value v) {
	switch (clj_num_kind_of(v)) {
	case CLJ_NUM_FIXNUM: {
		intptr_t i = clj_fixnum_val(v);
		return i < 0 ? -1 : i > 0 ? 1 : 0;
	}
	case CLJ_NUM_LONG: return clj_long_val(v) < 0 ? -1 : 1;
	case CLJ_NUM_DOUBLE: {
		double d = clj_double_val(v);
		return d < 0 ? -1 : d > 0 ? 1 : 0;
	}
	case CLJ_NUM_BIGINT: return clj_bigint_sign(v);
	case CLJ_NUM_RATIO: return clj_bigint_sign(clj_ratio_num(v));
	case CLJ_NUM_DECIMAL: return clj_bigint_sign(clj_decimal_unscaled(v));
	case CLJ_NUM_NONE: break;
	}
	clj_fatal("clj_num_sign on a non-number");
}

void clj_num_as_fraction(clj_value v, clj_value *num, clj_value *den) {
	switch (clj_num_kind_of(v)) {
	case CLJ_NUM_RATIO:
		*num = clj_retain(clj_ratio_num(v));
		*den = clj_retain(clj_ratio_den(v));
		return;
	case CLJ_NUM_DECIMAL: clj_decimal_as_fraction(v, num, den); return;
	case CLJ_NUM_FIXNUM:
		*num = clj_bigint_from_i64(clj_fixnum_val(v));
		*den = clj_bigint_from_i64(1);
		return;
	case CLJ_NUM_LONG:
		*num = clj_bigint_from_i64(clj_long_val(v));
		*den = clj_bigint_from_i64(1);
		return;
	case CLJ_NUM_BIGINT:
		*num = clj_retain(v);
		*den = clj_bigint_from_i64(1);
		return;
	default: break;
	}
	clj_fatal("clj_num_as_fraction on a value that is not rational");
}

clj_value clj_num_truncate(clj_value v) {
	switch (clj_num_kind_of(v)) {
	case CLJ_NUM_FIXNUM: return clj_bigint_from_i64(clj_fixnum_val(v));
	case CLJ_NUM_LONG: return clj_bigint_from_i64(clj_long_val(v));
	case CLJ_NUM_BIGINT: return clj_retain(v);
	case CLJ_NUM_DOUBLE: return clj_bigint_from_double(clj_double_val(v));
	case CLJ_NUM_DECIMAL: return clj_decimal_truncate(v);
	case CLJ_NUM_RATIO: return clj_bigint_quot(clj_ratio_num(v), clj_ratio_den(v), NULL);
	case CLJ_NUM_NONE: break;
	}
	clj_fatal("clj_num_truncate on a non-number");
}

static clj_value not_a_number(clj_value v) { return clj_throw_msg("%s cannot be cast to a number", clj_type_name(v)); }

static clj_value double_arith(double p, double q, clj_num_op op) {
	switch (op) {
	case CLJ_OP_ADD: return clj_double_new(p + q);
	case CLJ_OP_SUB: return clj_double_new(p - q);
	case CLJ_OP_MUL: return clj_double_new(p * q);
	case CLJ_OP_DIV: return clj_double_new(p / q);
	case CLJ_OP_QUOT: {
		double r = p / q;
		return clj_double_new(r < 0 ? ceil(r) : floor(r));
	}
	case CLJ_OP_REM: return clj_double_new(fmod(p, q));
	}
	clj_fatal("unknown arithmetic op");
}

// demote is set for a long-rank pair, whose integral result narrows back to the canonical integer.
static clj_value integer_arith(clj_value a, clj_value b, clj_num_op op, bool demote) {
	clj_value r;
	switch (op) {
	case CLJ_OP_ADD: r = clj_bigint_add(a, b); break;
	case CLJ_OP_SUB: r = clj_bigint_sub(a, b); break;
	case CLJ_OP_MUL: r = clj_bigint_mul(a, b); break;
	case CLJ_OP_DIV:
		if (clj_num_sign(b) == 0) return clj_throw_msg("Divide by zero");
		r = clj_ratio_new(a, b);
		if (clj_is_ratio(r)) return r;
		break;
	case CLJ_OP_QUOT:
		if (clj_num_sign(b) == 0) return clj_throw_msg("Divide by zero");
		r = clj_bigint_quot(a, b, NULL);
		break;
	case CLJ_OP_REM:
		if (clj_num_sign(b) == 0) return clj_throw_msg("Divide by zero");
		clj_release(clj_bigint_quot(a, b, &r));
		break;
	default: clj_fatal("unknown arithmetic op");
	}
	if (!demote) return r;
	clj_value narrow = clj_bigint_demote(r);
	clj_release(r);
	return narrow;
}

// Checked 64-bit, the JVM's long; a promoting operator intercepts its operands before they reach here.
static clj_value long_arith(clj_value a, clj_value b, clj_num_op op) {
	int64_t x = 0, y = 0, r = 0;
	clj_int64_of(a, &x);
	clj_int64_of(b, &y);
	bool overflow = false;
	switch (op) {
	case CLJ_OP_ADD: overflow = __builtin_add_overflow(x, y, &r); break;
	case CLJ_OP_SUB: overflow = __builtin_sub_overflow(x, y, &r); break;
	case CLJ_OP_MUL: overflow = __builtin_mul_overflow(x, y, &r); break;
	case CLJ_OP_DIV:
		if (y == 0) return clj_throw_msg("Divide by zero");
		// -1 apart, so INT64_MIN reaches neither / nor % undefined.
		if (y == -1) {
			overflow = x == INT64_MIN;
			r = overflow ? 0 : -x;
			break;
		}
		if (x % y != 0) return integer_arith(a, b, CLJ_OP_DIV, true);
		r = x / y;
		break;
	case CLJ_OP_QUOT:
		if (y == 0) return clj_throw_msg("Divide by zero");
		if (y == -1) {
			overflow = x == INT64_MIN;
			r = overflow ? 0 : -x;
			break;
		}
		r = x / y;
		break;
	case CLJ_OP_REM:
		if (y == 0) return clj_throw_msg("Divide by zero");
		r = y == -1 ? 0 : x % y;
		break;
	}
	if (overflow) return clj_throw_msg("integer overflow");
	return clj_long_new(r);
}

static clj_value ratio_arith(clj_value a, clj_value b, clj_num_op op) {
	clj_value an, ad, bn, bd;
	clj_num_as_fraction(a, &an, &ad);
	clj_num_as_fraction(b, &bn, &bd);
	clj_value num = CLJ_NIL, den = CLJ_NIL, r = CLJ_THROWN;
	switch (op) {
	case CLJ_OP_ADD:
	case CLJ_OP_SUB: {
		clj_value l = clj_bigint_mul(an, bd), x = clj_bigint_mul(bn, ad);
		num = op == CLJ_OP_ADD ? clj_bigint_add(l, x) : clj_bigint_sub(l, x);
		den = clj_bigint_mul(ad, bd);
		clj_release(l);
		clj_release(x);
		break;
	}
	case CLJ_OP_MUL:
		num = clj_bigint_mul(an, bn);
		den = clj_bigint_mul(ad, bd);
		break;
	case CLJ_OP_DIV:
		num = clj_bigint_mul(an, bd);
		den = clj_bigint_mul(ad, bn);
		break;
	case CLJ_OP_QUOT:
	case CLJ_OP_REM: {
		clj_value p = clj_bigint_mul(an, bd), q = clj_bigint_mul(ad, bn);
		if (clj_bigint_is_zero(q)) {
			clj_release(p);
			clj_release(q);
			r = clj_throw_msg("Divide by zero");
			break;
		}
		clj_value whole = clj_bigint_quot(p, q, NULL);
		clj_release(p);
		clj_release(q);
		if (op == CLJ_OP_QUOT) {
			r = whole;
			break;
		}
		// a - (quot a b) * b, over the common denominator ad*bd.
		clj_value wb = clj_bigint_mul(whole, bn), scaled = clj_bigint_mul(wb, ad), left = clj_bigint_mul(an, bd);
		num = clj_bigint_sub(left, scaled);
		den = clj_bigint_mul(ad, bd);
		clj_release(whole);
		clj_release(wb);
		clj_release(scaled);
		clj_release(left);
		break;
	}
	}
	if (!clj_is_nil(den)) {
		r = clj_bigint_is_zero(den) ? clj_throw_msg("Divide by zero") : clj_ratio_new(num, den);
		clj_release(num);
		clj_release(den);
	}
	clj_release(an);
	clj_release(ad);
	clj_release(bn);
	clj_release(bd);
	return r;
}

// CLJ_NIL for a ratio whose expansion does not terminate, which no decimal can hold.
static clj_value as_decimal(clj_value v) {
	if (clj_is_decimal(v)) return clj_retain(v);
	if (clj_is_ratio(v)) return clj_decimal_from_fraction(clj_ratio_num(v), clj_ratio_den(v));
	return clj_decimal_new(v, 0);
}

static clj_value inexact_quotient(void) {
	return clj_throw_msg("Non-terminating decimal expansion; with-precision is not supported");
}

static clj_value decimal_arith(clj_value a, clj_value b, clj_num_op op) {
	clj_value x = as_decimal(a), y = as_decimal(b), r;
	if (clj_is_nil(x) || clj_is_nil(y)) {
		clj_release(x);
		clj_release(y);
		return inexact_quotient();
	}
	switch (op) {
	case CLJ_OP_ADD: r = clj_decimal_add(x, y); break;
	case CLJ_OP_SUB: r = clj_decimal_sub(x, y); break;
	case CLJ_OP_MUL: r = clj_decimal_mul(x, y); break;
	case CLJ_OP_DIV: {
		if (clj_num_sign(y) == 0) r = clj_throw_msg("Divide by zero");
		else {
			r = clj_decimal_div(x, y);
			if (clj_is_nil(r)) r = inexact_quotient();
		}
		break;
	}
	case CLJ_OP_QUOT:
	case CLJ_OP_REM: {
		if (clj_num_sign(y) == 0) {
			r = clj_throw_msg("Divide by zero");
			break;
		}
		clj_value xn, xd, yn, yd;
		clj_decimal_as_fraction(x, &xn, &xd);
		clj_decimal_as_fraction(y, &yn, &yd);
		clj_value p = clj_bigint_mul(xn, yd), q = clj_bigint_mul(xd, yn);
		clj_value whole = clj_bigint_quot(p, q, NULL);
		clj_release(p);
		clj_release(q);
		clj_release(xn);
		clj_release(xd);
		clj_release(yn);
		clj_release(yd);
		if (op == CLJ_OP_QUOT) {
			r = clj_decimal_new(whole, 0);
		} else {
			clj_value wd = clj_decimal_new(whole, 0);
			clj_value prod = clj_decimal_mul(wd, y);
			r = clj_decimal_sub(x, prod);
			clj_release(wd);
			clj_release(prod);
		}
		clj_release(whole);
		break;
	}
	default: clj_fatal("unknown arithmetic op");
	}
	clj_release(x);
	clj_release(y);
	return r;
}

clj_value clj_num_arith(clj_value a, clj_value b, clj_num_op op) {
	clj_num_kind ka = clj_num_kind_of(a), kb = clj_num_kind_of(b);
	if (ka == CLJ_NUM_NONE) return not_a_number(a);
	if (kb == CLJ_NUM_NONE) return not_a_number(b);
	switch (ka > kb ? ka : kb) {
	case CLJ_NUM_DOUBLE: return double_arith(clj_num_to_double(a), clj_num_to_double(b), op);
	case CLJ_NUM_RATIO: return ratio_arith(a, b, op);
	case CLJ_NUM_DECIMAL: return decimal_arith(a, b, op);
	case CLJ_NUM_BIGINT: return integer_arith(a, b, op, false);
	case CLJ_NUM_LONG: return long_arith(a, b, op);
	default: return integer_arith(a, b, op, true);
	}
}

clj_value clj_num_cmp(clj_value a, clj_value b, int *out) {
	clj_num_kind ka = clj_num_kind_of(a), kb = clj_num_kind_of(b);
	if (ka == CLJ_NUM_NONE) return not_a_number(a);
	if (kb == CLJ_NUM_NONE) return not_a_number(b);
	switch (ka > kb ? ka : kb) {
	case CLJ_NUM_DOUBLE: {
		double p = clj_num_to_double(a), q = clj_num_to_double(b);
		*out = p < q ? -1 : p > q ? 1 : p == q ? 0 : 2;
		return CLJ_NIL;
	}
	// A decimal is an exact rational too, so the fraction compare covers both and cannot fail to terminate.
	case CLJ_NUM_DECIMAL:
	case CLJ_NUM_RATIO: {
		clj_value an, ad, bn, bd;
		clj_num_as_fraction(a, &an, &ad);
		clj_num_as_fraction(b, &bn, &bd);
		clj_value l = clj_bigint_mul(an, bd), r = clj_bigint_mul(bn, ad);
		*out = clj_bigint_cmp(l, r);
		clj_release(l);
		clj_release(r);
		clj_release(an);
		clj_release(ad);
		clj_release(bn);
		clj_release(bd);
		return CLJ_NIL;
	}
	case CLJ_NUM_LONG:
	case CLJ_NUM_FIXNUM: {
		int64_t x = 0, y = 0;
		clj_int64_of(a, &x);
		clj_int64_of(b, &y);
		*out = x < y ? -1 : x > y ? 1 : 0;
		return CLJ_NIL;
	}
	default: *out = clj_bigint_cmp(a, b); return CLJ_NIL;
	}
}
