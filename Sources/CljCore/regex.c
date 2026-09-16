// @ai-generated(solo)
// A backtracking matcher over code points for a java.util.regex subset (NOTES.md, "Regex").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/core.h"
#include "clj/regex.h"

// ---- input text

// cp[i] is code point i, off[i] its byte offset, off[n] the byte length.
typedef struct {
	uint32_t *cp;
	uint32_t *off;
	uint32_t  n;
} re_text;

static void text_free(re_text *t) {
	free(t->cp);
	free(t->off);
	t->cp = t->off = NULL;
	t->n = 0;
}

static void text_of(clj_value s, re_text *t) {
	const char *bytes = clj_string_bytes(s);
	size_t      len = clj_string_len(s), pos = 0, n = 0;
	t->cp = malloc((len + 1) * sizeof *t->cp);
	t->off = malloc((len + 2) * sizeof *t->off);
	if (!t->cp || !t->off) clj_fatal("out of memory");
	while (pos < len) {
		uint32_t cp;
		t->off[n] = (uint32_t)pos;
		pos += clj_utf8_decode(bytes, len, pos, &cp);
		t->cp[n++] = cp;
	}
	t->off[n] = (uint32_t)len;
	t->n = (uint32_t)n;
}

static clj_value text_slice(const re_text *t, clj_value s, uint32_t from, uint32_t to) {
	return clj_string_new(clj_string_bytes(s) + t->off[from], t->off[to] - t->off[from]);
}

// ---- character classes

typedef struct re_class {
	bool             negate;
	uint32_t         nranges, rcap;
	uint32_t        *ranges; // lo, hi pairs
	struct re_class *nested; // union members, linked through next
	struct re_class *next;
	struct re_class *isect; // the right side of &&
} re_class;

static re_class *class_new(void) {
	re_class *c = calloc(1, sizeof *c);
	if (!c) clj_fatal("out of memory");
	return c;
}

static void class_free(re_class *c) {
	while (c) {
		re_class *next = c->next;
		class_free(c->nested);
		class_free(c->isect);
		free(c->ranges);
		free(c);
		c = next;
	}
}

static void class_range(re_class *c, uint32_t lo, uint32_t hi) {
	if (c->nranges == c->rcap) {
		c->rcap = c->rcap ? c->rcap * 2 : 4;
		c->ranges = realloc(c->ranges, 2 * c->rcap * sizeof *c->ranges);
		if (!c->ranges) clj_fatal("out of memory");
	}
	c->ranges[2 * c->nranges] = lo;
	c->ranges[2 * c->nranges + 1] = hi;
	c->nranges++;
}

static uint32_t swap_case(uint32_t cp) {
	if (cp >= 'a' && cp <= 'z') return cp - 32;
	if (cp >= 'A' && cp <= 'Z') return cp + 32;
	return cp;
}

// Folding belongs inside the range test: (?i)[^a] must still refuse A.
static bool range_hit(uint32_t lo, uint32_t hi, uint32_t cp, bool ci) {
	if (cp >= lo && cp <= hi) return true;
	if (!ci) return false;
	uint32_t other = swap_case(cp);
	return other != cp && other >= lo && other <= hi;
}

static bool class_member(const re_class *c, uint32_t cp, bool ci) {
	bool in = false;
	for (uint32_t i = 0; i < c->nranges && !in; i++) in = range_hit(c->ranges[2 * i], c->ranges[2 * i + 1], cp, ci);
	for (const re_class *n = c->nested; n && !in; n = n->next) in = class_member(n, cp, ci);
	if (in && c->isect) in = class_member(c->isect, cp, ci);
	return c->negate ? !in : in;
}

static bool is_word(uint32_t cp) {
	return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_';
}

// ---- instructions

enum {
	RE_CHAR,
	RE_CLASS,
	RE_ANY,
	RE_SPLIT,
	RE_JMP,
	RE_SAVE,
	RE_MARK,
	RE_PROGRESS,
	RE_MATCH,
	RE_SUBEND,
	RE_BOL,
	RE_EOL,
	RE_BOI,
	RE_EOI,
	RE_EOI_NL,
	RE_WORDB,
	RE_NWORDB,
	RE_BACKREF,
	RE_LOOKAHEAD,
	RE_LOOKBEHIND,
	RE_ATOMIC,
};

enum {
	RE_F_I = 1,   // case-insensitive
	RE_F_S = 2,   // . takes a newline
	RE_F_M = 4,   // ^ and $ match at a line terminator
	RE_F_X = 8,   // whitespace and # comments ignored
	RE_F_NEG = 16 // negative lookaround
};

typedef struct {
	uint8_t  op, flags;
	uint32_t a;    // code point, class index, slot, group or width
	int32_t  x, y; // jump targets, relative to the instruction's own index
} re_inst;

typedef struct {
	re_inst   *inst;
	uint32_t   n, cap;
	re_class **classes;
	uint32_t   nclasses, ccap;
	uint32_t   nslots, nmarks;
} re_prog;

static void prog_free(re_prog *p) {
	if (!p) return;
	for (uint32_t i = 0; i < p->nclasses; i++) class_free(p->classes[i]);
	free(p->classes);
	free(p->inst);
	free(p);
}

static void prog_reserve(re_prog *p, uint32_t extra) {
	if (p->n + extra <= p->cap) return;
	uint32_t cap = p->cap ? p->cap : 16;
	while (cap < p->n + extra) cap *= 2;
	p->inst = realloc(p->inst, cap * sizeof *p->inst);
	if (!p->inst) clj_fatal("out of memory");
	p->cap = cap;
}

static re_inst *emit(re_prog *p, uint8_t op, uint8_t flags) {
	prog_reserve(p, 1);
	re_inst *i = &p->inst[p->n++];
	memset(i, 0, sizeof *i);
	i->op = op;
	i->flags = flags;
	return i;
}

static void prog_insert(re_prog *p, uint32_t at, uint32_t count) {
	prog_reserve(p, count);
	memmove(p->inst + at + count, p->inst + at, (p->n - at) * sizeof *p->inst);
	memset(p->inst + at, 0, count * sizeof *p->inst);
	p->n += count;
}

static void prog_append_block(re_prog *p, const re_inst *block, uint32_t len) {
	prog_reserve(p, len);
	memcpy(p->inst + p->n, block, len * sizeof *p->inst);
	p->n += len;
}

static uint32_t prog_class(re_prog *p, re_class *c) {
	if (p->nclasses == p->ccap) {
		p->ccap = p->ccap ? p->ccap * 2 : 8;
		p->classes = realloc(p->classes, p->ccap * sizeof *p->classes);
		if (!p->classes) clj_fatal("out of memory");
	}
	p->classes[p->nclasses] = c;
	return p->nclasses++;
}

// ---- compiler

#define RE_INF     UINT32_MAX
#define RE_MAX_REP 5000
#define RE_MAX_PROG 200000

// Code point width of a sub-expression; a lookbehind body needs min == max.
typedef struct {
	uint32_t min, max;
} re_width;

typedef struct {
	const char *src;
	size_t      len, pos;
	re_prog    *p;
	uint32_t    ngroups, flags;
	clj_value   names;
	const char *err;
	size_t      errpos;
} comp;

static bool fail(comp *c, const char *msg, size_t at) {
	if (!c->err) {
		c->err = msg;
		c->errpos = at;
	}
	return false;
}

static bool at_end(const comp *c) { return c->pos >= c->len; }
static char peek(const comp *c) { return c->pos < c->len ? c->src[c->pos] : 0; }
static char peek2(const comp *c) { return c->pos + 1 < c->len ? c->src[c->pos + 1] : 0; }

static uint32_t next_cp(comp *c) {
	uint32_t cp;
	c->pos += clj_utf8_decode(c->src, c->len, c->pos, &cp);
	return cp;
}

static bool eat(comp *c, char ch) {
	if (peek(c) != ch) return false;
	c->pos++;
	return true;
}

// In COMMENTS mode whitespace and a # comment separate tokens.
static void skip_x(comp *c) {
	if (!(c->flags & RE_F_X)) return;
	for (;;) {
		char ch = peek(c);
		if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == 0x0B) c->pos++;
		else if (ch == '#') {
			while (!at_end(c) && peek(c) != '\n') c->pos++;
		} else return;
	}
}

static bool hex_digits(comp *c, int n, uint32_t *out) {
	uint32_t v = 0;
	for (int i = 0; i < n; i++) {
		char     ch = peek(c);
		uint32_t d;
		if (ch >= '0' && ch <= '9') d = (uint32_t)(ch - '0');
		else if (ch >= 'a' && ch <= 'f') d = (uint32_t)(ch - 'a' + 10);
		else if (ch >= 'A' && ch <= 'F') d = (uint32_t)(ch - 'A' + 10);
		else return false;
		v = v * 16 + d;
		c->pos++;
	}
	*out = v;
	return true;
}

// Every property here is ASCII, as the runtime's case tables are (NOTES.md).
static re_class *property_class(const char *name, size_t n) {
	static const struct {
		const char *name;
		uint32_t    lo[3], hi[3];
	} table[] = {
		{"L", {'A', 'a'}, {'Z', 'z'}},        {"IsL", {'A', 'a'}, {'Z', 'z'}},
		{"Alpha", {'A', 'a'}, {'Z', 'z'}},    {"IsAlphabetic", {'A', 'a'}, {'Z', 'z'}},
		{"Lu", {'A'}, {'Z'}},                 {"Upper", {'A'}, {'Z'}},
		{"IsUppercase", {'A'}, {'Z'}},        {"Ll", {'a'}, {'z'}},
		{"Lower", {'a'}, {'z'}},              {"IsLowercase", {'a'}, {'z'}},
		{"N", {'0'}, {'9'}},                  {"Nd", {'0'}, {'9'}},
		{"Digit", {'0'}, {'9'}},              {"IsDigit", {'0'}, {'9'}},
		{"Alnum", {'0', 'A', 'a'}, {'9', 'Z', 'z'}},
		{"XDigit", {'0', 'A', 'a'}, {'9', 'F', 'f'}},
		{"Blank", {'\t', ' '}, {'\t', ' '}},
		{"Space", {'\t', ' '}, {'\r', ' '}},  {"IsWhite_Space", {'\t', ' '}, {'\r', ' '}},
		{"Punct", {'!', ':', '{'}, {'/', '@', '~'}},
		{"Graph", {'!'}, {'~'}},              {"Print", {' '}, {'~'}},
		{"ASCII", {0}, {0x7F}},               {"Cntrl", {0, 0x7F}, {0x1F, 0x7F}},
	};
	for (size_t i = 0; i < sizeof table / sizeof *table; i++) {
		if (strlen(table[i].name) != n || memcmp(table[i].name, name, n) != 0) continue;
		re_class *c = class_new();
		class_range(c, table[i].lo[0], table[i].hi[0]);
		// A zero hi is an unused slot: no property here ends at U+0000.
		for (size_t k = 1; k < 3 && table[i].hi[k]; k++) class_range(c, table[i].lo[k], table[i].hi[k]);
		// Punct's fourth range sits between @ and {, which the three slots cannot hold.
		if (n == 5 && memcmp(name, "Punct", 5) == 0) class_range(c, '[', '`');
		return c;
	}
	return NULL;
}

// \d \w \s as ranges; the uppercase spelling is the same class negated.
static re_class *shorthand_class(char kind, bool negated) {
	re_class *c = class_new();
	c->negate = negated;
	switch (kind) {
	case 'd': class_range(c, '0', '9'); break;
	case 'w':
		class_range(c, '0', '9');
		class_range(c, 'A', 'Z');
		class_range(c, 'a', 'z');
		class_range(c, '_', '_');
		break;
	default:
		class_range(c, '\t', '\r');
		class_range(c, ' ', ' ');
		break;
	}
	return c;
}

// The escape past the backslash. is_char false means an alphanumeric the caller must interpret.
static bool escape_char(comp *c, uint32_t *out, bool *is_char) {
	size_t start = c->pos;
	char   e = peek(c);
	c->pos++;
	*is_char = true;
	switch (e) {
	case 't': *out = '\t'; return true;
	case 'n': *out = '\n'; return true;
	case 'r': *out = '\r'; return true;
	case 'f': *out = '\f'; return true;
	case 'a': *out = 0x07; return true;
	case 'e': *out = 0x1B; return true;
	case '0': {
		uint32_t v = 0;
		int      digits = 0;
		while (digits < 3 && peek(c) >= '0' && peek(c) <= '7') {
			v = v * 8 + (uint32_t)(peek(c) - '0');
			c->pos++;
			digits++;
		}
		if (!digits) return fail(c, "Illegal octal escape sequence", start);
		*out = v;
		return true;
	}
	case 'x': {
		uint32_t v = 0;
		if (eat(c, '{')) {
			int digits = 0;
			for (uint32_t d; hex_digits(c, 1, &d); digits++) v = v * 16 + d;
			if (!digits || !eat(c, '}')) return fail(c, "Illegal hexadecimal escape sequence", start);
		} else if (!hex_digits(c, 2, &v)) return fail(c, "Illegal hexadecimal escape sequence", start);
		*out = v;
		return true;
	}
	case 'u': {
		uint32_t v;
		if (!hex_digits(c, 4, &v)) return fail(c, "Illegal Unicode escape sequence", start);
		// A surrogate pair written as two escapes is one scalar here, as in a Java string.
		if (v >= 0xD800 && v <= 0xDBFF && peek(c) == '\\' && peek2(c) == 'u') {
			size_t   save = c->pos;
			uint32_t low;
			c->pos += 2;
			if (hex_digits(c, 4, &low) && low >= 0xDC00 && low <= 0xDFFF) v = 0x10000 + ((v - 0xD800) << 10) + (low - 0xDC00);
			else c->pos = save;
		}
		*out = v;
		return true;
	}
	default: break;
	}
	c->pos = start;
	if ((e >= 'a' && e <= 'z') || (e >= 'A' && e <= 'Z') || (e >= '0' && e <= '9')) {
		*is_char = false;
		return true;
	}
	// Java reads a backslash before punctuation as that character, which the reader's \" relies on.
	*out = next_cp(c);
	return true;
}

static re_class *parse_class_body(comp *c, bool allow_caret);

// An escape inside a class: a code point (*cp) or a whole class (*cls).
static bool class_escape(comp *c, uint32_t *cp, re_class **cls) {
	*cls = NULL;
	char e = peek(c);
	if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S') {
		c->pos++;
		*cls = shorthand_class((char)(e | 0x20), e < 'a');
		return true;
	}
	if (e == 'p' || e == 'P') {
		size_t at = c->pos;
		c->pos++;
		size_t start, n;
		if (eat(c, '{')) {
			start = c->pos;
			while (!at_end(c) && peek(c) != '}') c->pos++;
			n = c->pos - start;
			if (!eat(c, '}')) return fail(c, "Unclosed character property", at);
		} else {
			if (at_end(c)) return fail(c, "Unclosed character property", at);
			start = c->pos;
			c->pos++;
			n = 1;
		}
		*cls = property_class(c->src + start, n);
		if (!*cls) return fail(c, "Unknown character property name", start);
		(*cls)->negate = e == 'P';
		return true;
	}
	bool   is_char;
	size_t at = c->pos;
	if (!escape_char(c, cp, &is_char)) return false;
	if (!is_char) return fail(c, "Illegal/unsupported escape sequence", at);
	return true;
}

// One item: a code point range added to cls, or a sub-class linked into its union.
static bool class_item(comp *c, re_class *cls) {
	if (peek(c) == '[') {
		c->pos++;
		re_class *nested = parse_class_body(c, true);
		if (!nested) return false;
		nested->next = cls->nested;
		cls->nested = nested;
		return true;
	}
	uint32_t  lo;
	re_class *sub = NULL;
	if (peek(c) == '\\') {
		c->pos++;
		if (!class_escape(c, &lo, &sub)) return false;
	} else lo = next_cp(c);
	if (sub) {
		sub->next = cls->nested;
		cls->nested = sub;
		return true;
	}
	uint32_t hi = lo;
	if (peek(c) == '-' && c->pos + 1 < c->len && peek2(c) != ']') {
		size_t at = c->pos;
		c->pos++;
		if (peek(c) == '\\') {
			c->pos++;
			re_class *unexpected = NULL;
			if (!class_escape(c, &hi, &unexpected)) return false;
			if (unexpected) {
				class_free(unexpected);
				return fail(c, "Bad character range", at);
			}
		} else hi = next_cp(c);
		if (hi < lo) return fail(c, "Illegal character range", at);
	}
	class_range(cls, lo, hi);
	return true;
}

// Items up to the closing ']', which is consumed. && makes the rest the intersection operand.
static re_class *parse_class_body(comp *c, bool allow_caret) {
	re_class *cls = class_new();
	if (allow_caret) {
		skip_x(c);
		if (eat(c, '^')) cls->negate = true;
	}
	for (bool first = true;; first = false) {
		skip_x(c);
		if (at_end(c)) {
			fail(c, "Unclosed character class", c->pos);
			break;
		}
		if (peek(c) == ']' && !first) {
			c->pos++;
			return cls;
		}
		if (peek(c) == '&' && peek2(c) == '&') {
			c->pos += 2;
			cls->isect = parse_class_body(c, false);
			if (!cls->isect) break;
			return cls;
		}
		if (!class_item(c, cls)) break;
	}
	class_free(cls);
	return NULL;
}

static bool parse_alt(comp *c, re_width *w);

static uint32_t width_add(uint32_t a, uint32_t b) {
	if (a == RE_INF || b == RE_INF) return RE_INF;
	uint64_t s = (uint64_t)a + b;
	return s >= RE_INF ? RE_INF : (uint32_t)s;
}

static uint32_t width_mul(uint32_t a, uint32_t b) {
	if (a == 0 || b == 0) return 0;
	if (a == RE_INF || b == RE_INF) return RE_INF;
	uint64_t s = (uint64_t)a * b;
	return s >= RE_INF ? RE_INF : (uint32_t)s;
}

// (?idmsux-idmsux) and its scoped form; *terminator is ':' or ')'.
static bool parse_flags(comp *c, uint32_t *on, uint32_t *off, char *terminator) {
	bool negating = false;
	*on = 0;
	*off = 0;
	for (;;) {
		char     ch = peek(c);
		uint32_t bit;
		switch (ch) {
		case 'i': bit = RE_F_I; break;
		case 's': bit = RE_F_S; break;
		case 'm': bit = RE_F_M; break;
		case 'x': bit = RE_F_X; break;
		case '-':
			c->pos++;
			negating = true;
			continue;
		case ':':
		case ')':
			c->pos++;
			*terminator = ch;
			return true;
		default: return fail(c, "Unknown inline modifier", c->pos);
		}
		c->pos++;
		if (negating) *off |= bit;
		else *on |= bit;
	}
}

static void save_group(re_prog *p, uint32_t slot) { emit(p, RE_SAVE, 0)->a = slot; }

typedef struct {
	uint32_t at; // where the atom's instructions start
	re_width w;
	bool     quantifiable;
} atom;

static bool parse_group(comp *c, atom *a) {
	re_width *w = &a->w;
	re_prog  *p = c->p;
	uint32_t  saved = c->flags;
	if (!eat(c, '?')) {
		uint32_t g = ++c->ngroups;
		save_group(p, 2 * g);
		if (!parse_alt(c, w)) return false;
		c->flags = saved;
		if (!eat(c, ')')) return fail(c, "Unclosed group", c->pos);
		save_group(p, 2 * g + 1);
		return true;
	}
	char kind = peek(c);
	if (kind == ':') {
		c->pos++;
		if (!parse_alt(c, w)) return false;
		c->flags = saved;
		return eat(c, ')') ? true : fail(c, "Unclosed group", c->pos);
	}
	if (kind == '>' || kind == '=' || kind == '!' || (kind == '<' && (peek2(c) == '=' || peek2(c) == '!'))) {
		bool behind = kind == '<';
		bool neg = behind ? peek2(c) == '!' : kind == '!';
		c->pos += behind ? 2 : 1;
		uint8_t  op = kind == '>' ? RE_ATOMIC : behind ? RE_LOOKBEHIND : RE_LOOKAHEAD;
		uint32_t at = p->n;
		emit(p, op, (uint8_t)(op != RE_ATOMIC && neg ? RE_F_NEG : 0));
		re_width body;
		if (!parse_alt(c, &body)) return false;
		c->flags = saved;
		if (!eat(c, ')')) return fail(c, "Unclosed group", c->pos);
		if (behind && (body.min != body.max || body.min == RE_INF)) {
			return fail(c, "Look-behind group does not have an obvious maximum length", c->pos);
		}
		emit(p, RE_SUBEND, 0);
		p->inst[at].a = behind ? body.min : 0;
		p->inst[at].x = (int32_t)(p->n - at);
		if (op == RE_ATOMIC) *w = body;
		else {
			w->min = 0;
			w->max = 0;
		}
		return true;
	}
	if (kind == '<') {
		c->pos++;
		size_t start = c->pos;
		while (!at_end(c) && peek(c) != '>') c->pos++;
		if (c->pos == start || at_end(c)) return fail(c, "Named capturing group has no name", start);
		clj_value name = clj_string_new(c->src + start, c->pos - start);
		c->pos++;
		uint32_t g = ++c->ngroups;
		if (clj_is_nil(c->names)) c->names = clj_map_empty();
		c->names = clj_map_assoc(c->names, name, clj_fixnum((intptr_t)g));
		clj_release(name);
		save_group(p, 2 * g);
		if (!parse_alt(c, w)) return false;
		c->flags = saved;
		if (!eat(c, ')')) return fail(c, "Unclosed group", c->pos);
		save_group(p, 2 * g + 1);
		return true;
	}
	uint32_t on, off;
	char     term = 0;
	if (!parse_flags(c, &on, &off, &term)) return false;
	c->flags = (saved | on) & ~off;
	// A flag group emits nothing, so a quantifier after it has no atom to repeat, as Java reports.
	if (term == ')') {
		w->min = 0;
		w->max = 0;
		a->quantifiable = false;
		return true;
	}
	if (!parse_alt(c, w)) return false;
	c->flags = saved;
	return eat(c, ')') ? true : fail(c, "Unclosed group", c->pos);
}

static bool zero_width(comp *c, atom *a, uint8_t op, uint8_t flags) {
	emit(c->p, op, flags);
	a->w.min = 0;
	a->w.max = 0;
	a->quantifiable = false;
	return true;
}

static bool emit_class(comp *c, re_class *cls, uint8_t flags) {
	emit(c->p, RE_CLASS, flags)->a = prog_class(c->p, cls);
	return true;
}

static bool parse_escaped_atom(comp *c, atom *a, uint8_t fl, size_t start) {
	re_prog *p = c->p;
	char     e = peek(c);
	switch (e) {
	case 'd':
	case 'D':
	case 'w':
	case 'W':
	case 's':
	case 'S':
		c->pos++;
		return emit_class(c, shorthand_class((char)(e | 0x20), e < 'a'), fl);
	case 'p':
	case 'P': {
		uint32_t  cp;
		re_class *cls;
		if (!class_escape(c, &cp, &cls)) return false;
		return emit_class(c, cls, fl);
	}
	case 'b':
	case 'B':
		c->pos++;
		return zero_width(c, a, e == 'b' ? RE_WORDB : RE_NWORDB, fl);
	case 'A':
		c->pos++;
		return zero_width(c, a, RE_BOI, fl);
	case 'z':
		c->pos++;
		return zero_width(c, a, RE_EOI, fl);
	case 'Z':
		c->pos++;
		return zero_width(c, a, RE_EOI_NL, fl);
	case 'Q': {
		c->pos++;
		uint32_t n = 0;
		while (!at_end(c)) {
			if (peek(c) == '\\' && peek2(c) == 'E') {
				c->pos += 2;
				break;
			}
			emit(p, RE_CHAR, fl)->a = next_cp(c);
			n++;
		}
		a->w.min = n;
		a->w.max = n;
		a->quantifiable = n == 1;
		return true;
	}
	case 'k': {
		c->pos++;
		if (!eat(c, '<')) return fail(c, "\\k is not followed by '<' for a named group", c->pos);
		size_t nstart = c->pos;
		while (!at_end(c) && peek(c) != '>') c->pos++;
		clj_value name = clj_string_new(c->src + nstart, c->pos - nstart);
		bool      closed = eat(c, '>');
		clj_value g = closed && !clj_is_nil(c->names) ? clj_map_get(c->names, name, CLJ_NIL) : CLJ_NIL;
		bool      known = clj_is_fixnum(g);
		if (known) emit(p, RE_BACKREF, fl)->a = (uint32_t)clj_fixnum_val(g);
		clj_release(name);
		if (!closed) return fail(c, "Unclosed group name", nstart);
		if (!known) return fail(c, "Unknown group name", nstart);
		a->w.min = 0;
		a->w.max = RE_INF;
		return true;
	}
	default: break;
	}
	if (e >= '1' && e <= '9') {
		uint32_t g = 0;
		while (peek(c) >= '0' && peek(c) <= '9') {
			uint32_t next = g * 10 + (uint32_t)(peek(c) - '0');
			if (next > c->ngroups) break;
			g = next;
			c->pos++;
		}
		if (!g) return fail(c, "No such group", start);
		emit(p, RE_BACKREF, fl)->a = g;
		a->w.min = 0;
		a->w.max = RE_INF;
		return true;
	}
	uint32_t cp;
	bool     is_char;
	if (!escape_char(c, &cp, &is_char)) return false;
	if (!is_char) return fail(c, "Illegal/unsupported escape sequence", start);
	emit(p, RE_CHAR, fl)->a = cp;
	return true;
}

static bool parse_atom(comp *c, atom *a) {
	re_prog *p = c->p;
	a->at = p->n;
	a->quantifiable = true;
	a->w.min = 1;
	a->w.max = 1;
	uint8_t fl = (uint8_t)(c->flags & (RE_F_I | RE_F_S | RE_F_M));
	size_t  start = c->pos;
	char    ch = peek(c);
	switch (ch) {
	case '(':
		c->pos++;
		return parse_group(c, a);
	case '[': {
		c->pos++;
		re_class *cls = parse_class_body(c, true);
		return cls ? emit_class(c, cls, fl) : false;
	}
	case '.':
		c->pos++;
		emit(p, RE_ANY, fl);
		return true;
	case '^':
		c->pos++;
		return zero_width(c, a, RE_BOL, fl);
	case '$':
		c->pos++;
		return zero_width(c, a, RE_EOL, fl);
	case '*':
	case '+':
	case '?': return fail(c, "Dangling meta character", start);
	case ')': return fail(c, "Unmatched closing ')'", start);
	default: break;
	}
	if (ch != '\\') {
		emit(p, RE_CHAR, fl)->a = next_cp(c);
		return true;
	}
	c->pos++;
	return parse_escaped_atom(c, a, fl, start);
}

// Rebuilds the block at `at` as n to m copies; relative jumps make a copy a memcpy.
static void repeat_block(re_prog *p, uint32_t at, uint32_t n, uint32_t m, bool lazy) {
	uint32_t len = p->n - at;
	re_inst *tmp = malloc(len * sizeof *tmp);
	if (!tmp) clj_fatal("out of memory");
	memcpy(tmp, p->inst + at, len * sizeof *tmp);
	p->n = at;
	for (uint32_t i = 0; i < n; i++) prog_append_block(p, tmp, len);
	if (m == RE_INF) {
		uint32_t mark = p->nmarks++, s = p->n;
		emit(p, RE_SPLIT, 0);
		emit(p, RE_MARK, 0)->a = mark;
		prog_append_block(p, tmp, len);
		emit(p, RE_PROGRESS, 0)->a = mark;
		emit(p, RE_JMP, 0)->x = -(int32_t)(len + 3);
		p->inst[s].x = lazy ? (int32_t)(len + 4) : 1;
		p->inst[s].y = lazy ? 1 : (int32_t)(len + 4);
	} else {
		for (uint32_t i = n; i < m; i++) {
			uint32_t s = p->n;
			emit(p, RE_SPLIT, 0);
			prog_append_block(p, tmp, len);
			p->inst[s].x = lazy ? (int32_t)(len + 1) : 1;
			p->inst[s].y = lazy ? 1 : (int32_t)(len + 1);
		}
	}
	free(tmp);
}

// ok false when the brace is a literal, as Java reads it; error refuses the pattern.
typedef struct {
	uint32_t n, m;
	bool     ok, error;
} re_bound;

static re_bound parse_bound(comp *c) {
	const re_bound literal = {0, 0, false, false}, refused = {0, 0, false, true};
	size_t         save = c->pos;
	c->pos++;
	if (peek(c) < '0' || peek(c) > '9') {
		c->pos = save;
		return literal;
	}
	uint32_t lo = 0, hi;
	while (peek(c) >= '0' && peek(c) <= '9') {
		lo = lo * 10 + (uint32_t)(peek(c) - '0');
		if (lo > RE_MAX_REP) {
			fail(c, "Repetition count too large", save);
			return refused;
		}
		c->pos++;
	}
	hi = lo;
	if (eat(c, ',')) {
		if (peek(c) == '}') hi = RE_INF;
		else {
			hi = 0;
			while (peek(c) >= '0' && peek(c) <= '9') {
				hi = hi * 10 + (uint32_t)(peek(c) - '0');
				if (hi > RE_MAX_REP) {
					fail(c, "Repetition count too large", save);
					return refused;
				}
				c->pos++;
			}
		}
	}
	if (!eat(c, '}')) {
		fail(c, "Unclosed counted closure", save);
		return refused;
	}
	if (hi < lo) {
		fail(c, "Illegal repetition range", save);
		return refused;
	}
	re_bound b = {lo, hi, true, false};
	return b;
}

static bool apply_quantifier(comp *c, uint32_t at, uint32_t n, uint32_t m, bool lazy) {
	re_prog *p = c->p;
	uint32_t len = p->n - at;
	if (m != RE_INF && width_mul(len, m) > RE_MAX_PROG) return fail(c, "Repetition count too large", c->pos);
	if (n == 0 && m == RE_INF) {
		uint32_t mark = p->nmarks++;
		prog_insert(p, at, 2);
		p->inst[at].op = RE_SPLIT;
		p->inst[at + 1].op = RE_MARK;
		p->inst[at + 1].a = mark;
		emit(p, RE_PROGRESS, 0)->a = mark;
		emit(p, RE_JMP, 0)->x = -(int32_t)(len + 3);
		p->inst[at].x = lazy ? (int32_t)(len + 4) : 1;
		p->inst[at].y = lazy ? 1 : (int32_t)(len + 4);
	} else if (n == 1 && m == RE_INF) {
		uint32_t mark = p->nmarks++;
		prog_insert(p, at, 1);
		p->inst[at].op = RE_MARK;
		p->inst[at].a = mark;
		emit(p, RE_PROGRESS, 0)->a = mark;
		re_inst *s = emit(p, RE_SPLIT, 0);
		s->x = lazy ? 1 : -(int32_t)(len + 2);
		s->y = lazy ? -(int32_t)(len + 2) : 1;
	} else if (n == 0 && m == 1) {
		prog_insert(p, at, 1);
		p->inst[at].op = RE_SPLIT;
		p->inst[at].x = lazy ? (int32_t)(len + 1) : 1;
		p->inst[at].y = lazy ? 1 : (int32_t)(len + 1);
	} else {
		repeat_block(p, at, n, m, lazy);
	}
	return true;
}

static bool parse_concat(comp *c, re_width *w) {
	re_prog *p = c->p;
	w->min = 0;
	w->max = 0;
	for (;;) {
		skip_x(c);
		if (at_end(c) || peek(c) == '|' || peek(c) == ')') return true;
		atom a;
		if (!parse_atom(c, &a)) return false;
		skip_x(c);
		uint32_t n = 0, m = 0;
		bool     quantified = true;
		char     q = peek(c);
		if (q == '{') {
			re_bound b = parse_bound(c);
			if (b.error) return false;
			quantified = b.ok;
			n = b.n;
			m = b.m;
		} else if (q == '*' || q == '+' || q == '?') {
			c->pos++;
			n = q == '+' ? 1 : 0;
			m = q == '?' ? 1 : RE_INF;
		} else quantified = false;
		if (!quantified) {
			w->min = width_add(w->min, a.w.min);
			w->max = width_add(w->max, a.w.max);
			continue;
		}
		if (!a.quantifiable) return fail(c, "Dangling meta character", c->pos);
		bool lazy = eat(c, '?');
		bool possessive = !lazy && eat(c, '+');
		if (!apply_quantifier(c, a.at, n, m, lazy)) return false;
		if (possessive) {
			prog_insert(p, a.at, 1);
			p->inst[a.at].op = RE_ATOMIC;
			emit(p, RE_SUBEND, 0);
			p->inst[a.at].x = (int32_t)(p->n - a.at);
		}
		w->min = width_add(w->min, width_mul(a.w.min, n));
		w->max = width_add(w->max, m == RE_INF ? RE_INF : width_mul(a.w.max, m));
	}
}

enum { RE_MAX_ALTS = 64 };

static bool parse_alt(comp *c, re_width *w) {
	re_prog *p = c->p;
	uint32_t start = p->n, jmps[RE_MAX_ALTS], njmps = 0;
	if (!parse_concat(c, w)) return false;
	while (peek(c) == '|') {
		c->pos++;
		if (njmps == RE_MAX_ALTS) return fail(c, "Too many alternatives", c->pos);
		prog_insert(p, start, 1);
		for (uint32_t i = 0; i < njmps; i++) jmps[i]++;
		jmps[njmps++] = p->n;
		emit(p, RE_JMP, 0);
		p->inst[start].op = RE_SPLIT;
		p->inst[start].x = 1;
		p->inst[start].y = (int32_t)(p->n - start);
		re_width b;
		if (!parse_concat(c, &b)) return false;
		w->min = b.min < w->min ? b.min : w->min;
		w->max = w->max == RE_INF || b.max == RE_INF ? RE_INF : (b.max > w->max ? b.max : w->max);
	}
	for (uint32_t i = 0; i < njmps; i++) p->inst[jmps[i]].x = (int32_t)(p->n - jmps[i]);
	return true;
}

// ---- matcher

typedef struct {
	uint32_t slot;
	int32_t  val;
} re_undo;

typedef struct {
	uint32_t pc, sp, undo;
} re_bt;

typedef struct {
	const re_text *t;
	const re_prog *p;
	int32_t       *slots;
	re_undo       *undo;
	uint32_t       undo_n, undo_cap;
	re_bt         *bt;
	uint32_t       bt_n, bt_cap;
	uint32_t       budget;
	bool           whole, timeout;
} re_ctx;

// One clock read per this many backtracks: the deadline is the only stop a runaway pattern has.
enum { RE_DEADLINE_EVERY = 4096 };

static void undo_push(re_ctx *c, uint32_t slot, int32_t val) {
	if (c->undo_n == c->undo_cap) {
		c->undo_cap = c->undo_cap ? c->undo_cap * 2 : 64;
		c->undo = realloc(c->undo, c->undo_cap * sizeof *c->undo);
		if (!c->undo) clj_fatal("out of memory");
	}
	c->undo[c->undo_n].slot = slot;
	c->undo[c->undo_n].val = val;
	c->undo_n++;
}

static void undo_to(re_ctx *c, uint32_t n) {
	while (c->undo_n > n) {
		c->undo_n--;
		c->slots[c->undo[c->undo_n].slot] = c->undo[c->undo_n].val;
	}
}

static void slot_set(re_ctx *c, uint32_t slot, int32_t val) {
	undo_push(c, slot, c->slots[slot]);
	c->slots[slot] = val;
}

static void bt_push(re_ctx *c, uint32_t pc, uint32_t sp) {
	if (c->bt_n == c->bt_cap) {
		c->bt_cap = c->bt_cap ? c->bt_cap * 2 : 64;
		c->bt = realloc(c->bt, c->bt_cap * sizeof *c->bt);
		if (!c->bt) clj_fatal("out of memory");
	}
	c->bt[c->bt_n].pc = pc;
	c->bt[c->bt_n].sp = sp;
	c->bt[c->bt_n].undo = c->undo_n;
	c->bt_n++;
}

static bool re_run(re_ctx *c, uint32_t pc, uint32_t sp, uint32_t *end) {
	const uint32_t base = c->bt_n, ubase = c->undo_n;
	for (;;) {
		const re_inst *i = &c->p->inst[pc];
		bool           bad = false;
		switch (i->op) {
		case RE_CHAR:
			if (sp < c->t->n && (c->t->cp[sp] == i->a || ((i->flags & RE_F_I) && swap_case(c->t->cp[sp]) == i->a))) {
				sp++;
				pc++;
			} else bad = true;
			break;
		case RE_CLASS:
			if (sp < c->t->n && class_member(c->p->classes[i->a], c->t->cp[sp], (i->flags & RE_F_I) != 0)) {
				sp++;
				pc++;
			} else bad = true;
			break;
		case RE_ANY:
			if (sp < c->t->n && ((i->flags & RE_F_S) || c->t->cp[sp] != '\n')) {
				sp++;
				pc++;
			} else bad = true;
			break;
		case RE_SPLIT:
			bt_push(c, (uint32_t)((int32_t)pc + i->y), sp);
			pc = (uint32_t)((int32_t)pc + i->x);
			break;
		case RE_JMP: pc = (uint32_t)((int32_t)pc + i->x); break;
		case RE_SAVE:
		case RE_MARK:
			slot_set(c, i->a, (int32_t)sp);
			pc++;
			break;
		case RE_PROGRESS:
			if ((int32_t)sp == c->slots[i->a]) bad = true;
			else pc++;
			break;
		case RE_MATCH:
			if (c->whole && sp != c->t->n) {
				bad = true;
				break;
			}
			*end = sp;
			c->bt_n = base;
			return true;
		case RE_SUBEND:
			*end = sp;
			c->bt_n = base;
			return true;
		case RE_BOL:
			if (sp == 0 || ((i->flags & RE_F_M) && sp < c->t->n && c->t->cp[sp - 1] == '\n')) pc++;
			else bad = true;
			break;
		case RE_EOL:
			if (i->flags & RE_F_M) {
				if (sp == c->t->n || c->t->cp[sp] == '\n') pc++;
				else bad = true;
			} else if (sp == c->t->n || (sp + 1 == c->t->n && c->t->cp[sp] == '\n')) pc++;
			else bad = true;
			break;
		case RE_BOI:
			if (sp == 0) pc++;
			else bad = true;
			break;
		case RE_EOI:
			if (sp == c->t->n) pc++;
			else bad = true;
			break;
		case RE_EOI_NL:
			if (sp == c->t->n || (sp + 1 == c->t->n && c->t->cp[sp] == '\n')) pc++;
			else bad = true;
			break;
		case RE_WORDB:
		case RE_NWORDB: {
			bool before = sp > 0 && is_word(c->t->cp[sp - 1]);
			bool after = sp < c->t->n && is_word(c->t->cp[sp]);
			if ((before != after) == (i->op == RE_WORDB)) pc++;
			else bad = true;
			break;
		}
		case RE_BACKREF: {
			int32_t from = c->slots[2 * i->a], to = c->slots[2 * i->a + 1];
			// A group that did not participate matches nothing, as Matcher does.
			if (from < 0 || to < 0) {
				bad = true;
				break;
			}
			uint32_t len = (uint32_t)(to - from);
			bool     ok = sp + len <= c->t->n;
			for (uint32_t k = 0; ok && k < len; k++) {
				uint32_t x = c->t->cp[sp + k], y = c->t->cp[(uint32_t)from + k];
				ok = x == y || ((i->flags & RE_F_I) && swap_case(x) == y);
			}
			if (ok) {
				sp += len;
				pc++;
			} else bad = true;
			break;
		}
		case RE_ATOMIC: {
			uint32_t sub, save_bt = c->bt_n;
			bool     ok = re_run(c, pc + 1, sp, &sub);
			c->bt_n = save_bt;
			if (c->timeout) return false;
			if (!ok) {
				bad = true;
				break;
			}
			sp = sub;
			pc = (uint32_t)((int32_t)pc + i->x);
			break;
		}
		case RE_LOOKAHEAD:
		case RE_LOOKBEHIND: {
			uint32_t sub, save_bt = c->bt_n, save_undo = c->undo_n;
			bool     ok = i->op == RE_LOOKAHEAD ? re_run(c, pc + 1, sp, &sub) : sp >= i->a && re_run(c, pc + 1, sp - i->a, &sub) && sub == sp;
			c->bt_n = save_bt;
			if (c->timeout) return false;
			if (i->flags & RE_F_NEG) {
				undo_to(c, save_undo);
				ok = !ok;
			}
			if (!ok) {
				undo_to(c, save_undo);
				bad = true;
				break;
			}
			pc = (uint32_t)((int32_t)pc + i->x);
			break;
		}
		default: clj_fatal("unknown regex instruction");
		}
		if (!bad) continue;
		if (!--c->budget) {
			c->budget = RE_DEADLINE_EVERY;
			if (clj_deadline_expired()) {
				c->timeout = true;
				return false;
			}
		}
		if (c->bt_n == base) {
			undo_to(c, ubase);
			return false;
		}
		c->bt_n--;
		undo_to(c, c->bt[c->bt_n].undo);
		pc = c->bt[c->bt_n].pc;
		sp = c->bt[c->bt_n].sp;
	}
}

// 1 on a match, 0 on none, -1 with the deadline exception pending.
// A scan keeps one context across matches: its three buffers are the only allocation a match needs.
static void ctx_start(re_ctx *c, clj_value re, const re_text *t) {
	memset(c, 0, sizeof *c);
	c->t = t;
	c->p = clj_regex_of(re)->prog;
	c->slots = malloc(c->p->nslots * sizeof *c->slots);
	if (!c->slots) clj_fatal("out of memory");
}

static void ctx_done(re_ctx *c) {
	free(c->slots);
	free(c->undo);
	free(c->bt);
}

static int re_exec_in(re_ctx *c, clj_value re, uint32_t from, bool whole, int32_t *groups) {
	const clj_regex *r = clj_regex_of(re);
	c->whole = whole;
	c->budget = RE_DEADLINE_EVERY;
	c->timeout = false;
	for (uint32_t start = from; start <= c->t->n; start++) {
		for (uint32_t i = 0; i < c->p->nslots; i++) c->slots[i] = -1;
		c->undo_n = 0;
		c->bt_n = 0;
		uint32_t end;
		if (re_run(c, 0, start, &end)) {
			memcpy(groups, c->slots, 2 * (r->ngroups + 1) * sizeof *groups);
			return 1;
		}
		if (c->timeout) {
			clj_throw_msg(CLJ_DEADLINE_MESSAGE);
			return -1;
		}
		if (whole) break;
	}
	return 0;
}

static int re_exec(clj_value re, const re_text *t, uint32_t from, bool whole, int32_t *groups) {
	re_ctx c;
	ctx_start(&c, re, t);
	int result = re_exec_in(&c, re, from, whole, groups);
	ctx_done(&c);
	return result;
}

// ---- type descriptors

static void regex_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_regex *r = self;
	visit(r->pattern, ctx);
	visit(r->names, ctx);
}

static void regex_finalize(void *self) { prog_free(((clj_regex *)self)->prog); }

// JVM patterns are equal by identity; a value type costs nothing here (docs/jvm-differences.md).
static uint32_t regex_hash(void *self) { return clj_hash(((clj_regex *)self)->pattern); }

static bool regex_equals(void *self, clj_value other) {
	return clj_is_regex(other) && clj_equals(((clj_regex *)self)->pattern, clj_regex_pattern(other));
}

const clj_type clj_regex_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "regex",
	.each_child = regex_each_child,
	.finalize = regex_finalize,
	.hash = regex_hash,
	.equals = regex_equals,
};

static void matcher_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_matcher *m = self;
	visit(m->re, ctx);
	visit(m->input, ctx);
}

static void matcher_finalize(void *self) {
	clj_matcher *m = self;
	text_free(m->text);
	free(m->text);
	free(m->slots);
}

static clj_value matcher_lookup(clj_value self, clj_value key, clj_value not_found) {
	clj_matcher *m = clj_matcher_of(self);
	intptr_t     n;
	if (!clj_index_arg(key, &n) || !m->matched || n < 0 || (uintptr_t)n > clj_regex_group_count(m->re)) return clj_retain(not_found);
	return clj_matcher_group(self, n);
}

const clj_type clj_matcher_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "matcher",
	.each_child = matcher_each_child,
	.finalize = matcher_finalize,
	.lookup = matcher_lookup,
};

// ---- construction

static clj_value syntax_error(clj_value pattern, const char *desc, size_t offset) {
	clj_value data = clj_map_empty();
	clj_value kw = clj_keyword_from_cstr("pattern");
	data = clj_map_assoc(data, kw, pattern);
	clj_release(kw);
	kw = clj_keyword_from_cstr("offset");
	data = clj_map_assoc(data, kw, clj_fixnum((intptr_t)offset));
	clj_release(kw);
	char msg[512];
	snprintf(msg, sizeof msg, "%s near index %zu\n%.256s", desc, offset, clj_string_bytes(pattern));
	clj_value text = clj_string_from_cstr(msg);
	clj_value ex = clj_ex_info(text, data);
	clj_release(text);
	clj_release(data);
	return clj_throw(ex);
}

clj_value clj_regex_new(clj_value pattern) {
	if (!clj_is_string(pattern)) return clj_throw_msg("%s cannot be cast to a pattern", clj_type_name(pattern));
	re_prog *p = calloc(1, sizeof *p);
	if (!p) clj_fatal("out of memory");
	comp c = {0};
	c.src = clj_string_bytes(pattern);
	c.len = clj_string_len(pattern);
	c.p = p;
	c.names = CLJ_NIL;
	emit(p, RE_SAVE, 0);
	re_width w;
	bool     ok = parse_alt(&c, &w);
	if (ok && !at_end(&c)) ok = fail(&c, "Unmatched closing ')'", c.pos);
	if (!ok) {
		prog_free(p);
		clj_release(c.names);
		return syntax_error(pattern, c.err, c.errpos);
	}
	save_group(p, 1);
	emit(p, RE_MATCH, 0);
	// Mark slots sit past the group bounds, whose count is only known once the whole pattern is parsed.
	uint32_t bounds = 2 * (c.ngroups + 1);
	for (uint32_t i = 0; i < p->n; i++) {
		if (p->inst[i].op == RE_MARK || p->inst[i].op == RE_PROGRESS) p->inst[i].a += bounds;
	}
	p->nslots = bounds + p->nmarks;
	clj_regex *r = clj_alloc(&clj_regex_type, sizeof *r);
	r->pattern = clj_retain(pattern);
	r->names = c.names;
	r->prog = p;
	r->ngroups = c.ngroups;
	return clj_from_ptr(r);
}

clj_value clj_matcher_new(clj_value re, clj_value input) {
	if (!clj_is_regex(re)) return clj_throw_msg("re-matcher expects a pattern, got: %s", clj_type_name(re));
	if (!clj_is_string(input)) return clj_throw_msg("re-matcher expects a string, got: %s", clj_type_name(input));
	clj_matcher *m = clj_alloc(&clj_matcher_type, sizeof *m);
	m->re = clj_retain(re);
	m->input = clj_retain(input);
	m->text = calloc(1, sizeof(re_text));
	if (!m->text) clj_fatal("out of memory");
	text_of(input, m->text);
	uint32_t n = 2 * (clj_regex_group_count(re) + 1);
	m->slots = malloc(n * sizeof *m->slots);
	if (!m->slots) clj_fatal("out of memory");
	for (uint32_t i = 0; i < n; i++) m->slots[i] = -1;
	return clj_from_ptr(m);
}

// ---- results

// The matched string when the pattern has no group, else group 0 and every group.
static clj_value result_of(clj_value re, clj_value s, const re_text *t, const int32_t *g) {
	uint32_t n = clj_regex_group_count(re);
	if (!n) return text_slice(t, s, (uint32_t)g[0], (uint32_t)g[1]);
	clj_value v = clj_vector_empty();
	for (uint32_t i = 0; i <= n; i++) {
		clj_value item = g[2 * i] < 0 ? CLJ_NIL : text_slice(t, s, (uint32_t)g[2 * i], (uint32_t)g[2 * i + 1]);
		v = clj_vector_conj(v, item);
		clj_release(item);
	}
	return v;
}

static clj_value bad_args(const char *what, clj_value re, clj_value s) {
	if (!clj_is_regex(re)) return clj_throw_msg("%s expects a pattern, got: %s", what, clj_type_name(re));
	return clj_throw_msg("%s expects a string, got: %s", what, clj_type_name(s));
}

static clj_value find_one(const char *what, clj_value re, clj_value s, bool whole) {
	if (!clj_is_regex(re) || !clj_is_string(s)) return bad_args(what, re, s);
	re_text t = {0};
	text_of(s, &t);
	int32_t *g = malloc(2 * (clj_regex_group_count(re) + 1) * sizeof *g);
	if (!g) clj_fatal("out of memory");
	int       hit = re_exec(re, &t, 0, whole, g);
	clj_value r = hit > 0 ? result_of(re, s, &t, g) : hit < 0 ? CLJ_THROWN : CLJ_NIL;
	free(g);
	text_free(&t);
	return r;
}

clj_value clj_re_find(clj_value re, clj_value s) { return find_one("re-find", re, s, false); }
clj_value clj_re_matches(clj_value re, clj_value s) { return find_one("re-matches", re, s, true); }

clj_value clj_matcher_find(clj_value m) {
	if (!clj_is_matcher(m)) return clj_throw_msg("re-find expects a matcher, got: %s", clj_type_name(m));
	clj_matcher   *x = clj_matcher_of(m);
	const re_text *t = x->text;
	if (x->from > t->n) {
		x->matched = false;
		return CLJ_NIL;
	}
	int hit = re_exec(x->re, t, x->from, false, x->slots);
	if (hit < 0) return CLJ_THROWN;
	if (!hit) {
		x->matched = false;
		x->from = t->n + 1;
		return CLJ_NIL;
	}
	x->matched = true;
	// An empty match would find itself for ever, so the scan moves on by one.
	x->from = (uint32_t)x->slots[1] + (x->slots[0] == x->slots[1] ? 1 : 0);
	return result_of(x->re, x->input, t, x->slots);
}

clj_value clj_matcher_groups(clj_value m) {
	if (!clj_is_matcher(m)) return clj_throw_msg("re-groups expects a matcher, got: %s", clj_type_name(m));
	clj_matcher *x = clj_matcher_of(m);
	if (!x->matched) return clj_throw_msg("No match found");
	return result_of(x->re, x->input, x->text, x->slots);
}

clj_value clj_matcher_group(clj_value m, intptr_t n) {
	clj_matcher *x = clj_matcher_of(m);
	if (!x->matched) return clj_throw_msg("No match found");
	uint32_t count = clj_regex_group_count(x->re);
	if (n < 0 || (uintptr_t)n > count) return clj_throw_msg("Index %lld out of bounds for length %u", (long long)n, count + 1);
	int32_t from = x->slots[2 * n], to = x->slots[2 * n + 1];
	if (from < 0) return CLJ_NIL;
	return text_slice(x->text, x->input, (uint32_t)from, (uint32_t)to);
}

// ---- clojure.string's regex paths

typedef struct {
	char  *data;
	size_t len, cap;
} buf;

static void buf_put(buf *b, const char *s, size_t n) {
	if (!n) return;
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

static void buf_slice(buf *b, const re_text *t, clj_value s, uint32_t from, uint32_t to) {
	buf_put(b, clj_string_bytes(s) + t->off[from], t->off[to] - t->off[from]);
}

// Java's Pattern.split, including the leading zero-width match it drops and the trailing empties.
clj_value clj_regex_split(clj_value re, clj_value s, intptr_t limit) {
	if (!clj_is_regex(re) || !clj_is_string(s)) return bad_args("split", re, s);
	re_text t = {0};
	text_of(s, &t);
	int32_t *g = malloc(2 * (clj_regex_group_count(re) + 1) * sizeof *g);
	if (!g) clj_fatal("out of memory");
	clj_value parts = clj_vector_empty();
	uint32_t  index = 0, at = 0;
	bool      thrown = false, none = true;
	re_ctx    ctx;
	ctx_start(&ctx, re, &t);
	while (at <= t.n) {
		int hit = re_exec_in(&ctx, re, at, false, g);
		if (hit < 0) {
			thrown = true;
			break;
		}
		if (!hit) break;
		uint32_t from = (uint32_t)g[0], to = (uint32_t)g[1];
		if (limit > 0 && (intptr_t)clj_vector_count(parts) == limit - 1) {
			clj_value tail = text_slice(&t, s, index, t.n);
			parts = clj_vector_conj(parts, tail);
			clj_release(tail);
			index = to;
			none = false;
			break;
		}
		if (!(index == 0 && from == 0 && to == 0)) {
			clj_value piece = text_slice(&t, s, index, from);
			parts = clj_vector_conj(parts, piece);
			clj_release(piece);
			index = to;
			none = false;
		}
		at = to == from ? to + 1 : to;
	}
	if (thrown) {
		clj_release(parts);
		parts = CLJ_THROWN;
	} else if (none) {
		clj_release(parts);
		parts = clj_vector_empty();
		parts = clj_vector_conj(parts, s);
	} else {
		if (limit <= 0 || (intptr_t)clj_vector_count(parts) < limit) {
			clj_value tail = text_slice(&t, s, index, t.n);
			parts = clj_vector_conj(parts, tail);
			clj_release(tail);
		}
		if (limit == 0) {
			uint32_t n = clj_vector_count(parts);
			while (n > 0 && clj_string_len(clj_vector_nth(parts, n - 1)) == 0) n--;
			while (clj_vector_count(parts) > n) parts = clj_vector_pop(parts);
		}
	}
	ctx_done(&ctx);
	free(g);
	text_free(&t);
	return parts;
}

// $0..$n and ${name} name groups; a backslash quotes the next character.
static bool expand(buf *b, clj_value re, clj_value s, const re_text *t, const int32_t *g, clj_value repl) {
	const char *r = clj_string_bytes(repl);
	size_t      n = clj_string_len(repl);
	uint32_t    count = clj_regex_group_count(re);
	for (size_t i = 0; i < n; i++) {
		if (r[i] == '\\' && i + 1 < n) {
			buf_put(b, r + ++i, 1);
			continue;
		}
		if (r[i] != '$') {
			buf_put(b, r + i, 1);
			continue;
		}
		uint32_t group;
		if (i + 1 < n && r[i + 1] == '{') {
			size_t start = i + 2, end = start;
			while (end < n && r[end] != '}') end++;
			if (end == n) {
				clj_throw_msg("Unclosed group name in the replacement");
				return false;
			}
			clj_value name = clj_string_new(r + start, end - start);
			clj_value idx = clj_is_nil(clj_regex_of(re)->names) ? CLJ_NIL : clj_map_get(clj_regex_of(re)->names, name, CLJ_NIL);
			bool      known = clj_is_fixnum(idx);
			group = known ? (uint32_t)clj_fixnum_val(idx) : 0;
			clj_release(name);
			if (!known) {
				clj_throw_msg("No group with name {%.*s}", (int)(end - start), r + start);
				return false;
			}
			i = end;
		} else {
			size_t j = i + 1;
			if (j >= n || r[j] < '0' || r[j] > '9') {
				clj_throw_msg("Illegal group reference");
				return false;
			}
			// The first digit is always the group, as appendReplacement reads it; the rest extend it while legal.
			group = (uint32_t)(r[j++] - '0');
			while (j < n && r[j] >= '0' && r[j] <= '9') {
				uint32_t next = group * 10 + (uint32_t)(r[j] - '0');
				if (next > count) break;
				group = next;
				j++;
			}
			if (group > count) {
				clj_throw_msg("No group %u", group);
				return false;
			}
			i = j - 1;
		}
		if (g[2 * group] >= 0) buf_slice(b, t, s, (uint32_t)g[2 * group], (uint32_t)g[2 * group + 1]);
	}
	return true;
}

// The scan both replace forms share; f nil means the literal replacement.
static clj_value replace_scan(clj_value re, clj_value s, clj_value repl, clj_value f, bool first_only) {
	if (!clj_is_regex(re) || !clj_is_string(s)) return bad_args("replace", re, s);
	re_text t = {0};
	text_of(s, &t);
	int32_t *g = malloc(2 * (clj_regex_group_count(re) + 1) * sizeof *g);
	if (!g) clj_fatal("out of memory");
	buf      b = {0};
	uint32_t at = 0, copied = 0;
	bool     thrown = false;
	re_ctx   ctx;
	ctx_start(&ctx, re, &t);
	while (at <= t.n) {
		int hit = re_exec_in(&ctx, re, at, false, g);
		if (hit < 0) {
			thrown = true;
			break;
		}
		if (!hit) break;
		uint32_t from = (uint32_t)g[0], to = (uint32_t)g[1];
		buf_slice(&b, &t, s, copied, from);
		if (clj_is_nil(f)) {
			if (!expand(&b, re, s, &t, g, repl)) {
				thrown = true;
				break;
			}
		} else {
			clj_value arg = result_of(re, s, &t, g);
			clj_value out = clj_invoke(f, &arg, 1);
			clj_release(arg);
			if (out == CLJ_THROWN) {
				thrown = true;
				break;
			}
			if (!clj_is_string(out)) {
				clj_throw_msg("%s cannot be cast to a string", clj_type_name(out));
				clj_release(out);
				thrown = true;
				break;
			}
			buf_put(&b, clj_string_bytes(out), clj_string_len(out));
			clj_release(out);
		}
		copied = to;
		at = to == from ? to + 1 : to;
		if (first_only) break;
	}
	clj_value out;
	if (thrown) out = CLJ_THROWN;
	else {
		buf_slice(&b, &t, s, copied, t.n);
		out = clj_string_new(b.data, b.len);
	}
	ctx_done(&ctx);
	free(b.data);
	free(g);
	text_free(&t);
	return out;
}

clj_value clj_regex_replace(clj_value re, clj_value s, clj_value replacement, bool first_only) {
	if (!clj_is_string(replacement)) return clj_throw_msg("replace expects a string replacement, got: %s", clj_type_name(replacement));
	return replace_scan(re, s, replacement, CLJ_NIL, first_only);
}

clj_value clj_regex_replace_by(clj_value re, clj_value s, clj_value f, bool first_only) {
	if (!clj_has_core(f, CLJ_CORE_FN)) return clj_throw_msg("replace expects a function, got: %s", clj_type_name(f));
	return replace_scan(re, s, CLJ_NIL, f, first_only);
}

clj_value clj_regex_quote_replacement(clj_value s) {
	if (!clj_is_string(s)) return clj_throw_msg("re-quote-replacement expects a string, got: %s", clj_type_name(s));
	const char *bytes = clj_string_bytes(s);
	size_t      n = clj_string_len(s);
	buf         b = {0};
	for (size_t i = 0; i < n; i++) {
		if (bytes[i] == '\\' || bytes[i] == '$') buf_put(&b, "\\", 1);
		buf_put(&b, bytes + i, 1);
	}
	clj_value out = clj_string_new(b.data, b.len);
	free(b.data);
	return out;
}

// ---- builtins

static clj_value b_re_pattern(const clj_value *args, size_t n) {
	(void)n;
	if (clj_is_regex(args[0])) return clj_retain(args[0]);
	return clj_regex_new(args[0]);
}

static clj_value b_regex_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_regex(args[0]));
}

static clj_value b_re_matcher(const clj_value *args, size_t n) {
	(void)n;
	return clj_matcher_new(args[0], args[1]);
}

static clj_value b_re_find(const clj_value *args, size_t n) {
	if (n == 1) return clj_matcher_find(args[0]);
	return clj_re_find(args[0], args[1]);
}

static clj_value b_re_matches(const clj_value *args, size_t n) {
	(void)n;
	return clj_re_matches(args[0], args[1]);
}

static clj_value b_re_groups(const clj_value *args, size_t n) {
	(void)n;
	return clj_matcher_groups(args[0]);
}

static clj_value b_re_quote(const clj_value *args, size_t n) {
	(void)n;
	return clj_regex_quote_replacement(args[0]);
}

static clj_value b_re_split(const clj_value *args, size_t n) {
	intptr_t limit = 0;
	if (n == 3 && !clj_index_arg(args[2], &limit)) return clj_throw_msg("split expects an integer limit");
	return clj_regex_split(args[0], args[1], limit);
}

static clj_value b_re_replace(const clj_value *args, size_t n) {
	(void)n;
	bool first_only = clj_truthy(args[3]);
	if (clj_is_string(args[2])) return clj_regex_replace(args[0], args[1], args[2], first_only);
	return clj_regex_replace_by(args[0], args[1], args[2], first_only);
}

void clj_regex_builtins_install(void) {
	clj_builtin_bind("re-pattern", b_re_pattern, 1, 1);
	clj_builtin_bind("regex?", b_regex_p, 1, 1);
	clj_builtin_bind("re-matcher", b_re_matcher, 2, 2);
	clj_builtin_bind("re-find", b_re_find, 1, 2);
	clj_builtin_bind("re-matches", b_re_matches, 2, 2);
	clj_builtin_bind("re-groups", b_re_groups, 1, 1);
	clj_builtin_bind("re-quote-replacement*", b_re_quote, 1, 1);
	clj_builtin_bind("re-split*", b_re_split, 2, 3);
	clj_builtin_bind("re-replace*", b_re_replace, 4, 4);
}
