// @ai-generated(solo)
#include <stdlib.h>
#include <string.h>

#include "clj/compare.h"
#include "clj/core.h"

// Bytes, which is code point order for UTF-8; the JVM compares UTF-16 units instead (docs/jvm-differences.md).
static int compare_strings(clj_value a, clj_value b) {
	uint32_t la = clj_string_len(a), lb = clj_string_len(b);
	uint32_t n = la < lb ? la : lb;
	int      c = memcmp(clj_string_bytes(a), clj_string_bytes(b), n);
	if (c) return c < 0 ? -1 : 1;
	return la < lb ? -1 : la > lb ? 1 : 0;
}

// Symbol.compareTo: an unqualified name before any qualified one, then the namespaces, then the names.
static int compare_named(clj_value ans, clj_value aname, clj_value bns, clj_value bname) {
	if (clj_is_nil(ans) != clj_is_nil(bns)) return clj_is_nil(ans) ? -1 : 1;
	if (!clj_is_nil(ans)) {
		int c = compare_strings(ans, bns);
		if (c) return c;
	}
	return compare_strings(aname, bname);
}

static clj_value cast_error(clj_value v, const char *to) { return clj_throw_msg("%s cannot be cast to %s", clj_type_name(v), to); }

static clj_value compare_vectors(clj_value a, clj_value b, int *out) {
	uint32_t na = clj_vector_count(a), nb = clj_vector_count(b);
	if (na != nb) {
		*out = na < nb ? -1 : 1;
		return CLJ_NIL;
	}
	for (uint32_t i = 0; i < na; i++) {
		if (clj_compare(clj_vector_nth(a, i), clj_vector_nth(b, i), out) == CLJ_THROWN) return CLJ_THROWN;
		if (*out) return CLJ_NIL;
	}
	*out = 0;
	return CLJ_NIL;
}

clj_value clj_compare(clj_value a, clj_value b, int *out) {
	*out = 0;
	if (a == b) return CLJ_NIL;
	if (clj_is_nil(a)) {
		*out = -1;
		return CLJ_NIL;
	}
	if (clj_is_nil(b)) {
		*out = 1;
		return CLJ_NIL;
	}
	if (clj_is_fixnum(a) && clj_is_fixnum(b)) {
		intptr_t x = clj_fixnum_val(a), y = clj_fixnum_val(b);
		*out = x < y ? -1 : x > y ? 1 : 0;
		return CLJ_NIL;
	}
	if (clj_is_number(a)) {
		if (!clj_is_number(b)) return cast_error(b, "a number");
		int c;
		if (clj_num_cmp(a, b, &c) == CLJ_THROWN) return CLJ_THROWN;
		// 2 is unordered (a NaN); Clojure's compare answers 0 for it.
		*out = c == 2 ? 0 : c;
		return CLJ_NIL;
	}
	if (clj_is_bool(a)) {
		if (!clj_is_bool(b)) return cast_error(b, "a boolean");
		*out = a == CLJ_TRUE ? 1 : -1;
		return CLJ_NIL;
	}
	if (clj_is_char(a)) {
		if (!clj_is_char(b)) return cast_error(b, "a char");
		uint32_t x = clj_char_val(a), y = clj_char_val(b);
		*out = x < y ? -1 : x > y ? 1 : 0;
		return CLJ_NIL;
	}
	if (clj_is_string(a)) {
		if (!clj_is_string(b)) return cast_error(b, "a string");
		*out = compare_strings(a, b);
		return CLJ_NIL;
	}
	if (clj_is_keyword(a)) {
		if (!clj_is_keyword(b)) return cast_error(b, "a keyword");
		*out = compare_named(clj_keyword_ns(a), clj_keyword_name(a), clj_keyword_ns(b), clj_keyword_name(b));
		return CLJ_NIL;
	}
	if (clj_is_symbol(a)) {
		if (!clj_is_symbol(b)) return cast_error(b, "a symbol");
		*out = compare_named(clj_symbol_ns(a), clj_symbol_name(a), clj_symbol_ns(b), clj_symbol_name(b));
		return CLJ_NIL;
	}
	if (clj_is_vector(a)) {
		if (!clj_is_vector(b)) return cast_error(b, "a vector");
		return compare_vectors(a, b, out);
	}
	if (clj_is_uuid(a)) {
		if (!clj_is_uuid(b)) return cast_error(b, "a UUID");
		const clj_uuid *x = clj_uuid_of(a), *y = clj_uuid_of(b);
		*out = x->hi < y->hi ? -1 : x->hi > y->hi ? 1 : x->lo < y->lo ? -1 : x->lo > y->lo ? 1 : 0;
		return CLJ_NIL;
	}
	if (clj_is_inst(a)) {
		if (!clj_is_inst(b)) return cast_error(b, "a Date");
		int64_t x = clj_inst_ms(a), y = clj_inst_ms(b);
		*out = x < y ? -1 : x > y ? 1 : 0;
		return CLJ_NIL;
	}
	return cast_error(a, "Comparable");
}

bool clj_compare_with(const clj_call *call, clj_value a, clj_value b, int *out) {
	if (clj_is_nil(call->f)) return clj_compare(a, b, out) != CLJ_THROWN;
	clj_value args[2] = {a, b};
	clj_value r = clj_call_invoke(call, args);
	if (r == CLJ_THROWN) return false;
	int64_t n;
	if (clj_int64_of(r, &n)) {
		*out = n < 0 ? -1 : n > 0 ? 1 : 0;
	} else if (clj_is_double(r)) {
		double d = clj_double_val(r);
		*out = d < 0 ? -1 : d > 0 ? 1 : 0;
	} else if (clj_truthy(r)) {
		*out = -1;
	} else {
		// A predicate comparator answers only "a first": ask the other way round to tell equal from above.
		clj_release(r);
		clj_value swapped[2] = {b, a};
		clj_value back = clj_call_invoke(call, swapped);
		if (back == CLJ_THROWN) return false;
		*out = clj_truthy(back) ? 1 : 0;
		clj_release(back);
		return true;
	}
	clj_release(r);
	return true;
}

// ---- sort

typedef struct {
	const clj_call *cmp;
	const clj_call *key; // NULL for sort, the key fn of sort-by otherwise
	bool            thrown;
} sort_ctx;

// Keys are computed per comparison, as Clojure's sort-by does: a keyfn with side effects is called as often.
static int sort_compare(sort_ctx *s, clj_value a, clj_value b) {
	int out = 0;
	if (!s->key) {
		if (!clj_compare_with(s->cmp, a, b, &out)) s->thrown = true;
		return out;
	}
	clj_value ka = clj_call_invoke(s->key, &a);
	if (ka == CLJ_THROWN) {
		s->thrown = true;
		return 0;
	}
	clj_value kb = clj_call_invoke(s->key, &b);
	if (kb == CLJ_THROWN) {
		clj_release(ka);
		s->thrown = true;
		return 0;
	}
	if (!clj_compare_with(s->cmp, ka, kb, &out)) s->thrown = true;
	clj_release(ka);
	clj_release(kb);
	return out;
}

// Bottom-up and stable; the result is whichever buffer the last pass wrote, NULL once a comparison threw.
static clj_value *merge_sort(clj_value *src, clj_value *dst, size_t n, sort_ctx *s) {
	for (size_t width = 1; width < n; width *= 2) {
		for (size_t lo = 0; lo < n; lo += 2 * width) {
			size_t mid = lo + width < n ? lo + width : n;
			size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
			size_t i = lo, j = mid, k = lo;
			while (i < mid && j < hi) {
				int c = sort_compare(s, src[i], src[j]);
				if (s->thrown) return NULL;
				dst[k++] = c <= 0 ? src[i++] : src[j++];
			}
			while (i < mid) dst[k++] = src[i++];
			while (j < hi) dst[k++] = src[j++];
		}
		clj_value *t = src;
		src = dst;
		dst = t;
	}
	return src;
}

static clj_value sort_items(clj_value coll, sort_ctx *s) {
	size_t    n = 0;
	clj_value keep = CLJ_NIL;
	// The items stay borrowed from `keep` for the whole sort: no retain per element.
	clj_value *items = clj_seq_items(coll, &n, &keep);
	if (!items) return CLJ_THROWN;
	clj_value *sorted = items;
	clj_value *scratch = NULL;
	if (n > 1) {
		scratch = malloc(n * sizeof *scratch);
		if (!scratch) clj_fatal("out of memory");
		sorted = merge_sort(items, scratch, n, s);
	}
	clj_value out = sorted ? clj_list_from_array(sorted, n) : CLJ_THROWN;
	free(scratch);
	free(items);
	clj_release(keep);
	return out;
}

clj_value clj_sort(clj_value coll, clj_value cmp) {
	clj_call call = clj_call_prepare(cmp, 2);
	sort_ctx s = {&call, NULL, false};
	return sort_items(coll, &s);
}

clj_value clj_sort_by(clj_value coll, clj_value keyfn, clj_value cmp) {
	clj_call call = clj_call_prepare(cmp, 2);
	clj_call key = clj_call_prepare(keyfn, 1);
	sort_ctx s = {&call, &key, false};
	return sort_items(coll, &s);
}
