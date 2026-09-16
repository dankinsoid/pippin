// @ai-generated(solo)
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "clj/array.h"
#include "clj/core.h"

_Static_assert(offsetof(clj_array, data) % 8 == 0, "inline elements must stay 8-aligned");

static const uint8_t kind_size[CLJ_ARRAY_KIND_COUNT] = {1, 1, 2, 4, 8, 4, 8, 1, 4, sizeof(clj_value)};
static const char   *kind_name[CLJ_ARRAY_KIND_COUNT] = {"byte", "u8", "short", "int", "long", "float", "double", "boolean", "char", "object"};
// The C spellings, accepted next to the Clojure ones; :boolean, :char and :object have no second name.
static const char *kind_alias[CLJ_ARRAY_KIND_COUNT] = {"i8", "u8", "i16", "i32", "i64", "f32", "f64", NULL, NULL, NULL};

size_t      clj_array_elem_size(clj_array_kind kind) { return kind_size[kind]; }
const char *clj_array_kind_name(clj_array_kind kind) { return kind_name[kind]; }

bool clj_array_kind_named(clj_value v, clj_array_kind *out) {
	clj_value name = clj_is_keyword(v) ? clj_keyword_name(v) : clj_is_symbol(v) ? clj_symbol_name(v) : clj_is_string(v) ? v : CLJ_NIL;
	if (clj_is_nil(name)) return false;
	if (!clj_is_nil(clj_is_keyword(v) ? clj_keyword_ns(v) : clj_is_symbol(v) ? clj_symbol_ns(v) : CLJ_NIL)) return false;
	const char *text = clj_string_bytes(name);
	size_t      len = clj_string_len(name);
	for (int i = 0; i < CLJ_ARRAY_KIND_COUNT; i++) {
		if (strlen(kind_name[i]) == len && !memcmp(kind_name[i], text, len)) {
			*out = (clj_array_kind)i;
			return true;
		}
		if (kind_alias[i] && strlen(kind_alias[i]) == len && !memcmp(kind_alias[i], text, len)) {
			*out = (clj_array_kind)i;
			return true;
		}
	}
	return false;
}

static clj_value out_of_range(clj_array_kind kind, clj_value v) {
	clj_value text = clj_pr_str(v);
	clj_value e = clj_throw_msg("Value out of range for %s: %s", kind_name[kind], clj_string_bytes(text));
	clj_release(text);
	return e;
}

// Truncated toward zero, as RT.longCast does; false when v is outside the int64 range.
static bool as_i64(clj_value v, int64_t *out) {
	if (clj_int64_of(v, out)) return true;
	if (clj_is_bigint(v)) return clj_bigint_to_i64(v, out);
	if (clj_is_double(v)) {
		double d = clj_double_val(v);
		if (!isfinite(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0) return false;
		*out = (int64_t)d;
		return true;
	}
	clj_value t = clj_num_truncate(v);
	bool      ok = clj_bigint_to_i64(t, out);
	clj_release(t);
	return ok;
}

static clj_value cast_int(clj_array_kind kind, clj_value v, int64_t lo, uint64_t hi, void *slot) {
	if (!clj_is_number(v)) return clj_throw_msg("%s cannot be cast to a number", clj_type_name(v));
	int64_t n;
	if (!as_i64(v, &n) || n < lo || (n > 0 && (uint64_t)n > hi)) return out_of_range(kind, v);
	switch (kind) {
	case CLJ_ARRAY_I8: *(int8_t *)slot = (int8_t)n; break;
	case CLJ_ARRAY_U8: *(uint8_t *)slot = (uint8_t)n; break;
	case CLJ_ARRAY_I16: *(int16_t *)slot = (int16_t)n; break;
	case CLJ_ARRAY_I32: *(int32_t *)slot = (int32_t)n; break;
	default: *(int64_t *)slot = n; break;
	}
	return CLJ_NIL;
}

// Writes v into one element slot; the caller has already released whatever an :object slot held.
static clj_value cast_into(clj_array_kind kind, clj_value v, void *slot) {
	switch (kind) {
	case CLJ_ARRAY_I8: return cast_int(kind, v, INT8_MIN, INT8_MAX, slot);
	case CLJ_ARRAY_U8: return cast_int(kind, v, 0, UINT8_MAX, slot);
	case CLJ_ARRAY_I16: return cast_int(kind, v, INT16_MIN, INT16_MAX, slot);
	case CLJ_ARRAY_I32: return cast_int(kind, v, INT32_MIN, INT32_MAX, slot);
	case CLJ_ARRAY_I64: return cast_int(kind, v, INT64_MIN, INT64_MAX, slot);
	case CLJ_ARRAY_F32: {
		if (!clj_is_number(v)) return clj_throw_msg("%s cannot be cast to a number", clj_type_name(v));
		double d = clj_num_to_double(v);
		// RT.floatCast: a NaN passes both comparisons, an infinity does not.
		if (d < -(double)FLT_MAX || d > (double)FLT_MAX) return out_of_range(kind, v);
		*(float *)slot = (float)d;
		return CLJ_NIL;
	}
	case CLJ_ARRAY_F64:
		if (!clj_is_number(v)) return clj_throw_msg("%s cannot be cast to a number", clj_type_name(v));
		*(double *)slot = clj_num_to_double(v);
		return CLJ_NIL;
	case CLJ_ARRAY_BOOL:
		// RT.booleanCast: everything but nil and false is true.
		*(uint8_t *)slot = clj_truthy(v);
		return CLJ_NIL;
	case CLJ_ARRAY_CHAR:
		if (clj_is_char(v)) {
			*(uint32_t *)slot = clj_char_val(v);
			return CLJ_NIL;
		}
		if (clj_is_fixnum(v)) {
			intptr_t n = clj_fixnum_val(v);
			if (n < 0 || n > 0x10FFFF || (n >= 0xD800 && n <= 0xDFFF)) return out_of_range(kind, v);
			*(uint32_t *)slot = (uint32_t)n;
			return CLJ_NIL;
		}
		return clj_throw_msg("%s cannot be cast to a char", clj_type_name(v));
	default: *(clj_value *)slot = clj_retain(v); return CLJ_NIL;
	}
}

static clj_value box(clj_array_kind kind, const void *slot) {
	switch (kind) {
	case CLJ_ARRAY_I8: return clj_fixnum(*(const int8_t *)slot);
	case CLJ_ARRAY_U8: return clj_fixnum(*(const uint8_t *)slot);
	case CLJ_ARRAY_I16: return clj_fixnum(*(const int16_t *)slot);
	case CLJ_ARRAY_I32: return clj_fixnum(*(const int32_t *)slot);
	case CLJ_ARRAY_I64: return clj_long_new(*(const int64_t *)slot);
	case CLJ_ARRAY_F32: return clj_double_new((double)*(const float *)slot);
	case CLJ_ARRAY_F64: return clj_double_new(*(const double *)slot);
	case CLJ_ARRAY_BOOL: return clj_bool(*(const uint8_t *)slot != 0);
	case CLJ_ARRAY_CHAR: return clj_char(*(const uint32_t *)slot);
	default: return clj_retain(*(const clj_value *)slot);
	}
}

static void *slot_of(clj_array *a, uint32_t i) { return a->data + (size_t)i * kind_size[a->kind]; }

clj_value clj_array_new(clj_array_kind kind, uint32_t count) {
	clj_array *a = clj_alloc(&clj_array_type, sizeof *a + (size_t)count * kind_size[kind]);
	a->kind = (uint32_t)kind;
	a->count = count;
	return clj_from_ptr(a);
}

clj_value clj_array_get(clj_value arr, uint32_t i) {
	clj_array *a = clj_array_of(arr);
	CLJ_ASSERT(i < a->count, "array index out of range");
	return box((clj_array_kind)a->kind, slot_of(a, i));
}

clj_value clj_array_set(clj_value arr, uint32_t i, clj_value v) {
	clj_array *a = clj_array_of(arr);
	CLJ_ASSERT(i < a->count, "array index out of range");
	void *slot = slot_of(a, i);
	if (a->kind != CLJ_ARRAY_OBJECT) return cast_into((clj_array_kind)a->kind, v, slot);
	// Every child of a shared object is shared (object.h), and a write is how a new one gets in.
	if (a->h.flags & CLJ_FLAG_SHARED) clj_share(v);
	clj_value old = *(clj_value *)slot;
	*(clj_value *)slot = clj_retain(v);
	clj_release(old);
	return CLJ_NIL;
}

clj_value clj_array_cast(clj_array_kind kind, clj_value v) {
	if (kind == CLJ_ARRAY_OBJECT) return clj_retain(v);
	// A union, not a byte array: an f64 or i64 store needs the alignment.
	union {
		int64_t  i;
		double   d;
		uint32_t u;
	} slot;
	if (cast_into(kind, v, &slot) == CLJ_THROWN) return CLJ_THROWN;
	return box(kind, &slot);
}

clj_value clj_array_clone(clj_value arr) {
	clj_array *a = clj_array_of(arr);
	clj_value  copy = clj_array_new((clj_array_kind)a->kind, a->count);
	clj_array *c = clj_array_of(copy);
	memcpy(c->data, a->data, (size_t)a->count * kind_size[a->kind]);
	if (a->kind == CLJ_ARRAY_OBJECT) {
		for (uint32_t i = 0; i < a->count; i++) clj_retain(((clj_value *)c->data)[i]);
	}
	return copy;
}

clj_value clj_array_from_seq(clj_array_kind kind, clj_value coll) {
	size_t     n = 0;
	clj_value  keep = CLJ_NIL;
	clj_value *items = clj_seq_items(coll, &n, &keep);
	if (!items) return CLJ_THROWN;
	clj_value arr = clj_array_new(kind, (uint32_t)n);
	clj_value err = CLJ_NIL;
	for (size_t i = 0; i < n; i++) {
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

// ---- slots

static void array_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_array *a = self;
	if (a->kind != CLJ_ARRAY_OBJECT) return;
	for (uint32_t i = 0; i < a->count; i++) visit(((clj_value *)a->data)[i], ctx);
}

// Identity, as on the JVM: a mutable array is never equal to a copy of itself and hashes by address.
static uint32_t array_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool array_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

static clj_value array_count(clj_value self) { return clj_fixnum(clj_array_count(self)); }

// A slot without ILookup, as a string has one: RT.get special-cases arrays too.
static clj_value array_lookup(clj_value self, clj_value key, clj_value not_found) {
	if (!clj_is_fixnum(key)) return clj_retain(not_found);
	intptr_t i = clj_fixnum_val(key);
	if (i < 0 || (uintptr_t)i >= clj_array_count(self)) return clj_retain(not_found);
	return clj_array_get(self, (uint32_t)i);
}

static clj_value array_seq(clj_value self) {
	if (!clj_array_count(self)) return CLJ_NIL;
	clj_array_seq *s = clj_alloc(&clj_array_seq_type, sizeof *s);
	s->arr = clj_retain(self);
	return clj_from_ptr(s);
}

static clj_value array_reduce_from(clj_value self, uint32_t i, clj_value f, clj_value init) {
	clj_reducer r = clj_reducer_start(f, init, 2);
	uint32_t    n = clj_array_count(self);
	for (; i < n; i++) {
		clj_value item = clj_array_get(self, i);
		bool      more = clj_reducer_step(&r, item);
		clj_release(item);
		if (!more) break;
	}
	return clj_reducer_finish(&r);
}

static clj_value array_reduce(clj_value self, clj_value f, clj_value init) { return array_reduce_from(self, 0, f, init); }

const clj_type clj_array_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "array",
	// Seqable alone: a JVM array is no IPersistentCollection, and count, nth and reduce are slots without bits.
	.core_bits = CLJ_CORE_SEQABLE,
	.each_child = array_each_child,
	.hash = array_hash,
	.equals = array_equals,
	.seq = array_seq,
	.count = array_count,
	.lookup = array_lookup,
	.reduce = array_reduce,
};

// ---- seq view

static clj_array_seq *seq_of(clj_value v) { return (clj_array_seq *)clj_to_ptr(v); }

static void array_seq_each_child(void *self, clj_visitor visit, void *ctx) { visit(((clj_array_seq *)self)->arr, ctx); }

static clj_value array_seq_first(clj_value self) { return clj_array_get(seq_of(self)->arr, seq_of(self)->i); }

static clj_value array_seq_next(clj_value self) {
	clj_array_seq *s = seq_of(self);
	if (s->i + 1 >= clj_array_count(s->arr)) return CLJ_NIL;
	clj_array_seq *next = clj_alloc(&clj_array_seq_type, sizeof *next);
	next->i = s->i + 1;
	next->arr = clj_retain(s->arr);
	return clj_from_ptr(next);
}

static clj_value array_seq_count(clj_value self) { return clj_fixnum(clj_array_count(seq_of(self)->arr) - seq_of(self)->i); }

static clj_value array_seq_reduce(clj_value self, clj_value f, clj_value init) {
	return array_reduce_from(seq_of(self)->arr, seq_of(self)->i, f, init);
}

const clj_type clj_array_seq_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "array-seq",
	CLJ_ASEQ_TRAIT(CLJ_CORE_COUNTED | CLJ_CORE_REDUCE),
	.each_child = array_seq_each_child,
	.seq = clj_aseq_seq,
	.first = array_seq_first,
	.next = array_seq_next,
	.count = array_seq_count,
	.reduce = array_seq_reduce,
};
