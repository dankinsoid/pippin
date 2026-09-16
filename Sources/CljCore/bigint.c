// @ai-generated(solo)
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "clj/bigint.h"
#include "clj/string.h"

// Every entry point takes a fixnum or a bigint and returns a bigint; only clj_bigint_demote narrows.
typedef struct {
	const uint32_t *limbs;
	uint32_t        n;
	int             sign;
	uint32_t        tmp[2];
} view;

static void view_of(clj_value v, view *out) {
	if (clj_is_fixnum(v)) {
		int64_t  x = clj_fixnum_val(v);
		uint64_t m = x < 0 ? -(uint64_t)x : (uint64_t)x;
		out->tmp[0] = (uint32_t)m;
		out->tmp[1] = (uint32_t)(m >> 32);
		out->limbs = out->tmp;
		out->n = out->tmp[1] ? 2 : out->tmp[0] ? 1 : 0;
		out->sign = x < 0 ? -1 : x > 0 ? 1 : 0;
		return;
	}
	clj_bigint *b = clj_bigint_of(v);
	out->limbs = b->limbs;
	out->n = b->n;
	out->sign = b->sign;
}

static clj_bigint *raw_new(uint32_t cap) {
	return clj_alloc(&clj_bigint_type, sizeof(clj_bigint) + (size_t)cap * sizeof(uint32_t));
}

static clj_value finish(clj_bigint *b, uint32_t n, int sign) {
	while (n > 0 && b->limbs[n - 1] == 0) n--;
	b->n = n;
	b->sign = n == 0 ? 0 : sign;
	return clj_from_ptr(b);
}

static int mag_cmp(const uint32_t *a, uint32_t an, const uint32_t *b, uint32_t bn) {
	if (an != bn) return an < bn ? -1 : 1;
	for (uint32_t i = an; i-- > 0;) {
		if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
	}
	return 0;
}

// out needs max(an, bn) + 1 limbs.
static uint32_t mag_add(const uint32_t *a, uint32_t an, const uint32_t *b, uint32_t bn, uint32_t *out) {
	uint32_t n = an > bn ? an : bn;
	uint64_t carry = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint64_t s = carry + (i < an ? a[i] : 0) + (i < bn ? b[i] : 0);
		out[i] = (uint32_t)s;
		carry = s >> 32;
	}
	if (carry) out[n++] = (uint32_t)carry;
	return n;
}

// Magnitude of a must be >= that of b; out may alias a.
static uint32_t mag_sub(const uint32_t *a, uint32_t an, const uint32_t *b, uint32_t bn, uint32_t *out) {
	int64_t borrow = 0;
	for (uint32_t i = 0; i < an; i++) {
		int64_t d = (int64_t)a[i] - (i < bn ? (int64_t)b[i] : 0) - borrow;
		borrow = d < 0;
		out[i] = (uint32_t)(d + (borrow ? ((int64_t)1 << 32) : 0));
	}
	return an;
}

// out needs an + bn limbs and must be zeroed.
static void mag_mul(const uint32_t *a, uint32_t an, const uint32_t *b, uint32_t bn, uint32_t *out) {
	for (uint32_t i = 0; i < an; i++) {
		uint64_t carry = 0;
		for (uint32_t j = 0; j < bn; j++) {
			uint64_t s = (uint64_t)a[i] * b[j] + out[i + j] + carry;
			out[i + j] = (uint32_t)s;
			carry = s >> 32;
		}
		out[i + bn] = (uint32_t)carry;
	}
}

static uint32_t mag_bits(const uint32_t *a, uint32_t an) {
	if (an == 0) return 0;
	uint32_t top = a[an - 1], k = 0;
	while (top) {
		top >>= 1;
		k++;
	}
	return (an - 1) * 32 + k;
}

// Shift-subtract division: quadratic in the bit length, and every bigint the runtime meets is small.
static void mag_divmod(const uint32_t *a, uint32_t an, const uint32_t *b, uint32_t bn, uint32_t *q, uint32_t *r) {
	memset(q, 0, (size_t)an * sizeof(uint32_t));
	memset(r, 0, ((size_t)bn + 1) * sizeof(uint32_t));
	uint32_t rn = 0;
	for (uint32_t i = mag_bits(a, an); i-- > 0;) {
		uint32_t carry = (a[i / 32] >> (i % 32)) & 1;
		for (uint32_t k = 0; k <= bn; k++) {
			uint32_t next = r[k] >> 31;
			r[k] = (r[k] << 1) | carry;
			carry = next;
		}
		rn = bn + 1;
		while (rn > 0 && r[rn - 1] == 0) rn--;
		if (mag_cmp(r, rn, b, bn) >= 0) {
			mag_sub(r, rn, b, bn, r);
			q[i / 32] |= (uint32_t)1 << (i % 32);
		}
	}
}

static uint32_t bigint_hash(void *self) {
	clj_value v = clj_from_ptr(self);
	int64_t   i;
	if (clj_bigint_to_i64(v, &i)) {
		uint64_t n = (uint64_t)i;
		return clj_fmix32((uint32_t)(n ^ (n >> 32)));
	}
	clj_bigint *b = self;
	uint32_t    h = 1;
	for (uint32_t k = 0; k < b->n; k++) h = h * 31 + b->limbs[k];
	return clj_fmix32(b->sign < 0 ? ~h : h);
}

static bool bigint_equals(void *self, clj_value other) {
	clj_value v = clj_from_ptr(self);
	if (clj_is_fixnum(other)) {
		int64_t i;
		return clj_bigint_to_i64(v, &i) && i == (int64_t)clj_fixnum_val(other);
	}
	return clj_is_bigint(other) && clj_bigint_cmp(v, other) == 0;
}

const clj_type clj_bigint_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "bigint",
	.hash = bigint_hash,
	.equals = bigint_equals,
};

clj_value clj_bigint_from_i64(int64_t v) {
	clj_bigint *b = raw_new(2);
	uint64_t    m = v < 0 ? -(uint64_t)v : (uint64_t)v;
	b->limbs[0] = (uint32_t)m;
	b->limbs[1] = (uint32_t)(m >> 32);
	return finish(b, 2, v < 0 ? -1 : 1);
}

static clj_value pow2(uint32_t bits) {
	uint32_t    cap = bits / 32 + 1;
	clj_bigint *b = raw_new(cap);
	b->limbs[bits / 32] = (uint32_t)1 << (bits % 32);
	return finish(b, cap, 1);
}

clj_value clj_bigint_from_double(double d) {
	d = trunc(d);
	if (d >= -9.007199254740992e15 && d <= 9.007199254740992e15) return clj_bigint_from_i64((int64_t)d);
	int    sign = d < 0 ? -1 : 1;
	int    exp = 0;
	double m = frexp(fabs(d), &exp);
	// 2^53 scaling makes the mantissa integral, and |d| > 2^53 leaves the remaining exponent non-negative.
	clj_value mant = clj_bigint_from_i64((int64_t)ldexp(m, 53));
	clj_value shift = pow2((uint32_t)(exp - 53));
	clj_value r = clj_bigint_mul(mant, shift);
	clj_release(mant);
	clj_release(shift);
	if (sign < 0) {
		clj_value neg = clj_bigint_neg(r);
		clj_release(r);
		return neg;
	}
	return r;
}

clj_value clj_bigint_parse(const char *s, size_t n, unsigned radix) {
	size_t i = 0;
	int    sign = 1;
	if (n > 0 && (s[0] == '+' || s[0] == '-')) {
		sign = s[0] == '-' ? -1 : 1;
		i = 1;
	}
	if (i == n) return CLJ_NIL;
	uint32_t    cap = (uint32_t)((n - i) * 6 / 32) + 2;
	clj_bigint *b = raw_new(cap);
	uint32_t    len = 0;
	for (; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		unsigned      d = c >= '0' && c <= '9' ? (unsigned)(c - '0') : c >= 'a' && c <= 'z' ? (unsigned)(c - 'a' + 10) : c >= 'A' && c <= 'Z' ? (unsigned)(c - 'A' + 10) : 99;
		if (d >= radix) {
			clj_release(clj_from_ptr(b));
			return CLJ_NIL;
		}
		uint64_t carry = d;
		for (uint32_t k = 0; k < len; k++) {
			uint64_t s2 = (uint64_t)b->limbs[k] * radix + carry;
			b->limbs[k] = (uint32_t)s2;
			carry = s2 >> 32;
		}
		while (carry) {
			b->limbs[len++] = (uint32_t)carry;
			carry >>= 32;
		}
	}
	return finish(b, len, sign);
}

bool clj_bigint_to_i64(clj_value v, int64_t *out) {
	clj_bigint *b = clj_bigint_of(v);
	if (b->n > 2) return false;
	uint64_t m = b->n == 0 ? 0 : b->n == 1 ? b->limbs[0] : ((uint64_t)b->limbs[1] << 32) | b->limbs[0];
	if (b->sign < 0) {
		if (m > (uint64_t)1 << 63) return false;
		*out = m == (uint64_t)1 << 63 ? INT64_MIN : -(int64_t)m;
		return true;
	}
	if (m > (uint64_t)INT64_MAX) return false;
	*out = (int64_t)m;
	return true;
}

double clj_bigint_to_double(clj_value v) {
	int64_t i;
	if (clj_bigint_to_i64(v, &i)) return (double)i;
	// strtod rounds correctly and reports overflow as ±HUGE_VAL, which reimplementing here would not.
	size_t cap = clj_bigint_digits10(v) + 2;
	char  *buf = malloc(cap);
	if (!buf) clj_fatal("out of memory");
	size_t k = clj_bigint_digits(v, buf, cap);
	double d = 0;
	if (k > 0) d = strtod(buf, NULL);
	free(buf);
	return clj_bigint_sign(v) < 0 ? -d : d;
}

clj_value clj_bigint_demote(clj_value v) {
	int64_t i;
	if (clj_bigint_to_i64(v, &i) && i >= CLJ_FIXNUM_MIN && i <= CLJ_FIXNUM_MAX) return clj_fixnum((intptr_t)i);
	return clj_retain(v);
}

static clj_value add_signed(clj_value a, clj_value b, int b_sign_flip) {
	view x, y;
	view_of(a, &x);
	view_of(b, &y);
	int ys = y.sign * b_sign_flip;
	if (x.sign == 0) {
		clj_bigint *r = raw_new(y.n);
		memcpy(r->limbs, y.limbs, (size_t)y.n * sizeof(uint32_t));
		return finish(r, y.n, ys);
	}
	if (ys == 0) {
		clj_bigint *r = raw_new(x.n);
		memcpy(r->limbs, x.limbs, (size_t)x.n * sizeof(uint32_t));
		return finish(r, x.n, x.sign);
	}
	if (x.sign == ys) {
		uint32_t    cap = (x.n > y.n ? x.n : y.n) + 1;
		clj_bigint *r = raw_new(cap);
		uint32_t    n = mag_add(x.limbs, x.n, y.limbs, y.n, r->limbs);
		return finish(r, n, x.sign);
	}
	int c = mag_cmp(x.limbs, x.n, y.limbs, y.n);
	if (c == 0) return finish(raw_new(0), 0, 0);
	const view *big = c > 0 ? &x : &y, *small = c > 0 ? &y : &x;
	clj_bigint *r = raw_new(big->n);
	uint32_t    n = mag_sub(big->limbs, big->n, small->limbs, small->n, r->limbs);
	return finish(r, n, c > 0 ? x.sign : ys);
}

clj_value clj_bigint_add(clj_value a, clj_value b) { return add_signed(a, b, 1); }
clj_value clj_bigint_sub(clj_value a, clj_value b) { return add_signed(a, b, -1); }

clj_value clj_bigint_mul(clj_value a, clj_value b) {
	view x, y;
	view_of(a, &x);
	view_of(b, &y);
	if (x.sign == 0 || y.sign == 0) return finish(raw_new(0), 0, 0);
	uint32_t    cap = x.n + y.n;
	clj_bigint *r = raw_new(cap);
	mag_mul(x.limbs, x.n, y.limbs, y.n, r->limbs);
	return finish(r, cap, x.sign * y.sign);
}

clj_value clj_bigint_quot(clj_value a, clj_value b, clj_value *rem) {
	view x, y;
	view_of(a, &x);
	view_of(b, &y);
	if (y.sign == 0) clj_fatal("bigint division by zero");
	if (mag_cmp(x.limbs, x.n, y.limbs, y.n) < 0) {
		if (rem) {
			clj_bigint *r = raw_new(x.n);
			memcpy(r->limbs, x.limbs, (size_t)x.n * sizeof(uint32_t));
			*rem = finish(r, x.n, x.sign);
		}
		return finish(raw_new(0), 0, 0);
	}
	clj_bigint *q = raw_new(x.n);
	clj_bigint *r = raw_new(y.n + 1);
	mag_divmod(x.limbs, x.n, y.limbs, y.n, q->limbs, r->limbs);
	clj_value rv = finish(r, y.n + 1, x.sign);
	if (rem) *rem = rv;
	else clj_release(rv);
	return finish(q, x.n, x.sign * y.sign);
}

clj_value clj_bigint_neg(clj_value a) {
	view v;
	view_of(a, &v);
	clj_bigint *r = raw_new(v.n);
	memcpy(r->limbs, v.limbs, (size_t)v.n * sizeof(uint32_t));
	return finish(r, v.n, -v.sign);
}

clj_value clj_bigint_abs(clj_value a) {
	view v;
	view_of(a, &v);
	clj_bigint *r = raw_new(v.n);
	memcpy(r->limbs, v.limbs, (size_t)v.n * sizeof(uint32_t));
	return finish(r, v.n, v.sign < 0 ? 1 : v.sign);
}

clj_value clj_bigint_gcd(clj_value a, clj_value b) {
	clj_value x = clj_bigint_abs(a), y = clj_bigint_abs(b);
	while (!clj_bigint_is_zero(y)) {
		clj_value r = CLJ_NIL;
		clj_value q = clj_bigint_quot(x, y, &r);
		clj_release(q);
		clj_release(x);
		x = y;
		y = r;
	}
	clj_release(y);
	return x;
}

clj_value clj_bigint_pow10(uint32_t e) {
	clj_value acc = clj_bigint_from_i64(1);
	clj_value base = clj_bigint_from_i64(10);
	while (e) {
		if (e & 1) {
			clj_value next = clj_bigint_mul(acc, base);
			clj_release(acc);
			acc = next;
		}
		e >>= 1;
		if (e) {
			clj_value sq = clj_bigint_mul(base, base);
			clj_release(base);
			base = sq;
		}
	}
	clj_release(base);
	return acc;
}

int clj_bigint_cmp(clj_value a, clj_value b) {
	view x, y;
	view_of(a, &x);
	view_of(b, &y);
	if (x.sign != y.sign) return x.sign < y.sign ? -1 : 1;
	int c = mag_cmp(x.limbs, x.n, y.limbs, y.n);
	return x.sign < 0 ? -c : c;
}

size_t clj_bigint_digits10(clj_value v) {
	clj_bigint *b = clj_bigint_of(v);
	return b->n == 0 ? 1 : (size_t)b->n * 10 + 1;
}

size_t clj_bigint_digits(clj_value v, char *buf, size_t cap) {
	clj_bigint *b = clj_bigint_of(v);
	if (cap < clj_bigint_digits10(v) + 1) return 0;
	if (b->n == 0) {
		buf[0] = '0';
		buf[1] = '\0';
		return 1;
	}
	uint32_t *tmp = malloc((size_t)b->n * sizeof(uint32_t));
	if (!tmp) clj_fatal("out of memory");
	memcpy(tmp, b->limbs, (size_t)b->n * sizeof(uint32_t));
	uint32_t len = b->n;
	size_t   k = 0;
	while (len > 0) {
		uint64_t rem = 0;
		for (uint32_t i = len; i-- > 0;) {
			uint64_t cur = (rem << 32) | tmp[i];
			tmp[i] = (uint32_t)(cur / 1000000000u);
			rem = cur % 1000000000u;
		}
		while (len > 0 && tmp[len - 1] == 0) len--;
		for (int d = 0; d < 9 && (len > 0 || rem > 0); d++) {
			buf[k++] = (char)('0' + rem % 10);
			rem /= 10;
		}
	}
	free(tmp);
	for (size_t i = 0, j = k - 1; i < j; i++, j--) {
		char t = buf[i];
		buf[i] = buf[j];
		buf[j] = t;
	}
	buf[k] = '\0';
	return k;
}

clj_value clj_bigint_to_string(clj_value v) {
	size_t cap = clj_bigint_digits10(v) + 2;
	char  *buf = malloc(cap);
	if (!buf) clj_fatal("out of memory");
	size_t k = clj_bigint_digits(v, buf + 1, cap - 1);
	buf[0] = '-';
	clj_value s = clj_bigint_sign(v) < 0 ? clj_string_new(buf, k + 1) : clj_string_new(buf + 1, k);
	free(buf);
	return s;
}
