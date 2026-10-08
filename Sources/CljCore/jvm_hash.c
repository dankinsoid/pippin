// @ai-generated(solo)
// What JVM Clojure 1.12.6's hash answers, for keys shared with a JVM process; hash stays ours (design §10).
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/decimal.h"
#include "clj/long.h"
#include "clj/ratio.h"
#include "clj/record.h"
#include "guard_internal.h"

// ---- clojure.lang.Murmur3, seed 0

static uint32_t rotl(uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }

static uint32_t mix_k1(uint32_t k1) { return rotl(k1 * 0xcc9e2d51u, 15) * 0x1b873593u; }

static uint32_t mix_h1(uint32_t h1, uint32_t k1) { return rotl(h1 ^ k1, 13) * 5 + 0xe6546b64u; }

static uint32_t fmix(uint32_t h1, uint32_t length) { return clj_fmix32(h1 ^ length); }

static uint32_t hash_int(uint32_t input) { return input ? fmix(mix_h1(0, mix_k1(input)), 4) : 0; }

static uint32_t hash_long(int64_t input) {
	if (!input) return 0;
	uint64_t u = (uint64_t)input;
	return fmix(mix_h1(mix_h1(0, mix_k1((uint32_t)u)), mix_k1((uint32_t)(u >> 32))), 8);
}

// Util.hashCombine: Java's int >> is arithmetic.
static uint32_t hash_combine(uint32_t seed, uint32_t hash) {
	return seed ^ (hash + 0x9e3779b9u + (seed << 6) + (uint32_t)((int32_t)seed >> 2));
}

// ---- strings as the JVM holds them: UTF-16 code units

typedef struct {
	const char *bytes;
	size_t      len, pos;
	uint32_t    low; // the pending low surrogate, 0 when none
} utf16_iter;

static bool utf16_next(utf16_iter *it, uint32_t *unit) {
	if (it->low) {
		*unit = it->low;
		it->low = 0;
		return true;
	}
	if (it->pos >= it->len) return false;
	uint32_t cp;
	it->pos += clj_utf8_decode(it->bytes, it->len, it->pos, &cp);
	if (cp >= 0x10000) {
		cp -= 0x10000;
		*unit = 0xD800 + (cp >> 10);
		it->low = 0xDC00 + (cp & 0x3FF);
	} else {
		*unit = cp;
	}
	return true;
}

static uint32_t string_hash_code(clj_value s) {
	utf16_iter it = {clj_string_bytes(s), clj_string_len(s), 0, 0};
	uint32_t   h = 0, unit;
	while (utf16_next(&it, &unit)) h = 31 * h + unit;
	return h;
}

static uint32_t hash_unencoded_chars(clj_value s) {
	utf16_iter it = {clj_string_bytes(s), clj_string_len(s), 0, 0};
	uint32_t   h1 = 0, n = 0, a, b;
	while (utf16_next(&it, &a)) {
		n++;
		if (!utf16_next(&it, &b)) {
			h1 ^= mix_k1(a);
			break;
		}
		n++;
		h1 = mix_h1(h1, mix_k1(a | (b << 16)));
	}
	return fmix(h1, 2 * n);
}

// Symbol.hasheq; a keyword adds the golden ratio.
static uint32_t symbol_hasheq(clj_value ns, clj_value name) {
	return hash_combine(hash_unencoded_chars(name), clj_is_nil(ns) ? 0 : string_hash_code(ns));
}

// ---- numbers

// BigInteger.hashCode of a magnitude given as little-endian limbs.
static uint32_t limbs_hash_code(int sign, const uint32_t *limbs, uint32_t n) {
	uint32_t h = 0;
	for (uint32_t i = n; i-- > 0;) h = 31 * h + limbs[i];
	return h * (uint32_t)sign;
}

static uint32_t i64_hash_code(int64_t v) {
	if (!v) return 0;
	uint64_t mag = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
	uint32_t limbs[2] = {(uint32_t)mag, (uint32_t)(mag >> 32)};
	return limbs_hash_code(v < 0 ? -1 : 1, limbs, limbs[1] ? 2 : 1);
}

// BigInteger.hashCode of a fixnum, a boxed long or a bigint.
static uint32_t big_hash_code(clj_value v) {
	int64_t small;
	if (clj_int64_of(v, &small)) return i64_hash_code(small);
	const clj_bigint *b = clj_bigint_of(v);
	return limbs_hash_code(b->sign, b->limbs, b->n);
}

// Numbers.hasheq of a BigDecimal: stripTrailingZeros, then 31 * unscaled.hashCode() + scale; zero is 0.
static uint32_t decimal_hasheq(clj_value d) {
	clj_value unscaled = clj_retain(clj_decimal_unscaled(d));
	int64_t   scale = clj_decimal_scale(d);
	int64_t   small;
	// The unscaled part is a bigint even when it fits a long (decimal.h).
	if (clj_int64_of(unscaled, &small) ? small == 0 : clj_bigint_is_zero(unscaled)) {
		clj_release(unscaled);
		return 0;
	}
	clj_value ten = clj_bigint_from_i64(10);
	for (;;) {
		if (clj_int64_of(unscaled, &small)) {
			while (small % 10 == 0) {
				small /= 10;
				scale--;
			}
			clj_release(unscaled);
			clj_release(ten);
			return 31 * i64_hash_code(small) + (uint32_t)scale;
		}
		clj_value rem, q = clj_bigint_quot(unscaled, ten, &rem);
		bool      exact = clj_int64_of(rem, &small) ? small == 0 : clj_bigint_is_zero(rem);
		clj_release(rem);
		if (!exact) {
			clj_release(q);
			uint32_t h = 31 * big_hash_code(unscaled) + (uint32_t)scale;
			clj_release(unscaled);
			clj_release(ten);
			return h;
		}
		clj_release(unscaled);
		unscaled = clj_bigint_demote(q);
		clj_release(q);
		scale--;
	}
}

static uint32_t double_hasheq(double d) {
	if (d == 0) return 0; // Numbers.hasheq answers -0.0 as 0.0
	uint64_t bits;
	if (isnan(d)) bits = 0x7ff8000000000000ull; // doubleToLongBits' canonical NaN
	else memcpy(&bits, &d, sizeof bits);
	return (uint32_t)(bits ^ (bits >> 32));
}

// ---- the walk

typedef struct {
	char *stack_limit;
} walk;

static bool jvm_hasheq(walk *w, clj_value v, uint32_t *out);

static bool refuse(clj_value v, const char *why) {
	clj_throw_msg("jvm-hash of type %s: %s", clj_type_name(v), why);
	return false;
}

// Murmur3.hashOrdered and hashUnordered over a seq of v.
static bool hash_coll(walk *w, clj_value v, bool ordered, uint32_t *out) {
	uint32_t  h = ordered ? 1 : 0, n = 0;
	clj_value s = clj_seq(v);
	while (s != CLJ_THROWN && !clj_is_nil(s)) {
		clj_value x = clj_first(s);
		uint32_t  xh;
		bool      ok = x != CLJ_THROWN && jvm_hasheq(w, x, &xh);
		clj_release(x);
		if (!ok) {
			clj_release(s);
			return false;
		}
		h = ordered ? 31 * h + xh : h + xh;
		n++;
		clj_value nx = clj_next(s);
		clj_release(s);
		s = nx;
	}
	if (s == CLJ_THROWN) return false;
	*out = clj_mix_coll_hash(h, n);
	return true;
}

// defrecord xors in (hash classname), the symbol ns.Name with the namespace's dashes as namespace-munge writes them.
static uint32_t record_type_hash(clj_value v) {
	const char *name = clj_type_of(v)->name;
	size_t      len = strlen(name);
	const char *dot = strrchr(name, '.');
	char       *munged = malloc(len + 1);
	if (!munged) clj_fatal("out of memory");
	memcpy(munged, name, len + 1);
	for (size_t i = 0; dot && i < (size_t)(dot - name); i++) {
		if (munged[i] == '-') munged[i] = '_';
	}
	clj_value s = clj_string_new(munged, len);
	free(munged);
	uint32_t h = symbol_hasheq(CLJ_NIL, s);
	clj_release(s);
	return h;
}

static bool jvm_hasheq(walk *w, clj_value v, uint32_t *out) {
	char here;
	if (__builtin_expect(&here < w->stack_limit, 0)) {
		clj_throw_msg("Stack overflow");
		return false;
	}
	if (clj_is_nil(v)) *out = 0;
	else if (v == CLJ_TRUE) *out = 1231;
	else if (v == CLJ_FALSE) *out = 1237;
	else if (clj_is_fixnum(v)) *out = hash_long(clj_fixnum_val(v));
	else if (clj_is_char(v)) {
		if (clj_char_val(v) > 0xFFFF) return refuse(v, "past the BMP, where a JVM char is one UTF-16 unit");
		*out = clj_char_val(v); // Character.hashCode
	} else if (!clj_is_ptr(v)) return refuse(v, "no JVM counterpart");
	else if (clj_is_string(v)) *out = hash_int(string_hash_code(v));
	else if (clj_is_keyword(v)) *out = symbol_hasheq(clj_keyword_ns(v), clj_keyword_name(v)) + 0x9e3779b9u;
	else if (clj_is_symbol(v)) *out = symbol_hasheq(clj_symbol_ns(v), clj_symbol_name(v));
	else if (clj_is_long(v)) *out = hash_long(clj_long_val(v));
	else if (clj_is_double(v)) *out = double_hasheq(clj_double_val(v));
	else if (clj_is_bigint(v)) {
		int64_t small;
		// BigInt.hasheq: a value in the long range hashes as that long.
		*out = clj_bigint_to_i64(v, &small) ? hash_long(small) : big_hash_code(v);
	} else if (clj_is_ratio(v)) *out = big_hash_code(clj_ratio_num(v)) ^ big_hash_code(clj_ratio_den(v));
	else if (clj_is_decimal(v)) *out = decimal_hasheq(v);
	else if (clj_is_uuid(v) || clj_is_inst(v)) *out = clj_hash_slow(v); // UUID.hashCode and Date.hashCode already
	else if (clj_is_record(v)) {
		uint32_t h;
		if (!hash_coll(w, v, false, &h)) return false;
		*out = record_type_hash(v) ^ h;
	} else if (clj_is_instance(v)) return refuse(v, "a deftype hashes by identity on the JVM");
	else if (clj_has_core(v, CLJ_CORE_MAP) || clj_has_core(v, CLJ_CORE_SET)) return hash_coll(w, v, false, out);
	else if (clj_has_core(v, CLJ_CORE_SEQUENTIAL)) return hash_coll(w, v, true, out);
	else if (clj_is_uri(v)) return refuse(v, "equal here by its text, where java.net.URI folds case and escapes");
	else return refuse(v, "the JVM hashes it by identity, or it has no JVM counterpart");
	return true;
}

static clj_value b_jvm_hash(const clj_value *args, size_t n) {
	(void)n;
	walk     w = {clj_stack_limit()};
	uint32_t h;
	if (!jvm_hasheq(&w, args[0], &h)) return CLJ_THROWN;
	return clj_fixnum((intptr_t)(int32_t)h);
}

void clj_jvm_hash_builtins_install(void) { clj_builtin_bind_extension("jvm-hash", b_jvm_hash, 1, 1); }
