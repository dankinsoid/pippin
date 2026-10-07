// @ai-generated(solo)
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/fn.h"

// ---- promoting operators: only a 64-bit overflow reaches the bigint, everything else is the plain tower

static clj_value promote2(clj_value a, clj_value b, clj_num_op op) {
	int64_t x, y;
	if (clj_int64_of(a, &x) && clj_int64_of(b, &y)) {
		int64_t r;
		bool    overflow = op == CLJ_OP_ADD   ? __builtin_add_overflow(x, y, &r)
		                   : op == CLJ_OP_SUB ? __builtin_sub_overflow(x, y, &r)
		                                      : __builtin_mul_overflow(x, y, &r);
		if (!overflow) return clj_long_new(r);
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

// A long in either representation; a bigint is not an int? on the JVM either.
static bool is_long(clj_value v) { return clj_is_fixnum(v) || clj_is_long(v); }

PRED(b_int_p, is_long)
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
	int64_t i;
	if (clj_int64_of(v, &i)) return i < lo || i > hi ? range_error(what, v) : clj_long_new(i);
	if (chars && clj_is_char(v)) return clj_fixnum(clj_char_val(v));
	if (clj_is_double(v)) {
		double d = clj_double_val(v);
		// (double)INT64_MAX rounds up to 2^63, which no int64_t holds, so the long bound is exclusive.
		bool in_range = hi == INT64_MAX ? d >= (double)lo && d < 9223372036854775808.0 : d >= (double)lo && d <= (double)hi;
		if (!in_range) return range_error(what, v);
		i = (int64_t)trunc(d);
		return i < lo || i > hi ? range_error(what, v) : clj_long_new(i);
	}
	if (!clj_is_number(v)) return not_a_number(v);
	clj_value t = clj_num_truncate(v);
	i = 0;
	bool ok = clj_bigint_to_i64(t, &i) && i >= lo && i <= hi;
	clj_release(t);
	return ok ? clj_long_new(i) : range_error(what, v);
}

static clj_value b_long(const clj_value *args, size_t n) {
	(void)n;
	return int_cast(args[0], "long", INT64_MIN, INT64_MAX, true);
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
	if (clj_is_ratio(v)) return clj_num_ratio_to_decimal(v);
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
	return clj_bigint_demote(clj_ratio_num(args[0]));
}

static clj_value b_denominator(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_ratio(args[0])) return clj_throw_msg("%s cannot be cast to a ratio", clj_type_name(args[0]));
	return clj_bigint_demote(clj_ratio_den(args[0]));
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
	bool    fits = clj_bigint_to_i64(big, &v);
	clj_release(big);
	return fits ? clj_long_new(v) : CLJ_NIL;
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

// ---- unchecked coercions: Java's narrowing conversions, as RT.uncheckedIntCast and its siblings

// (long)d: NaN is 0 and the range saturates.
static int64_t java_d2l(double d) {
	if (d != d) return 0;
	if (d >= 9223372036854775807.0) return INT64_MAX;
	if (d <= -9223372036854775808.0) return INT64_MIN;
	return (int64_t)d;
}

static int32_t java_d2i(double d) {
	if (d != d) return 0;
	if (d >= 2147483647.0) return INT32_MAX;
	if (d <= -2147483648.0) return INT32_MIN;
	return (int32_t)d;
}

// Number.longValue: a double saturates, anything else keeps the low 64 bits of its truncation; false for a
// value that is no number.
static bool java_long(clj_value v, int64_t *out) {
	if (clj_int64_of(v, out)) return true;
	if (clj_is_double(v)) {
		*out = java_d2l(clj_double_val(v));
		return true;
	}
	if (!clj_is_number(v)) return false;
	clj_value t = clj_num_truncate(v);
	*out = clj_bigint_low64(t);
	clj_release(t);
	return true;
}

// Number.intValue: Ratio's goes through its double, as a Double's does; the rest keeps the low 32 bits.
static bool java_int(clj_value v, int32_t *out) {
	if (clj_is_double(v) || clj_is_ratio(v)) {
		*out = java_d2i(clj_num_to_double(v));
		return true;
	}
	int64_t l;
	if (!java_long(v, &l)) return false;
	*out = (int32_t)(uint32_t)(uint64_t)l;
	return true;
}

static clj_value b_unchecked_byte(const clj_value *args, size_t n) {
	(void)n;
	int32_t i;
	return java_int(args[0], &i) ? clj_fixnum((int8_t)(uint8_t)(uint32_t)i) : not_a_number(args[0]);
}

static clj_value b_unchecked_short(const clj_value *args, size_t n) {
	(void)n;
	int32_t i;
	return java_int(args[0], &i) ? clj_fixnum((int16_t)(uint16_t)(uint32_t)i) : not_a_number(args[0]);
}

// RT.uncheckedIntCast alone of the family takes a char.
static clj_value b_unchecked_int(const clj_value *args, size_t n) {
	(void)n;
	if (clj_is_char(args[0])) return clj_fixnum(clj_char_val(args[0]));
	int32_t i;
	return java_int(args[0], &i) ? clj_fixnum(i) : not_a_number(args[0]);
}

static clj_value b_unchecked_long(const clj_value *args, size_t n) {
	(void)n;
	int64_t l;
	return java_long(args[0], &l) ? clj_long_new(l) : not_a_number(args[0]);
}

// (char) of the long: the low 16 bits. A surrogate half is no Unicode scalar, which is what a char is here.
static clj_value b_unchecked_char(const clj_value *args, size_t n) {
	(void)n;
	if (clj_is_char(args[0])) return args[0];
	int64_t l;
	if (!java_long(args[0], &l)) return not_a_number(args[0]);
	uint32_t c = (uint32_t)(uint64_t)l & 0xFFFF;
	if (c >= 0xD800 && c <= 0xDFFF) return range_error("char", args[0]);
	return clj_char(c);
}

static clj_value b_unchecked_float(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_number(args[0])) return not_a_number(args[0]);
	return clj_double_new((double)(float)clj_num_to_double(args[0]));
}

static clj_value b_unchecked_double(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_number(args[0])) return not_a_number(args[0]);
	return clj_double_new(clj_num_to_double(args[0]));
}

// The operands of the -int family are RT.intCast'd, which checks the range; only the operation wraps.
static bool int_operands(const clj_value *args, size_t n, int32_t *out) {
	for (size_t i = 0; i < n; i++) {
		clj_value v = int_cast(args[i], "int", INT32_MIN, INT32_MAX, false);
		if (v == CLJ_THROWN) return false;
		int64_t x = 0;
		clj_int64_of(v, &x);
		clj_release(v);
		out[i] = (int32_t)x;
	}
	return true;
}

static clj_value unchecked_int2(const clj_value *args, char op) {
	int32_t x[2];
	if (!int_operands(args, 2, x)) return CLJ_THROWN;
	uint32_t a = (uint32_t)x[0], b = (uint32_t)x[1];
	switch (op) {
	case '+': return clj_fixnum((int32_t)(a + b));
	case '-': return clj_fixnum((int32_t)(a - b));
	case '*': return clj_fixnum((int32_t)(a * b));
	default: break;
	}
	if (x[1] == 0) return clj_throw_msg("Divide by zero");
	// INT32_MIN / -1 is undefined in C; Java's int division wraps it to INT32_MIN and its remainder is 0.
	if (x[0] == INT32_MIN && x[1] == -1) return clj_fixnum(op == '/' ? INT32_MIN : 0);
	return clj_fixnum(op == '/' ? x[0] / x[1] : x[0] % x[1]);
}

static clj_value unchecked_int1(const clj_value *args, int32_t delta, bool negate) {
	int32_t x;
	if (!int_operands(args, 1, &x)) return CLJ_THROWN;
	uint32_t u = (uint32_t)x;
	return clj_fixnum((int32_t)(negate ? 0u - u : u + (uint32_t)delta));
}

static clj_value b_unchecked_add_int(const clj_value *args, size_t n) { (void)n; return unchecked_int2(args, '+'); }
static clj_value b_unchecked_subtract_int(const clj_value *args, size_t n) { (void)n; return unchecked_int2(args, '-'); }
static clj_value b_unchecked_multiply_int(const clj_value *args, size_t n) { (void)n; return unchecked_int2(args, '*'); }
static clj_value b_unchecked_divide_int(const clj_value *args, size_t n) { (void)n; return unchecked_int2(args, '/'); }
static clj_value b_unchecked_remainder_int(const clj_value *args, size_t n) { (void)n; return unchecked_int2(args, '%'); }
static clj_value b_unchecked_negate_int(const clj_value *args, size_t n) { (void)n; return unchecked_int1(args, 0, true); }
static clj_value b_unchecked_inc_int(const clj_value *args, size_t n) { (void)n; return unchecked_int1(args, 1, false); }
static clj_value b_unchecked_dec_int(const clj_value *args, size_t n) { (void)n; return unchecked_int1(args, -1, false); }

// ---- unchecked arithmetic: two's-complement wrap at 64 bits, as on the JVM

static clj_value unchecked2(const clj_value *args, clj_num_op op) {
	int64_t a, b;
	if (!clj_int64_of(args[0], &a) || !clj_int64_of(args[1], &b)) return clj_num_arith(args[0], args[1], op);
	uint64_t x = (uint64_t)a, y = (uint64_t)b;
	uint64_t r = op == CLJ_OP_ADD ? x + y : op == CLJ_OP_SUB ? x - y : x * y;
	return clj_long_new((int64_t)r);
}

static clj_value b_unchecked_add(const clj_value *args, size_t n) { (void)n; return unchecked2(args, CLJ_OP_ADD); }
static clj_value b_unchecked_subtract(const clj_value *args, size_t n) { (void)n; return unchecked2(args, CLJ_OP_SUB); }
static clj_value b_unchecked_multiply(const clj_value *args, size_t n) { (void)n; return unchecked2(args, CLJ_OP_MUL); }

static clj_value unchecked1(clj_value v, int64_t delta, bool negate) {
	int64_t i;
	if (!clj_int64_of(v, &i)) {
		if (negate) return clj_num_arith(clj_fixnum(0), v, CLJ_OP_SUB);
		return clj_num_arith(v, clj_fixnum(delta), CLJ_OP_ADD);
	}
	uint64_t x = (uint64_t)i;
	uint64_t r = negate ? 0u - x : x + (uint64_t)delta;
	return clj_long_new((int64_t)r);
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
	{"unchecked-byte", b_unchecked_byte, 1, 1}, {"unchecked-short", b_unchecked_short, 1, 1}, {"unchecked-int", b_unchecked_int, 1, 1},
	{"unchecked-long", b_unchecked_long, 1, 1}, {"unchecked-char", b_unchecked_char, 1, 1}, {"unchecked-float", b_unchecked_float, 1, 1},
	{"unchecked-double", b_unchecked_double, 1, 1},
	{"unchecked-add-int", b_unchecked_add_int, 2, 2}, {"unchecked-subtract-int", b_unchecked_subtract_int, 2, 2},
	{"unchecked-multiply-int", b_unchecked_multiply_int, 2, 2}, {"unchecked-divide-int", b_unchecked_divide_int, 2, 2},
	{"unchecked-remainder-int", b_unchecked_remainder_int, 2, 2}, {"unchecked-negate-int", b_unchecked_negate_int, 1, 1},
	{"unchecked-inc-int", b_unchecked_inc_int, 1, 1}, {"unchecked-dec-int", b_unchecked_dec_int, 1, 1},
	{"unchecked-add", b_unchecked_add, 2, 2},
	{"unchecked-subtract", b_unchecked_subtract, 2, 2},
	{"unchecked-multiply", b_unchecked_multiply, 2, 2},
	{"unchecked-inc", b_unchecked_inc, 1, 1},
	{"unchecked-dec", b_unchecked_dec, 1, 1},
	{"unchecked-negate", b_unchecked_negate, 1, 1},
};

// ---- the boxed number classes as namespaces of vars (the shape Thread/sleep has, docs/jvm-differences.md)

// A class a static resolves through is a namespace, so Long/MAX_VALUE reads as any ns/var does.
static void bind_static(const char *ns_name, const char *name, clj_value val) {
	clj_value ns_sym = clj_symbol_from_cstr(ns_name);
	clj_value ns = clj_ns_find_or_create(ns_sym);
	clj_value sym = clj_symbol_from_cstr(name);
	clj_var_bind_root(clj_ns_intern(ns, sym), val);
	// A root bound at boot outlives the process, as core's do (runtime.c, immortalize_root).
	if (clj_is_ptr(val)) clj_header_of(val)->flags |= CLJ_FLAG_IMMORTAL;
	clj_release(val);
	clj_release(sym);
	clj_release(ns_sym);
}

// One integer type, so the box is the value; a string argument would need Long/parseLong's answer, not this one.
static clj_value b_long_value_of(const clj_value *args, size_t n) {
	(void)n;
	int64_t v;
	if (!clj_int64_of(args[0], &v)) return clj_throw_msg("Long/valueOf expects an integer, got: %s", clj_type_name(args[0]));
	return clj_retain(args[0]);
}

static void bind_static_fn(const char *ns_name, const char *name, clj_native_fn fn, uint32_t min, uint32_t max) {
	clj_value ns_sym = clj_symbol_from_cstr(ns_name);
	clj_value sym = clj_symbol_from_cstr(name);
	clj_value qualified = clj_symbol_new(clj_symbol_name(ns_sym), clj_symbol_name(sym));
	bind_static(ns_name, name, clj_fn_native(qualified, fn, min, max));
	clj_release(qualified);
	clj_release(sym);
	clj_release(ns_sym);
}

// Demand, not a java.lang surface: a value this runtime cannot hold is absent (docs/jvm-differences.md).
static void install_boxed_classes(void) {
	bind_static("Long", "MAX_VALUE", clj_long_new(INT64_MAX));
	bind_static("Long", "MIN_VALUE", clj_long_new(INT64_MIN));
	bind_static("Integer", "MAX_VALUE", clj_fixnum(INT32_MAX));
	bind_static("Integer", "MIN_VALUE", clj_fixnum(INT32_MIN));
	bind_static("Short", "MAX_VALUE", clj_fixnum(INT16_MAX));
	bind_static("Byte", "MAX_VALUE", clj_fixnum(INT8_MAX));
	bind_static("Double", "MAX_VALUE", clj_double_new(DBL_MAX));
	bind_static("Double", "NaN", clj_double_new((double)NAN));
	bind_static("Double", "POSITIVE_INFINITY", clj_double_new((double)INFINITY));
	bind_static_fn("Long", "valueOf", b_long_value_of, 1, 1);
	bind_static_fn("Double", "isNaN", b_nan_p, 1, 1);
}

void clj_number_builtins_install(void) {
	for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) clj_builtin_bind(entries[i].name, entries[i].fn, entries[i].min, entries[i].max);
	install_boxed_classes();
}
