// @ai-generated(solo)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/decimal.h"
#include "clj/string.h"

static void decimal_each_child(void *self, clj_visitor visit, void *ctx) {
	visit(((clj_decimal *)self)->unscaled, ctx);
}

static clj_value scale_up(clj_value u, uint32_t k) {
	if (k == 0) return clj_retain(u);
	clj_value p = clj_bigint_pow10(k);
	clj_value r = clj_bigint_mul(u, p);
	clj_release(p);
	return r;
}

// Both unscaled values brought to the larger scale, so bigint compare and add apply directly.
static int32_t align(clj_value a, clj_value b, clj_value *ua, clj_value *ub) {
	int32_t sa = clj_decimal_scale(a), sb = clj_decimal_scale(b);
	int32_t s = sa > sb ? sa : sb;
	*ua = scale_up(clj_decimal_unscaled(a), (uint32_t)(s - sa));
	*ub = scale_up(clj_decimal_unscaled(b), (uint32_t)(s - sb));
	return s;
}

// Trailing zeros carry no value but change the representation, and equal values must hash alike.
static clj_value stripped(clj_value v, int32_t *scale) {
	clj_value u = clj_retain(clj_decimal_unscaled(v));
	int32_t   s = clj_decimal_scale(v);
	if (clj_bigint_is_zero(u)) {
		*scale = 0;
		return u;
	}
	clj_value ten = clj_bigint_from_i64(10);
	for (;;) {
		clj_value rem = CLJ_NIL;
		clj_value q = clj_bigint_quot(u, ten, &rem);
		bool      divisible = clj_bigint_is_zero(rem);
		clj_release(rem);
		if (!divisible) {
			clj_release(q);
			break;
		}
		clj_release(u);
		u = q;
		s--;
	}
	clj_release(ten);
	*scale = s;
	return u;
}

static uint32_t decimal_hash(void *self) {
	int32_t   s;
	clj_value u = stripped(clj_from_ptr(self), &s);
	uint32_t  h = clj_hash_combine(clj_hash(u), clj_fmix32((uint32_t)s));
	clj_release(u);
	return h;
}

static bool decimal_equals(void *self, clj_value other) {
	return clj_is_decimal(other) && clj_decimal_cmp(clj_from_ptr(self), other) == 0;
}

const clj_type clj_decimal_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "decimal",
	.each_child = decimal_each_child,
	.hash = decimal_hash,
	.equals = decimal_equals,
};

clj_value clj_decimal_new(clj_value unscaled, int32_t scale) {
	clj_decimal *d = clj_alloc(&clj_decimal_type, sizeof *d);
	d->unscaled = clj_is_bigint(unscaled) ? clj_retain(unscaled) : clj_bigint_from_i64(clj_fixnum_val(unscaled));
	d->scale = scale;
	return clj_from_ptr(d);
}

int clj_decimal_cmp(clj_value a, clj_value b) {
	clj_value ua, ub;
	align(a, b, &ua, &ub);
	int c = clj_bigint_cmp(ua, ub);
	clj_release(ua);
	clj_release(ub);
	return c;
}

clj_value clj_decimal_add(clj_value a, clj_value b) {
	clj_value ua, ub;
	int32_t   s = align(a, b, &ua, &ub);
	clj_value sum = clj_bigint_add(ua, ub);
	clj_release(ua);
	clj_release(ub);
	clj_value r = clj_decimal_new(sum, s);
	clj_release(sum);
	return r;
}

clj_value clj_decimal_sub(clj_value a, clj_value b) {
	clj_value ua, ub;
	int32_t   s = align(a, b, &ua, &ub);
	clj_value diff = clj_bigint_sub(ua, ub);
	clj_release(ua);
	clj_release(ub);
	clj_value r = clj_decimal_new(diff, s);
	clj_release(diff);
	return r;
}

clj_value clj_decimal_mul(clj_value a, clj_value b) {
	clj_value u = clj_bigint_mul(clj_decimal_unscaled(a), clj_decimal_unscaled(b));
	clj_value r = clj_decimal_new(u, clj_decimal_scale(a) + clj_decimal_scale(b));
	clj_release(u);
	return r;
}

clj_value clj_decimal_neg(clj_value a) {
	clj_value u = clj_bigint_neg(clj_decimal_unscaled(a));
	clj_value r = clj_decimal_new(u, clj_decimal_scale(a));
	clj_release(u);
	return r;
}

// Divides out every factor of `factor`, counting them: what decides whether p/q terminates.
static clj_value strip_factor(clj_value q, uint32_t factor, uint32_t *count) {
	clj_value f = clj_bigint_from_i64(factor);
	clj_value cur = clj_retain(q);
	*count = 0;
	for (;;) {
		clj_value rem = CLJ_NIL;
		clj_value next = clj_bigint_quot(cur, f, &rem);
		bool      divisible = clj_bigint_is_zero(rem);
		clj_release(rem);
		if (!divisible) {
			clj_release(next);
			break;
		}
		clj_release(cur);
		cur = next;
		(*count)++;
	}
	clj_release(f);
	return cur;
}

static clj_value pow_small(uint32_t base, uint32_t e) {
	clj_value acc = clj_bigint_from_i64(1);
	clj_value b = clj_bigint_from_i64(base);
	while (e) {
		if (e & 1) {
			clj_value next = clj_bigint_mul(acc, b);
			clj_release(acc);
			acc = next;
		}
		e >>= 1;
		if (e) {
			clj_value sq = clj_bigint_mul(b, b);
			clj_release(b);
			b = sq;
		}
	}
	clj_release(b);
	return acc;
}

clj_value clj_decimal_from_fraction(clj_value p, clj_value q) {
	if (clj_is_bigint(q) ? clj_bigint_is_zero(q) : clj_fixnum_val(q) == 0) return CLJ_NIL;
	clj_value g = clj_bigint_gcd(p, q);
	if (clj_bigint_is_zero(g)) {
		clj_release(g);
		clj_value zero = clj_bigint_from_i64(0);
		clj_value r = clj_decimal_new(zero, 0);
		clj_release(zero);
		return r;
	}
	clj_value n = clj_bigint_quot(p, g, NULL);
	clj_value d = clj_bigint_quot(q, g, NULL);
	clj_release(g);
	if (clj_bigint_sign(d) < 0) {
		clj_value nn = clj_bigint_neg(n), nd = clj_bigint_neg(d);
		clj_release(n);
		clj_release(d);
		n = nn;
		d = nd;
	}
	uint32_t  twos, fives;
	clj_value d2 = strip_factor(d, 2, &twos);
	clj_release(d);
	clj_value d5 = strip_factor(d2, 5, &fives);
	clj_release(d2);
	int64_t one;
	bool    terminates = clj_bigint_to_i64(d5, &one) && one == 1;
	clj_release(d5);
	if (!terminates) {
		clj_release(n);
		return CLJ_NIL;
	}
	uint32_t  m = twos > fives ? twos : fives;
	clj_value a = pow_small(2, m - twos), b = pow_small(5, m - fives);
	clj_value f = clj_bigint_mul(a, b);
	clj_release(a);
	clj_release(b);
	clj_value u = clj_bigint_mul(n, f);
	clj_release(n);
	clj_release(f);
	clj_value r = clj_decimal_new(u, (int32_t)m);
	clj_release(u);
	return r;
}

clj_value clj_decimal_div(clj_value a, clj_value b) {
	if (clj_bigint_is_zero(clj_decimal_unscaled(b))) return CLJ_NIL;
	clj_value exact = clj_decimal_from_fraction(clj_decimal_unscaled(a), clj_decimal_unscaled(b));
	if (clj_is_nil(exact)) return CLJ_NIL;
	clj_value r = clj_decimal_new(clj_decimal_unscaled(exact), clj_decimal_scale(exact) + clj_decimal_scale(a) - clj_decimal_scale(b));
	clj_release(exact);
	return r;
}

clj_value clj_decimal_truncate(clj_value v) {
	int32_t s = clj_decimal_scale(v);
	if (s <= 0) return scale_up(clj_decimal_unscaled(v), (uint32_t)-s);
	clj_value p = clj_bigint_pow10((uint32_t)s);
	clj_value q = clj_bigint_quot(clj_decimal_unscaled(v), p, NULL);
	clj_release(p);
	return q;
}

void clj_decimal_as_fraction(clj_value v, clj_value *num, clj_value *den) {
	int32_t s = clj_decimal_scale(v);
	if (s <= 0) {
		*num = scale_up(clj_decimal_unscaled(v), (uint32_t)-s);
		*den = clj_bigint_from_i64(1);
		return;
	}
	*num = clj_retain(clj_decimal_unscaled(v));
	*den = clj_bigint_pow10((uint32_t)s);
}

clj_value clj_decimal_parse(const char *s, size_t n) {
	size_t i = 0;
	int    sign = 1;
	if (i < n && (s[i] == '+' || s[i] == '-')) {
		sign = s[i] == '-' ? -1 : 1;
		i++;
	}
	char  *digits = malloc(n + 2);
	if (!digits) clj_fatal("out of memory");
	size_t nd = 0, frac = 0;
	bool   any = false;
	while (i < n && s[i] >= '0' && s[i] <= '9') {
		digits[nd++] = s[i++];
		any = true;
	}
	if (i < n && s[i] == '.') {
		i++;
		while (i < n && s[i] >= '0' && s[i] <= '9') {
			digits[nd++] = s[i++];
			frac++;
			any = true;
		}
	}
	int64_t exp = 0;
	if (any && i < n && (s[i] == 'e' || s[i] == 'E')) {
		i++;
		int esign = 1;
		if (i < n && (s[i] == '+' || s[i] == '-')) {
			esign = s[i] == '-' ? -1 : 1;
			i++;
		}
		size_t start = i;
		while (i < n && s[i] >= '0' && s[i] <= '9') {
			// A scale beyond this needs more memory than any decimal literal is worth.
			if (exp < 100000000) exp = exp * 10 + (s[i] - '0');
			i++;
		}
		if (i == start) {
			free(digits);
			return CLJ_NIL;
		}
		exp *= esign;
	}
	int64_t scale = (int64_t)frac - exp;
	if (!any || i != n || scale > 1000000 || scale < -1000000) {
		free(digits);
		return CLJ_NIL;
	}
	if (nd == 0) digits[nd++] = '0';
	clj_value mag = clj_bigint_parse(digits, nd, 10);
	free(digits);
	if (clj_is_nil(mag)) return CLJ_NIL;
	clj_value u = sign < 0 ? clj_bigint_neg(mag) : clj_retain(mag);
	clj_release(mag);
	clj_value r = clj_decimal_new(u, (int32_t)scale);
	clj_release(u);
	return r;
}

clj_value clj_decimal_from_double(double d) {
	char tmp[48];
	int  prec = 17;
	for (int p = 2; p <= 17; p++) {
		snprintf(tmp, sizeof tmp, "%.*e", p - 1, d);
		if (strtod(tmp, NULL) == d) {
			prec = p;
			break;
		}
	}
	const char *q = tmp;
	int         sign = 1;
	if (*q == '-') {
		sign = -1;
		q++;
	}
	char   mant[24];
	size_t nd = 0;
	for (; *q && *q != 'e' && *q != 'E'; q++) {
		if (*q >= '0' && *q <= '9') mant[nd++] = *q;
	}
	int64_t e10 = *q ? strtol(q + 1, NULL, 10) : 0;
	int64_t scale = (int64_t)(prec - 1) - e10;
	double  a = fabs(d);
	// Double.toString keeps plain notation in [1e-3, 1e7), where BigDecimal.valueOf then has scale >= 1.
	if ((a >= 1e-3 && a < 1e7) || d == 0.0) {
		if (scale < 1) {
			clj_value mag0 = clj_bigint_parse(mant, nd, 10);
			clj_value up = scale_up(mag0, (uint32_t)(1 - scale));
			clj_release(mag0);
			clj_value u0 = sign < 0 ? clj_bigint_neg(up) : clj_retain(up);
			clj_release(up);
			clj_value r0 = clj_decimal_new(u0, 1);
			clj_release(u0);
			return r0;
		}
	}
	clj_value mag = clj_bigint_parse(mant, nd, 10);
	clj_value u = sign < 0 ? clj_bigint_neg(mag) : clj_retain(mag);
	clj_release(mag);
	clj_value r = clj_decimal_new(u, (int32_t)scale);
	clj_release(u);
	return r;
}

double clj_decimal_to_double(clj_value v) {
	clj_value s = clj_decimal_to_string(v);
	double    d = strtod(clj_string_bytes(s), NULL);
	clj_release(s);
	return d;
}

// BigDecimal.toString: plain when the scale is non-negative and the adjusted exponent is above -7.
clj_value clj_decimal_to_string(clj_value v) {
	clj_value u = clj_decimal_unscaled(v);
	int32_t   scale = clj_decimal_scale(v);
	size_t    cap = clj_bigint_digits10(u) + 2;
	char     *dig = malloc(cap);
	if (!dig) clj_fatal("out of memory");
	size_t  nd = clj_bigint_digits(u, dig, cap);
	bool    neg = clj_bigint_sign(u) < 0;
	int64_t adjusted = (int64_t)nd - 1 - scale;
	size_t  out_cap = nd + (size_t)(scale > 0 ? scale : 0) + 32;
	char   *out = malloc(out_cap);
	if (!out) clj_fatal("out of memory");
	size_t k = 0;
	if (neg) out[k++] = '-';
	if (scale == 0) {
		memcpy(out + k, dig, nd);
		k += nd;
	} else if (scale > 0 && adjusted >= -6) {
		if ((int64_t)nd > scale) {
			size_t split = nd - (size_t)scale;
			memcpy(out + k, dig, split);
			k += split;
			out[k++] = '.';
			memcpy(out + k, dig + split, nd - split);
			k += nd - split;
		} else {
			out[k++] = '0';
			out[k++] = '.';
			for (int64_t z = scale - (int64_t)nd; z > 0; z--) out[k++] = '0';
			memcpy(out + k, dig, nd);
			k += nd;
		}
	} else {
		out[k++] = dig[0];
		if (nd > 1) {
			out[k++] = '.';
			memcpy(out + k, dig + 1, nd - 1);
			k += nd - 1;
		}
		k += (size_t)snprintf(out + k, out_cap - k, "E%+lld", (long long)adjusted);
	}
	clj_value s = clj_string_new(out, k);
	free(dig);
	free(out);
	return s;
}
