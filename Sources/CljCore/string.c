// @ai-generated(solo)
#include <string.h>

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

const clj_type clj_string_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "string",
	.hash = string_hash,
	.equals = string_equals,
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
