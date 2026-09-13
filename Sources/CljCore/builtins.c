// @ai-generated(guided)
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/core.h"
#include "clj/fn.h"
#include "clj/runtime.h"

// ---- numbers

typedef struct {
	bool     is_double;
	intptr_t i;
	double   d;
} num;

static bool to_num(clj_value v, num *out) {
	if (clj_is_fixnum(v)) {
		*out = (num){false, clj_fixnum_val(v), 0};
		return true;
	}
	if (clj_is_double(v)) {
		*out = (num){true, 0, clj_double_val(v)};
		return true;
	}
	return false;
}

static double as_double(const num *n) { return n->is_double ? n->d : (double)n->i; }

static clj_value not_a_number(clj_value v) { return clj_throw_msg("%s cannot be cast to a number", clj_type_name(v)); }

static clj_value from_num(const num *n) {
	if (n->is_double) return clj_double_new(n->d);
	if (n->i > CLJ_FIXNUM_MAX || n->i < CLJ_FIXNUM_MIN) return clj_throw_msg("integer overflow");
	return clj_fixnum(n->i);
}

typedef enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV } arith_op;

static clj_value arith_step(num *acc, clj_value v, arith_op op) {
	num b;
	if (!to_num(v, &b)) return not_a_number(v);
	if (acc->is_double || b.is_double) {
		double x = as_double(acc), y = as_double(&b);
		acc->is_double = true;
		switch (op) {
		case OP_ADD: acc->d = x + y; break;
		case OP_SUB: acc->d = x - y; break;
		case OP_MUL: acc->d = x * y; break;
		case OP_DIV: acc->d = x / y; break;
		}
		return CLJ_NIL;
	}
	bool overflow = false;
	switch (op) {
	case OP_ADD: overflow = __builtin_add_overflow(acc->i, b.i, &acc->i); break;
	case OP_SUB: overflow = __builtin_sub_overflow(acc->i, b.i, &acc->i); break;
	case OP_MUL: overflow = __builtin_mul_overflow(acc->i, b.i, &acc->i); break;
	case OP_DIV:
		if (b.i == 0) return clj_throw_msg("Divide by zero");
		// An inexact quotient is a double until ratios exist (NOTES.md).
		if (acc->i % b.i != 0) {
			acc->is_double = true;
			acc->d = (double)acc->i / (double)b.i;
		} else {
			overflow = (acc->i == INTPTR_MIN && b.i == -1);
			if (!overflow) acc->i /= b.i;
		}
		break;
	}
	return overflow ? clj_throw_msg("integer overflow") : CLJ_NIL;
}

static clj_value arith(const clj_value *args, size_t n, arith_op op, intptr_t identity) {
	num    acc = {false, identity, 0};
	size_t start = 0;
	if (n > 0 && (op == OP_ADD || op == OP_MUL || n > 1)) {
		if (!to_num(args[0], &acc)) return not_a_number(args[0]);
		start = 1;
	}
	for (size_t i = start; i < n; i++) {
		if (arith_step(&acc, args[i], op) == CLJ_THROWN) return CLJ_THROWN;
	}
	return from_num(&acc);
}

static clj_value b_add(const clj_value *args, size_t n) { return arith(args, n, OP_ADD, 0); }
static clj_value b_sub(const clj_value *args, size_t n) { return arith(args, n, OP_SUB, 0); }
static clj_value b_mul(const clj_value *args, size_t n) { return arith(args, n, OP_MUL, 1); }
static clj_value b_div(const clj_value *args, size_t n) { return arith(args, n, OP_DIV, 1); }

static clj_value b_inc(const clj_value *args, size_t n) {
	(void)n;
	clj_value one = clj_fixnum(1);
	clj_value pair[2] = {args[0], one};
	return arith(pair, 2, OP_ADD, 0);
}

static clj_value b_dec(const clj_value *args, size_t n) {
	(void)n;
	clj_value pair[2] = {args[0], clj_fixnum(1)};
	return arith(pair, 2, OP_SUB, 0);
}

typedef enum { CMP_LT, CMP_LE, CMP_GT, CMP_GE } cmp_op;

static clj_value compare(const clj_value *args, size_t n, cmp_op op) {
	num prev;
	if (!to_num(args[0], &prev)) return not_a_number(args[0]);
	for (size_t i = 1; i < n; i++) {
		num cur;
		if (!to_num(args[i], &cur)) return not_a_number(args[i]);
		bool ok;
		if (prev.is_double || cur.is_double) {
			double a = as_double(&prev), b = as_double(&cur);
			ok = op == CMP_LT ? a < b : op == CMP_LE ? a <= b : op == CMP_GT ? a > b : a >= b;
		} else {
			intptr_t a = prev.i, b = cur.i;
			ok = op == CMP_LT ? a < b : op == CMP_LE ? a <= b : op == CMP_GT ? a > b : a >= b;
		}
		if (!ok) return CLJ_FALSE;
		prev = cur;
	}
	return CLJ_TRUE;
}

static clj_value b_lt(const clj_value *args, size_t n) { return compare(args, n, CMP_LT); }
static clj_value b_le(const clj_value *args, size_t n) { return compare(args, n, CMP_LE); }
static clj_value b_gt(const clj_value *args, size_t n) { return compare(args, n, CMP_GT); }
static clj_value b_ge(const clj_value *args, size_t n) { return compare(args, n, CMP_GE); }

static clj_value b_eq(const clj_value *args, size_t n) {
	for (size_t i = 1; i < n; i++) {
		if (!clj_equals(args[0], args[i])) return CLJ_FALSE;
	}
	return CLJ_TRUE;
}

static clj_value b_neq(const clj_value *args, size_t n) { return clj_bool(b_eq(args, n) == CLJ_FALSE); }

static clj_value int_arg(clj_value v, intptr_t *out) {
	if (!clj_is_fixnum(v)) {
		clj_value text = clj_pr_str(v);
		clj_value r = clj_throw_msg("Argument must be an integer: %s", clj_string_bytes(text));
		clj_release(text);
		return r;
	}
	*out = clj_fixnum_val(v);
	return CLJ_NIL;
}

static clj_value b_zero(const clj_value *args, size_t n) {
	(void)n;
	num v;
	if (!to_num(args[0], &v)) return not_a_number(args[0]);
	return clj_bool(as_double(&v) == 0);
}

static clj_value b_pos(const clj_value *args, size_t n) {
	(void)n;
	num v;
	if (!to_num(args[0], &v)) return not_a_number(args[0]);
	return clj_bool(as_double(&v) > 0);
}

static clj_value b_neg(const clj_value *args, size_t n) {
	(void)n;
	num v;
	if (!to_num(args[0], &v)) return not_a_number(args[0]);
	return clj_bool(as_double(&v) < 0);
}

static clj_value b_even(const clj_value *args, size_t n) {
	(void)n;
	intptr_t i;
	if (int_arg(args[0], &i) == CLJ_THROWN) return CLJ_THROWN;
	return clj_bool(i % 2 == 0);
}

static clj_value b_odd(const clj_value *args, size_t n) {
	(void)n;
	intptr_t i;
	if (int_arg(args[0], &i) == CLJ_THROWN) return CLJ_THROWN;
	return clj_bool(i % 2 != 0);
}

// ---- predicates

static bool is_map(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_map_type; }

#define PREDICATE(name, test) \
	static clj_value name(const clj_value *args, size_t n) { \
		(void)n; \
		return clj_bool(test(args[0])); \
	}

static bool is_number(clj_value v) { return clj_is_fixnum(v) || clj_is_double(v); }
static bool is_not(clj_value v) { return !clj_truthy(v); }

PREDICATE(b_not, is_not)
PREDICATE(b_nil, clj_is_nil)
PREDICATE(b_number, is_number)
PREDICATE(b_string, clj_is_string)
PREDICATE(b_keyword, clj_is_keyword)
PREDICATE(b_symbol, clj_is_symbol)
PREDICATE(b_vector_p, clj_is_vector)
PREDICATE(b_map, is_map)
PREDICATE(b_list_p, clj_is_list)
PREDICATE(b_fn, clj_is_fn)

// ---- collections

static clj_value not_a_seq(clj_value v) { return clj_throw_msg("Don't know how to create ISeq from: %s", clj_type_name(v)); }

static clj_value b_get(const clj_value *args, size_t n) { return clj_get(args[0], args[1], n == 3 ? args[2] : CLJ_NIL); }

static clj_value b_nth(const clj_value *args, size_t n) { return clj_nth(args[0], args[1], n == 3, n == 3 ? args[2] : CLJ_NIL); }

static clj_value b_assoc(const clj_value *args, size_t n) {
	if (n % 2 == 0) return clj_throw_msg("assoc expects even number of arguments after map/vector, found odd number");
	clj_value coll = args[0];
	if (clj_is_nil(coll)) coll = clj_map_empty();
	if (is_map(coll)) {
		coll = clj_retain(coll);
		for (size_t i = 1; i < n; i += 2) coll = clj_map_assoc(coll, args[i], args[i + 1]);
		return coll;
	}
	if (clj_is_vector(coll)) {
		coll = clj_retain(coll);
		for (size_t i = 1; i < n; i += 2) {
			if (!clj_is_fixnum(args[i])) {
				clj_release(coll);
				return clj_throw_msg("Key must be integer");
			}
			intptr_t idx = clj_fixnum_val(args[i]);
			if (idx < 0 || (uintptr_t)idx > clj_vector_count(coll)) {
				uint32_t count = clj_vector_count(coll);
				clj_release(coll);
				return clj_throw_msg("Index %lld out of bounds for length %u", (long long)idx, count);
			}
			coll = clj_vector_assoc(coll, (uint32_t)idx, args[i + 1]);
		}
		return coll;
	}
	return clj_throw_msg("assoc not supported on this type: %s", clj_type_name(coll));
}

static clj_value b_dissoc(const clj_value *args, size_t n) {
	clj_value coll = args[0];
	if (clj_is_nil(coll)) return CLJ_NIL;
	if (!is_map(coll)) return clj_throw_msg("dissoc not supported on this type: %s", clj_type_name(coll));
	coll = clj_retain(coll);
	for (size_t i = 1; i < n; i++) coll = clj_map_dissoc(coll, args[i]);
	return coll;
}

static clj_value b_contains(const clj_value *args, size_t n) {
	(void)n;
	clj_value coll = args[0], key = args[1];
	if (clj_is_nil(coll)) return CLJ_FALSE;
	if (is_map(coll)) return clj_bool(clj_map_contains(coll, key));
	if (clj_is_vector(coll)) return clj_bool(clj_is_fixnum(key) && clj_fixnum_val(key) >= 0 && (uintptr_t)clj_fixnum_val(key) < clj_vector_count(coll));
	return clj_throw_msg("contains? not supported on type: %s", clj_type_name(coll));
}

static size_t string_count(clj_value s) {
	const unsigned char *p = (const unsigned char *)clj_string_bytes(s);
	size_t               len = clj_string_len(s), count = 0;
	for (size_t i = 0; i < len; i++) count += (p[i] & 0xC0) != 0x80;
	return count;
}

static clj_value b_count(const clj_value *args, size_t n) {
	(void)n;
	clj_value coll = args[0];
	if (clj_is_nil(coll)) return clj_fixnum(0);
	if (clj_is_vector(coll)) return clj_fixnum(clj_vector_count(coll));
	if (is_map(coll)) return clj_fixnum(clj_map_count(coll));
	if (clj_is_list(coll)) return clj_fixnum((intptr_t)clj_list_count(coll));
	if (clj_is_string(coll)) return clj_fixnum((intptr_t)string_count(coll));
	return clj_throw_msg("count not supported on this type: %s", clj_type_name(coll));
}

typedef struct {
	clj_value *entries;
	size_t     n;
} collect;

static bool collect_entry(clj_value key, clj_value val, void *ctx) {
	collect *c = ctx;
	c->entries[c->n++] = key;
	c->entries[c->n++] = val;
	return true;
}

static clj_value b_conj(const clj_value *args, size_t n) {
	if (n == 0) return clj_vector_empty();
	clj_value coll = clj_retain(args[0]);
	if (clj_is_nil(coll)) coll = clj_list_empty();
	if (clj_is_vector(coll)) {
		for (size_t i = 1; i < n; i++) coll = clj_vector_conj(coll, args[i]);
		return coll;
	}
	if (clj_is_list(coll)) {
		for (size_t i = 1; i < n; i++) {
			clj_value c = clj_cons_new(args[i], coll);
			clj_release(coll);
			coll = c;
		}
		return coll;
	}
	if (is_map(coll)) {
		for (size_t i = 1; i < n; i++) {
			clj_value item = args[i];
			if (clj_is_nil(item)) continue;
			if (clj_is_vector(item) && clj_vector_count(item) == 2) {
				coll = clj_map_assoc(coll, clj_vector_nth(item, 0), clj_vector_nth(item, 1));
			} else if (is_map(item)) {
				collect c = {malloc((2 * (size_t)clj_map_count(item) + 1) * sizeof(clj_value)), 0};
				if (!c.entries) clj_fatal("out of memory");
				clj_map_each(item, collect_entry, &c);
				for (size_t j = 0; j < c.n; j += 2) coll = clj_map_assoc(coll, c.entries[j], c.entries[j + 1]);
				free(c.entries);
			} else {
				clj_release(coll);
				return clj_throw_msg("Vector arg to map conj must be a pair");
			}
		}
		return coll;
	}
	clj_value r = clj_throw_msg("conj not supported on this type: %s", clj_type_name(coll));
	clj_release(coll);
	return r;
}

static clj_value b_first(const clj_value *args, size_t n) {
	(void)n;
	clj_value coll = args[0];
	if (clj_is_nil(coll)) return CLJ_NIL;
	if (!clj_is_list(coll) && !clj_is_vector(coll)) return not_a_seq(coll);
	clj_seq_iter it = clj_seq_iter_start(coll);
	clj_value    item;
	return clj_seq_iter_next(&it, &item) ? clj_retain(item) : CLJ_NIL;
}

// The tail of a cons, or a fresh list of a vector's remaining items; empty is () for rest and nil for next.
static clj_value rest_of(clj_value coll, bool next) {
	clj_value r;
	if (clj_is_nil(coll) || clj_is_empty_list(coll)) {
		r = clj_list_empty();
	} else if (clj_is_list(coll)) {
		r = clj_retain(clj_cons_of(coll)->rest);
		if (clj_is_nil(r)) r = clj_list_empty();
	} else if (clj_is_vector(coll)) {
		uint32_t   count = clj_vector_count(coll);
		clj_value *items = malloc((count ? count : 1) * sizeof *items);
		if (!items) clj_fatal("out of memory");
		for (uint32_t i = 1; i < count; i++) items[i - 1] = clj_vector_nth(coll, i);
		r = clj_list_from_array(items, count ? count - 1 : 0);
		free(items);
	} else {
		return not_a_seq(coll);
	}
	if (next) {
		clj_seq_iter it = clj_seq_iter_start(r);
		clj_value    item;
		if (!clj_seq_iter_next(&it, &item)) {
			clj_release(r);
			return CLJ_NIL;
		}
	}
	return r;
}

static clj_value b_rest(const clj_value *args, size_t n) {
	(void)n;
	return rest_of(args[0], false);
}

static clj_value b_next(const clj_value *args, size_t n) {
	(void)n;
	return rest_of(args[0], true);
}

static clj_value b_cons(const clj_value *args, size_t n) {
	(void)n;
	clj_value seq = args[1];
	if (!clj_is_nil(seq) && !clj_is_list(seq) && !clj_is_vector(seq)) return not_a_seq(seq);
	return clj_cons_new(args[0], seq);
}

static clj_value b_list(const clj_value *args, size_t n) { return clj_list_from_array(args, n); }

static clj_value b_vector(const clj_value *args, size_t n) {
	if (n > UINT32_MAX) return clj_throw_msg("vector too long");
	return clj_vector_from_array(args, (uint32_t)n);
}

static clj_value b_hash_map(const clj_value *args, size_t n) {
	if (n % 2) {
		clj_value text = clj_pr_str(args[n - 1]);
		clj_value r = clj_throw_msg("No value supplied for key: %s", clj_string_bytes(text));
		clj_release(text);
		return r;
	}
	clj_value m = clj_map_empty();
	for (size_t i = 0; i < n; i += 2) m = clj_map_assoc(m, args[i], args[i + 1]);
	return m;
}

// ---- strings and output

typedef struct {
	char  *data;
	size_t len, cap;
} buf;

static void buf_put(buf *b, const char *s, size_t n) {
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

// Clojure `str`: strings raw, nil empty, everything else as pr-str (a char as its text).
static void put_str(buf *b, clj_value v) {
	if (clj_is_nil(v)) return;
	if (clj_is_string(v)) {
		buf_put(b, clj_string_bytes(v), clj_string_len(v));
		return;
	}
	if (clj_is_char(v)) {
		uint32_t cp = clj_char_val(v);
		char     u[4];
		size_t   n;
		if (cp < 0x80) {
			u[0] = (char)cp;
			n = 1;
		} else if (cp < 0x800) {
			u[0] = (char)(0xC0 | (cp >> 6));
			u[1] = (char)(0x80 | (cp & 0x3F));
			n = 2;
		} else if (cp < 0x10000) {
			u[0] = (char)(0xE0 | (cp >> 12));
			u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
			u[2] = (char)(0x80 | (cp & 0x3F));
			n = 3;
		} else {
			u[0] = (char)(0xF0 | (cp >> 18));
			u[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
			u[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
			u[3] = (char)(0x80 | (cp & 0x3F));
			n = 4;
		}
		buf_put(b, u, n);
		return;
	}
	clj_value s = clj_pr_str(v);
	buf_put(b, clj_string_bytes(s), clj_string_len(s));
	clj_release(s);
}

static void put_print(buf *b, clj_value v) {
	clj_value s = clj_print_str(v);
	buf_put(b, clj_string_bytes(s), clj_string_len(s));
	clj_release(s);
}

static void put_pr(buf *b, clj_value v) {
	clj_value s = clj_pr_str(v);
	buf_put(b, clj_string_bytes(s), clj_string_len(s));
	clj_release(s);
}

static void join(buf *b, const clj_value *args, size_t n, void (*put)(buf *, clj_value), bool spaces) {
	for (size_t i = 0; i < n; i++) {
		if (spaces && i) buf_put(b, " ", 1);
		put(b, args[i]);
	}
}

static clj_value b_str(const clj_value *args, size_t n) {
	buf b = {0};
	join(&b, args, n, put_str, false);
	clj_value s = clj_string_new(b.data, b.len);
	free(b.data);
	return s;
}

static clj_value b_pr_str(const clj_value *args, size_t n) {
	buf b = {0};
	join(&b, args, n, put_pr, true);
	clj_value s = clj_string_new(b.data, b.len);
	free(b.data);
	return s;
}

static clj_value print_line(const clj_value *args, size_t n, void (*put)(buf *, clj_value), bool newline) {
	buf b = {0};
	join(&b, args, n, put, true);
	if (newline) buf_put(&b, "\n", 1);
	clj_output(b.data ? b.data : "", b.len);
	free(b.data);
	return CLJ_NIL;
}

static clj_value b_println(const clj_value *args, size_t n) { return print_line(args, n, put_print, true); }
static clj_value b_print(const clj_value *args, size_t n) { return print_line(args, n, put_print, false); }
static clj_value b_prn(const clj_value *args, size_t n) { return print_line(args, n, put_pr, true); }
static clj_value b_pr(const clj_value *args, size_t n) { return print_line(args, n, put_pr, false); }

// ---- functions

static clj_value b_identity(const clj_value *args, size_t n) {
	(void)n;
	return clj_retain(args[0]);
}

static clj_value b_apply(const clj_value *args, size_t n) { return clj_apply(args[0], args + 1, n - 1); }

// ---- registration

typedef struct {
	const char   *name;
	clj_native_fn fn;
	uint32_t      min, max;
} entry;

#define ANY CLJ_ARITY_ANY

static const entry entries[] = {
	{"+", b_add, 0, ANY},          {"-", b_sub, 1, ANY},         {"*", b_mul, 0, ANY},          {"/", b_div, 1, ANY},
	{"<", b_lt, 1, ANY},           {"<=", b_le, 1, ANY},         {">", b_gt, 1, ANY},           {">=", b_ge, 1, ANY},
	{"=", b_eq, 1, ANY},           {"not=", b_neq, 1, ANY},      {"inc", b_inc, 1, 1},          {"dec", b_dec, 1, 1},
	{"not", b_not, 1, 1},          {"nil?", b_nil, 1, 1},        {"zero?", b_zero, 1, 1},       {"pos?", b_pos, 1, 1},
	{"neg?", b_neg, 1, 1},         {"even?", b_even, 1, 1},      {"odd?", b_odd, 1, 1},         {"number?", b_number, 1, 1},
	{"string?", b_string, 1, 1},   {"keyword?", b_keyword, 1, 1}, {"symbol?", b_symbol, 1, 1},  {"vector?", b_vector_p, 1, 1},
	{"map?", b_map, 1, 1},         {"list?", b_list_p, 1, 1},      {"fn?", b_fn, 1, 1},           {"get", b_get, 2, 3},
	{"assoc", b_assoc, 3, ANY},    {"dissoc", b_dissoc, 1, ANY}, {"contains?", b_contains, 2, 2}, {"count", b_count, 1, 1},
	{"conj", b_conj, 0, ANY},      {"nth", b_nth, 2, 3},         {"first", b_first, 1, 1},      {"rest", b_rest, 1, 1},
	{"next", b_next, 1, 1},        {"cons", b_cons, 2, 2},       {"list", b_list, 0, ANY},      {"vector", b_vector, 0, ANY},
	{"hash-map", b_hash_map, 0, ANY}, {"str", b_str, 0, ANY},    {"pr-str", b_pr_str, 0, ANY},  {"pr", b_pr, 0, ANY},
	{"prn", b_prn, 0, ANY},        {"print", b_print, 0, ANY},   {"println", b_println, 0, ANY}, {"identity", b_identity, 1, 1},
	{"apply", b_apply, 2, ANY},
};

void clj_builtins_install(void) {
	clj_value core = clj_ns_core();
	clj_value core_name = clj_symbol_name(clj_ns_name(core));
	for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) {
		const entry *e = &entries[i];
		clj_value    name = clj_string_from_cstr(e->name);
		clj_value    sym = clj_symbol_new(CLJ_NIL, name);
		clj_value    qualified = clj_symbol_new(core_name, name);
		clj_value    fn = clj_fn_native(qualified, e->fn, e->min, e->max);
		clj_var_bind_root(clj_ns_intern(core, sym), fn);
		clj_release(fn);
		clj_release(qualified);
		clj_release(sym);
		clj_release(name);
	}
}
