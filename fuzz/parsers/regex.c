// @ai-generated(solo)
// Input: a pattern, then after a NUL the subject; with no NUL the pattern is its own subject.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fuzz.h"

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

// The deadline is the only stop a backtracking pattern has; an operation must end within it plus the slack.
enum { DEADLINE_MS = 200, SLACK_MS = 3000, MAX_FINDS = 256 };

static clj_value str_fn;

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	(void)argc, (void)argv;
	fz_boot();
	str_fn = fz_eval("clojure.core/str");
	return 0;
}

static uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static const char *op_name;
static uint64_t    op_start;

static void begin(const char *what) {
	op_name = what;
	op_start = now_ms();
	clj_deadline_set_ms(DEADLINE_MS);
}

static void end(clj_value pattern) {
	uint64_t took = now_ms() - op_start;
	clj_deadline_set_ms(0);
	if (took > DEADLINE_MS + SLACK_MS) fz_finding("%s ran %llu ms past a %d ms deadline: %s", op_name, (unsigned long long)(took - DEADLINE_MS), DEADLINE_MS, fz_cstr(pattern));
}

// A pattern prints as #"..." and must read back as a pattern with the same groups. Printing is verbatim, as
// RT.print's is, so a bare `"`, or a last backslash inside \Q, prints unreadably on the JVM too.
static void round_trip(clj_value re, clj_value pattern) {
	const char *s = fz_cstr(pattern);
	size_t      n = clj_string_len(pattern);
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\\' && i + 1 < n) i++;
		else if (s[i] == '"' || s[i] == '\\') return;
	}
	clj_value text = clj_pr_str(re);
	if (text == CLJ_THROWN) fz_finding("a pattern does not print: %s", fz_cstr(pattern));
	clj_value back;
	char      msg[256];
	if (fz_read(fz_cstr(text), clj_string_len(text), &back, msg, sizeof msg) != CLJ_READ_OK)
		fz_finding("a printed pattern does not read back: %s: %s", fz_cstr(text), msg);
	if (!clj_is_regex(back) || clj_regex_group_count(back) != clj_regex_group_count(re))
		fz_finding("a printed pattern reads back as another: %s", fz_cstr(text));
	clj_release(back);
	clj_release(text);
}

static void scan(clj_value re, clj_value pattern, clj_value subject) {
	clj_value m = clj_matcher_new(re, subject);
	if (m == CLJ_THROWN) fz_finding("re-matcher throws on a pattern and a string");
	begin("a matcher scan");
	for (int i = 0; i < MAX_FINDS; i++) {
		clj_value hit = clj_matcher_find(m);
		if (hit == CLJ_THROWN || clj_is_nil(hit)) {
			fz_drop(hit);
			break;
		}
		clj_release(hit);
		fz_drop(clj_matcher_groups(m));
		for (intptr_t g = 0; g <= (intptr_t)clj_regex_group_count(re) + 1; g++) fz_drop(clj_matcher_group(m, g));
	}
	end(pattern);
	clj_release(m);
}

static void replacements(clj_value re, clj_value pattern, clj_value subject) {
	const char *with[] = {"<$0>", "$1", "${a}", "\\$", ""};
	for (size_t i = 0; i < sizeof with / sizeof *with; i++) {
		clj_value r = clj_string_from_cstr(with[i]);
		begin("re-replace");
		fz_drop(clj_regex_replace(re, subject, r, i % 2 == 0));
		end(pattern);
		clj_release(r);
	}
	begin("re-replace-by");
	fz_drop(clj_regex_replace_by(re, subject, str_fn, false));
	end(pattern);
	const intptr_t limits[] = {0, -1, 2};
	for (size_t i = 0; i < sizeof limits / sizeof *limits; i++) {
		begin("re-split");
		fz_drop(clj_regex_split(re, subject, limits[i]));
		end(pattern);
	}
}

static void match(const uint8_t *data, size_t size) {
	const uint8_t *nul = memchr(data, 0, size);
	size_t         plen = nul ? (size_t)(nul - data) : size;
	const uint8_t *sbytes = nul ? nul + 1 : data;
	size_t         slen = nul ? size - plen - 1 : size;
	clj_value      pattern = fz_string(data, plen), subject = fz_string(sbytes, slen);
	begin("re-pattern");
	clj_value re = clj_regex_new(pattern);
	end(pattern);
	if (re == CLJ_THROWN) {
		clj_value ex = clj_take_pending();
		if (!clj_is_ex_info(ex) || clj_is_nil(clj_exception_data(ex))) fz_finding("a pattern syntax error carries no data: %s", fz_cstr(pattern));
		clj_release(ex);
	} else {
		round_trip(re, pattern);
		begin("re-matches");
		clj_value whole = clj_re_matches(re, subject);
		end(pattern);
		begin("re-find");
		clj_value found = clj_re_find(re, subject);
		end(pattern);
		// A whole-input match starts at 0, where a find tries first and so cannot fail.
		if (whole != CLJ_THROWN && !clj_is_nil(whole) && found != CLJ_THROWN && clj_is_nil(found))
			fz_finding("re-matches matches where re-find finds nothing: %s", fz_cstr(pattern));
		fz_drop(whole);
		fz_drop(found);
		scan(re, pattern, subject);
		replacements(re, pattern, subject);
		clj_release(re);
	}
	clj_release(subject);
	clj_release(pattern);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	const uint8_t *nul = memchr(data, 0, size);
	size_t         plen = nul ? (size_t)(nul - data) : size;
	if (!fz_utf8(data, plen) || (nul && !fz_utf8(nul + 1, size - plen - 1))) return -1;
	return fz_run(match, data, size);
}
