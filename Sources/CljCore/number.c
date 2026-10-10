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
	clj_double *b = clj_alloc_uninit(&clj_double_type, sizeof *b);
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

// Past the long range Numbers.quotient goes through BigDecimal, which rejects an infinity or a NaN.
static bool double_div_ok(double p, double q, double *quotient) {
	*quotient = p / q;
	return isfinite(*quotient);
}

// (double)Long.MAX_VALUE, which rounds up to 2^63: the bound Numbers tests the quotient against.
#define CLJ_JLONG_LIMIT 9223372036854775808.0

// Numbers truncates through a long cast, so a zero quotient is +0.0 where trunc would keep the sign.
static double integral_quotient(double r) {
	return r > -CLJ_JLONG_LIMIT && r < CLJ_JLONG_LIMIT ? (double)(int64_t)r : trunc(r);
}

clj_value clj_double_quot(double p, double q) {
	double r;
	if (!double_div_ok(p, q, &r)) return clj_throw_msg(q == 0 ? "Divide by zero" : "Infinite or NaN");
	return clj_double_new(integral_quotient(r));
}

clj_value clj_double_rem(double p, double q) {
	double r;
	if (!double_div_ok(p, q, &r)) return clj_throw_msg(q == 0 ? "Divide by zero" : "Infinite or NaN");
	// Its own statement: fused into the subtraction the product stays unrounded, where the JVM rounds it.
	double whole = integral_quotient(r) * q;
	return clj_double_new(p - whole);
}

static clj_value double_arith(double p, double q, clj_num_op op) {
	switch (op) {
	case CLJ_OP_ADD: return clj_double_new(p + q);
	case CLJ_OP_SUB: return clj_double_new(p - q);
	case CLJ_OP_MUL: return clj_double_new(p * q);
	case CLJ_OP_DIV: return clj_double_new(p / q);
	case CLJ_OP_QUOT: return clj_double_quot(p, q);
	case CLJ_OP_REM: return clj_double_rem(p, q);
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
			// An exact quotient that no long holds is a bigint, not an error: `/` is the rational division.
			if (x == INT64_MIN) return integer_arith(a, b, CLJ_OP_DIV, true);
			r = -x;
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

// *math-context*, which with-precision binds to {:precision n :rounding :HALF_UP}; nil is exact arithmetic.
// @ai-generated(solo)
static bool math_context(clj_math_context *mc) {
	static const char *const modes[] = {"UP", "DOWN", "CEILING", "FLOOR", "HALF_UP", "HALF_DOWN", "HALF_EVEN", "UNNECESSARY"};
	static clj_value         var = CLJ_NIL;
	mc->precision = 0;
	mc->rounding = CLJ_ROUND_HALF_UP;
	// Vars are immortal, so a racing second lookup stores the same var.
	if (clj_is_nil(var)) {
		clj_value sym = clj_symbol_from_cstr("*math-context*");
		var = clj_ns_resolve(clj_ns_core(), sym);
		clj_release(sym);
		if (clj_is_nil(var)) return true;
	}
	if (!clj_var_is_bound(var)) return true;
	clj_value ctx = clj_var_deref(var);
	if (clj_is_nil(ctx)) return true;
	clj_value p = clj_get(ctx, clj_keyword_from_cstr("precision"), CLJ_NIL);
	clj_value m = p == CLJ_THROWN ? CLJ_THROWN : clj_get(ctx, clj_keyword_from_cstr("rounding"), CLJ_NIL);
	if (m == CLJ_THROWN) {
		clj_release(p);
		clj_release(ctx);
		return false;
	}
	int64_t digits = -1;
	int       mode = -1;
	if (clj_is_keyword(m) && clj_is_nil(clj_keyword_ns(m))) {
		clj_value name = clj_keyword_name(m);
		for (int i = 0; i < (int)(sizeof modes / sizeof *modes); i++) {
			if (strlen(modes[i]) == clj_string_len(name) && memcmp(modes[i], clj_string_bytes(name), clj_string_len(name)) == 0) mode = i;
		}
	}
	bool ok = clj_int64_of(p, &digits) && digits >= 0 && digits <= INT32_MAX && mode >= 0;
	clj_release(p);
	clj_release(m);
	if (!ok) {
		clj_value text = clj_pr_str_max(ctx, 200);
		clj_release(ctx);
		clj_throw_msg("*math-context* must be nil or {:precision n :rounding mode}, not %s", clj_string_bytes(text));
		clj_release(text);
		return false;
	}
	clj_release(ctx);
	mc->precision = (uint32_t)digits;
	mc->rounding = (clj_rounding)mode;
	return true;
}

// A ratio becomes a decimal as Numbers.toBigDecimal makes it: numerator divided by denominator under mc.
static clj_value as_decimal(clj_value v, const clj_math_context *mc) {
	if (clj_is_decimal(v)) return clj_retain(v);
	if (!clj_is_ratio(v)) return clj_decimal_new(v, 0);
	clj_value n = clj_decimal_new(clj_ratio_num(v), 0), d = clj_decimal_new(clj_ratio_den(v), 0);
	clj_value r = clj_decimal_div_mc(n, d, mc);
	clj_release(n);
	clj_release(d);
	return r;
}

clj_value clj_num_ratio_to_decimal(clj_value ratio) {
	clj_math_context mc;
	if (!math_context(&mc)) return CLJ_THROWN;
	return as_decimal(ratio, &mc);
}

clj_value clj_num_decimal_negate(clj_value v) {
	clj_math_context mc;
	if (!math_context(&mc)) return CLJ_THROWN;
	clj_value neg = clj_decimal_neg(v), r = clj_decimal_round(neg, &mc);
	clj_release(neg);
	return r;
}

static clj_value decimal_arith(clj_value a, clj_value b, clj_num_op op) {
	clj_math_context mc;
	if (!math_context(&mc)) return CLJ_THROWN;
	clj_value x = as_decimal(a, &mc);
	if (x == CLJ_THROWN) return x;
	clj_value y = as_decimal(b, &mc);
	if (y == CLJ_THROWN) {
		clj_release(x);
		return y;
	}
	clj_value r;
	switch (op) {
	case CLJ_OP_SUB: {
		// Numbers.minus is add(x, negate(y)), and negate rounds y first: (with-precision 2 (- 100M 99.99M)) is 0M.
		clj_value neg = clj_decimal_neg(y), negr = clj_decimal_round(neg, &mc);
		clj_release(neg);
		if (negr == CLJ_THROWN) {
			r = negr;
			break;
		}
		clj_value exact = clj_decimal_add(x, negr);
		clj_release(negr);
		r = clj_decimal_round(exact, &mc);
		clj_release(exact);
		break;
	}
	case CLJ_OP_ADD:
	case CLJ_OP_MUL: {
		clj_value exact = op == CLJ_OP_ADD ? clj_decimal_add(x, y) : clj_decimal_mul(x, y);
		r = clj_decimal_round(exact, &mc);
		clj_release(exact);
		break;
	}
	case CLJ_OP_DIV: {
		if (clj_num_sign(y) == 0) r = clj_throw_msg("Divide by zero");
		else r = clj_decimal_div_mc(x, y, &mc);
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
		if (!clj_decimal_integral_fits(whole, &mc)) {
			r = clj_throw_msg("Division impossible");
		} else if (op == CLJ_OP_QUOT) {
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

// @ai-generated(solo)
size_t clj_int64_decimal(int64_t v, char out[20]) {
	static const char pairs[201] = "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
	                               "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
	                               "8081828384858687888990919293949596979899";
	// -INT64_MIN does not fit an int64_t.
	uint64_t mag = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
	char     tmp[20];
	size_t   n = sizeof tmp;
	while (mag >= 100) {
		const char *d = pairs + 2 * (mag % 100);
		mag /= 100;
		tmp[--n] = d[1];
		tmp[--n] = d[0];
	}
	if (mag >= 10) {
		tmp[--n] = pairs[2 * mag + 1];
		tmp[--n] = pairs[2 * mag];
	} else {
		tmp[--n] = (char)('0' + mag);
	}
	size_t len = v < 0;
	if (len) out[0] = '-';
	memcpy(out + len, tmp + n, sizeof tmp - n);
	return len + sizeof tmp - n;
}
