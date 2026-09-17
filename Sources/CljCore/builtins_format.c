// @ai-generated(solo)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"

typedef struct {
	char  *data;
	size_t len, cap;
} buf;

static void put(buf *b, const char *s, size_t n) {
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

static void put_cstr(buf *b, const char *s) { put(b, s, strlen(s)); }

static void put_repeat(buf *b, char c, size_t n) {
	for (size_t i = 0; i < n; i++) put(b, &c, 1);
}

// One %[index$][flags][width][.precision]conversion, as java.util.Formatter parses it.
typedef struct {
	const char *start; // of the % sign
	size_t      len;
	size_t      index; // 1-based explicit argument, 0 for the next ordinary one
	bool        left, zero, plus, space, group, upper;
	int         width, precision; // -1 when absent
	char        conv;
} spec;

static clj_value spec_error(const spec *s, const char *what) { return clj_throw_msg("format: %s in %.*s", what, (int)s->len, s->start); }

// false with the exception pending on a malformed spec.
static bool parse_spec(const char *p, const char *end, spec *s) {
	const char *q = p + 1;
	memset(s, 0, sizeof *s);
	s->start = p;
	s->width = s->precision = -1;
	// An explicit index is digits followed by $; without the $ the digits are the width.
	const char *digits = q;
	while (q < end && *q >= '0' && *q <= '9') q++;
	if (q < end && *q == '$' && q > digits) {
		s->index = (size_t)atoi(digits);
		q++;
	} else {
		q = digits;
	}
	for (; q < end && strchr("-+ 0,#(", *q); q++) {
		switch (*q) {
		case '-': s->left = true; break;
		case '+': s->plus = true; break;
		case ' ': s->space = true; break;
		case '0': s->zero = true; break;
		case ',': s->group = true; break;
		default:
			s->len = (size_t)(q - p + 1);
			spec_error(s, "unsupported flag");
			return false;
		}
	}
	if (q < end && *q >= '1' && *q <= '9') {
		s->width = 0;
		while (q < end && *q >= '0' && *q <= '9') s->width = s->width * 10 + (*q++ - '0');
	}
	if (q < end && *q == '.') {
		q++;
		if (q >= end || *q < '0' || *q > '9') {
			s->len = (size_t)(q - p);
			spec_error(s, "missing precision");
			return false;
		}
		s->precision = 0;
		while (q < end && *q >= '0' && *q <= '9') s->precision = s->precision * 10 + (*q++ - '0');
	}
	if (q >= end) {
		s->len = (size_t)(q - p);
		spec_error(s, "missing conversion");
		return false;
	}
	s->conv = *q++;
	s->len = (size_t)(q - p);
	if (s->conv >= 'A' && s->conv <= 'Z' && strchr("SBCXEG", s->conv)) {
		s->upper = true;
		s->conv = (char)(s->conv - 'A' + 'a');
	}
	if (!strchr("sbcdoxefgn%", s->conv)) {
		spec_error(s, "unsupported conversion");
		return false;
	}
	return true;
}

// Pads text to the width: spaces on the left, or on the right with -, or zeros after the sign with 0.
static void put_padded(buf *b, const spec *s, const char *sign, const char *text, bool zero_ok) {
	size_t n = strlen(sign) + strlen(text);
	size_t pad = s->width > 0 && (size_t)s->width > n ? (size_t)s->width - n : 0;
	if (s->left) {
		put_cstr(b, sign);
		put_cstr(b, text);
		put_repeat(b, ' ', pad);
	} else if (s->zero && zero_ok) {
		put_cstr(b, sign);
		put_repeat(b, '0', pad);
		put_cstr(b, text);
	} else {
		put_repeat(b, ' ', pad);
		put_cstr(b, sign);
		put_cstr(b, text);
	}
}

// Inserts a comma every three digits of the integer part of digits (no sign), into out.
static void group_digits(const char *digits, char *out, size_t cap) {
	size_t int_len = strcspn(digits, ".eE");
	size_t o = 0;
	for (size_t i = 0; i < int_len && o + 1 < cap; i++) {
		if (i && (int_len - i) % 3 == 0) out[o++] = ',';
		out[o++] = digits[i];
	}
	snprintf(out + o, cap - o, "%s", digits + int_len);
}

static const char *sign_of(const spec *s, bool negative) { return negative ? "-" : s->plus ? "+" : s->space ? " " : ""; }

static clj_value format_integer(buf *b, const spec *s, clj_value v) {
	if (s->precision >= 0) return spec_error(s, "precision not allowed");
	int64_t n;
	char    text[128];
	bool    negative;
	if (clj_is_bigint(v) && s->conv == 'd') {
		clj_value t = clj_bigint_to_string(v);
		negative = clj_string_bytes(t)[0] == '-';
		snprintf(text, sizeof text, "%s", clj_string_bytes(t) + negative);
		clj_release(t);
	} else if (clj_int64_of(v, &n)) {
		negative = n < 0;
		uint64_t mag = negative ? 0u - (uint64_t)n : (uint64_t)n;
		if (s->conv == 'd') snprintf(text, sizeof text, "%llu", (unsigned long long)mag);
		else if (s->conv == 'x') snprintf(text, sizeof text, "%llx", (unsigned long long)n), negative = false;
		else snprintf(text, sizeof text, "%llo", (unsigned long long)n), negative = false;
	} else {
		return clj_throw_msg("format: %c != %s in %.*s", s->conv, clj_type_name(v), (int)s->len, s->start);
	}
	if (s->conv != 'd' && (s->plus || s->space || s->group)) return spec_error(s, "flag not allowed");
	if (s->upper) {
		for (char *c = text; *c; c++) {
			if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
		}
	}
	char grouped[192];
	if (s->group) group_digits(text, grouped, sizeof grouped);
	put_padded(b, s, sign_of(s, negative), s->group ? grouped : text, true);
	return CLJ_NIL;
}

// Java rounds the shortest round-trip digits HALF_UP, not the binary value: (format "%.2f" 1.005) is 1.01 where
// C's printf says 1.00. The digits of |x| as d.ddd… × 10^exp, at most 17 of them plus the NUL.
typedef struct {
	char digits[24];
	int  n, exp;
} decimal_digits;

static decimal_digits shortest(double x) {
	decimal_digits d = {{'0'}, 1, 0};
	if (x == 0) return d;
	char tmp[40];
	for (int prec = 1; prec <= 17; prec++) {
		snprintf(tmp, sizeof tmp, "%.*e", prec - 1, x);
		if (strtod(tmp, NULL) == x) break;
	}
	d.n = 0;
	for (const char *p = tmp; *p != 'e'; p++) {
		if (*p != '.') d.digits[d.n++] = *p;
	}
	d.digits[d.n] = '\0';
	d.exp = atoi(strchr(tmp, 'e') + 1);
	return d;
}

// Keeps the first `keep` digits, rounding half up on the next one; a carry out of the top adds a digit.
static void round_to(decimal_digits *d, int keep) {
	if (keep >= d->n) {
		while (d->n < keep) d->digits[d->n++] = '0';
		d->digits[d->n] = '\0';
		return;
	}
	bool up = keep >= 0 && d->digits[keep] >= '5';
	if (keep < 0) {
		d->digits[0] = '0';
		d->n = 1;
		d->digits[1] = '\0';
		return;
	}
	d->n = keep;
	if (up) {
		int i = keep - 1;
		while (i >= 0 && d->digits[i] == '9') d->digits[i--] = '0';
		if (i >= 0) {
			d->digits[i]++;
		} else {
			memmove(d->digits + 1, d->digits, (size_t)d->n);
			d->digits[0] = '1';
			d->n++;
			d->exp++;
		}
	}
	if (d->n == 0) {
		d->digits[0] = '0';
		d->n = 1;
	}
	d->digits[d->n] = '\0';
}

// %.pf: integer digits, then p fraction digits.
static void format_fixed(char *text, size_t cap, double x, int precision) {
	decimal_digits d = shortest(x);
	round_to(&d, d.exp + 1 + precision);
	size_t o = 0;
	if (d.exp < 0) {
		o += (size_t)snprintf(text + o, cap - o, "0");
	} else {
		for (int i = 0; i <= d.exp && o + 1 < cap; i++) text[o++] = i < d.n ? d.digits[i] : '0';
	}
	if (precision > 0 && o + 1 < cap) text[o++] = '.';
	for (int i = 0; i < precision && o + 1 < cap; i++) {
		int at = d.exp + 1 + i;
		text[o++] = at >= 0 && at < d.n ? d.digits[at] : '0';
	}
	text[o] = '\0';
}

// %.pe: one digit, p fraction digits, e±dd.
static void format_sci(char *text, size_t cap, double x, int precision) {
	decimal_digits d = shortest(x);
	round_to(&d, precision + 1);
	size_t o = (size_t)snprintf(text, cap, "%c", d.digits[0]);
	if (precision > 0 && o + 1 < cap) text[o++] = '.';
	for (int i = 1; i <= precision && o + 1 < cap; i++) text[o++] = i < d.n ? d.digits[i] : '0';
	snprintf(text + o, cap - o, "e%c%02d", d.exp < 0 ? '-' : '+', d.exp < 0 ? -d.exp : d.exp);
}

// Formatter's %g: decimal for 1e-4 <= |x| < 10^precision after rounding, scientific otherwise, trailing zeros kept.
static void format_general(char *text, size_t cap, double x, int precision) {
	if (precision == 0) precision = 1;
	decimal_digits d = shortest(x);
	round_to(&d, precision);
	if (x == 0 || (d.exp >= -4 && d.exp < precision)) format_fixed(text, cap, x, precision - 1 - (x == 0 ? 0 : d.exp));
	else format_sci(text, cap, x, precision - 1);
}

static clj_value format_float(buf *b, const spec *s, clj_value v) {
	double x;
	if (clj_is_double(v)) x = clj_double_val(v);
	else if (clj_is_decimal(v)) x = clj_num_to_double(v);
	else return clj_throw_msg("format: %c != %s in %.*s", s->conv, clj_type_name(v), (int)s->len, s->start);
	int  precision = s->precision < 0 ? 6 : s->precision;
	char text[400];
	bool negative = signbit(x) && !isnan(x);
	if (isnan(x) || isinf(x)) {
		snprintf(text, sizeof text, "%s", isnan(x) ? "NaN" : "Infinity");
		put_padded(b, s, sign_of(s, negative), text, false);
		return CLJ_NIL;
	}
	x = fabs(x);
	if (s->conv == 'f') format_fixed(text, sizeof text, x, precision);
	else if (s->conv == 'e') format_sci(text, sizeof text, x, precision);
	else format_general(text, sizeof text, x, precision);
	if (s->upper) {
		for (char *c = text; *c; c++) {
			if (*c == 'e') *c = 'E';
		}
	}
	if (s->group && s->conv == 'e') return spec_error(s, "flag not allowed");
	char grouped[460];
	if (s->group) group_digits(text, grouped, sizeof grouped);
	put_padded(b, s, sign_of(s, negative), s->group ? grouped : text, true);
	return CLJ_NIL;
}

static clj_value format_text(buf *b, const spec *s, clj_value v) {
	if (s->zero || s->plus || s->space || s->group) return spec_error(s, "flag not allowed");
	clj_value text;
	if (s->conv == 'b') {
		text = clj_string_from_cstr(clj_is_nil(v) || v == CLJ_FALSE ? "false" : "true");
	} else if (s->conv == 'c') {
		int64_t cp;
		if (clj_is_nil(v)) {
			text = clj_string_from_cstr("null");
		} else if (clj_is_char(v) || (clj_int64_of(v, &cp) && cp >= 0 && cp <= 0x10FFFF)) {
			text = clj_str_value(clj_is_char(v) ? v : clj_char((uint32_t)cp));
		} else {
			return clj_throw_msg("format: c != %s in %.*s", clj_type_name(v), (int)s->len, s->start);
		}
	} else {
		text = clj_is_nil(v) ? clj_string_from_cstr("null") : clj_str_value(v);
		if (text == CLJ_THROWN) return CLJ_THROWN;
	}
	const char *bytes = clj_string_bytes(text);
	size_t      len = clj_string_len(text);
	if (s->precision >= 0) {
		// The precision counts characters; a code point is never split.
		size_t pos = 0;
		for (int i = 0; i < s->precision && pos < len; i++) {
			uint32_t cp;
			pos += clj_utf8_decode(bytes, len, pos, &cp);
		}
		len = pos;
	}
	size_t width = s->width > 0 ? (size_t)s->width : 0, chars = 0;
	for (size_t pos = 0; pos < len;) {
		uint32_t cp;
		pos += clj_utf8_decode(bytes, len, pos, &cp);
		chars++;
	}
	size_t pad = width > chars ? width - chars : 0;
	if (!s->left) put_repeat(b, ' ', pad);
	if (s->upper) {
		for (size_t i = 0; i < len; i++) {
			char c = bytes[i] >= 'a' && bytes[i] <= 'z' ? (char)(bytes[i] - 'a' + 'A') : bytes[i];
			put(b, &c, 1);
		}
	} else {
		put(b, bytes, len);
	}
	if (s->left) put_repeat(b, ' ', pad);
	clj_release(text);
	return CLJ_NIL;
}

static clj_value b_format(const clj_value *args, size_t n) {
	if (!clj_is_string(args[0])) return clj_throw_msg("format expects a string, got: %s", clj_type_name(args[0]));
	const char *p = clj_string_bytes(args[0]), *end = p + clj_string_len(args[0]);
	buf         b = {0};
	size_t      next = 1; // the ordinary argument index, independent of explicit ones as in Formatter
	clj_value   err = CLJ_NIL;
	while (p < end && err != CLJ_THROWN) {
		const char *pct = memchr(p, '%', (size_t)(end - p));
		if (!pct) {
			put(&b, p, (size_t)(end - p));
			break;
		}
		put(&b, p, (size_t)(pct - p));
		spec s;
		if (!parse_spec(pct, end, &s)) {
			err = CLJ_THROWN;
			break;
		}
		p = pct + s.len;
		if (s.conv == '%') {
			put_padded(&b, &s, "", "%", false);
			continue;
		}
		if (s.conv == 'n') {
			put(&b, "\n", 1);
			continue;
		}
		size_t at = s.index ? s.index : next++;
		if (at >= n) {
			err = clj_throw_msg("format: missing argument for %.*s", (int)s.len, s.start);
			break;
		}
		clj_value v = args[at];
		if (s.conv == 'd' || s.conv == 'x' || s.conv == 'o') err = format_integer(&b, &s, v);
		else if (s.conv == 'f' || s.conv == 'e' || s.conv == 'g') err = format_float(&b, &s, v);
		else err = format_text(&b, &s, v);
	}
	clj_value r = err == CLJ_THROWN ? CLJ_THROWN : clj_string_new(b.data, b.len);
	free(b.data);
	return r;
}

void clj_format_builtins_install(void) { clj_builtin_bind("format", b_format, 1, CLJ_ARITY_ANY); }
