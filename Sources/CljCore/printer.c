// @ai-generated(solo)
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/record.h"

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_message, kw_data, kw_cause;

static void intern_keywords(void) {
	kw_message = clj_keyword_from_cstr("message");
	kw_data = clj_keyword_from_cstr("data");
	kw_cause = clj_keyword_from_cstr("cause");
}

void clj_printer_intern_keywords(void) { pthread_once(&keywords_once, intern_keywords); }

typedef struct {
	char  *data;
	size_t len, cap;
} buf;

static void buf_reserve(buf *b, size_t extra) {
	if (b->len + extra <= b->cap) return;
	size_t cap = b->cap ? b->cap : 64;
	while (cap < b->len + extra) cap *= 2;
	b->data = realloc(b->data, cap);
	if (!b->data) clj_fatal("out of memory");
	b->cap = cap;
}

static void put_bytes(buf *b, const char *s, size_t n) {
	buf_reserve(b, n);
	memcpy(b->data + b->len, s, n);
	b->len += n;
}

static void put_cstr(buf *b, const char *s) { put_bytes(b, s, strlen(s)); }

static void put_char(buf *b, char c) {
	buf_reserve(b, 1);
	b->data[b->len++] = c;
}

static void put_fmt(buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put_fmt(buf *b, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0) clj_fatal("vsnprintf failed");
	buf_reserve(b, (size_t)n + 1);
	va_start(ap, fmt);
	vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
	va_end(ap);
	b->len += (size_t)n;
}

static void put_utf8(buf *b, uint32_t cp) {
	char s[4];
	size_t n;
	if (cp < 0x80) {
		s[0] = (char)cp;
		n = 1;
	} else if (cp < 0x800) {
		s[0] = (char)(0xC0 | (cp >> 6));
		s[1] = (char)(0x80 | (cp & 0x3F));
		n = 2;
	} else if (cp < 0x10000) {
		s[0] = (char)(0xE0 | (cp >> 12));
		s[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		s[2] = (char)(0x80 | (cp & 0x3F));
		n = 3;
	} else {
		s[0] = (char)(0xF0 | (cp >> 18));
		s[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		s[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		s[3] = (char)(0x80 | (cp & 0x3F));
		n = 4;
	}
	put_bytes(b, s, n);
}

// Java Double.toString: plain notation in [1e-3, 1e7), else d.dE±n; shortest round-trip digits, at least two.
static void put_double(buf *b, double d) {
	if (isnan(d)) {
		put_cstr(b, "##NaN");
		return;
	}
	if (isinf(d)) {
		put_cstr(b, d < 0 ? "##-Inf" : "##Inf");
		return;
	}
	if (d == 0.0) {
		put_cstr(b, signbit(d) ? "-0.0" : "0.0");
		return;
	}
	char tmp[40];
	// Locale-dependent formatting; the runtime never calls setlocale.
	for (int prec = 2; prec <= 17; prec++) {
		snprintf(tmp, sizeof tmp, "%.*e", prec - 1, d);
		if (strtod(tmp, NULL) == d) break;
	}
	const char *p = tmp;
	if (*p == '-') {
		put_char(b, '-');
		p++;
	}
	char   digits[20];
	size_t nd = 0;
	for (; *p != 'e'; p++) {
		if (*p != '.') digits[nd++] = *p;
	}
	int exp = atoi(p + 1);
	while (nd > 1 && digits[nd - 1] == '0') nd--;
	if (exp >= -3 && exp < 7) {
		if (exp >= 0) {
			size_t int_len = (size_t)exp + 1;
			for (size_t i = 0; i < int_len; i++) put_char(b, i < nd ? digits[i] : '0');
			put_char(b, '.');
			if (nd > int_len) put_bytes(b, digits + int_len, nd - int_len);
			else put_char(b, '0');
		} else {
			put_cstr(b, "0.");
			for (int i = 0; i < -exp - 1; i++) put_char(b, '0');
			put_bytes(b, digits, nd);
		}
	} else {
		put_char(b, digits[0]);
		put_char(b, '.');
		if (nd > 1) put_bytes(b, digits + 1, nd - 1);
		else put_char(b, '0');
		put_fmt(b, "E%d", exp);
	}
}

static void put_string_literal(buf *b, clj_value s) {
	const unsigned char *p = (const unsigned char *)clj_string_bytes(s);
	size_t n = clj_string_len(s);
	put_char(b, '"');
	for (size_t i = 0; i < n; i++) {
		unsigned char c = p[i];
		switch (c) {
		case '"': put_cstr(b, "\\\""); break;
		case '\\': put_cstr(b, "\\\\"); break;
		case '\n': put_cstr(b, "\\n"); break;
		case '\t': put_cstr(b, "\\t"); break;
		case '\r': put_cstr(b, "\\r"); break;
		case '\b': put_cstr(b, "\\b"); break;
		case '\f': put_cstr(b, "\\f"); break;
		default:
			if (c < 0x20 || c == 0x7F) put_fmt(b, "\\u%04x", c);
			else put_char(b, (char)c);
		}
	}
	put_char(b, '"');
}

static void put_char_literal(buf *b, uint32_t cp) {
	put_char(b, '\\');
	switch (cp) {
	case '\n': put_cstr(b, "newline"); return;
	case ' ': put_cstr(b, "space"); return;
	case '\t': put_cstr(b, "tab"); return;
	case '\b': put_cstr(b, "backspace"); return;
	case '\f': put_cstr(b, "formfeed"); return;
	case '\r': put_cstr(b, "return"); return;
	}
	if (cp < 0x20 || cp == 0x7F) put_fmt(b, "u%04x", cp);
	else put_utf8(b, cp);
}

static void put_symbol_text(buf *b, clj_value ns, clj_value name) {
	if (!clj_is_nil(ns)) {
		put_bytes(b, clj_string_bytes(ns), clj_string_len(ns));
		put_char(b, '/');
	}
	put_bytes(b, clj_string_bytes(name), clj_string_len(name));
}

typedef enum { F_SEQ, F_QUEUE, F_VECTOR, F_MAP, F_SET, F_ARRAY } frame_kind;

typedef struct {
	frame_kind   kind;
	clj_seq_iter it;
	clj_value    vec;
	uint32_t     idx, count;
	clj_value   *entries; // key, value, ... of a map; borrowed from the live map
	size_t       n, i;
	bool         value_next;
	bool         first;
} frame;

typedef struct {
	frame *items;
	size_t count, cap;
} frame_stack;

static frame *push_frame(frame_stack *s, frame_kind kind) {
	if (s->count == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 16;
		s->items = realloc(s->items, s->cap * sizeof *s->items);
		if (!s->items) clj_fatal("out of memory");
	}
	frame *f = &s->items[s->count++];
	memset(f, 0, sizeof *f);
	f->kind = kind;
	f->first = true;
	return f;
}

typedef struct {
	clj_value *entries;
	size_t     n;
} collect_ctx;

static bool collect_entry(clj_value key, clj_value val, void *ctx) {
	collect_ctx *c = ctx;
	c->entries[c->n++] = key;
	c->entries[c->n++] = val;
	return true;
}

static bool collect_item(clj_value item, void *ctx) {
	collect_ctx *c = ctx;
	c->entries[c->n++] = item;
	return true;
}

// An array frame owns its entries: the elements are boxed on the way out, not borrowed.
static void free_entries(frame *f) {
	if (f->kind == F_ARRAY) {
		for (size_t i = 0; i < f->n; i++) clj_release(f->entries[i]);
	}
	free(f->entries);
}

static bool collect_sorted_item(clj_value key, clj_value val, void *ctx) {
	(void)val;
	return collect_item(key, ctx);
}

// *print-length* and *print-level* as fixnums, -1 when unbound; read once per print.
typedef struct {
	int64_t length, level;
} limits;

// What *print-level* counts: everything printed through a frame except the #error map.
static bool is_collection(clj_value v) {
	if (!clj_is_ptr(v)) return false;
	return clj_is_seq(v) || clj_is_queue(v) || clj_is_vector(v) || clj_type_of(v) == &clj_map_type || clj_is_set(v) || clj_is_sorted(v) || clj_is_record(v) ||
	       clj_is_array(v);
}

// Scalars are written outright; a collection writes its opener and pushes a frame, or `#` past *print-level*.
// Not readably (Clojure's *print-readably* false): strings and chars as their text.
static void emit(buf *b, frame_stack *stack, clj_value v, bool readably, const limits *lim) {
	if (lim->level >= 0 && (int64_t)stack->count >= lim->level && is_collection(v)) {
		put_char(b, '#');
		return;
	}
	if (clj_is_nil(v)) {
		put_cstr(b, "nil");
	} else if (v == CLJ_TRUE) {
		put_cstr(b, "true");
	} else if (v == CLJ_FALSE) {
		put_cstr(b, "false");
	} else if (clj_is_fixnum(v)) {
		put_fmt(b, "%lld", (long long)clj_fixnum_val(v));
	} else if (clj_is_long(v)) {
		put_fmt(b, "%lld", (long long)clj_long_val(v));
	} else if (clj_is_char(v)) {
		if (readably) put_char_literal(b, clj_char_val(v));
		else put_utf8(b, clj_char_val(v));
	} else if (clj_is_double(v)) {
		put_double(b, clj_double_val(v));
	} else if (clj_is_bigint(v) || clj_is_ratio(v) || clj_is_decimal(v)) {
		// (str 1N) is "1" and (pr-str 1N) is "1N", as the JVM's print-method appends the tag.
		clj_value text = clj_is_bigint(v) ? clj_bigint_to_string(v) : clj_is_ratio(v) ? clj_ratio_to_string(v) : clj_decimal_to_string(v);
		put_bytes(b, clj_string_bytes(text), clj_string_len(text));
		if (readably && !clj_is_ratio(v)) put_char(b, clj_is_bigint(v) ? 'N' : 'M');
		clj_release(text);
	} else if (clj_is_string(v)) {
		if (readably) put_string_literal(b, v);
		else put_bytes(b, clj_string_bytes(v), clj_string_len(v));
	} else if (clj_is_regex(v)) {
		// The text is verbatim in both directions: the reader applies no string escapes inside #"".
		clj_value p = clj_regex_pattern(v);
		if (readably) put_cstr(b, "#\"");
		put_bytes(b, clj_string_bytes(p), clj_string_len(p));
		if (readably) put_char(b, '"');
	} else if (clj_is_uuid(v)) {
		char text[37];
		clj_uuid_format(v, text);
		put_fmt(b, "#uuid \"%s\"", text);
	} else if (clj_is_inst(v)) {
		char text[40];
		clj_inst_format(v, text, sizeof text);
		put_fmt(b, "#inst \"%s\"", text);
	} else if (clj_is_keyword(v)) {
		put_char(b, ':');
		put_symbol_text(b, clj_keyword_ns(v), clj_keyword_name(v));
	} else if (!clj_is_ptr(v)) {
		put_fmt(b, "#<special %lu>", (unsigned long)v);
	} else if (clj_is_symbol(v)) {
		put_symbol_text(b, clj_symbol_ns(v), clj_symbol_name(v));
	} else if (clj_is_var(v)) {
		put_cstr(b, "#'");
		put_symbol_text(b, CLJ_NIL, clj_symbol_name(clj_var_ns(v)));
		put_char(b, '/');
		put_symbol_text(b, CLJ_NIL, clj_symbol_name(clj_var_name(v)));
	} else if (clj_is_fn(v)) {
		put_cstr(b, "#object[fn");
		clj_value name = clj_fn_of(v)->name;
		if (!clj_is_nil(name)) {
			put_char(b, ' ');
			put_symbol_text(b, clj_symbol_ns(name), clj_symbol_name(name));
		}
		put_char(b, ']');
	} else if (clj_is_ex_info(v)) {
		// Printed as a map literal after the tag, so the map frame does the field walk.
		pthread_once(&keywords_once, intern_keywords);
		put_cstr(b, "#error {");
		frame *f = push_frame(stack, F_MAP);
		bool with_cause = !clj_is_nil(clj_exception_cause(v));
		f->entries = malloc(6 * sizeof *f->entries);
		if (!f->entries) clj_fatal("out of memory");
		f->entries[0] = kw_message;
		f->entries[1] = clj_exception_message(v);
		f->entries[2] = kw_data;
		f->entries[3] = clj_exception_data(v);
		f->entries[4] = kw_cause;
		f->entries[5] = clj_exception_cause(v);
		f->n = with_cause ? 6 : 4;
	} else if (clj_is_host_error(v)) {
		// ex-data holds the value itself; spelled out here rather than walked.
		put_cstr(b, "#error {:message ");
		put_string_literal(b, clj_host_error_message(v));
		put_cstr(b, ", :data {:host/error #object[host-error]}}");
	} else if (clj_is_seq(v)) {
		put_char(b, '(');
		push_frame(stack, F_SEQ)->it = clj_seq_iter_start(v);
	} else if (clj_is_queue(v)) {
		// The JVM prints an address; the items read back through into, which is more use.
		put_cstr(b, "#queue [");
		push_frame(stack, F_QUEUE)->it = clj_seq_iter_start(v);
	} else if (clj_is_vector(v)) {
		put_char(b, '[');
		frame *f = push_frame(stack, F_VECTOR);
		f->vec = v;
		f->count = clj_vector_count(v);
	} else if (clj_header_of(v)->type == &clj_map_type) {
		put_char(b, '{');
		frame *f = push_frame(stack, F_MAP);
		size_t n = 2 * (size_t)clj_map_count(v);
		f->entries = n ? malloc(n * sizeof *f->entries) : NULL;
		if (n && !f->entries) clj_fatal("out of memory");
		collect_ctx c = {f->entries, 0};
		clj_map_each(v, collect_entry, &c);
		f->n = n;
	} else if (clj_is_set(v)) {
		put_cstr(b, "#{");
		frame *f = push_frame(stack, F_SET);
		size_t n = clj_set_count(v);
		f->entries = n ? malloc(n * sizeof *f->entries) : NULL;
		if (n && !f->entries) clj_fatal("out of memory");
		collect_ctx c = {f->entries, 0};
		clj_set_each(v, collect_item, &c);
		f->n = n;
	} else if (clj_is_sorted(v)) {
		bool set = clj_is_sorted_set(v);
		put_cstr(b, set ? "#{" : "{");
		frame *f = push_frame(stack, set ? F_SET : F_MAP);
		size_t n = (set ? 1 : 2) * (size_t)clj_sorted_count(v);
		f->entries = n ? malloc(n * sizeof *f->entries) : NULL;
		if (n && !f->entries) clj_fatal("out of memory");
		collect_ctx c = {f->entries, 0};
		clj_sorted_each(v, set ? collect_sorted_item : collect_entry, &c);
		f->n = n;
	} else if (clj_is_record(v)) {
		put_char(b, '#');
		put_cstr(b, clj_type_of(v)->name);
		put_char(b, '{');
		frame *f = push_frame(stack, F_MAP);
		size_t n = 2 * (size_t)clj_record_count(v);
		f->entries = n ? malloc(n * sizeof *f->entries) : NULL;
		if (n && !f->entries) clj_fatal("out of memory");
		collect_ctx c = {f->entries, 0};
		clj_record_each(v, collect_entry, &c);
		f->n = n;
	} else if (clj_is_array(v)) {
		// The JVM prints an address; the elements are more use (docs/jvm-differences.md).
		put_cstr(b, "#array[:");
		put_cstr(b, clj_array_kind_name(clj_array_kind_of(v)));
		frame *f = push_frame(stack, F_ARRAY);
		size_t n = clj_array_count(v);
		f->entries = n ? malloc(n * sizeof *f->entries) : NULL;
		if (n && !f->entries) clj_fatal("out of memory");
		for (size_t i = 0; i < n; i++) f->entries[i] = clj_array_get(v, (uint32_t)i);
		f->n = n;
	} else if (clj_is_type(v)) {
		put_cstr(b, ((const clj_type *)clj_to_ptr(v))->name);
	} else if (clj_is_protocol(v)) {
		put_cstr(b, "#object[protocol ");
		put_symbol_text(b, clj_symbol_ns(clj_protocol_of(v)->name), clj_symbol_name(clj_protocol_of(v)->name));
		put_char(b, ']');
	} else {
		put_fmt(b, "#object[%s]", clj_type_name(v));
	}
}

// True once `printed` items are out and *print-length* allows no more: writes the separator and `...`, as print-sequential does.
static bool elide(buf *b, const limits *lim, size_t printed, const char *sep) {
	if (lim->length < 0 || (int64_t)printed < lim->length) return false;
	if (printed) put_cstr(b, sep);
	put_cstr(b, "...");
	return true;
}

// Yields the next child of the top frame, or closes it. false when the frame is done.
static bool next_child(buf *b, frame_stack *stack, clj_value *out, bool *thrown, const limits *lim) {
	frame *f = &stack->items[stack->count - 1];
	switch (f->kind) {
	case F_SEQ:
	case F_QUEUE:
		if (clj_seq_iter_next(&f->it, out)) {
			if (!elide(b, lim, f->idx, " ")) {
				if (f->idx) put_char(b, ' ');
				f->idx++;
				return true;
			}
			clj_seq_iter_close(&f->it);
		}
		if (f->it.thrown) *thrown = true;
		put_char(b, f->kind == F_SEQ ? ')' : ']');
		break;
	case F_VECTOR:
		if (f->idx < f->count && !elide(b, lim, f->idx, " ")) {
			if (f->idx) put_char(b, ' ');
			*out = clj_vector_nth(f->vec, f->idx++);
			return true;
		}
		put_char(b, ']');
		break;
	case F_MAP:
		if (f->value_next) {
			put_char(b, ' ');
			f->value_next = false;
			*out = f->entries[f->i++];
			return true;
		}
		if (f->i < f->n && !elide(b, lim, f->i / 2, ", ")) {
			if (f->i) put_cstr(b, ", ");
			f->value_next = true;
			*out = f->entries[f->i++];
			return true;
		}
		put_char(b, '}');
		free_entries(f);
		break;
	case F_SET:
		if (f->i < f->n && !elide(b, lim, f->i, " ")) {
			if (f->i) put_char(b, ' ');
			*out = f->entries[f->i++];
			return true;
		}
		put_char(b, '}');
		free_entries(f);
		break;
	case F_ARRAY:
		if (f->i < f->n) {
			put_char(b, ' ');
			if (elide(b, lim, f->i, "")) {
				put_char(b, ']');
				free_entries(f);
				break;
			}
			*out = f->entries[f->i++];
			return true;
		}
		put_char(b, ']');
		free_entries(f);
		break;
	}
	stack->count--;
	return false;
}

// Realizes lazy seqs on the way; a thunk that throws makes the whole print throw, as in Clojure.
// max > 0 stops once that many bytes are written and closes the open collections after "...", so an
// unbounded seq realizes only what the text shows.
static clj_value print_to_string(clj_value root, bool readably, size_t max, const limits *lim) {
	buf         b = {0};
	frame_stack stack = {0};
	clj_value   v = root;
	bool        pending = true, thrown = false, truncated = false;
	for (;;) {
		if (pending) emit(&b, &stack, v, readably, lim);
		if (!stack.count || thrown) break;
		if (max && b.len >= max) {
			truncated = true;
			break;
		}
		pending = next_child(&b, &stack, &v, &thrown, lim);
	}
	if (truncated) put_cstr(&b, " ...");
	for (size_t i = stack.count; i > 0; i--) {
		frame *f = &stack.items[i - 1];
		bool seq = f->kind == F_SEQ || f->kind == F_QUEUE;
		if (truncated) put_char(&b, f->kind == F_SEQ ? ')' : f->kind == F_VECTOR || f->kind == F_ARRAY || f->kind == F_QUEUE ? ']' : '}');
		if (!seq && f->kind != F_VECTOR) free_entries(f);
		if (seq) clj_seq_iter_close(&f->it);
	}
	free(stack.items);
	clj_value s = thrown ? CLJ_THROWN : clj_string_new(b.data, b.len);
	free(b.data);
	return s;
}

static const limits unlimited = {-1, -1};

clj_value clj_pr_str(clj_value v) { return print_to_string(v, true, 0, &unlimited); }

clj_value clj_pr_str_max(clj_value v, size_t max) { return print_to_string(v, true, max, &unlimited); }

clj_value clj_print_str(clj_value v) { return print_to_string(v, false, 0, &unlimited); }

// The var is looked up on each call until core.clj has defined it; vars are immortal, so the cache never goes stale.
static clj_value print_var(clj_value *slot, const char *name) {
	if (clj_is_nil(*slot)) {
		clj_value sym = clj_symbol_from_cstr(name);
		*slot = clj_ns_resolve(clj_ns_core(), sym);
		clj_release(sym);
	}
	return *slot;
}

// nil is -1; a negative *print-length* prints everything and a negative *print-level* nothing, as on the JVM.
static bool read_limit(clj_value *slot, const char *name, bool level, int64_t *out) {
	clj_value var = print_var(slot, name);
	*out = -1;
	if (clj_is_nil(var) || !clj_var_is_bound(var)) return true;
	clj_value v = clj_var_deref(var);
	if (clj_is_nil(v)) return true;
	int64_t n;
	if (!clj_int64_of(v, &n)) {
		clj_throw_msg("%s cannot be cast to a number", clj_type_name(v));
		clj_release(v);
		return false;
	}
	clj_release(v);
	*out = level ? (n < 0 ? 0 : n) : n;
	return true;
}

clj_value clj_pr_str_dynamic(clj_value v, bool readably) {
	static clj_value length_var, level_var;
	limits           lim;
	if (!read_limit(&length_var, "*print-length*", false, &lim.length) || !read_limit(&level_var, "*print-level*", true, &lim.level)) return CLJ_THROWN;
	return print_to_string(v, readably, 0, &lim);
}
