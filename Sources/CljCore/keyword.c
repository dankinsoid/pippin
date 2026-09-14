// @ai-generated(solo)
#include <pthread.h>

#include "clj/coll.h"
#include "clj/fn.h"
#include "clj/keyword.h"
#include "clj/map.h"

static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;
static clj_value       table; // symbol -> keyword; nil until the first intern

static void keyword_each_child(void *self, clj_visitor visit, void *ctx) {
	visit(((clj_keyword *)self)->sym, ctx);
}

static uint32_t keyword_hash(void *self) {
	clj_keyword *k = self;
	uint32_t h = clj_hash_cache_load(&k->hash);
	if (h) return h;
	return clj_hash_cache_store(&k->hash, clj_hash_slow(k->sym) + 0x9e3779b9);
}

static bool keyword_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

static clj_value keyword_invoke(clj_value self, const clj_value *args, size_t n) {
	if (n != 1 && n != 2) return clj_arity_error(self, n);
	return clj_get(args[0], self, n == 2 ? args[1] : CLJ_NIL);
}

const clj_type clj_keyword_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "keyword",
	.core_bits = CLJ_CORE_FN,
	.each_child = keyword_each_child,
	.hash = keyword_hash,
	.equals = keyword_equals,
	.invoke = keyword_invoke,
};

// Consumes sym: a new keyword takes it over, an existing one drops it.
static clj_value intern(clj_value sym) {
	pthread_mutex_lock(&table_lock);
	if (clj_is_nil(table)) table = clj_map_empty();
	clj_value kw = clj_map_get(table, sym, CLJ_NIL);
	if (clj_is_nil(kw)) {
		clj_keyword *k = clj_alloc(&clj_keyword_type, sizeof *k);
		k->h.flags |= CLJ_FLAG_IMMORTAL | CLJ_FLAG_SHARED;
		clj_share(sym);
		k->sym = sym;
		kw = clj_from_ptr(k);
		table = clj_map_assoc(table, sym, kw);
	} else {
		clj_release(sym);
	}
	pthread_mutex_unlock(&table_lock);
	return kw;
}

clj_value clj_keyword_intern(clj_value ns, clj_value name) { return intern(clj_symbol_new(ns, name)); }

clj_value clj_keyword_from_cstr(const char *s) { return intern(clj_symbol_from_cstr(s)); }
