// @ai-generated(solo)
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"

typedef enum { F_LIST, F_VECTOR, F_MAP, F_SET, F_QUOTE, F_DEREF, F_DISCARD, F_VAR, F_SYNTAX_QUOTE, F_UNQUOTE, F_UNQUOTE_SPLICING, F_META } frame_kind;

typedef struct {
	frame_kind kind;
	size_t     start; // index into the value stack where this collection's items begin
	uint32_t   line, col;
	clj_value  meta; // F_META: the map read after ^, owned; CLJ_UNBOUND until then
} frame;

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_line, kw_column, kw_tag;

static void intern_keywords(void) {
	kw_line = clj_keyword_from_cstr("line");
	kw_column = clj_keyword_from_cstr("column");
	kw_tag = clj_keyword_from_cstr("tag");
}

// Explicit stacks: nesting depth is bounded by memory, not by the C stack.
typedef struct {
	clj_reader *r;
	clj_value  *vals;
	size_t      nvals, vcap;
	frame      *frames;
	size_t      nframes, fcap;
	size_t      sq_depth; // open syntax-quote frames, so ~ knows whether it is inside one
} parser;

typedef struct {
	char  *data;
	size_t len, cap;
} buf;

static void buf_put(buf *b, const char *s, size_t n) {
	if (b->len + n > b->cap) {
		size_t cap = b->cap ? b->cap : 32;
		while (cap < b->len + n) cap *= 2;
		b->data = realloc(b->data, cap);
		if (!b->data) clj_fatal("out of memory");
		b->cap = cap;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
}

static void buf_put_utf8(buf *b, uint32_t cp) {
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
	buf_put(b, s, n);
}

// Byte length of the scalar at s, 0 when malformed (overlong, surrogate, truncated, > U+10FFFF).
static size_t decode_utf8(const unsigned char *s, size_t n, uint32_t *cp) {
	if (n == 0) return 0;
	unsigned char c = s[0];
	size_t len;
	uint32_t v, min;
	if (c < 0x80) {
		*cp = c;
		return 1;
	} else if ((c & 0xE0) == 0xC0) {
		len = 2, v = c & 0x1F, min = 0x80;
	} else if ((c & 0xF0) == 0xE0) {
		len = 3, v = c & 0x0F, min = 0x800;
	} else if ((c & 0xF8) == 0xF0) {
		len = 4, v = c & 0x07, min = 0x10000;
	} else {
		return 0;
	}
	if (n < len) return 0;
	for (size_t i = 1; i < len; i++) {
		if ((s[i] & 0xC0) != 0x80) return 0;
		v = (v << 6) | (s[i] & 0x3F);
	}
	if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0;
	*cp = v;
	return len;
}

static bool at_eof(const clj_reader *r) { return r->pos >= r->len; }
static unsigned char peek(const clj_reader *r) { return (unsigned char)r->bytes[r->pos]; }

static void advance(clj_reader *r) {
	unsigned char c = peek(r);
	r->pos++;
	if (c == '\n') {
		r->line++;
		r->col = 1;
	} else if ((c & 0xC0) != 0x80) {
		r->col++;
	}
}

static bool is_ws(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == '\f' || c == '\v'; }

// Clojure's terminating macro characters; `#`, `'` and `%` may appear inside a token.
static bool is_terminating(unsigned char c) { return strchr("\";@^`~()[]{}\\", c) != NULL && c != '\0'; }

static bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

static int hex_val(unsigned char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static clj_read_status fail(parser *p, uint32_t line, uint32_t col, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static clj_read_status fail(parser *p, uint32_t line, uint32_t col, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(p->r->message, sizeof p->r->message, fmt, ap);
	va_end(ap);
	p->r->error_line = line;
	p->r->error_col = col;
	return CLJ_READ_ERROR;
}

static void skip_line(clj_reader *r) {
	while (!at_eof(r) && peek(r) != '\n') advance(r);
}

static void skip_blank(clj_reader *r) {
	while (!at_eof(r)) {
		unsigned char c = peek(r);
		if (is_ws(c)) advance(r);
		else if (c == ';') skip_line(r);
		else break;
	}
}

// Consumes the rest of a token; the caller has already consumed any literal first character.
static void read_token_tail(clj_reader *r) {
	while (!at_eof(r) && !is_ws(peek(r)) && !is_terminating(peek(r))) advance(r);
}

static void push_frame(parser *p, frame_kind kind, uint32_t line, uint32_t col) {
	if (p->nframes == p->fcap) {
		p->fcap = p->fcap ? p->fcap * 2 : 16;
		p->frames = realloc(p->frames, p->fcap * sizeof *p->frames);
		if (!p->frames) clj_fatal("out of memory");
	}
	p->frames[p->nframes++] = (frame){kind, p->nvals, line, col, CLJ_UNBOUND};
}

static void push_raw(parser *p, clj_value v) {
	if (p->nvals == p->vcap) {
		p->vcap = p->vcap ? p->vcap * 2 : 64;
		p->vals = realloc(p->vals, p->vcap * sizeof *p->vals);
		if (!p->vals) clj_fatal("out of memory");
	}
	p->vals[p->nvals++] = v;
}

// Consumes sym and v.
static clj_value wrap(clj_value sym, clj_value v) {
	clj_value items[2] = {sym, v};
	clj_value list = clj_list_from_array(items, 2);
	clj_release(sym);
	clj_release(v);
	return list;
}

// ---- syntax-quote, expanded at read time exactly as Clojure's LispReader does

typedef struct {
	parser   *p;
	clj_value gensyms; // map: auto-gensym symbol → its generated symbol
	uint32_t  line, col;
} sq;

static bool is_map(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_map_type; }

static bool symbol_named(clj_value v, const char *ns, const char *name) {
	if (!clj_is_symbol(v) || clj_is_nil(clj_symbol_ns(v))) return false;
	return strcmp(clj_string_bytes(clj_symbol_ns(v)), ns) == 0 && strcmp(clj_string_bytes(clj_symbol_name(v)), name) == 0;
}

static bool is_tagged(clj_value form, const char *name) {
	return clj_is_list(form) && !clj_is_empty_list(form) && symbol_named(clj_cons_of(form)->first, "clojure.core", name);
}

static clj_value second(clj_value list) { return clj_cons_of(clj_cons_of(list)->rest)->first; }

static bool is_auto_gensym(clj_value sym) {
	if (!clj_is_nil(clj_symbol_ns(sym))) return false;
	clj_value name = clj_symbol_name(sym);
	uint32_t  len = clj_string_len(name);
	return len > 1 && clj_string_bytes(name)[len - 1] == '#';
}

// Owned; the same symbol for the same name within one syntax-quote.
static clj_value gensym_for(sq *q, clj_value sym) {
	clj_value found = clj_map_get(q->gensyms, sym, CLJ_NIL);
	if (!clj_is_nil(found)) return clj_retain(found);
	clj_value name = clj_symbol_name(sym);
	size_t    cap = clj_string_len(name) + 32;
	char     *text = malloc(cap);
	if (!text) clj_fatal("out of memory");
	snprintf(text, cap, "%.*s__%llu__auto__", (int)clj_string_len(name) - 1, clj_string_bytes(name), (unsigned long long)clj_next_id());
	clj_value gs = clj_symbol_from_cstr(text);
	free(text);
	q->gensyms = clj_map_assoc(q->gensyms, sym, gs);
	return gs;
}

// (items...) consuming every item.
static clj_value list_owning(clj_value *items, size_t n) {
	clj_value list = clj_list_from_array(items, n);
	for (size_t i = 0; i < n; i++) clj_release(items[i]);
	return list;
}

static clj_value core_sym(const char *name) {
	char text[64];
	snprintf(text, sizeof text, "clojure.core/%s", name);
	return clj_symbol_from_cstr(text);
}

// (clojure.core/list v) consuming v.
static clj_value wrap_list(clj_value v) {
	clj_value pair[2] = {core_sym("list"), v};
	return list_owning(pair, 2);
}

// (seq (concat args...)) consuming args.
static clj_value seq_concat(clj_value *args, size_t n) {
	clj_value *with_head = malloc((n + 1) * sizeof *with_head);
	if (!with_head) clj_fatal("out of memory");
	with_head[0] = core_sym("concat");
	memcpy(with_head + 1, args, n * sizeof *args);
	clj_value concat = list_owning(with_head, n + 1);
	free(with_head);
	clj_value pair[2] = {core_sym("seq"), concat};
	return list_owning(pair, 2);
}

// (apply f (seq (concat args...))) consuming args.
static clj_value apply_to(const char *fn, clj_value *args, size_t n) {
	clj_value triple[3] = {core_sym("apply"), core_sym(fn), seq_concat(args, n)};
	return list_owning(triple, 3);
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

// Borrowed items of a list, a vector, a set or a map (flattened to key value ...).
static clj_value *coll_items(clj_value coll, size_t *n) {
	if (clj_is_set(coll)) {
		collect_ctx c = {calloc((size_t)clj_set_count(coll) + 1, sizeof(clj_value)), 0};
		if (!c.entries) clj_fatal("out of memory");
		clj_set_each(coll, collect_item, &c);
		*n = c.n;
		return c.entries;
	}
	if (is_map(coll)) {
		collect_ctx c = {calloc(2 * (size_t)clj_map_count(coll) + 1, sizeof(clj_value)), 0};
		if (!c.entries) clj_fatal("out of memory");
		clj_map_each(coll, collect_entry, &c);
		*n = c.n;
		return c.entries;
	}
	size_t     count = clj_list_count(coll);
	clj_value *items = calloc(count + 1, sizeof *items);
	if (!items) clj_fatal("out of memory");
	clj_seq_iter it = clj_seq_iter_start(coll);
	size_t       i = 0;
	while (clj_seq_iter_next(&it, &items[i])) i++;
	*n = count;
	return items;
}

static bool is_sq_coll(clj_value v) { return (clj_is_list(v) && !clj_is_empty_list(v)) || clj_is_vector(v) || is_map(v) || clj_is_set(v); }

// Everything but a non-empty collection.
static clj_value sq_atom(sq *q, clj_value form) {
	if (clj_is_symbol(form)) {
		clj_value sym;
		if (is_auto_gensym(form)) sym = gensym_for(q, form);
		else if (q->p->r->resolve) sym = q->p->r->resolve(form, q->p->r->resolve_ctx);
		else sym = clj_retain(form);
		clj_value pair[2] = {clj_symbol_from_cstr("quote"), sym};
		return list_owning(pair, 2);
	}
	if (clj_is_empty_list(form)) {
		clj_value head = core_sym("list");
		return list_owning(&head, 1);
	}
	if (clj_is_keyword(form) || clj_is_fixnum(form) || clj_is_double(form) || clj_is_char(form) || clj_is_string(form)) return clj_retain(form);
	clj_value pair[2] = {clj_symbol_from_cstr("quote"), clj_retain(form)};
	return list_owning(pair, 2);
}

// One collection being rebuilt: its items, and the concat arguments produced for them so far.
typedef struct {
	clj_value  form;
	clj_value *items; // borrowed
	clj_value *args;  // owned
	size_t     n, i;
} sq_frame;

typedef struct {
	sq_frame *items;
	size_t    n, cap;
} sq_stack;

static void sq_push(sq_stack *s, clj_value form) {
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 16;
		s->items = realloc(s->items, s->cap * sizeof *s->items);
		if (!s->items) clj_fatal("out of memory");
	}
	sq_frame *f = &s->items[s->n++];
	f->form = form;
	f->items = coll_items(form, &f->n);
	f->args = calloc(f->n + 1, sizeof *f->args);
	if (!f->args) clj_fatal("out of memory");
	f->i = 0;
}

// Pops the frame and builds its expansion.
static clj_value sq_pop(sq_stack *s) {
	sq_frame *f = &s->items[--s->n];
	clj_value v;
	if (clj_is_vector(f->form)) v = apply_to("vector", f->args, f->n);
	else if (is_map(f->form)) v = apply_to("hash-map", f->args, f->n);
	else if (clj_is_set(f->form)) v = apply_to("hash-set", f->args, f->n);
	else v = seq_concat(f->args, f->n);
	free(f->args);
	free(f->items);
	return v;
}

// Iterative like the parser, so nesting depth is bounded by memory. ~@ only fails at the root: inside
// a collection it always has a list to splice into.
static clj_read_status sq_form(sq *q, clj_value root, clj_value *out) {
	if (is_tagged(root, "unquote")) {
		*out = clj_retain(second(root));
		return CLJ_READ_OK;
	}
	if (is_tagged(root, "unquote-splicing")) return fail(q->p, q->line, q->col, "splice not in list");
	if (!is_sq_coll(root)) {
		*out = sq_atom(q, root);
		return CLJ_READ_OK;
	}
	sq_stack stack = {0};
	sq_push(&stack, root);
	for (;;) {
		sq_frame *f = &stack.items[stack.n - 1];
		if (f->i == f->n) {
			clj_value built = sq_pop(&stack);
			if (!stack.n) {
				*out = built;
				break;
			}
			f = &stack.items[stack.n - 1];
			f->args[f->i++] = wrap_list(built);
			continue;
		}
		clj_value item = f->items[f->i];
		if (is_tagged(item, "unquote-splicing")) f->args[f->i++] = clj_retain(second(item));
		else if (is_tagged(item, "unquote")) f->args[f->i++] = wrap_list(clj_retain(second(item)));
		else if (is_sq_coll(item)) sq_push(&stack, item);
		else f->args[f->i++] = wrap_list(sq_atom(q, item));
	}
	free(stack.items);
	return CLJ_READ_OK;
}

// Consumes form. The gensym table is fresh per syntax-quote, so a nested one names its own x#.
static clj_read_status syntax_quote(parser *p, const frame *f, clj_value form, clj_value *out) {
	sq q = {p, clj_map_empty(), f->line, f->col};
	clj_read_status st = sq_form(&q, form, out);
	clj_release(q.gensyms);
	clj_release(form);
	return st;
}

// ^:kw is {:kw true}, ^Sym and ^"str" are {:tag ...}, a map is itself. Consumes v; nil with the error set otherwise.
// @ai-generated(guided)
static clj_value meta_map_of(parser *p, const frame *f, clj_value v) {
	clj_value m;
	if (is_map(v)) return v;
	if (clj_is_keyword(v)) m = clj_map_assoc(clj_map_empty(), v, CLJ_TRUE);
	else if (clj_is_symbol(v) || clj_is_string(v)) m = clj_map_assoc(clj_map_empty(), kw_tag, v);
	else {
		fail(p, f->line, f->col, "Metadata must be Symbol,Keyword,String or Map");
		m = CLJ_NIL;
	}
	clj_release(v);
	return m;
}

// Consumes v and m: v carrying m's entries over its own meta, as LispReader stacks ^a ^b x with a winning.
// @ai-generated(guided)
static clj_read_status apply_meta(parser *p, const frame *f, clj_value v, clj_value m, clj_value *out) {
	if (!clj_is_ptr(v) || !clj_type_of(v)->with_meta) {
		clj_release(v);
		clj_release(m);
		return fail(p, f->line, f->col, "Metadata can only be applied to IMetas");
	}
	clj_value existing = clj_meta(v);
	if (!clj_is_nil(existing)) {
		size_t     n;
		clj_value *entries = coll_items(m, &n);
		for (size_t i = 0; i < n; i += 2) existing = clj_map_assoc(existing, entries[i], entries[i + 1]);
		free(entries);
		clj_release(m);
		m = existing;
	}
	*out = clj_with_meta(v, m);
	clj_release(m);
	if (*out == CLJ_THROWN) {
		clj_release(clj_take_pending());
		return fail(p, f->line, f->col, "Metadata can only be applied to IMetas");
	}
	return CLJ_READ_OK;
}

// Consumes v: applies pending prefix frames (quote, deref, discard, ...), then stores it.
static clj_read_status push_value(parser *p, clj_value v) {
	while (p->nframes) {
		frame *f = &p->frames[p->nframes - 1];
		switch (f->kind) {
		case F_META: {
			if (f->meta == CLJ_UNBOUND) {
				f->meta = meta_map_of(p, f, v);
				return clj_is_nil(f->meta) ? CLJ_READ_ERROR : CLJ_READ_OK;
			}
			clj_value       tagged;
			clj_value       m = f->meta;
			f->meta = CLJ_UNBOUND;
			clj_read_status st = apply_meta(p, f, v, m, &tagged);
			if (st != CLJ_READ_OK) return st;
			v = tagged;
			break;
		}
		case F_QUOTE: v = wrap(clj_symbol_from_cstr("quote"), v); break;
		case F_DEREF: v = wrap(clj_symbol_from_cstr("clojure.core/deref"), v); break;
		case F_VAR: v = wrap(clj_symbol_from_cstr("var"), v); break;
		case F_UNQUOTE: v = wrap(clj_symbol_from_cstr("clojure.core/unquote"), v); break;
		case F_UNQUOTE_SPLICING: v = wrap(clj_symbol_from_cstr("clojure.core/unquote-splicing"), v); break;
		case F_SYNTAX_QUOTE: {
			clj_value expanded;
			clj_read_status st = syntax_quote(p, f, v, &expanded);
			p->sq_depth--;
			if (st != CLJ_READ_OK) return st;
			v = expanded;
			break;
		}
		case F_DISCARD:
			clj_release(v);
			p->nframes--;
			return CLJ_READ_OK;
		default:
			push_raw(p, v);
			return CLJ_READ_OK;
		}
		p->nframes--;
	}
	push_raw(p, v);
	return CLJ_READ_OK;
}

static clj_read_status parse_number(parser *p, const char *tok, size_t n, uint32_t line, uint32_t col) {
	size_t i = 0;
	bool neg = false;
	if (tok[0] == '+' || tok[0] == '-') {
		neg = tok[0] == '-';
		i = 1;
	}
	size_t digits = i;
	while (digits < n && is_digit((unsigned char)tok[digits])) digits++;
	if (tok[i] == '0' && digits - i == 1 && digits < n && (tok[digits] == 'x' || tok[digits] == 'X'))
		return fail(p, line, col, "Hex literals are not supported yet: %.*s", (int)n, tok);
	if (digits < n && (tok[digits] == 'r' || tok[digits] == 'R') && digits - i <= 2)
		return fail(p, line, col, "Radix literals are not supported yet: %.*s", (int)n, tok);
	if (digits < n && tok[digits] == '/') {
		size_t j = digits + 1;
		while (j < n && is_digit((unsigned char)tok[j])) j++;
		if (j == n && j > digits + 1) return fail(p, line, col, "Ratios are not supported yet: %.*s", (int)n, tok);
		return fail(p, line, col, "Invalid number: %.*s", (int)n, tok);
	}
	if (digits < n && (tok[digits] == '.' || tok[digits] == 'e' || tok[digits] == 'E')) {
		size_t j = digits;
		if (tok[j] == '.') {
			j++;
			while (j < n && is_digit((unsigned char)tok[j])) j++;
		}
		if (j < n && (tok[j] == 'e' || tok[j] == 'E')) {
			j++;
			if (j < n && (tok[j] == '+' || tok[j] == '-')) j++;
			size_t exp_start = j;
			while (j < n && is_digit((unsigned char)tok[j])) j++;
			if (j == exp_start) return fail(p, line, col, "Invalid number: %.*s", (int)n, tok);
		}
		if (j + 1 == n && tok[j] == 'M') return fail(p, line, col, "BigDecimal literals (M suffix) are not supported yet: %.*s", (int)n, tok);
		if (j != n) return fail(p, line, col, "Invalid number: %.*s", (int)n, tok);
		char *copy = malloc(n + 1);
		if (!copy) clj_fatal("out of memory");
		memcpy(copy, tok, n);
		copy[n] = '\0';
		// Locale-dependent; the runtime never calls setlocale.
		double d = strtod(copy, NULL);
		free(copy);
		return push_value(p, clj_double_new(d));
	}
	if (digits + 1 == n && tok[digits] == 'N') return fail(p, line, col, "BigInt literals (N suffix) are not supported yet: %.*s", (int)n, tok);
	if (digits != n) return fail(p, line, col, "Invalid number: %.*s", (int)n, tok);
	if (tok[i] == '0' && digits - i > 1) return fail(p, line, col, "Octal literals are not supported yet: %.*s", (int)n, tok);
	uint64_t limit = neg ? (uint64_t)1 << 62 : ((uint64_t)1 << 62) - 1;
	uint64_t v = 0;
	for (size_t j = i; j < n; j++) {
		uint64_t d = (uint64_t)(tok[j] - '0');
		if (v > (limit - d) / 10) return fail(p, line, col, "Integer out of fixnum range, bigint is not supported yet: %.*s", (int)n, tok);
		v = v * 10 + d;
	}
	return push_value(p, clj_fixnum(neg ? -(intptr_t)v : (intptr_t)v));
}

// Clojure's symbolPat: `(P/)?(/|N)` with P and N starting with a non-digit, N without slashes.
static bool valid_symbol_text(const char *s, size_t n) {
	if (n == 1 && s[0] == '/') return true;
	if (is_digit((unsigned char)s[0]) || s[0] == '/') return false;
	if (s[n - 1] == ':') return false;
	size_t last = n;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == ':' && i + 1 < n && s[i + 1] == ':') return false;
		if (s[i] == '/') last = i;
	}
	if (last == n) return true;
	if (s[last - 1] == ':') return false;
	if (last + 1 == n) return last >= 2 && s[last - 1] == '/';
	return !is_digit((unsigned char)s[last + 1]);
}

static clj_read_status parse_symbol(parser *p, const char *tok, size_t n, uint32_t line, uint32_t col) {
	bool keyword = tok[0] == ':';
	if (keyword && n > 1 && tok[1] == ':')
		return fail(p, line, col, "Auto-resolved keywords (::) need a current namespace, not supported yet: %.*s", (int)n, tok);
	const char *s = tok + keyword;
	size_t len = n - keyword;
	if (len == 0 || !valid_symbol_text(s, len)) return fail(p, line, col, "Invalid token: %.*s", (int)n, tok);
	clj_value ns = CLJ_NIL, name;
	const char *slash = memchr(s, '/', len);
	if (!slash || len == 1) {
		name = clj_string_new(s, len);
	} else {
		ns = clj_string_new(s, (size_t)(slash - s));
		name = clj_string_new(slash + 1, len - (size_t)(slash - s) - 1);
	}
	clj_value v = keyword ? clj_keyword_intern(ns, name) : clj_symbol_new(ns, name);
	clj_release(ns);
	clj_release(name);
	return push_value(p, v);
}

static clj_read_status read_token(parser *p, uint32_t line, uint32_t col) {
	clj_reader *r = p->r;
	size_t start = r->pos;
	read_token_tail(r);
	const char *tok = r->bytes + start;
	size_t n = r->pos - start;
	if (n == 3 && memcmp(tok, "nil", 3) == 0) {
		return push_value(p, CLJ_NIL);
	} else if (n == 4 && memcmp(tok, "true", 4) == 0) {
		return push_value(p, CLJ_TRUE);
	} else if (n == 5 && memcmp(tok, "false", 5) == 0) {
		return push_value(p, CLJ_FALSE);
	} else if (is_digit((unsigned char)tok[0]) || (n > 1 && (tok[0] == '+' || tok[0] == '-') && is_digit((unsigned char)tok[1]))) {
		return parse_number(p, tok, n, line, col);
	} else {
		return parse_symbol(p, tok, n, line, col);
	}
	return CLJ_READ_OK;
}

// Exactly `count` hex digits; -1 when fewer are present.
static int64_t read_hex(clj_reader *r, int count) {
	int64_t v = 0;
	for (int i = 0; i < count; i++) {
		if (at_eof(r) || hex_val(peek(r)) < 0) return -1;
		v = v * 16 + hex_val(peek(r));
		advance(r);
	}
	return v;
}

static clj_read_status read_string(parser *p, uint32_t line, uint32_t col) {
	clj_reader *r = p->r;
	advance(r);
	buf b = {0};
	clj_read_status st = CLJ_READ_OK;
	for (;;) {
		if (at_eof(r)) {
			st = fail(p, line, col, "EOF while reading string");
			break;
		}
		unsigned char c = peek(r);
		if (c == '"') {
			advance(r);
			break;
		}
		if (c != '\\') {
			buf_put(&b, r->bytes + r->pos, 1);
			advance(r);
			continue;
		}
		uint32_t eline = r->line, ecol = r->col;
		advance(r);
		if (at_eof(r)) {
			st = fail(p, line, col, "EOF while reading string");
			break;
		}
		unsigned char e = peek(r);
		advance(r);
		char simple = 0;
		switch (e) {
		case 't': simple = '\t'; break;
		case 'r': simple = '\r'; break;
		case 'n': simple = '\n'; break;
		case '\\': simple = '\\'; break;
		case '"': simple = '"'; break;
		case 'b': simple = '\b'; break;
		case 'f': simple = '\f'; break;
		}
		if (simple) {
			buf_put(&b, &simple, 1);
		} else if (e == 'u') {
			int64_t cp = read_hex(r, 4);
			if (cp < 0) {
				st = fail(p, eline, ecol, "Invalid unicode escape: expected 4 hex digits");
				break;
			}
			// UTF-16 surrogate pairs written as two escapes form one scalar, as in a Java string.
			if (cp >= 0xD800 && cp <= 0xDBFF && r->pos + 1 < r->len && r->bytes[r->pos] == '\\' && r->bytes[r->pos + 1] == 'u') {
				size_t save_pos = r->pos;
				uint32_t save_line = r->line, save_col = r->col;
				advance(r);
				advance(r);
				int64_t low = read_hex(r, 4);
				if (low >= 0xDC00 && low <= 0xDFFF) {
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
				} else {
					r->pos = save_pos;
					r->line = save_line;
					r->col = save_col;
				}
			}
			if (cp >= 0xD800 && cp <= 0xDFFF) {
				st = fail(p, eline, ecol, "Invalid unicode escape: lone surrogate \\u%04llx", (long long)cp);
				break;
			}
			buf_put_utf8(&b, (uint32_t)cp);
		} else if (e >= '0' && e <= '7') {
			uint32_t v = e - '0';
			for (int i = 0; i < 2 && !at_eof(r) && peek(r) >= '0' && peek(r) <= '7'; i++) {
				v = v * 8 + (peek(r) - '0');
				advance(r);
			}
			if (v > 0377) {
				st = fail(p, eline, ecol, "Octal escape sequence must be in range [0, 377]");
				break;
			}
			buf_put_utf8(&b, v);
		} else {
			st = fail(p, eline, ecol, "Unsupported escape character: \\%c", e);
			break;
		}
	}
	if (st == CLJ_READ_OK) st = push_value(p, clj_string_new(b.data, b.len));
	free(b.data);
	return st;
}

static clj_read_status read_char(parser *p, uint32_t line, uint32_t col) {
	clj_reader *r = p->r;
	advance(r);
	if (at_eof(r)) return fail(p, line, col, "EOF while reading character");
	uint32_t first;
	size_t first_len = decode_utf8((const unsigned char *)r->bytes + r->pos, r->len - r->pos, &first);
	if (!first_len) return fail(p, line, col, "Invalid UTF-8 in character literal");
	size_t start = r->pos;
	for (size_t i = 0; i < first_len; i++) advance(r);
	read_token_tail(r);
	const char *tok = r->bytes + start;
	size_t n = r->pos - start;
	uint32_t cp;
	if (n == first_len) {
		cp = first;
	} else if (n == 7 && memcmp(tok, "newline", 7) == 0) {
		cp = '\n';
	} else if (n == 5 && memcmp(tok, "space", 5) == 0) {
		cp = ' ';
	} else if (n == 3 && memcmp(tok, "tab", 3) == 0) {
		cp = '\t';
	} else if (n == 9 && memcmp(tok, "backspace", 9) == 0) {
		cp = '\b';
	} else if (n == 8 && memcmp(tok, "formfeed", 8) == 0) {
		cp = '\f';
	} else if (n == 6 && memcmp(tok, "return", 6) == 0) {
		cp = '\r';
	} else if (tok[0] == 'u' && n == 5) {
		int64_t v = 0;
		for (size_t i = 1; i < 5; i++) {
			int h = hex_val((unsigned char)tok[i]);
			if (h < 0) return fail(p, line, col, "Unsupported character: \\%.*s", (int)n, tok);
			v = v * 16 + h;
		}
		if (v >= 0xD800 && v <= 0xDFFF) return fail(p, line, col, "Invalid character constant: \\%.*s", (int)n, tok);
		cp = (uint32_t)v;
	} else if (tok[0] == 'o' && n >= 2 && n <= 4) {
		uint32_t v = 0;
		for (size_t i = 1; i < n; i++) {
			if (tok[i] < '0' || tok[i] > '7') return fail(p, line, col, "Unsupported character: \\%.*s", (int)n, tok);
			v = v * 8 + (uint32_t)(tok[i] - '0');
		}
		if (v > 0377) return fail(p, line, col, "Octal escape sequence must be in range [0, 377]");
		cp = v;
	} else {
		return fail(p, line, col, "Unsupported character: \\%.*s", (int)n, tok);
	}
	return push_value(p, clj_char(cp));
}

static clj_read_status read_symbolic_value(parser *p, uint32_t line, uint32_t col) {
	clj_reader *r = p->r;
	size_t start = r->pos;
	read_token_tail(r);
	const char *tok = r->bytes + start;
	size_t n = r->pos - start;
	double d;
	if (n == 3 && memcmp(tok, "Inf", 3) == 0) d = __builtin_inf();
	else if (n == 4 && memcmp(tok, "-Inf", 4) == 0) d = -__builtin_inf();
	else if (n == 3 && memcmp(tok, "NaN", 3) == 0) d = __builtin_nan("");
	else return fail(p, line, col, "Unknown symbolic value: ##%.*s", (int)n, tok);
	return push_value(p, clj_double_new(d));
}

static clj_read_status read_dispatch(parser *p, uint32_t line, uint32_t col) {
	clj_reader *r = p->r;
	advance(r);
	if (at_eof(r)) return fail(p, line, col, "EOF while reading dispatch character");
	unsigned char c = peek(r);
	switch (c) {
	case '_':
		advance(r);
		push_frame(p, F_DISCARD, line, col);
		return CLJ_READ_OK;
	case '#':
		advance(r);
		return read_symbolic_value(p, line, col);
	case '!':
		skip_line(r);
		return CLJ_READ_OK;
	case '{':
		advance(r);
		push_frame(p, F_SET, line, col);
		return CLJ_READ_OK;
	case '(': return fail(p, line, col, "Anonymous function literals are not supported yet");
	case '"': return fail(p, line, col, "Regex literals are not supported yet");
	case '\'':
		advance(r);
		push_frame(p, F_VAR, line, col);
		return CLJ_READ_OK;
	case ':': return fail(p, line, col, "Namespaced map literals are not supported yet");
	case '?': return fail(p, line, col, "Reader conditionals are not supported yet");
	case '=': return fail(p, line, col, "Read-eval is not supported yet");
	case '^':
		advance(r);
		push_frame(p, F_META, line, col);
		return CLJ_READ_OK;
	case '<': return fail(p, line, col, "Unreadable form");
	default: return fail(p, line, col, "Tagged literals are not supported yet");
	}
}

static clj_read_status close_map(parser *p, const frame *f, clj_value *out) {
	size_t n = p->nvals - f->start;
	if (n % 2) return fail(p, f->line, f->col, "Map literal must contain an even number of forms");
	clj_value m = clj_map_empty();
	for (size_t i = f->start; i < p->nvals; i += 2) {
		clj_value key = p->vals[i];
		if (clj_map_contains(m, key)) {
			clj_value text = clj_pr_str(key);
			clj_read_status st = fail(p, f->line, f->col, "Duplicate key: %s", clj_string_bytes(text));
			clj_release(text);
			clj_release(m);
			return st;
		}
		m = clj_map_assoc(m, key, p->vals[i + 1]);
	}
	*out = m;
	return CLJ_READ_OK;
}

static clj_read_status close_set(parser *p, const frame *f, clj_value *out) {
	clj_value s = clj_set_empty();
	for (size_t i = f->start; i < p->nvals; i++) {
		clj_value item = p->vals[i];
		if (clj_set_contains(s, item)) {
			clj_value text = clj_pr_str(item);
			clj_read_status st = fail(p, f->line, f->col, "Duplicate key: %s", clj_string_bytes(text));
			clj_release(text);
			clj_release(s);
			return st;
		}
		s = clj_set_conj(s, item);
	}
	*out = s;
	return CLJ_READ_OK;
}

static clj_read_status close_collection(parser *p, unsigned char closer, uint32_t line, uint32_t col) {
	frame_kind expected = closer == ')' ? F_LIST : closer == ']' ? F_VECTOR : F_MAP;
	frame_kind top = p->nframes ? p->frames[p->nframes - 1].kind : F_QUOTE;
	if (top != expected && !(expected == F_MAP && top == F_SET)) return fail(p, line, col, "Unmatched delimiter: %c", closer);
	frame f = p->frames[--p->nframes];
	size_t n = p->nvals - f.start;
	const clj_value *items = n ? p->vals + f.start : NULL;
	clj_value v;
	if (f.kind == F_LIST && n) {
		// Clojure attaches the opening paren's position to lists only; the head cell carries it.
		clj_value pos = clj_map_assoc(clj_map_assoc(clj_map_empty(), kw_line, clj_fixnum(f.line)), kw_column, clj_fixnum(f.col));
		clj_value tail = clj_list_from_array(items + 1, n - 1);
		v = clj_cons_new_meta(items[0], tail, pos);
		clj_release(tail);
		clj_release(pos);
	} else if (f.kind == F_LIST) {
		v = clj_list_empty();
	} else if (f.kind == F_VECTOR) {
		if (n > UINT32_MAX) return fail(p, f.line, f.col, "Vector literal too long");
		v = clj_vector_from_array(items, (uint32_t)n);
	} else {
		clj_read_status st = f.kind == F_SET ? close_set(p, &f, &v) : close_map(p, &f, &v);
		if (st != CLJ_READ_OK) return st;
	}
	for (size_t i = f.start; i < p->nvals; i++) clj_release(p->vals[i]);
	p->nvals = f.start;
	return push_value(p, v);
}

static clj_read_status read_form(parser *p) {
	clj_reader *r = p->r;
	uint32_t line = r->line, col = r->col;
	unsigned char c = peek(r);
	switch (c) {
	case '(':
	case '[':
	case '{':
		advance(r);
		push_frame(p, c == '(' ? F_LIST : c == '[' ? F_VECTOR : F_MAP, line, col);
		return CLJ_READ_OK;
	case ')':
	case ']':
	case '}':
		advance(r);
		return close_collection(p, c, line, col);
	case '\'':
		advance(r);
		push_frame(p, F_QUOTE, line, col);
		return CLJ_READ_OK;
	case '@':
		advance(r);
		push_frame(p, F_DEREF, line, col);
		return CLJ_READ_OK;
	case '"': return read_string(p, line, col);
	case '\\': return read_char(p, line, col);
	case '#': return read_dispatch(p, line, col);
	case '^':
		advance(r);
		push_frame(p, F_META, line, col);
		return CLJ_READ_OK;
	case '`':
		advance(r);
		push_frame(p, F_SYNTAX_QUOTE, line, col);
		p->sq_depth++;
		return CLJ_READ_OK;
	case '~': {
		advance(r);
		bool splicing = !at_eof(r) && peek(r) == '@';
		if (splicing) advance(r);
		if (!p->sq_depth) return fail(p, line, col, "%s outside syntax-quote", splicing ? "Unquote-splicing" : "Unquote");
		push_frame(p, splicing ? F_UNQUOTE_SPLICING : F_UNQUOTE, line, col);
		return CLJ_READ_OK;
	}
	default: return read_token(p, line, col);
	}
}

void clj_reader_init(clj_reader *r, const char *bytes, size_t len) {
	memset(r, 0, sizeof *r);
	r->bytes = bytes;
	r->len = len;
	r->line = r->col = 1;
}

const char *clj_reader_message(const clj_reader *r) { return r->message; }

clj_read_status clj_read(clj_reader *r, clj_value *out) {
	pthread_once(&keywords_once, intern_keywords);
	parser p = {.r = r};
	clj_read_status st;
	for (;;) {
		skip_blank(r);
		if (at_eof(r)) {
			if (p.nframes) {
				const frame *f = &p.frames[p.nframes - 1];
				st = fail(&p, f->line, f->col, "EOF while reading");
			} else {
				st = CLJ_READ_EOF;
			}
			break;
		}
		if (!p.nframes) {
			r->form_line = r->line;
			r->form_col = r->col;
		}
		st = read_form(&p);
		if (st != CLJ_READ_OK) break;
		if (!p.nframes && p.nvals) {
			*out = p.vals[0];
			p.nvals = 0;
			break;
		}
	}
	for (size_t i = 0; i < p.nvals; i++) clj_release(p.vals[i]);
	for (size_t i = 0; i < p.nframes; i++) {
		if (p.frames[i].meta != CLJ_UNBOUND) clj_release(p.frames[i].meta);
	}
	free(p.vals);
	free(p.frames);
	return st;
}
