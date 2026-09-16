// @ai-generated(solo)
#include <stdlib.h>
#include <string.h>

#include "clj/ratio.h"
#include "clj/string.h"

static void ratio_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_ratio *r = self;
	visit(r->num, ctx);
	visit(r->den, ctx);
}

static uint32_t ratio_hash(void *self) {
	clj_ratio *r = self;
	return clj_hash_combine(clj_hash(r->num), clj_hash(r->den));
}

// A normalised ratio is never integral, so it equals nothing outside its own type.
static bool ratio_equals(void *self, clj_value other) {
	clj_ratio *r = self;
	return clj_is_ratio(other) && clj_bigint_cmp(r->num, clj_ratio_num(other)) == 0 && clj_bigint_cmp(r->den, clj_ratio_den(other)) == 0;
}

const clj_type clj_ratio_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "ratio",
	.each_child = ratio_each_child,
	.hash = ratio_hash,
	.equals = ratio_equals,
};

clj_value clj_ratio_new(clj_value num, clj_value den) {
	clj_value g = clj_bigint_gcd(num, den);
	clj_value n, d;
	if (clj_bigint_is_zero(g)) {
		clj_release(g);
		return clj_bigint_from_i64(0);
	}
	n = clj_bigint_quot(num, g, NULL);
	d = clj_bigint_quot(den, g, NULL);
	clj_release(g);
	if (clj_bigint_sign(d) < 0) {
		clj_value nn = clj_bigint_neg(n), nd = clj_bigint_neg(d);
		clj_release(n);
		clj_release(d);
		n = nn;
		d = nd;
	}
	int64_t one;
	if (clj_bigint_to_i64(d, &one) && one == 1) {
		clj_release(d);
		return n;
	}
	clj_ratio *r = clj_alloc(&clj_ratio_type, sizeof *r);
	r->num = n;
	r->den = d;
	return clj_from_ptr(r);
}

double clj_ratio_to_double(clj_value v) {
	clj_ratio *r = clj_ratio_of(v);
	return clj_bigint_to_double(r->num) / clj_bigint_to_double(r->den);
}

int clj_ratio_cmp(clj_value a, clj_value b) {
	clj_value l = clj_bigint_mul(clj_ratio_num(a), clj_ratio_den(b));
	clj_value r = clj_bigint_mul(clj_ratio_num(b), clj_ratio_den(a));
	int       c = clj_bigint_cmp(l, r);
	clj_release(l);
	clj_release(r);
	return c;
}

clj_value clj_ratio_to_string(clj_value v) {
	clj_ratio *r = clj_ratio_of(v);
	size_t     cap = clj_bigint_digits10(r->num) + clj_bigint_digits10(r->den) + 4;
	char      *buf = malloc(cap);
	if (!buf) clj_fatal("out of memory");
	size_t k = 0;
	if (clj_bigint_sign(r->num) < 0) buf[k++] = '-';
	k += clj_bigint_digits(r->num, buf + k, cap - k);
	buf[k++] = '/';
	k += clj_bigint_digits(r->den, buf + k, cap - k);
	clj_value s = clj_string_new(buf, k);
	free(buf);
	return s;
}
