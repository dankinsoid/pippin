// @ai-generated(solo)
// Arbitrary bytes as a host hands source to the reader, not NUL-terminated: an overread is a finding.
#include <stdio.h>
#include <string.h>

#include "fuzz.h"

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

enum { MAX_FORMS = 64 };

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	(void)argc, (void)argv;
	fz_boot();
	clj_value features = fz_eval("#{:clj}");
	clj_reader_set_features(features);
	clj_release(features);
	return 0;
}

// A printed form reads back equal, and its print is a fixed point from the second read on: the first read's
// metadata can change a map's layout and so its order (docs/jvm-differences.md), which printing does not carry.
static void round_trip(clj_value form) {
	clj_value text = clj_pr_str(form);
	if (text == CLJ_THROWN) {
		fz_drop(text);
		return;
	}
	clj_value back;
	char      msg[256];
	if (fz_read(fz_cstr(text), clj_string_len(text), &back, msg, sizeof msg) != CLJ_READ_OK)
		fz_finding("a printed form does not read back: %s: %s", fz_cstr(text), msg);
	// NaN is not equal to itself, so a form holding one is compared by its print alone.
	if (!memmem(fz_cstr(text), clj_string_len(text), "##NaN", 5) && !clj_equals(form, back)) fz_finding("a printed form reads back as another value: %s", fz_cstr(text));
	clj_value again = clj_pr_str(back);
	if (again == CLJ_THROWN) fz_finding("a printed form reads back as one that cannot print: %s", fz_cstr(text));
	clj_value third;
	if (fz_read(fz_cstr(again), clj_string_len(again), &third, msg, sizeof msg) != CLJ_READ_OK)
		fz_finding("a printed form does not read back: %s: %s", fz_cstr(again), msg);
	clj_value last = clj_pr_str(third);
	if (last == CLJ_THROWN || strcmp(fz_cstr(last), fz_cstr(again)) != 0)
		fz_finding("pr-str is not a fixed point: %s reads back as %s", fz_cstr(again), last == CLJ_THROWN ? "a throw" : fz_cstr(last));
	clj_release(last);
	clj_release(third);
	clj_release(again);
	clj_release(back);
	clj_release(text);
}

static void read_all(const uint8_t *data, size_t size) {
	clj_reader r;
	clj_reader_init(&r, (const char *)data, size);
	clj_reader_use_namespaces(&r);
	for (int i = 0; i < MAX_FORMS; i++) {
		clj_value       form;
		clj_read_status st = clj_read(&r, &form);
		if (st == CLJ_READ_EOF) return;
		if (st == CLJ_READ_ERROR) {
			if (!*clj_reader_message(&r)) fz_finding("a reader error without a message");
			clj_release(clj_take_pending());
			return;
		}
		round_trip(form);
		clj_release(form);
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (!fz_syntax_quotes_ok(data, size)) return -1;
	return fz_run(read_all, data, size);
}
