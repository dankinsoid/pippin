// @ai-generated(solo)
// Input: one numeric text, through the reader and every parse function that takes one.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fuzz.h"

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static clj_value parse_long_fn, parse_double_fn, bigint_fn, bigdec_fn;

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	(void)argc, (void)argv;
	fz_boot();
	parse_long_fn = fz_eval("clojure.core/parse-long");
	parse_double_fn = fz_eval("clojure.core/parse-double");
	bigint_fn = fz_eval("clojure.core/bigint");
	bigdec_fn = fz_eval("clojure.core/bigdec");
	return 0;
}

static clj_value call1(clj_value f, clj_value arg) { return clj_host_invoke(f, &arg, 1); }

static bool is_nan(clj_value v) { return clj_is_double(v) && isnan(clj_double_val(v)); }

static clj_num_kind kind(clj_value v) {
	clj_num_kind k = clj_num_kind_of(v);
	return k == CLJ_NUM_LONG ? CLJ_NUM_FIXNUM : k;
}

// A number prints as text that reads back as the same kind and value, and prints the same again.
static void round_trip(clj_value v, const char *how, const char *src) {
	clj_value text = clj_pr_str(v);
	if (text == CLJ_THROWN) fz_finding("%s of %s does not print", how, src);
	clj_value back;
	char      msg[256];
	if (fz_read(fz_cstr(text), clj_string_len(text), &back, msg, sizeof msg) != CLJ_READ_OK)
		fz_finding("%s of %s prints as %s, which does not read: %s", how, src, fz_cstr(text), msg);
	if (kind(back) != kind(v)) fz_finding("%s of %s prints as %s, which reads as a %s", how, src, fz_cstr(text), clj_type_name(back));
	if (!is_nan(v) && !clj_equals(v, back)) fz_finding("%s of %s prints as %s, which reads as another value", how, src, fz_cstr(text));
	clj_value again = clj_pr_str(back);
	if (again == CLJ_THROWN || strcmp(fz_cstr(again), fz_cstr(text)) != 0)
		fz_finding("%s of %s prints as %s, which reads back and prints otherwise", how, src, fz_cstr(text));
	clj_release(again);
	clj_release(back);
	clj_release(text);
}

static size_t digits(const char *s, size_t i) {
	while (s[i] >= '0' && s[i] <= '9') i++;
	return i;
}

// [+-]?(0|[1-9][0-9]*): the text LispReader and Long.valueOf both take as the same decimal integer.
static bool plain_integer(const char *s) {
	size_t i = s[0] == '+' || s[0] == '-';
	if (s[i] == '0') return s[i + 1] == '\0';
	size_t end = digits(s, i);
	return end > i && s[end] == '\0';
}

// [+-]?[0-9]+(\.[0-9]*)?([eE][+-]?[0-9]+)? with a dot or an exponent: a double to both the reader and Double.valueOf.
static bool plain_double(const char *s) {
	size_t i = s[0] == '+' || s[0] == '-';
	size_t j = digits(s, i);
	if (j == i) return false;
	bool shaped = false;
	if (s[j] == '.') j = digits(s, j + 1), shaped = true;
	if (s[j] == 'e' || s[j] == 'E') {
		size_t k = j + 1 + (s[j + 1] == '+' || s[j + 1] == '-');
		j = digits(s, k);
		if (j == k) return false;
		shaped = true;
	}
	return shaped && s[j] == '\0';
}

// What the reader made of src against what the parse function of the same grammar makes of it.
static void agree(clj_value read, clj_value s, const char *src) {
	if (clj_is_double(read) && plain_double(src)) {
		clj_value d = call1(parse_double_fn, s);
		if (d == CLJ_THROWN || !clj_is_double(d)) fz_finding("%s reads as a double, but parse-double answers %s", src, d == CLJ_THROWN ? "a throw" : clj_type_name(d));
		double a = clj_double_val(read), b = clj_double_val(d);
		if (memcmp(&a, &b, sizeof a) != 0) fz_finding("%s reads as %.17g, but parse-double answers %.17g", src, a, b);
		clj_release(d);
	}
	if (clj_is_integer(read) && plain_integer(src)) {
		clj_value l = call1(parse_long_fn, s);
		if (l == CLJ_THROWN) fz_finding("parse-long of %s throws", src);
		int64_t n;
		bool    fits = clj_int64_of(read, &n);
		if (fits != !clj_is_nil(l)) fz_finding("%s reads as a %s, but parse-long answers %s", src, clj_type_name(read), clj_type_name(l));
		if (fits && !clj_equals(read, l)) fz_finding("%s reads as one long and parse-long answers another", src);
		clj_release(l);
	}
}

static void parse(const uint8_t *data, size_t size) {
	clj_value   s = fz_string(data, size);
	const char *src = fz_cstr(s);
	clj_value   read;
	if (fz_read(src, size, &read, NULL, 0) == CLJ_READ_OK) {
		if (clj_is_number(read)) {
			round_trip(read, "reading", src);
			agree(read, s, src);
		}
		clj_release(read);
	}
	const struct {
		const char *name;
		clj_value   fn;
	} parsers[] = {{"parse-long", parse_long_fn}, {"parse-double", parse_double_fn}, {"bigint", bigint_fn}, {"bigdec", bigdec_fn}};
	for (size_t i = 0; i < sizeof parsers / sizeof *parsers; i++) {
		clj_value v = call1(parsers[i].fn, s);
		if (v != CLJ_THROWN && !clj_is_nil(v)) {
			if (!clj_is_number(v)) fz_finding("%s of %s answers a %s", parsers[i].name, src, clj_type_name(v));
			round_trip(v, parsers[i].name, src);
		}
		fz_drop(v);
	}
	const unsigned radixes[] = {2, 8, 16, 36};
	for (size_t i = 0; i < sizeof radixes / sizeof *radixes; i++) {
		clj_value v = clj_bigint_parse(src, size, radixes[i]);
		if (!clj_is_nil(v)) round_trip(v, "clj_bigint_parse", src);
		clj_release(v);
	}
	clj_release(s);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (!fz_utf8(data, size)) return -1;
	return fz_run(parse, data, size);
}
