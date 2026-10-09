// @ai-generated(solo)
// Arbitrary bytes as a host hands source to the reader, not NUL-terminated: an overread is a finding.
#include <stdio.h>
#include <string.h>

#include "fuzz.h"

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

enum { MAX_FORMS = 64, MAX_SYNTAX_QUOTES = 5 };

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	(void)argc, (void)argv;
	fz_boot();
	clj_value features = fz_eval("#{:clj}");
	clj_reader_set_features(features);
	clj_release(features);
	return 0;
}

// pr-str is a fixed point after one read; metadata is not printed, so it is not compared.
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
	clj_value again = clj_pr_str(back);
	if (again == CLJ_THROWN) fz_finding("a printed form reads back as one that cannot print: %s", fz_cstr(text));
	if (clj_string_len(again) != clj_string_len(text) || memcmp(fz_cstr(again), fz_cstr(text), clj_string_len(text)) != 0)
		fz_finding("pr-str is not a fixed point: %s reads back as %s", fz_cstr(text), fz_cstr(again));
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
	// Each nested syntax-quote multiplies the expansion, on the JVM too: deep nesting is a big input, not a bug.
	size_t quotes = 0;
	for (size_t i = 0; i < size; i++) quotes += data[i] == '`';
	if (quotes > MAX_SYNTAX_QUOTES) return -1;
	return fz_run(read_all, data, size);
}
