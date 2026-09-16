// @ai-generated(guided)
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"

// Java's Character.isWhitespace: the space separators minus the non-breaking ones, plus the ASCII controls.
static bool is_whitespace(uint32_t cp) {
	if ((cp >= 9 && cp <= 13) || (cp >= 28 && cp <= 32)) return true;
	if (cp == 0x1680 || (cp >= 0x2000 && cp <= 0x2006) || (cp >= 0x2008 && cp <= 0x200A)) return true;
	return cp == 0x2028 || cp == 0x2029 || cp == 0x205F || cp == 0x3000;
}

static clj_value not_a_string(const char *what, clj_value v) { return clj_throw_msg("%s expects a string, got: %s", what, clj_type_name(v)); }

// Byte offset of code point index i, or the length for i == count; -1 past the end.
static intptr_t cp_offset(clj_value s, intptr_t i) {
	if (i < 0) return -1;
	const char *bytes = clj_string_bytes(s);
	size_t      len = clj_string_len(s), pos = 0;
	uint32_t    cp;
	for (intptr_t k = 0; k < i; k++) {
		if (pos >= len) return -1;
		pos += clj_utf8_decode(bytes, len, pos, &cp);
	}
	return (intptr_t)pos;
}

// Code points before byte offset pos.
static intptr_t cp_index(clj_value s, size_t pos) {
	const char *bytes = clj_string_bytes(s);
	size_t      len = clj_string_len(s), at = 0;
	intptr_t    i = 0;
	uint32_t    cp;
	while (at < pos && at < len) {
		at += clj_utf8_decode(bytes, len, at, &cp);
		i++;
	}
	return i;
}

static clj_value b_subs(const clj_value *args, size_t n) {
	if (!clj_is_string(args[0])) return not_a_string("subs", args[0]);
	intptr_t start, end = (intptr_t)clj_string_count(args[0]);
	if (!clj_index_arg(args[1], &start) || (n == 3 && !clj_index_arg(args[2], &end))) return clj_throw_msg("subs expects integer indices");
	intptr_t from = cp_offset(args[0], start), to = end < start ? -1 : cp_offset(args[0], end);
	if (from < 0 || to < 0) return clj_throw_msg("String index out of range: %ld", (long)(from < 0 ? start : end));
	return clj_string_new(clj_string_bytes(args[0]) + from, (size_t)(to - from));
}

static clj_value map_ascii(clj_value s, bool upper) {
	uint32_t len = clj_string_len(s);
	char    *text = malloc(len + 1);
	if (!text) clj_fatal("out of memory");
	memcpy(text, clj_string_bytes(s), len + 1);
	for (uint32_t i = 0; i < len; i++) {
		char c = text[i];
		if (upper && c >= 'a' && c <= 'z') text[i] = (char)(c - 32);
		if (!upper && c >= 'A' && c <= 'Z') text[i] = (char)(c + 32);
	}
	clj_value r = clj_string_new(text, len);
	free(text);
	return r;
}

// ASCII only: the runtime carries no Unicode case tables (NOTES.md).
static clj_value b_upper(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return not_a_string("upper-case", args[0]);
	return map_ascii(args[0], true);
}

static clj_value b_lower(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return not_a_string("lower-case", args[0]);
	return map_ascii(args[0], false);
}

// The needle as UTF-8 bytes: a string as is, a char encoded; owned string.
static clj_value needle_of(clj_value v) {
	if (clj_is_string(v)) return clj_retain(v);
	if (clj_is_char(v)) return clj_print_str(v);
	return clj_throw_msg("expected a string or char, got: %s", clj_type_name(v));
}

// (index-of* s value from) → code point index or nil.
static clj_value b_index_of(const clj_value *args, size_t n) {
	if (!clj_is_string(args[0])) return not_a_string("index-of", args[0]);
	clj_value needle = needle_of(args[1]);
	if (needle == CLJ_THROWN) return CLJ_THROWN;
	intptr_t from = 0;
	if (n == 3 && !clj_is_nil(args[2])) {
		if (!clj_index_arg(args[2], &from)) {
			clj_release(needle);
			return clj_throw_msg("index-of expects an integer index");
		}
		if (from < 0) from = 0;
	}
	const char *hay = clj_string_bytes(args[0]), *nd = clj_string_bytes(needle);
	size_t      hlen = clj_string_len(args[0]), nlen = clj_string_len(needle);
	intptr_t    start = cp_offset(args[0], from);
	clj_value   r = CLJ_NIL;
	if (start >= 0) {
		for (size_t pos = (size_t)start; pos + nlen <= hlen; pos++) {
			if (memcmp(hay + pos, nd, nlen) == 0) {
				r = clj_fixnum(cp_index(args[0], pos));
				break;
			}
		}
	}
	clj_release(needle);
	return r;
}

static clj_value b_last_index_of(const clj_value *args, size_t n) {
	if (!clj_is_string(args[0])) return not_a_string("last-index-of", args[0]);
	clj_value needle = needle_of(args[1]);
	if (needle == CLJ_THROWN) return CLJ_THROWN;
	const char *hay = clj_string_bytes(args[0]), *nd = clj_string_bytes(needle);
	size_t      hlen = clj_string_len(args[0]), nlen = clj_string_len(needle);
	intptr_t    last = (intptr_t)hlen;
	if (n == 3 && !clj_is_nil(args[2])) {
		intptr_t from;
		if (!clj_index_arg(args[2], &from)) {
			clj_release(needle);
			return clj_throw_msg("last-index-of expects an integer index");
		}
		intptr_t off = from < 0 ? -1 : cp_offset(args[0], from);
		last = off < 0 ? (from < 0 ? -1 : (intptr_t)hlen) : off;
	}
	clj_value r = CLJ_NIL;
	if (last >= 0 && nlen <= hlen) {
		size_t pos = (size_t)last < hlen - nlen ? (size_t)last : hlen - nlen;
		for (;;) {
			if (memcmp(hay + pos, nd, nlen) == 0) {
				r = clj_fixnum(cp_index(args[0], pos));
				break;
			}
			if (pos == 0) break;
			pos--;
		}
	}
	clj_release(needle);
	return r;
}

static clj_value trim(clj_value s, bool left, bool right) {
	const char *bytes = clj_string_bytes(s);
	size_t      len = clj_string_len(s), start = 0, end = len;
	uint32_t    cp;
	if (left) {
		while (start < len) {
			size_t w = clj_utf8_decode(bytes, len, start, &cp);
			if (!is_whitespace(cp)) break;
			start += w;
		}
	}
	if (right) {
		while (end > start) {
			size_t back = end - 1;
			while (back > start && (((unsigned char)bytes[back]) & 0xC0) == 0x80) back--;
			clj_utf8_decode(bytes, len, back, &cp);
			if (!is_whitespace(cp)) break;
			end = back;
		}
	}
	return clj_string_new(bytes + start, end - start);
}

static clj_value b_trim(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return not_a_string("trim", args[0]);
	return trim(args[0], true, true);
}

static clj_value b_triml(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return not_a_string("triml", args[0]);
	return trim(args[0], true, false);
}

static clj_value b_trimr(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return not_a_string("trimr", args[0]);
	return trim(args[0], false, true);
}

static clj_value b_blank_p(const clj_value *args, size_t n) {
	(void)n;
	if (clj_is_nil(args[0])) return CLJ_TRUE;
	if (!clj_is_string(args[0])) return not_a_string("blank?", args[0]);
	const char *bytes = clj_string_bytes(args[0]);
	size_t      len = clj_string_len(args[0]), pos = 0;
	uint32_t    cp;
	while (pos < len) {
		pos += clj_utf8_decode(bytes, len, pos, &cp);
		if (!is_whitespace(cp)) return CLJ_FALSE;
	}
	return CLJ_TRUE;
}

static clj_value b_whitespace_p(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_char(args[0])) return clj_throw_msg("whitespace? expects a char, got: %s", clj_type_name(args[0]));
	return clj_bool(is_whitespace(clj_char_val(args[0])));
}

static clj_value b_reverse_str(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return not_a_string("reverse", args[0]);
	const char *bytes = clj_string_bytes(args[0]);
	size_t      len = clj_string_len(args[0]), pos = 0, out = len;
	char       *text = malloc(len + 1);
	if (!text) clj_fatal("out of memory");
	uint32_t cp;
	while (pos < len) {
		size_t w = clj_utf8_decode(bytes, len, pos, &cp);
		out -= w;
		memcpy(text + out, bytes + pos, w);
		pos += w;
	}
	clj_value r = clj_string_new(text, len);
	free(text);
	return r;
}

typedef struct {
	char  *data;
	size_t len, cap;
} buf;

static void put(buf *b, const char *s, size_t n) {
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

// Literal replacement, as String.replace(CharSequence, CharSequence): an empty match sits between every char and at both ends.
static clj_value replace(clj_value s, clj_value match, clj_value repl, bool first_only) {
	const char *hay = clj_string_bytes(s), *m = clj_string_bytes(match), *r = clj_string_bytes(repl);
	size_t      hlen = clj_string_len(s), mlen = clj_string_len(match), rlen = clj_string_len(repl);
	buf         b = {0};
	if (mlen == 0) {
		put(&b, r, rlen);
		size_t   pos = 0;
		uint32_t cp;
		while (pos < hlen && !(first_only && pos > 0)) {
			size_t w = clj_utf8_decode(hay, hlen, pos, &cp);
			put(&b, hay + pos, w);
			pos += w;
			if (!first_only) put(&b, r, rlen);
		}
		if (first_only && pos < hlen) put(&b, hay + pos, hlen - pos);
	} else {
		size_t pos = 0;
		bool   done = false;
		while (pos < hlen) {
			if (!done && pos + mlen <= hlen && memcmp(hay + pos, m, mlen) == 0) {
				put(&b, r, rlen);
				pos += mlen;
				if (first_only) done = true;
			} else {
				put(&b, hay + pos, 1);
				pos++;
			}
		}
	}
	clj_value out = clj_string_new(b.data, b.len);
	free(b.data);
	return out;
}

static clj_value b_replace(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return not_a_string("replace", args[0]);
	clj_value match = needle_of(args[1]);
	if (match == CLJ_THROWN) return CLJ_THROWN;
	clj_value repl = needle_of(args[2]);
	if (repl == CLJ_THROWN) {
		clj_release(match);
		return CLJ_THROWN;
	}
	clj_value r = replace(args[0], match, repl, false);
	clj_release(match);
	clj_release(repl);
	return r;
}

static clj_value b_replace_first(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return not_a_string("replace-first", args[0]);
	clj_value match = needle_of(args[1]);
	if (match == CLJ_THROWN) return CLJ_THROWN;
	clj_value repl = needle_of(args[2]);
	if (repl == CLJ_THROWN) {
		clj_release(match);
		return CLJ_THROWN;
	}
	clj_value r = replace(args[0], match, repl, true);
	clj_release(match);
	clj_release(repl);
	return r;
}

typedef struct {
	clj_value *items;
	size_t     n, cap;
} parts;

static void add_part(parts *p, const char *s, size_t len) {
	if (p->n == p->cap) {
		p->cap = p->cap ? p->cap * 2 : 8;
		p->items = realloc(p->items, p->cap * sizeof *p->items);
		if (!p->items) clj_fatal("out of memory");
	}
	p->items[p->n++] = clj_string_new(s, len);
}

// Java's String.split with a literal separator: limit 0 drops trailing empty strings, a positive limit caps the parts.
static clj_value finish_split(parts *p, intptr_t limit) {
	size_t n = p->n;
	if (limit == 0) {
		while (n > 0 && clj_string_len(p->items[n - 1]) == 0) n--;
	}
	clj_value v = clj_vector_from_array(p->items, (uint32_t)n);
	for (size_t i = 0; i < p->n; i++) clj_release(p->items[i]);
	free(p->items);
	return v;
}

static clj_value b_split(const clj_value *args, size_t n) {
	if (!clj_is_string(args[0])) return not_a_string("split", args[0]);
	if (!clj_is_string(args[1])) return clj_throw_msg("split needs a string separator; regex patterns are not supported (NOTES.md)");
	intptr_t limit = 0;
	if (n == 3) {
		if (!clj_is_fixnum(args[2])) return clj_throw_msg("split expects an integer limit");
		limit = clj_fixnum_val(args[2]);
	}
	const char *hay = clj_string_bytes(args[0]), *sep = clj_string_bytes(args[1]);
	size_t      hlen = clj_string_len(args[0]), slen = clj_string_len(args[1]);
	parts       p = {0};
	if (hlen == 0) {
		add_part(&p, "", 0);
		return finish_split(&p, -1);
	}
	size_t   start = 0, pos = 0;
	uint32_t cp;
	while (pos < hlen) {
		bool at_limit = limit > 0 && (intptr_t)p.n == limit - 1;
		if (at_limit) break;
		if (slen == 0) {
			size_t w = clj_utf8_decode(hay, hlen, pos, &cp);
			if (pos + w >= hlen) break;
			add_part(&p, hay + pos, w);
			pos += w;
			start = pos;
		} else if (pos + slen <= hlen && memcmp(hay + pos, sep, slen) == 0) {
			add_part(&p, hay + start, pos - start);
			pos += slen;
			start = pos;
		} else {
			pos++;
		}
	}
	add_part(&p, hay + start, hlen - start);
	return finish_split(&p, limit);
}

static clj_value b_split_lines(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return not_a_string("split-lines", args[0]);
	const char *hay = clj_string_bytes(args[0]);
	size_t      hlen = clj_string_len(args[0]), start = 0;
	parts       p = {0};
	if (hlen == 0) {
		add_part(&p, "", 0);
		return finish_split(&p, -1);
	}
	for (size_t pos = 0; pos < hlen; pos++) {
		if (hay[pos] == '\n' || (hay[pos] == '\r' && pos + 1 < hlen && hay[pos + 1] == '\n')) {
			add_part(&p, hay + start, pos - start);
			if (hay[pos] == '\r') pos++;
			start = pos + 1;
		}
	}
	add_part(&p, hay + start, hlen - start);
	return finish_split(&p, 0);
}

static clj_value b_char(const clj_value *args, size_t n) {
	(void)n;
	if (clj_is_char(args[0])) return args[0];
	if (clj_is_fixnum(args[0])) {
		intptr_t v = clj_fixnum_val(args[0]);
		if (v < 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return clj_throw_msg("Value out of range for char: %ld", (long)v);
		return clj_char((uint32_t)v);
	}
	return clj_throw_msg("%s cannot be cast to a char", clj_type_name(args[0]));
}

#define ANY CLJ_ARITY_ANY

static const struct {
	const char   *name;
	clj_native_fn fn;
	uint32_t      min, max;
} entries[] = {
	{"subs", b_subs, 2, 3},                  {"str-upper*", b_upper, 1, 1},             {"str-lower*", b_lower, 1, 1},
	{"str-index-of*", b_index_of, 2, 3},     {"str-last-index-of*", b_last_index_of, 2, 3}, {"str-trim*", b_trim, 1, 1},
	{"str-triml*", b_triml, 1, 1},           {"str-trimr*", b_trimr, 1, 1},             {"str-blank?*", b_blank_p, 1, 1},
	{"str-whitespace?*", b_whitespace_p, 1, 1}, {"str-reverse*", b_reverse_str, 1, 1},  {"str-replace*", b_replace, 3, 3},
	{"str-replace-first*", b_replace_first, 3, 3}, {"str-split*", b_split, 2, 3},       {"str-split-lines*", b_split_lines, 1, 1},
	{"char", b_char, 1, 1},
};

void clj_string_builtins_install(void) {
	for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) clj_builtin_bind(entries[i].name, entries[i].fn, entries[i].min, entries[i].max);
}
