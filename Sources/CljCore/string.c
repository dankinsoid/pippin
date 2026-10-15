// @ai-generated(solo)
// The one file that sees a string's layout (string.h).
#define CLJ_STRING_LAYOUT
#include <string.h>

#include "clj/reduce.h"
#include "clj/seq.h"
#include "clj/string.h"
#include "ice_internal.h"

static inline clj_string *clj_string_of(clj_value v) { return (clj_string *)clj_to_ptr(v); }

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
	clj_str s = CLJ_STR(self);
	if (!s.len) return CLJ_NIL;
	uint32_t cp;
	clj_utf8_decode(s.bytes, s.len, 0, &cp);
	return clj_char(cp);
}

static clj_value string_next(clj_value self) {
	clj_str s = CLJ_STR(self);
	if (!s.len) return CLJ_NIL;
	uint32_t cp;
	size_t   next = clj_utf8_decode(s.bytes, s.len, 0, &cp);
	return next < s.len ? clj_string_seq_new(self, (uint32_t)next) : CLJ_NIL;
}

static clj_value string_count(clj_value self) { return clj_fixnum((intptr_t)clj_string_count(self)); }

// (get "abc" 1) is \b, as RT.get special-cases strings; a string is still no ILookup (no bit).
static clj_value string_lookup(clj_value self, clj_value key, clj_value not_found) {
	uint32_t cp;
	if (clj_is_fixnum(key) && clj_fixnum_val(key) >= 0 && clj_string_char_at(self, (size_t)clj_fixnum_val(key), &cp)) return clj_char(cp);
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

static bool all_ascii(const char *p, size_t n) {
	uint64_t acc = 0;
	size_t   i = 0;
	for (; i + 8 <= n; i += 8) {
		uint64_t w;
		memcpy(&w, p + i, 8);
		acc |= w;
	}
	for (; i < n; i++) acc |= (unsigned char)p[i];
	return !(acc & 0x8080808080808080ull);
}

// Inline, not a side allocation: freeing one would take strings off rc.c's leaf path, every string's death with them.
typedef struct {
	// (index << 32) | byte offset of a code point read last. One word, so a reader on another thread never pairs one
	// read's index with another's offset; every value stored is true of the bytes, so a stale one is still a start.
	_Atomic uint64_t hint;
	_Atomic uint32_t count;   // code points, 0 until built; crumb is valid once it is not
	_Atomic uint32_t crumb[]; // crumb[k]: the byte offset of code point (k + 1) * CLJ_STRING_CRUMB_STRIDE
} string_tail;

static inline size_t tail_offset(size_t len) { return (sizeof(clj_string) + len + 1 + 7) & ~(size_t)7; }
// Code points never outnumber bytes, so this bounds the crumbs below the last code point.
static inline size_t       tail_crumbs(size_t len) { return (len - 1) / CLJ_STRING_CRUMB_STRIDE; }
static inline string_tail *tail_of(const clj_string *s) { return (string_tail *)((char *)s + tail_offset(s->len)); }
static inline size_t       string_size(size_t len, bool tail) {
	return tail ? tail_offset(len) + sizeof(string_tail) + tail_crumbs(len) * sizeof(uint32_t) : sizeof(clj_string) + len + 1;
}

static inline void tail_init(string_tail *t) {
	atomic_init(&t->hint, 0);
	atomic_init(&t->count, 0);
}

clj_value clj_string_new(const char *bytes, size_t len) {
	if (len > UINT32_MAX) clj_fatal("string longer than 4 GiB");
	bool        ascii = all_ascii(bytes, len), tail = !ascii && len > CLJ_STRING_CRUMB_BYTES;
	clj_string *s = clj_alloc_uninit(&clj_string_type, string_size(len, tail));
	atomic_init(&s->hash, 0);
	s->len = (uint32_t)len;
	if (len) memcpy(s->bytes, bytes, len);
	s->bytes[len] = '\0';
	if (ascii) s->h.flags |= CLJ_STRING_ASCII;
	if (tail) tail_init(tail_of(s));
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

static inline bool is_lead(unsigned char c) { return (c & 0xC0) != 0x80; }

static size_t count_leads(const unsigned char *p, size_t from, size_t to) {
	size_t count = 0;
	for (size_t i = from; i < to; i++) count += is_lead(p[i]);
	return count;
}

// Racing builders store the same values; the release on count publishes the crumbs to an acquiring reader.
static uint32_t tail_build(const clj_string *s, string_tail *t) {
	const unsigned char *p = (const unsigned char *)s->bytes;
	uint32_t             count = 0;
	for (uint32_t i = 0; i < s->len; i++) {
		if (!is_lead(p[i])) continue;
		if (count && count % CLJ_STRING_CRUMB_STRIDE == 0)
			atomic_store_explicit(&t->crumb[count / CLJ_STRING_CRUMB_STRIDE - 1], i, memory_order_relaxed);
		count++;
	}
	atomic_store_explicit(&t->count, count, memory_order_release);
	return count;
}

// 0 only for malformed bytes without a single lead byte, which then rebuild on every call.
static uint32_t tail_count(const clj_string *s, string_tail *t) {
	uint32_t count = atomic_load_explicit(&t->count, memory_order_acquire);
	return count ? count : tail_build(s, t);
}

size_t clj_string_count_slow(clj_value v) {
	const clj_string *s = clj_string_of(v);
	if (s->len > CLJ_STRING_CRUMB_BYTES) return tail_count(s, tail_of(s));
	return count_leads((const unsigned char *)s->bytes, 0, s->len);
}

static size_t skip_points(const unsigned char *p, size_t len, size_t pos, size_t k) {
	for (; k && pos < len; k--) {
		pos++;
		while (pos < len && !is_lead(p[pos])) pos++;
	}
	return pos;
}

static size_t back_points(const unsigned char *p, size_t pos, size_t k) {
	for (; k && pos; k--) {
		pos--;
		while (pos && !is_lead(p[pos])) pos--;
	}
	return pos;
}

size_t clj_string_offset_slow(clj_value v, size_t i) {
	const clj_string    *s = clj_string_of(v);
	const unsigned char *p = (const unsigned char *)s->bytes;
	if (s->len <= CLJ_STRING_CRUMB_BYTES) return skip_points(p, s->len, 0, i);
	string_tail *t = tail_of(s);
	if (i >= tail_count(s, t)) return s->len;
	size_t c = i / CLJ_STRING_CRUMB_STRIDE, k = i - c * CLJ_STRING_CRUMB_STRIDE;
	size_t pos = c ? atomic_load_explicit(&t->crumb[c - 1], memory_order_relaxed) : 0;
	if (clj_opt_off(CLJ_NO_OPT_HINT)) return skip_points(p, s->len, pos, k);
	// A walk by index (a reader's charAt, a window copied a char at a time) is one code point from the last read.
	uint64_t h = atomic_load_explicit(&t->hint, memory_order_relaxed);
	size_t   hi = (size_t)(h >> 32), ho = (uint32_t)h;
	if (hi <= i && i - hi < k) pos = skip_points(p, s->len, ho, i - hi);
	else if (hi > i && hi - i < k) pos = back_points(p, ho, hi - i);
	else pos = skip_points(p, s->len, pos, k);
	if (hi != i) atomic_store_explicit(&t->hint, (uint64_t)i << 32 | pos, memory_order_relaxed);
	return pos;
}

size_t clj_string_index_slow(clj_value v, size_t pos) {
	const clj_string    *s = clj_string_of(v);
	const unsigned char *p = (const unsigned char *)s->bytes;
	size_t               from = 0, base = 0;
	if (pos > s->len) pos = s->len;
	if (s->len > CLJ_STRING_CRUMB_BYTES && pos > CLJ_STRING_CRUMB_STRIDE) {
		string_tail *t = tail_of(s);
		uint32_t     count = tail_count(s, t);
		// lo ends as the number of crumbs at or before pos; crumbs increase.
		size_t lo = 0, hi = count ? (count - 1) / CLJ_STRING_CRUMB_STRIDE : 0;
		while (lo < hi) {
			size_t mid = lo + (hi - lo) / 2;
			if (atomic_load_explicit(&t->crumb[mid], memory_order_relaxed) <= pos) lo = mid + 1;
			else hi = mid;
		}
		if (lo) {
			from = atomic_load_explicit(&t->crumb[lo - 1], memory_order_relaxed);
			base = lo * CLJ_STRING_CRUMB_STRIDE;
		}
	}
	return base + count_leads(p, from, pos);
}

bool clj_string_char_at(clj_value s, size_t i, uint32_t *out) {
	clj_str v = CLJ_STR(s);
	if (clj_string_is_ascii(s)) {
		if (i >= v.len) return false;
		*out = (unsigned char)v.bytes[i];
		return true;
	}
	if (i >= clj_string_count_slow(s)) return false;
	clj_utf8_decode(v.bytes, v.len, clj_string_offset_slow(s, i), out);
	return true;
}

uint32_t clj_string_cached_hash(clj_value s) { return clj_hash_cache_load(&clj_string_of(s)->hash); }

const char *clj_bytes_find(const char *hay, size_t hlen, const char *nd, size_t nlen) {
	if (nlen == 0) return hay;
	if (nlen > hlen) return NULL;
	const char *end = hay + (hlen - nlen) + 1;
	for (const char *p = hay; p < end;) {
		p = memchr(p, nd[0], (size_t)(end - p));
		if (!p) return NULL;
		if (memcmp(p + 1, nd + 1, nlen - 1) == 0) return p;
		p++;
	}
	return NULL;
}
