// @ai-generated(solo)
#include <stdlib.h>

#include "clj/array.h"
#include "clj/core.h"

static clj_value not_an_array(const char *what, clj_value v) {
	return clj_throw_msg("%s not supported on this type: %s", what, clj_type_name(v));
}

static clj_value bad_index(intptr_t i, uint32_t count) {
	return clj_throw_msg("Index %lld out of bounds for length %u", (long long)i, count);
}

// The one bounds check aget and aset share; the kind switch behind it is the whole dispatch.
static clj_value element_index(const char *what, const clj_value *args, uint32_t *out) {
	if (!clj_is_array(args[0])) return not_an_array(what, args[0]);
	if (!clj_is_fixnum(args[1])) return clj_throw_msg("%s: %s cannot be cast to an integer", what, clj_type_name(args[1]));
	intptr_t i = clj_fixnum_val(args[1]);
	uint32_t n = clj_array_count(args[0]);
	if (i < 0 || (uintptr_t)i >= n) return bad_index(i, n);
	*out = (uint32_t)i;
	return CLJ_NIL;
}

static clj_value b_aget(const clj_value *args, size_t n) {
	(void)n;
	uint32_t i;
	if (element_index("aget", args, &i) == CLJ_THROWN) return CLJ_THROWN;
	return clj_array_get(args[0], i);
}

static clj_value b_aset(const clj_value *args, size_t n) {
	(void)n;
	uint32_t i;
	if (element_index("aset", args, &i) == CLJ_THROWN) return CLJ_THROWN;
	if (clj_array_set(args[0], i, args[2]) == CLJ_THROWN) return CLJ_THROWN;
	return clj_retain(args[2]);
}

static clj_value b_alength(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_array(args[0])) return not_an_array("alength", args[0]);
	return clj_fixnum(clj_array_count(args[0]));
}

static clj_value b_aclone(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_array(args[0])) return not_an_array("aclone", args[0]);
	return clj_array_clone(args[0]);
}

// (X-array size), (X-array seq), (X-array size init) and (X-array size seq), as on the JVM.
static clj_value array_ctor(clj_array_kind kind, const clj_value *args, size_t n) {
	const char *name = clj_array_kind_name(kind);
	if (!clj_is_fixnum(args[0])) {
		if (n == 2) return clj_throw_msg("%s-array: %s cannot be cast to an integer", name, clj_type_name(args[0]));
		return clj_array_from_seq(kind, args[0]);
	}
	intptr_t size = clj_fixnum_val(args[0]);
	if (size < 0 || size > UINT32_MAX) return clj_throw_msg("%s-array: size out of range: %lld", name, (long long)size);
	clj_value arr = clj_array_new(kind, (uint32_t)size);
	if (n == 1) return arr;
	if (!clj_is_seqable(args[1])) {
		for (uint32_t i = 0; i < (uint32_t)size; i++) {
			if (clj_array_set(arr, i, args[1]) == CLJ_THROWN) {
				clj_release(arr);
				return CLJ_THROWN;
			}
		}
		return arr;
	}
	size_t     count = 0;
	clj_value  keep = CLJ_NIL;
	clj_value *items = clj_seq_items(args[1], &count, &keep);
	if (!items) {
		clj_release(arr);
		return CLJ_THROWN;
	}
	clj_value err = CLJ_NIL;
	for (size_t i = 0; i < count && i < (size_t)size; i++) {
		if (clj_array_set(arr, (uint32_t)i, items[i]) == CLJ_THROWN) {
			err = CLJ_THROWN;
			break;
		}
	}
	free(items);
	clj_release(keep);
	if (err == CLJ_THROWN) {
		clj_release(arr);
		return CLJ_THROWN;
	}
	return arr;
}

#define CTOR(fn, kind) \
	static clj_value fn(const clj_value *args, size_t n) { return array_ctor(kind, args, n); }

CTOR(b_byte_array, CLJ_ARRAY_I8)
CTOR(b_short_array, CLJ_ARRAY_I16)
CTOR(b_int_array, CLJ_ARRAY_I32)
CTOR(b_long_array, CLJ_ARRAY_I64)
CTOR(b_float_array, CLJ_ARRAY_F32)
CTOR(b_double_array, CLJ_ARRAY_F64)
CTOR(b_boolean_array, CLJ_ARRAY_BOOL)
CTOR(b_char_array, CLJ_ARRAY_CHAR)
CTOR(b_object_array, CLJ_ARRAY_OBJECT)

static clj_value kind_arg(const char *what, clj_value v, clj_array_kind *out) {
	if (clj_array_kind_named(v, out)) return CLJ_NIL;
	clj_value text = clj_pr_str(v);
	clj_value e = clj_throw_msg("%s: unknown array kind: %s", what, clj_string_bytes(text));
	clj_release(text);
	return e;
}

static clj_value b_make_array(const clj_value *args, size_t n) {
	// Multi-dimensional arrays have no representation here: an array of arrays is written out.
	if (n > 2) return clj_throw_msg("make-array: only one dimension is supported");
	clj_array_kind kind;
	if (kind_arg("make-array", args[0], &kind) == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_is_fixnum(args[1])) return clj_throw_msg("make-array: %s cannot be cast to an integer", clj_type_name(args[1]));
	return array_ctor(kind, args + 1, 1);
}

static clj_value b_into_array(const clj_value *args, size_t n) {
	if (n == 1) return clj_array_from_seq(CLJ_ARRAY_OBJECT, args[0]);
	clj_array_kind kind;
	if (kind_arg("into-array", args[0], &kind) == CLJ_THROWN) return CLJ_THROWN;
	return clj_array_from_seq(kind, args[1]);
}

static clj_value b_to_array(const clj_value *args, size_t n) {
	(void)n;
	return clj_array_from_seq(CLJ_ARRAY_OBJECT, args[0]);
}

// A persistent vector of the cast elements, not an unboxed one (NOTES.md, "Arrays").
static clj_value b_vector_of(const clj_value *args, size_t n) {
	clj_array_kind kind;
	if (kind_arg("vector-of", args[0], &kind) == CLJ_THROWN) return CLJ_THROWN;
	clj_value v = clj_vector_empty();
	for (size_t i = 1; i < n; i++) {
		clj_value x = clj_array_cast(kind, args[i]);
		if (x == CLJ_THROWN) {
			clj_release(v);
			return CLJ_THROWN;
		}
		v = clj_vector_conj(v, x);
		clj_release(x);
	}
	return v;
}

#define ANY CLJ_ARITY_ANY

static const struct {
	const char   *name;
	clj_native_fn fn;
	uint32_t      min, max;
} entries[] = {
	{"byte-array", b_byte_array, 1, 2},     {"short-array", b_short_array, 1, 2},   {"int-array", b_int_array, 1, 2},
	{"long-array", b_long_array, 1, 2},     {"float-array", b_float_array, 1, 2},   {"double-array", b_double_array, 1, 2},
	{"boolean-array", b_boolean_array, 1, 2}, {"char-array", b_char_array, 1, 2},   {"object-array", b_object_array, 1, 2},
	{"make-array", b_make_array, 2, ANY},   {"aget", b_aget, 2, 2},                 {"aset", b_aset, 3, 3},
	{"alength", b_alength, 1, 1},           {"aclone", b_aclone, 1, 1},
	{"aset-byte", b_aset, 3, 3},            {"aset-short", b_aset, 3, 3},           {"aset-int", b_aset, 3, 3},
	{"aset-long", b_aset, 3, 3},            {"aset-float", b_aset, 3, 3},           {"aset-double", b_aset, 3, 3},
	{"aset-boolean", b_aset, 3, 3},         {"aset-char", b_aset, 3, 3},            {"aset-object", b_aset, 3, 3},
	{"into-array", b_into_array, 1, 2},     {"to-array", b_to_array, 1, 1},         {"vector-of", b_vector_of, 1, ANY},
};

void clj_array_builtins_install(void) {
	for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) clj_builtin_bind(entries[i].name, entries[i].fn, entries[i].min, entries[i].max);
}
