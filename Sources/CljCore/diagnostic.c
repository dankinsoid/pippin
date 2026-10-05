// @ai-generated(solo)
// The rendering half of design §3 «Диагностика»: one text for every consumer, built from the ex-data
// alone. A fixed buffer would be a defect there, so the text grows and nothing is cut.
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/diagnostic.h"
#include "clj/error.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/printer.h"
#include "clj/string.h"
#include "clj/vector.h"

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_file, kw_line, kw_column, kw_end_line, kw_end_column, kw_form_line, kw_form_column, kw_suggestion;

static void intern_keywords(void) {
	kw_file = clj_keyword_from_cstr("file");
	kw_line = clj_keyword_from_cstr("line");
	kw_column = clj_keyword_from_cstr("column");
	kw_end_line = clj_keyword_from_cstr("end-line");
	kw_end_column = clj_keyword_from_cstr("end-column");
	kw_form_line = clj_keyword_from_cstr("form-line");
	kw_form_column = clj_keyword_from_cstr("form-column");
	kw_suggestion = clj_keyword_from_cstr("suggestion");
}

void clj_diagnostic_intern_keywords(void) { pthread_once(&keywords_once, intern_keywords); }

// ---- the growing text

typedef struct {
	char  *bytes;
	size_t n, cap;
} out;

static void put(out *o, const char *bytes, size_t n) {
	if (o->n + n + 1 > o->cap) {
		size_t cap = o->cap ? o->cap * 2 : 256;
		while (cap < o->n + n + 1) cap *= 2;
		char *p = realloc(o->bytes, cap);
		if (!p) clj_fatal("out of memory");
		o->bytes = p;
		o->cap = cap;
	}
	memcpy(o->bytes + o->n, bytes, n);
	o->n += n;
	o->bytes[o->n] = '\0';
}

static void put_cstr(out *o, const char *s) { put(o, s, strlen(s)); }

static void put_fmt(out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put_fmt(out *o, const char *fmt, ...) {
	va_list ap, count;
	va_start(ap, fmt);
	va_copy(count, ap);
	int n = vsnprintf(NULL, 0, fmt, count);
	va_end(count);
	if (n < 0) clj_fatal("vsnprintf failed");
	char *text = malloc((size_t)n + 1);
	if (!text) clj_fatal("out of memory");
	vsnprintf(text, (size_t)n + 1, fmt, ap);
	va_end(ap);
	put(o, text, (size_t)n);
	free(text);
}

// ---- reading the data

static int64_t number(clj_value map, clj_value key) {
	if (!clj_is_map(map)) return 0;
	clj_value v = clj_map_get(map, key, CLJ_NIL);
	return clj_is_fixnum(v) ? clj_fixnum_val(v) : 0;
}

// Every link of the cause chain, innermost last, is offered; the deepest answer wins.
clj_value clj_diagnostic_position(clj_value ex, bool with_file) {
	pthread_once(&keywords_once, intern_keywords);
	clj_value best = CLJ_NIL;
	clj_value cur = clj_retain(ex);
	while (!clj_is_nil(cur)) {
		clj_value data = clj_ex_data(cur);
		if (clj_is_map(data) && number(data, kw_line) > 0 && (!with_file || clj_is_string(clj_map_get(data, kw_file, CLJ_NIL)))) {
			clj_release(best);
			best = clj_map_empty();
			clj_value keys[5] = {kw_file, kw_line, kw_column, kw_end_line, kw_end_column};
			for (int i = 0; i < 5; i++) {
				clj_value v = clj_map_get(data, keys[i], CLJ_NIL);
				if (!clj_is_nil(v)) best = clj_map_assoc(best, keys[i], v);
			}
		}
		clj_release(data);
		clj_value next = clj_ex_cause(cur);
		clj_release(cur);
		cur = next;
	}
	return best;
}

// Whether `outer` is only `inner` with something prefixed: the shape a loader's "Syntax error compiling
// at (f:l:c). <message>" has, and the one case where the inner wording is the one to show.
static bool wraps(clj_value outer, clj_value inner) {
	if (!clj_is_string(outer) || !clj_is_string(inner)) return false;
	uint32_t no = clj_string_len(outer), ni = clj_string_len(inner);
	return no >= ni && memcmp(clj_string_bytes(outer) + (no - ni), clj_string_bytes(inner), ni) == 0;
}

// The link whose message is the diagnostic's, retained; nil when no link has one.
static clj_value message_link(clj_value ex) {
	clj_value cur = clj_retain(ex);
	while (!clj_is_nil(cur)) {
		clj_value message = clj_ex_message(cur);
		clj_value cause = clj_ex_cause(cur);
		clj_value inner = clj_ex_message(cause);
		bool      step = !clj_is_string(message) || wraps(message, inner);
		clj_release(message);
		clj_release(inner);
		if (!step) {
			clj_release(cause);
			return cur;
		}
		clj_release(cur);
		cur = cause;
	}
	return CLJ_NIL;
}

clj_value clj_diagnostic_cause_message(clj_value ex) {
	clj_value link = message_link(ex);
	clj_value message = clj_ex_message(link);
	clj_release(link);
	return message;
}

// The innermost link's own data, where the suggestion and the arities sit; nil when there is none.
static clj_value innermost_data(clj_value ex) {
	clj_value best = CLJ_NIL;
	clj_value cur = clj_retain(ex);
	while (!clj_is_nil(cur)) {
		clj_value data = clj_ex_data(cur);
		if (clj_is_map(data)) {
			clj_release(best);
			best = data;
		} else {
			clj_release(data);
		}
		clj_value next = clj_ex_cause(cur);
		clj_release(cur);
		cur = next;
	}
	return best;
}

// ---- the source excerpt

// Line `line` of the file, without its terminator; NULL when the file cannot be read or is shorter.
// The whole file is read: a source file is small, and this runs only once a diagnostic is being printed.
// @ai-generated(solo)
static char *source_line(clj_value file, int64_t line, size_t *len) {
	if (!clj_is_string(file) || line <= 0) return NULL;
	FILE *f = fopen(clj_string_bytes(file), "rb");
	if (!f) return NULL;
	char  *text = NULL;
	size_t n = 0, cap = 0;
	for (;;) {
		if (n == cap) {
			cap = cap ? cap * 2 : 1 << 16;
			char *p = realloc(text, cap);
			if (!p) clj_fatal("out of memory");
			text = p;
		}
		size_t got = fread(text + n, 1, cap - n, f);
		if (!got) break;
		n += got;
	}
	fclose(f);
	size_t at = 0;
	for (int64_t l = 1; l < line; l++) {
		while (at < n && text[at] != '\n') at++;
		if (at == n) {
			free(text);
			return NULL;
		}
		at++;
	}
	size_t stop = at;
	while (stop < n && text[stop] != '\n') stop++;
	while (stop > at && text[stop - 1] == '\r') stop--;
	char *answer = malloc(stop - at + 1);
	if (!answer) clj_fatal("out of memory");
	memcpy(answer, text + at, stop - at);
	answer[stop - at] = '\0';
	*len = stop - at;
	free(text);
	return answer;
}

// Byte offset of 1-based column `col`, counting as the reader counts: one per non-continuation byte.
static size_t column_offset(const char *bytes, size_t len, int64_t col) {
	size_t at = 0;
	for (int64_t c = 1; c < col && at < len; c++) {
		at++;
		while (at < len && ((unsigned char)bytes[at] & 0xC0) == 0x80) at++;
	}
	return at;
}

// Columns from `from` to `to` of the line, so a span of multibyte text gets one caret per character.
static size_t column_span(const char *bytes, size_t len, int64_t from, int64_t to) {
	size_t a = column_offset(bytes, len, from), b = column_offset(bytes, len, to);
	size_t n = 0;
	for (size_t at = a; at < b && at < len; at++) {
		if (((unsigned char)bytes[at] & 0xC0) != 0x80) n++;
	}
	return n;
}

// The line quoted under a gutter, with the span underlined. A tab in the prefix is copied into the
// underline, so the carets stay under the text whatever the terminal's tab width is.
// @ai-generated(solo)
static void put_excerpt(out *o, clj_value file, int64_t line, int64_t col, int64_t end_line, int64_t end_col) {
	size_t len = 0;
	char  *text = source_line(file, line, &len);
	if (!text) return;
	char gutter[24];
	snprintf(gutter, sizeof gutter, "%lld", (long long)line);
	size_t width = strlen(gutter);
	put_fmt(o, " %*s |\n", (int)width, "");
	put_fmt(o, " %s | %s\n", gutter, text);
	put_fmt(o, " %*s | ", (int)width, "");
	size_t prefix = column_offset(text, len, col);
	for (size_t i = 0; i < prefix; i++) put_cstr(o, text[i] == '\t' ? "\t" : " ");
	// A span that runs past this line is underlined to its end: only one line is quoted.
	int64_t to = end_line == line && end_col > col ? end_col : (int64_t)len + 1;
	size_t  carets = column_span(text, len, col, to);
	for (size_t i = 0; i < (carets ? carets : 1); i++) put_cstr(o, "^");
	put_cstr(o, "\n");
	free(text);
}

// ---- the whole diagnostic

clj_value clj_diagnostic_render(clj_value ex) {
	pthread_once(&keywords_once, intern_keywords);
	clj_value message = clj_diagnostic_cause_message(ex);
	if (clj_is_nil(message)) {
		// A thrown non-error, or an error without a message: its printed form is all there is to say.
		clj_release(message);
		message = clj_pr_str(ex);
		if (message == CLJ_THROWN) return CLJ_THROWN;
	}
	clj_value quotable = clj_diagnostic_position(ex, true);
	clj_value any = clj_diagnostic_position(ex, false);
	clj_value data = innermost_data(ex);
	clj_value pos = clj_is_nil(quotable) ? any : quotable;

	out o = {0};
	put_fmt(&o, "error: %.*s\n", (int)clj_string_len(message), clj_string_bytes(message));
	if (!clj_is_nil(pos)) {
		clj_value file = clj_map_get(pos, kw_file, CLJ_NIL);
		int64_t   line = number(pos, kw_line), col = number(pos, kw_column);
		if (clj_is_string(file)) put_fmt(&o, " --> %s:%lld:%lld\n", clj_string_bytes(file), (long long)line, (long long)col);
		else put_fmt(&o, " --> %lld:%lld\n", (long long)line, (long long)col);
		put_excerpt(&o, file, line, col, number(pos, kw_end_line), number(pos, kw_end_column));
	}
	if (clj_is_map(data)) {
		clj_value hint = clj_map_get(data, kw_suggestion, CLJ_NIL);
		if (!clj_is_nil(hint)) {
			clj_value text = clj_pr_str_max(hint, CLJ_ERROR_PRINT_MAX);
			if (text != CLJ_THROWN) {
				put_fmt(&o, " = help: did you mean %.*s?\n", (int)clj_string_len(text), clj_string_bytes(text));
				clj_release(text);
			}
		}
	}
	// The position shown is the one with a file; a deeper one without a file is still where the mistake is.
	if (!clj_is_nil(quotable) && !clj_is_nil(any) && number(any, kw_line) != number(quotable, kw_line))
		put_fmt(&o, " = note: raised at %lld:%lld\n", (long long)number(any, kw_line), (long long)number(any, kw_column));
	// Every message below the one shown, so a cause is never lost (design §3 «Диагностика»); a link that
	// only wraps the next one adds nothing, and two links wording it the same say it once.
	clj_value link = message_link(ex);
	clj_value below = clj_is_nil(link) ? CLJ_NIL : clj_ex_cause(link);
	clj_release(link);
	clj_value shown = clj_retain(message);
	while (!clj_is_nil(below)) {
		clj_value note = clj_ex_message(below);
		if (clj_is_string(note) && !wraps(shown, note) && !wraps(note, shown)) {
			put_fmt(&o, " = note: caused by: %.*s\n", (int)clj_string_len(note), clj_string_bytes(note));
			clj_release(shown);
			shown = clj_retain(note);
		}
		clj_release(note);
		clj_value next = clj_ex_cause(below);
		clj_release(below);
		below = next;
	}
	clj_release(shown);
	clj_value top = clj_ex_data(ex);
	if (clj_is_map(top) && number(top, kw_form_line) > 0 && number(top, kw_form_line) != number(pos, kw_line))
		put_fmt(&o, " = note: in the top-level form at %lld:%lld\n", (long long)number(top, kw_form_line), (long long)number(top, kw_form_column));
	clj_release(top);

	clj_value answer = clj_string_new(o.bytes, o.n);
	free(o.bytes);
	clj_release(message);
	clj_release(quotable);
	clj_release(any);
	clj_release(data);
	return answer;
}
