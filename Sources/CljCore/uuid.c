// @ai-generated(solo)
#include <stdio.h>
#include <stdlib.h>

#include "clj/core.h"
#include "clj/uuid.h"

// UUID.hashCode, so a uuid hashes as it does on the JVM.
static uint32_t uuid_hash(void *self) {
	const clj_uuid *u = self;
	uint64_t        hilo = (uint64_t)u->hi ^ (uint64_t)u->lo;
	return (uint32_t)(hilo >> 32) ^ (uint32_t)hilo;
}

static bool uuid_equals(void *self, clj_value other) {
	const clj_uuid *a = self;
	return clj_is_uuid(other) && a->hi == clj_uuid_of(other)->hi && a->lo == clj_uuid_of(other)->lo;
}

const clj_type clj_uuid_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "UUID",
	.hash = uuid_hash,
	.equals = uuid_equals,
};

clj_value clj_uuid_new(int64_t hi, int64_t lo) {
	clj_uuid *u = clj_alloc(&clj_uuid_type, sizeof *u);
	u->hi = hi;
	u->lo = lo;
	return clj_from_ptr(u);
}

static int hex_val(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// Long.parseLong(s, from, to, 16) minus the sign: 1–16 hex digits.
static bool parse_group(const char *s, size_t from, size_t to, uint64_t *out) {
	if (from == to || to - from > 16) return false;
	uint64_t v = 0;
	for (size_t i = from; i < to; i++) {
		int d = hex_val(s[i]);
		if (d < 0) return false;
		v = v << 4 | (uint64_t)d;
	}
	*out = v;
	return true;
}

clj_value clj_uuid_parse(const char *s, size_t n) {
	if (n > 36) return CLJ_NIL;
	size_t dash[5], found = 0;
	for (size_t i = 0; i < n && found < 5; i++) {
		if (s[i] == '-') dash[found++] = i;
	}
	if (found != 4) return CLJ_NIL;
	uint64_t g[5];
	if (!parse_group(s, 0, dash[0], &g[0]) || !parse_group(s, dash[0] + 1, dash[1], &g[1]) || !parse_group(s, dash[1] + 1, dash[2], &g[2]) ||
	    !parse_group(s, dash[2] + 1, dash[3], &g[3]) || !parse_group(s, dash[3] + 1, n, &g[4]))
		return CLJ_NIL;
	uint64_t hi = (g[0] & 0xffffffffu) << 32 | (g[1] & 0xffffu) << 16 | (g[2] & 0xffffu);
	uint64_t lo = (g[3] & 0xffffu) << 48 | (g[4] & 0xffffffffffffu);
	return clj_uuid_new((int64_t)hi, (int64_t)lo);
}

// The SplitMix rng is seeded from a counter, so two processes would draw the same ids; a v4 needs a CSPRNG.
clj_value clj_uuid_random(void) {
	unsigned char b[16];
	arc4random_buf(b, sizeof b);
	b[6] = (unsigned char)((b[6] & 0x0f) | 0x40);
	b[8] = (unsigned char)((b[8] & 0x3f) | 0x80);
	uint64_t hi = 0, lo = 0;
	for (int i = 0; i < 8; i++) {
		hi = hi << 8 | b[i];
		lo = lo << 8 | b[8 + i];
	}
	return clj_uuid_new((int64_t)hi, (int64_t)lo);
}

void clj_uuid_format(clj_value v, char out[37]) {
	const clj_uuid *u = clj_uuid_of(v);
	uint64_t        hi = (uint64_t)u->hi, lo = (uint64_t)u->lo;
	snprintf(out, 37, "%08llx-%04llx-%04llx-%04llx-%012llx", (unsigned long long)(hi >> 32), (unsigned long long)((hi >> 16) & 0xffff),
	         (unsigned long long)(hi & 0xffff), (unsigned long long)(lo >> 48), (unsigned long long)(lo & 0xffffffffffffu));
}

// ---- builtins

static clj_value b_uuid_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_uuid(args[0]));
}

static clj_value b_parse_uuid(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("%s cannot be cast to a string", clj_type_name(args[0]));
	return clj_uuid_parse(clj_string_bytes(args[0]), clj_string_len(args[0]));
}

static clj_value b_random_uuid(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_uuid_random();
}

// The #uuid data reader: clojure.uuid/default-uuid-reader, which refuses what fromString refuses.
clj_value clj_uuid_read(clj_value form) {
	if (!clj_is_string(form)) return clj_throw_msg("#uuid literal expects a string, got: %s", clj_type_name(form));
	clj_value u = clj_uuid_parse(clj_string_bytes(form), clj_string_len(form));
	if (clj_is_nil(u)) return clj_throw_msg("Invalid UUID string: %s", clj_string_bytes(form));
	return u;
}

static clj_value b_read_uuid(const clj_value *args, size_t n) {
	(void)n;
	return clj_uuid_read(args[0]);
}

void clj_uuid_builtins_install(void) {
	clj_builtin_bind("uuid?", b_uuid_p, 1, 1);
	clj_builtin_bind("parse-uuid", b_parse_uuid, 1, 1);
	clj_builtin_bind("random-uuid", b_random_uuid, 0, 0);
	clj_builtin_bind("read-uuid*", b_read_uuid, 1, 1);
}
