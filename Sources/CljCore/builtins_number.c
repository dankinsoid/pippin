// @ai-generated(solo)
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/fn.h"

// ---- promoting operators: only a fixnum overflow reaches the bigint, everything else is the plain tower

static clj_value promote2(clj_value a, clj_value b, clj_num_op op) {
	if (clj_is_fixnum(a) && clj_is_fixnum(b)) {
		intptr_t r;
		bool     overflow = op == CLJ_OP_ADD   ? __builtin_add_overflow(clj_fixnum_val(a), clj_fixnum_val(b), &r)
		                    : op == CLJ_OP_SUB ? __builtin_sub_overflow(clj_fixnum_val(a), clj_fixnum_val(b), &r)
		                                       : __builtin_mul_overflow(clj_fixnum_val(a), clj_fixnum_val(b), &r);
		if (!overflow && r <= CLJ_FIXNUM_MAX && r >= CLJ_FIXNUM_MIN) return clj_fixnum(r);
		switch (op) {
		case CLJ_OP_ADD: return clj_bigint_add(a, b);
		case CLJ_OP_SUB: return clj_bigint_sub(a, b);
		default: return clj_bigint_mul(a, b);
		}
	}
	return clj_num_arith(a, b, op);
}

static clj_value fold(const clj_value *args, size_t n, clj_num_op op, intptr_t identity, bool unary_is_self) {
	if (n == 0) return clj_fixnum(identity);
	if (n == 1) {
		if (!clj_is_number(args[0])) return clj_throw_msg("%s cannot be cast to a number", clj_type_name(args[0]));
		return unary_is_self ? clj_retain(args[0]) : promote2(clj_fixnum(identity), args[0], op);
	}
	clj_value acc = promote2(args[0], args[1], op);
	for (size_t i = 2; i < n && acc != CLJ_THROWN; i++) {
		clj_value next = promote2(acc, args[i], op);
		clj_release(acc);
		acc = next;
	}
	return acc;
}

static clj_value b_addp(const clj_value *args, size_t n) { return fold(args, n, CLJ_OP_ADD, 0, true); }
static clj_value b_subp(const clj_value *args, size_t n) { return fold(args, n, CLJ_OP_SUB, 0, false); }
static clj_value b_mulp(const clj_value *args, size_t n) { return fold(args, n, CLJ_OP_MUL, 1, true); }

static clj_value b_incp(const clj_value *args, size_t n) {
	(void)n;
	return promote2(args[0], clj_fixnum(1), CLJ_OP_ADD);
}

static clj_value b_decp(const clj_value *args, size_t n) {
	(void)n;
	return promote2(args[0], clj_fixnum(1), CLJ_OP_SUB);
}

// ---- numeric equality across kinds, where = keeps Clojure's category rule

static clj_value b_num_eq(const clj_value *args, size_t n) {
	if (n == 1) return clj_is_number(args[0]) ? CLJ_TRUE : clj_throw_msg("%s cannot be cast to a number", clj_type_name(args[0]));
	for (size_t i = 1; i < n; i++) {
		int c;
		if (clj_num_cmp(args[i - 1], args[i], &c) == CLJ_THROWN) return CLJ_THROWN;
		if (c != 0) return CLJ_FALSE;
	}
	return CLJ_TRUE;
}

// ---- predicates

#define PRED(bname, test) \
	static clj_value bname(const clj_value *args, size_t n) { \
		(void)n; \
		return clj_bool(test(args[0])); \
	}

static bool is_rational(clj_value v) { return clj_is_integer(v) || clj_is_ratio(v) || clj_is_decimal(v); }

PRED(b_int_p, clj_is_fixnum)
PRED(b_double_p, clj_is_double)
PRED(b_ratio_p, clj_is_ratio)
PRED(b_decimal_p, clj_is_decimal)
PRED(b_rational_p, is_rational)

static clj_value not_a_number(clj_value v);

static clj_value b_nan_p(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_number(args[0])) return not_a_number(args[0]);
	double d = clj_num_to_double(args[0]);
	return clj_bool(d != d);
}

static clj_value b_infinite_p(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_number(args[0])) return not_a_number(args[0]);
	return clj_bool(isinf(clj_num_to_double(args[0])));
}

// ---- coercions

static clj_value range_error(const char *what, clj_value v) {
	clj_value text = clj_pr_str_max(v, CLJ_ERROR_PRINT_MAX);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("Value out of range for %s: %s", what, clj_string_bytes(text));
	clj_release(text);
	return r;
}

static clj_value not_a_number(clj_value v) { return clj_throw_msg("%s cannot be cast to a number", clj_type_name(v)); }

// RT.intCast(double) rejects before truncating, so 2147483647.000001 is out of range for int.
static clj_value int_cast(clj_value v, const char *what, int64_t lo, int64_t hi, bool chars) {
	if (clj_is_fixnum(v)) {
		int64_t i = clj_fixnum_val(v);
		return i < lo || i > hi ? range_error(what, v) : clj_fixnum((intptr_t)i);
	}
	if (chars && clj_is_char(v)) return clj_fixnum(clj_char_val(v));
	if (clj_is_double(v)) {
		double d = clj_double_val(v);
		if (!(d >= (double)lo && d <= (double)hi)) return range_error(what, v);
		int64_t i = (int64_t)trunc(d);
		return i < lo || i > hi ? range_error(what, v) : clj_fixnum((intptr_t)i);
	}
	if (!clj_is_number(v)) return not_a_number(v);
	clj_value t = clj_num_truncate(v);
	int64_t   i = 0;
	bool      ok = clj_bigint_to_i64(t, &i) && i >= lo && i <= hi;
	clj_release(t);
	return ok ? clj_fixnum((intptr_t)i) : range_error(what, v);
}

static clj_value b_long(const clj_value *args, size_t n) {
	(void)n;
	return int_cast(args[0], "long", CLJ_FIXNUM_MIN, CLJ_FIXNUM_MAX, true);
}

static clj_value b_int(const clj_value *args, size_t n) {
	(void)n;
	return int_cast(args[0], "int", INT32_MIN, INT32_MAX, true);
}

static clj_value b_short(const clj_value *args, size_t n) {
	(void)n;
	return int_cast(args[0], "short", INT16_MIN, INT16_MAX, false);
}

static clj_value b_byte(const clj_value *args, size_t n) {
	(void)n;
	return int_cast(args[0], "byte", INT8_MIN, INT8_MAX, false);
}

// Narrowed through float, so (float Double/MIN_VALUE) is 0.0 as on the JVM; the box stays a double.
static clj_value b_float(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_number(args[0])) return not_a_number(args[0]);
	double d = clj_num_to_double(args[0]);
	if (d < -3.4028234663852886e38 || d > 3.4028234663852886e38) return range_error("float", args[0]);
	return clj_double_new((double)(float)d);
}

static clj_value b_double(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_number(args[0])) return not_a_number(args[0]);
	return clj_double_new(clj_num_to_double(args[0]));
}

static clj_value b_num(const clj_value *args, size_t n) {
	(void)n;
	if (clj_is_nil(args[0]) || clj_is_number(args[0])) return clj_retain(args[0]);
	return not_a_number(args[0]);
}

static clj_value b_bigint(const clj_value *args, size_t n) {
	(void)n;
	clj_value v = args[0];
	if (clj_is_string(v)) {
		clj_value r = clj_bigint_parse(clj_string_bytes(v), clj_string_len(v), 10);
		return clj_is_nil(r) ? clj_throw_msg("Invalid number: %s", clj_string_bytes(v)) : r;
	}
	if (clj_is_double(v) && !isfinite(clj_double_val(v))) return clj_throw_msg("Cannot convert %s to a bigint", clj_type_name(v));
	if (!clj_is_number(v)) return not_a_number(v);
	return clj_num_truncate(v);
}

static clj_value b_bigdec(const clj_value *args, size_t n) {
	(void)n;
	clj_value v = args[0];
	if (clj_is_decimal(v)) return clj_retain(v);
	if (clj_is_string(v)) {
		clj_value r = clj_decimal_parse(clj_string_bytes(v), clj_string_len(v));
		return clj_is_nil(r) ? clj_throw_msg("Invalid number: %s", clj_string_bytes(v)) : r;
	}
	if (clj_is_integer(v)) return clj_decimal_new(v, 0);
	if (clj_is_double(v)) {
		double d = clj_double_val(v);
		if (!isfinite(d)) return clj_throw_msg("Cannot convert %s to a bigdec", d != d ? "##NaN" : d < 0 ? "##-Inf" : "##Inf");
		return clj_decimal_from_double(d);
	}
	if (clj_is_ratio(v)) {
		clj_value r = clj_decimal_from_fraction(clj_ratio_num(v), clj_ratio_den(v));
		return clj_is_nil(r) ? clj_throw_msg("Non-terminating decimal expansion; with-precision is not supported") : r;
	}
	return not_a_number(v);
}

static clj_value b_rationalize(const clj_value *args, size_t n) {
	(void)n;
	clj_value v = args[0];
	if (clj_is_integer(v) || clj_is_ratio(v)) return clj_retain(v);
	clj_value dec;
	if (clj_is_decimal(v)) {
		dec = clj_retain(v);
	} else if (clj_is_double(v)) {
		double d = clj_double_val(v);
		if (!isfinite(d)) return clj_throw_msg("Cannot rationalize %s", d != d ? "##NaN" : d < 0 ? "##-Inf" : "##Inf");
		dec = clj_decimal_from_double(d);
	} else {
		return not_a_number(v);
	}
	clj_value num, den;
	clj_decimal_as_fraction(dec, &num, &den);
	clj_release(dec);
	clj_value r = clj_ratio_new(num, den);
	clj_release(num);
	clj_release(den);
	return r;
}

static clj_value b_numerator(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_ratio(args[0])) return clj_throw_msg("%s cannot be cast to a ratio", clj_type_name(args[0]));
	return clj_retain(clj_ratio_num(args[0]));
}

static clj_value b_denominator(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_ratio(args[0])) return clj_throw_msg("%s cannot be cast to a ratio", clj_type_name(args[0]));
	return clj_retain(clj_ratio_den(args[0]));
}

// ---- parsing, nil on a text that is not the whole number (the JVM contract)

static clj_value b_parse_long(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("%s cannot be cast to a string", clj_type_name(args[0]));
	const char *s = clj_string_bytes(args[0]);
	size_t      len = clj_string_len(args[0]);
	size_t      i = len > 0 && (s[0] == '+' || s[0] == '-') ? 1 : 0;
	if (i == len) return CLJ_NIL;
	for (size_t k = i; k < len; k++) {
		if (s[k] < '0' || s[k] > '9') return CLJ_NIL;
	}
	clj_value big = clj_bigint_parse(s, len, 10);
	if (clj_is_nil(big)) return CLJ_NIL;
	int64_t v;
	bool    fits = clj_bigint_to_i64(big, &v) && v >= CLJ_FIXNUM_MIN && v <= CLJ_FIXNUM_MAX;
	clj_release(big);
	return fits ? clj_fixnum((intptr_t)v) : CLJ_NIL;
}

static bool all_digits(const char *s, size_t from, size_t to) {
	if (from >= to) return false;
	for (size_t i = from; i < to; i++) {
		if (s[i] < '0' || s[i] > '9') return false;
	}
	return true;
}

static clj_value b_parse_double(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("%s cannot be cast to a string", clj_type_name(args[0]));
	const char *s = clj_string_bytes(args[0]);
	size_t      len = clj_string_len(args[0]);
	size_t      i = len > 0 && (s[0] == '+' || s[0] == '-') ? 1 : 0;
	bool        neg = i == 1 && s[0] == '-';
	if (len - i == 8 && memcmp(s + i, "Infinity", 8) == 0) return clj_double_new(neg ? -INFINITY : INFINITY);
	if (len - i == 3 && memcmp(s + i, "NaN", 3) == 0) return clj_double_new(NAN);
	size_t j = i;
	while (j < len && s[j] >= '0' && s[j] <= '9') j++;
	bool   int_digits = j > i;
	size_t frac = 0;
	if (j < len && s[j] == '.') {
		size_t start = ++j;
		while (j < len && s[j] >= '0' && s[j] <= '9') j++;
		frac = j - start;
	}
	if (!int_digits && frac == 0) return CLJ_NIL;
	if (j < len && (s[j] == 'e' || s[j] == 'E')) {
		size_t k = j + 1;
		if (k < len && (s[k] == '+' || s[k] == '-')) k++;
		if (!all_digits(s, k, len)) return CLJ_NIL;
		j = len;
	}
	if (j != len) return CLJ_NIL;
	char buf[350];
	if (len >= sizeof buf) return CLJ_NIL;
	memcpy(buf, s, len);
	buf[len] = '\0';
	return clj_double_new(strtod(buf, NULL));
}

// ---- unchecked arithmetic, wrapping at the 63-bit fixnum rather than the JVM's 64-bit long (NOTES.md)

static clj_value unchecked2(const clj_value *args, clj_num_op op) {
	if (!clj_is_fixnum(args[0]) || !clj_is_fixnum(args[1])) return clj_num_arith(args[0], args[1], op);
	uint64_t x = (uint64_t)(int64_t)clj_fixnum_val(args[0]), y = (uint64_t)(int64_t)clj_fixnum_val(args[1]);
	uint64_t r = op == CLJ_OP_ADD ? x + y : op == CLJ_OP_SUB ? x - y : x * y;
	return clj_fixnum((intptr_t)r);
}

static clj_value b_unchecked_add(const clj_value *args, size_t n) { (void)n; return unchecked2(args, CLJ_OP_ADD); }
static clj_value b_unchecked_subtract(const clj_value *args, size_t n) { (void)n; return unchecked2(args, CLJ_OP_SUB); }
static clj_value b_unchecked_multiply(const clj_value *args, size_t n) { (void)n; return unchecked2(args, CLJ_OP_MUL); }

static clj_value unchecked1(clj_value v, int64_t delta, bool negate) {
	if (!clj_is_fixnum(v)) {
		if (negate) return clj_num_arith(clj_fixnum(0), v, CLJ_OP_SUB);
		return clj_num_arith(v, clj_fixnum(delta), CLJ_OP_ADD);
	}
	uint64_t x = (uint64_t)(int64_t)clj_fixnum_val(v);
	uint64_t r = negate ? 0u - x : x + (uint64_t)delta;
	return clj_fixnum((intptr_t)r);
}

static clj_value b_unchecked_inc(const clj_value *args, size_t n) { (void)n; return unchecked1(args[0], 1, false); }
static clj_value b_unchecked_dec(const clj_value *args, size_t n) { (void)n; return unchecked1(args[0], -1, false); }
static clj_value b_unchecked_negate(const clj_value *args, size_t n) { (void)n; return unchecked1(args[0], 0, true); }

#define ANY CLJ_ARITY_ANY

static const struct {
	const char   *name;
	clj_native_fn fn;
	uint32_t      min, max;
} entries[] = {
	{"+'", b_addp, 0, ANY},
	{"-'", b_subp, 1, ANY},
	{"*'", b_mulp, 0, ANY},
	{"inc'", b_incp, 1, 1},
	{"dec'", b_decp, 1, 1},
	{"==", b_num_eq, 1, ANY},
	{"int?", b_int_p, 1, 1},
	{"double?", b_double_p, 1, 1},
	{"ratio?", b_ratio_p, 1, 1},
	{"decimal?", b_decimal_p, 1, 1},
	{"rational?", b_rational_p, 1, 1},
	{"NaN?", b_nan_p, 1, 1},
	{"infinite?", b_infinite_p, 1, 1},
	{"long", b_long, 1, 1},
	{"int", b_int, 1, 1},
	{"short", b_short, 1, 1},
	{"byte", b_byte, 1, 1},
	{"float", b_float, 1, 1},
	{"double", b_double, 1, 1},
	{"num", b_num, 1, 1},
	{"bigint", b_bigint, 1, 1},
	{"biginteger", b_bigint, 1, 1},
	{"bigdec", b_bigdec, 1, 1},
	{"rationalize", b_rationalize, 1, 1},
	{"numerator", b_numerator, 1, 1},
	{"denominator", b_denominator, 1, 1},
	{"parse-long", b_parse_long, 1, 1},
	{"parse-double", b_parse_double, 1, 1},
	{"unchecked-add", b_unchecked_add, 2, 2},
	{"unchecked-subtract", b_unchecked_subtract, 2, 2},
	{"unchecked-multiply", b_unchecked_multiply, 2, 2},
	{"unchecked-inc", b_unchecked_inc, 1, 1},
	{"unchecked-dec", b_unchecked_dec, 1, 1},
	{"unchecked-negate", b_unchecked_negate, 1, 1},
};

void clj_number_builtins_install(void) {
	for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) clj_builtin_bind(entries[i].name, entries[i].fn, entries[i].min, entries[i].max);
}
