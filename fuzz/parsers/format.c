// @ai-generated(solo)
// Input: the format string, then after a NUL one byte per argument, an index into the palette.
#include <stdio.h>
#include <string.h>

#include "fuzz.h"

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

// fuzz/parsers/seeds.py picks arguments by these positions: keep the two in step.
static const char *const palette_src[] = {
	"0", "42", "-7", "255", "9223372036854775807", "-9223372036854775808", "18446744073709551616N", "-170141183460469231731687303715884105728N",
	"1/3", "-2/3",
	"0.0", "-0.0", "1.5", "-2.25", "0.1", "1.005", "2.5", "1e-5", "1e-300", "4.9E-324", "1.7976931348623157E308", "123456789.987654321",
	"##Inf", "##-Inf", "##NaN",
	"1.5M", "-0.00001M", "1E+400M", "12345678901234567890.123456789M",
	"\"\"", "\"abc\"", "\"héllo wörld\"", "\"日本語\"", "\"😀x\"",
	"\\a", "\\é", "\\space",
	"nil", "true", "false", ":kw", ":ns/kw", "'sym", "[1 \"two\" :three]", "{:a 1}", "#{}", "'()", "(range 3)",
	"#\"a+b\"", "#inst \"2020-02-29T12:34:56.789-00:00\"", "#uuid \"00000000-0000-0000-0000-000000000001\"",
	"65", "1114111", "1114112", "55296", "-1",
};
enum { PALETTE = sizeof palette_src / sizeof *palette_src, MAX_ARGS = 16 };

static clj_value format_fn, palette[PALETTE];

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	(void)argc, (void)argv;
	fz_boot();
	format_fn = fz_eval("clojure.core/format");
	for (size_t i = 0; i < PALETTE; i++) palette[i] = fz_eval(palette_src[i]);
	return 0;
}

static void format(const uint8_t *data, size_t size) {
	const uint8_t *nul = memchr(data, 0, size);
	size_t         flen = nul ? (size_t)(nul - data) : size;
	clj_value      args[1 + MAX_ARGS];
	size_t         n = 1;
	args[0] = fz_string(data, flen);
	for (size_t i = flen + 1; i < size && n <= MAX_ARGS; i++) args[n++] = palette[data[i] % PALETTE];
	clj_value r = clj_host_invoke(format_fn, args, n);
	if (r != CLJ_THROWN && !clj_is_string(r)) fz_finding("format answers a %s", clj_type_name(r));
	fz_drop(r);
	clj_release(args[0]);
}

// A huge width that fits an int is padding asked for, on the JVM too; one past the int range must still fail cleanly.
static bool asks_for_huge_output(const uint8_t *s, size_t n) {
	for (size_t i = 0; i < n;) {
		if (s[i] < '0' || s[i] > '9') {
			i++;
			continue;
		}
		uint64_t v = 0;
		for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) v = v > UINT32_MAX ? v : v * 10 + (s[i] - '0');
		if (v >= 100000 && v <= INT32_MAX) return true;
	}
	return false;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	const uint8_t *nul = memchr(data, 0, size);
	size_t         flen = nul ? (size_t)(nul - data) : size;
	if (!fz_utf8(data, flen) || asks_for_huge_output(data, flen)) return -1;
	return fz_run(format, data, size);
}
