// @ai-generated(solo)
#include "fuzz.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/cc.h"

// The weak boot hook of a Swift host; a C-only host comes up without one (design §5).
void clj_host_boot(void);
void clj_host_boot(void) {}

void fz_boot(void) {
	// ASan sees object boundaries only with the system allocator.
	setenv("CLJ_SYSTEM_ALLOC", "1", 1);
	clj_init();
}

_Noreturn void fz_finding(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fputs("fuzz finding: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	abort();
}

void fz_drop(clj_value v) {
	if (v == CLJ_THROWN) clj_release(clj_take_pending());
	else clj_release(v);
}

clj_read_status fz_read(const char *bytes, size_t len, clj_value *out, char *msg, size_t cap) {
	clj_reader r;
	clj_reader_init(&r, bytes, len);
	clj_reader_use_namespaces(&r);
	clj_read_status st = clj_read(&r, out);
	if (st == CLJ_READ_ERROR) {
		if (msg) snprintf(msg, cap, "%s", clj_reader_message(&r));
		clj_release(clj_take_pending());
	}
	return st;
}

clj_value fz_eval(const char *src) {
	clj_value form;
	char      msg[256];
	if (fz_read(src, strlen(src), &form, msg, sizeof msg) != CLJ_READ_OK) fz_finding("harness form does not read: %s: %s", src, msg);
	clj_value v = clj_eval(form, NULL);
	clj_release(form);
	if (v == CLJ_THROWN) {
		clj_value text = clj_pr_str(clj_pending());
		fz_finding("harness form throws: %s: %s", src, text == CLJ_THROWN ? "?" : fz_cstr(text));
	}
	return v;
}

bool fz_utf8(const uint8_t *s, size_t n) {
	for (size_t i = 0; i < n;) {
		uint8_t c = s[i];
		if (c == 0) return false;
		if (c < 0x80) {
			i++;
			continue;
		}
		size_t   len;
		uint32_t cp, min;
		if ((c & 0xE0) == 0xC0) len = 2, cp = c & 0x1F, min = 0x80;
		else if ((c & 0xF0) == 0xE0) len = 3, cp = c & 0x0F, min = 0x800;
		else if ((c & 0xF8) == 0xF0) len = 4, cp = c & 0x07, min = 0x10000;
		else return false;
		if (i + len > n) return false;
		for (size_t k = 1; k < len; k++) {
			if ((s[i + k] & 0xC0) != 0x80) return false;
			cp = cp << 6 | (s[i + k] & 0x3F);
		}
		if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
		i += len;
	}
	return true;
}

clj_value fz_string(const uint8_t *data, size_t size) { return clj_string_new((const char *)data, size); }

// The types whose live count body changes, to stderr: what a leak finding names.
static void report_growth(fz_body body, const uint8_t *data, size_t size) {
	enum { CAP = 1024 };
	static const clj_type *types[CAP], *after_types[CAP];
	static int64_t         counts[CAP], after_counts[CAP];
	size_t                 n = clj_debug_live_by_type(types, counts, CAP);
	body(data, size);
	clj_cc_collect();
	size_t m = clj_debug_live_by_type(after_types, after_counts, CAP);
	for (size_t i = 0; i < m; i++) {
		int64_t was = 0;
		for (size_t k = 0; k < n; k++) {
			if (types[k] == after_types[i]) was = counts[k];
		}
		if (after_counts[i] != was) fprintf(stderr, "  %s: %+lld\n", after_types[i]->name, (long long)(after_counts[i] - was));
	}
}

int fz_run(fz_body body, const uint8_t *data, size_t size) {
	body(data, size);
	clj_cc_collect();
	int64_t before = clj_debug_live_objects();
	body(data, size);
	clj_cc_collect();
	int64_t after = clj_debug_live_objects();
	if (!clj_is_nil(clj_pending())) fz_finding("an exception is left pending after the run");
	if (after > before) {
		fprintf(stderr, "live objects by type, one more run:\n");
		report_growth(body, data, size);
		fz_finding("leak: the second run of the input left %lld more live objects", (long long)(after - before));
	}
	return 0;
}
