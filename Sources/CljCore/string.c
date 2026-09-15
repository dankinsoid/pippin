// @ai-generated(solo)
#include <string.h>

#include "clj/reduce.h"
#include "clj/seq.h"
#include "clj/string.h"

static inline uint32_t rotl32(uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }

// MurmurHash3_x86_32, seed 0; blocks read little-endian regardless of host order.
static uint32_t murmur3_32(const unsigned char *data, size_t len) {
	const uint32_t c1 = 0xcc9e2d51, c2 = 0x1b873593;
	uint32_t h = 0;
	size_t blocks = len / 4;
	for (size_t i = 0; i < blocks; i++) {
		const unsigned char *p = data + 4 * i;
		uint32_t k = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
		k *= c1;
		k = rotl32(k, 15);
		k *= c2;
		h ^= k;
		h = rotl32(h, 13);
		h = h * 5 + 0xe6546b64;
	}
	const unsigned char *tail = data + 4 * blocks;
	uint32_t k = 0;
	for (size_t i = len & 3; i > 0; i--) k = k << 8 | tail[i - 1];
	if (len & 3) {
		k *= c1;
		k = rotl32(k, 15);
		k *= c2;
		h ^= k;
	}
	return clj_fmix32(h ^ (uint32_t)len);
}

static uint32_t string_hash(void *self) {
	clj_string *s = self;
	uint32_t h = clj_hash_cache_load(&s->hash);
	if (h) return h;
	return clj_hash_cache_store(&s->hash, murmur3_32((const unsigned char *)s->bytes, s->len));
}

static bool string_equals(void *self, clj_value other) {
	if (!clj_is_string(other)) return false;
	const clj_string *a = self, *b = clj_string_of(other);
	return a->len == b->len && memcmp(a->bytes, b->bytes, a->len) == 0;
}

static clj_value string_seq(clj_value self) { return clj_string_len(self) ? clj_string_seq_new(self, 0) : CLJ_NIL; }

static clj_value string_first(clj_value self) {
	if (!clj_string_len(self)) return CLJ_NIL;
	uint32_t cp;
	clj_utf8_decode(clj_string_bytes(self), clj_string_len(self), 0, &cp);
	return clj_char(cp);
}

static clj_value string_next(clj_value self) {
	uint32_t len = clj_string_len(self);
	if (!len) return CLJ_NIL;
	uint32_t cp;
	size_t   next = clj_utf8_decode(clj_string_bytes(self), len, 0, &cp);
	return next < len ? clj_string_seq_new(self, (uint32_t)next) : CLJ_NIL;
}

static clj_value string_count(clj_value self) { return clj_fixnum((intptr_t)clj_string_count(self)); }

// (get "abc" 1) is \b, as RT.get special-cases strings; a string is still no ILookup (no bit).
static clj_value string_lookup(clj_value self, clj_value key, clj_value not_found) {
	if (clj_is_fixnum(key) && clj_fixnum_val(key) >= 0) {
		const char *p = clj_string_bytes(self);
		size_t      n = clj_string_len(self), pos = 0;
		uint32_t    cp;
		for (intptr_t k = 0; pos < n; k++) {
			size_t len = clj_utf8_decode(p, n, pos, &cp);
			if (k == clj_fixnum_val(key)) return clj_char(cp);
			pos += len;
		}
	}
	return clj_retain(not_found);
}

const clj_type clj_string_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "string",
	.core_bits = CLJ_CORE_SEQABLE,
	.hash = string_hash,
	.equals = string_equals,
	.seq = string_seq,
	.first = string_first,
	.next = string_next,
	.count = string_count,
	.lookup = string_lookup,
	.reduce = clj_reduce_iter,
};

clj_value clj_string_new(const char *bytes, size_t len) {
	if (len > UINT32_MAX) clj_fatal("string longer than 4 GiB");
	clj_string *s = clj_alloc(&clj_string_type, sizeof *s + len + 1);
	s->len = (uint32_t)len;
	if (len) memcpy(s->bytes, bytes, len);
	s->bytes[len] = '\0';
	return clj_from_ptr(s);
}

clj_value clj_string_from_cstr(const char *s) { return clj_string_new(s, strlen(s)); }

size_t clj_utf8_decode(const char *bytes, size_t len, size_t pos, uint32_t *out) {
	const unsigned char *p = (const unsigned char *)bytes;
	unsigned char        c = p[pos];
	size_t               n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
	uint32_t             cp = n == 1 ? c : c & (0xFFu >> (n + 1));
	for (size_t j = 1; j < n && pos + j < len; j++) cp = (cp << 6) | (p[pos + j] & 0x3F);
	*out = cp;
	return n;
}

size_t clj_string_count(clj_value s) {
	const unsigned char *p = (const unsigned char *)clj_string_bytes(s);
	size_t               n = clj_string_len(s), count = 0;
	for (size_t i = 0; i < n; i++) count += (p[i] & 0xC0) != 0x80;
	return count;
}
