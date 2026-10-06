// @ai-generated(solo)
#include <string.h>

#include "clj/string.h"
#include "clj/symbol.h"

static void symbol_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_symbol *s = self;
	visit(s->ns.v, ctx);
	visit(s->name.v, ctx);
	visit(s->meta.v, ctx);
}

// Component hashes bypass clj_hash so a test hash override never lands in the cache.
static uint32_t symbol_hash(void *self) {
	clj_symbol *s = self;
	uint32_t h = clj_hash_cache_load(&s->hash);
	if (h) return h;
	uint32_t ns = clj_is_nil(s->ns.v) ? 0 : clj_hash_slow(s->ns.v);
	return clj_hash_cache_store(&s->hash, clj_hash_combine(clj_hash_slow(s->name.v), ns));
}

static bool symbol_equals(void *self, clj_value other) {
	if (!clj_is_symbol(other)) return false;
	const clj_symbol *a = self, *b = clj_symbol_of(other);
	return clj_equals(a->ns.v, b->ns.v) && clj_equals(a->name.v, b->name.v);
}

static clj_value symbol_meta(clj_value self) { return clj_retain(clj_symbol_of(self)->meta.v); }

// @ai-generated(guided)
static clj_value symbol_with_meta(clj_value self, clj_value m) {
	clj_symbol *s = clj_symbol_of(self);
	if (!clj_is_unique(self)) {
		clj_symbol *c = clj_alloc(&clj_symbol_type, sizeof *c);
		atomic_store_explicit(&c->hash, clj_hash_cache_load(&s->hash), memory_order_relaxed);
		clj_slot_init(&c->h, &c->ns, clj_retain(s->ns.v));
		clj_slot_init(&c->h, &c->name, clj_retain(s->name.v));
		clj_release(self);
		s = c;
	}
	clj_value old = s->meta.v;
	clj_slot_store(&s->h, &s->meta, clj_retain(m));
	clj_release(old);
	return clj_from_ptr(s);
}

const clj_type clj_symbol_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "symbol",
	.core_bits = CLJ_CORE_META | CLJ_CORE_OBJ,
	.each_child = symbol_each_child,
	.hash = symbol_hash,
	.equals = symbol_equals,
	.meta = symbol_meta,
	.with_meta = symbol_with_meta,
};

clj_value clj_symbol_new(clj_value ns, clj_value name) {
	CLJ_ASSERT(clj_is_nil(ns) || clj_is_string(ns), "symbol ns must be a string or nil");
	CLJ_ASSERT(clj_is_string(name), "symbol name must be a string");
	clj_symbol *s = clj_alloc(&clj_symbol_type, sizeof *s);
	clj_slot_init(&s->h, &s->ns, clj_retain(ns));
	clj_slot_init(&s->h, &s->name, clj_retain(name));
	return clj_from_ptr(s);
}

static _Atomic uint64_t next_id;

uint64_t clj_next_id(void) { return atomic_fetch_add_explicit(&next_id, 1, memory_order_relaxed) + 1; }

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
