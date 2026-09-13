// @ai-generated(solo)
#include <string.h>

#include "clj/string.h"
#include "clj/symbol.h"

static void symbol_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_symbol *s = self;
	visit(s->ns, ctx);
	visit(s->name, ctx);
}

// Component hashes bypass clj_hash so a test hash override never lands in the cache.
static uint32_t symbol_hash(void *self) {
	clj_symbol *s = self;
	uint32_t h = clj_hash_cache_load(&s->hash);
	if (h) return h;
	uint32_t ns = clj_is_nil(s->ns) ? 0 : clj_hash_slow(s->ns);
	return clj_hash_cache_store(&s->hash, clj_hash_combine(clj_hash_slow(s->name), ns));
}

static bool symbol_equals(void *self, clj_value other) {
	if (!clj_is_symbol(other)) return false;
	const clj_symbol *a = self, *b = clj_symbol_of(other);
	return clj_equals(a->ns, b->ns) && clj_equals(a->name, b->name);
}

const clj_type clj_symbol_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "symbol",
	.each_child = symbol_each_child,
	.hash = symbol_hash,
	.equals = symbol_equals,
};

clj_value clj_symbol_new(clj_value ns, clj_value name) {
	CLJ_ASSERT(clj_is_nil(ns) || clj_is_string(ns), "symbol ns must be a string or nil");
	CLJ_ASSERT(clj_is_string(name), "symbol name must be a string");
	clj_symbol *s = clj_alloc(&clj_symbol_type, sizeof *s);
	s->ns = clj_retain(ns);
	s->name = clj_retain(name);
	return clj_from_ptr(s);
}

clj_value clj_symbol_from_cstr(const char *s) {
	const char *slash = strchr(s, '/');
	clj_value ns = CLJ_NIL, name;
	if (!slash || (slash == s && s[1] == '\0')) {
		name = clj_string_from_cstr(s);
	} else {
		ns = clj_string_new(s, (size_t)(slash - s));
		name = clj_string_from_cstr(slash + 1);
	}
	clj_value sym = clj_symbol_new(ns, name);
	clj_release(ns);
	clj_release(name);
	return sym;
}
