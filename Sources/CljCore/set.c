// @ai-generated(guided)
#include <stdlib.h>

#include "clj/coll.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/list.h"
#include "clj/reduce.h"
#include "clj/set.h"

static void set_each_child(void *self, clj_visitor visit, void *ctx) {
	visit(((clj_set *)self)->impl, ctx);
	visit(((clj_set *)self)->meta, ctx);
}

// Storing into a shared wrapper must keep the invariant that its children are shared.
static void store(clj_header *owner, clj_value *slot, clj_value v) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	*slot = v;
}

typedef struct {
	clj_set_item_fn fn;
	void           *ctx;
} each_ctx;

static bool each_entry(clj_value key, clj_value val, void *ctx) {
	(void)val;
	each_ctx *c = ctx;
	return c->fn(key, c->ctx);
}

void clj_set_each(clj_value set, clj_set_item_fn fn, void *ctx) {
	each_ctx c = {fn, ctx};
	clj_map_each(clj_set_of(set)->impl, each_entry, &c);
}

static bool hash_item(clj_value item, void *ctx) {
	*(uint32_t *)ctx += clj_hash(item);
	return true;
}

static uint32_t set_hash(void *self) {
	clj_set *s = self;
	uint32_t h = clj_hash_cache_load(&s->hash);
	if (h) return h;
	uint32_t sum = 0;
	clj_set_each(clj_from_ptr(s), hash_item, &sum);
	return clj_hash_cache_store(&s->hash, clj_mix_coll_hash(sum, clj_set_count(clj_from_ptr(s))));
}

typedef struct {
	clj_value other;
	bool      equal;
} equals_ctx;

static bool equals_item(clj_value item, void *ctx) {
	equals_ctx *ec = ctx;
	ec->equal = clj_set_contains(ec->other, item);
	return ec->equal;
}

// Another IPersistentSet representation (a sorted set) is equal by content, so the elements go through its lookup.
static bool equals_foreign_item(clj_value item, void *ctx) {
	equals_ctx *ec = ctx;
	clj_value   found = clj_equals_lookup(ec->other, item, CLJ_UNBOUND);
	ec->equal = found != CLJ_UNBOUND;
	clj_release(found);
	return ec->equal;
}

static bool set_equals(void *self, clj_value other) {
	if (!clj_has_core(other, CLJ_CORE_SET)) return false;
	clj_value me = clj_from_ptr(self);
	clj_value n = clj_count(other);
	if (n == CLJ_THROWN) {
		clj_release(clj_take_pending());
		return false;
	}
	if (clj_set_count(me) != (uint32_t)clj_fixnum_val(n)) return false;
	equals_ctx ec = {other, true};
	clj_set_each(me, clj_is_set(other) ? equals_item : equals_foreign_item, &ec);
	return ec.equal;
}

typedef struct {
	clj_value *items;
	size_t     n;
} collect_ctx;

static bool collect_item(clj_value item, void *ctx) {
	collect_ctx *c = ctx;
	c->items[c->n++] = item;
	return true;
}

// Borrowed items of the live set in a malloc'd array.
static clj_value *items_of(clj_value set, size_t *n) {
	*n = clj_set_count(set);
	collect_ctx c = {malloc((*n + 1) * sizeof(clj_value)), 0};
	if (!c.items) clj_fatal("out of memory");
	clj_set_each(set, collect_item, &c);
	return c.items;
}

// Eager list, as a map's seq is: no O(1) view.
static clj_value set_seq(clj_value self) {
	if (!clj_set_count(self)) return CLJ_NIL;
	size_t     n;
	clj_value *items = items_of(self, &n);
	clj_value  list = clj_list_from_array(items, n);
	free(items);
	return list;
}

static clj_value set_count(clj_value self) { return clj_fixnum(clj_set_count(self)); }

static clj_value set_lookup(clj_value self, clj_value key, clj_value not_found) {
	return clj_retain(clj_set_get(self, key, not_found));
}

static bool reduce_item(clj_value item, void *ctx) { return clj_reducer_step(ctx, item); }

static clj_value set_reduce(clj_value self, clj_value f, clj_value init) {
	clj_reducer r = clj_reducer_start(f, init, 2);
	clj_set_each(self, reduce_item, &r);
	return clj_reducer_finish(&r);
}

static clj_value set_invoke(clj_value self, const clj_value *args, size_t n) {
	if (n != 1 && n != 2) return clj_arity_error(self, n);
	return set_lookup(self, args[0], n == 2 ? args[1] : CLJ_NIL);
}

static clj_value set_meta(clj_value self) { return clj_retain(clj_set_of(self)->meta); }

// Consumes self; the caller writes the wrapper that comes back.
static clj_set *set_own(clj_value self) {
	clj_set *s = clj_set_of(self);
	if (clj_is_unique(self)) {
		atomic_store_explicit(&s->hash, 0, memory_order_relaxed);
		return s;
	}
	clj_set *c = clj_alloc(&clj_set_type, sizeof *c);
	c->impl = clj_retain(s->impl);
	c->meta = clj_retain(s->meta);
	clj_release(self);
	return c;
}

static clj_value set_with_meta(clj_value self, clj_value m) {
	if (clj_is_nil(m) && clj_is_nil(clj_set_of(self)->meta)) return self;
	uint32_t hash = clj_hash_cache_load(&clj_set_of(self)->hash);
	clj_set *s = set_own(self);
	atomic_store_explicit(&s->hash, hash, memory_order_relaxed);
	clj_value old = s->meta;
	store(&s->h, &s->meta, clj_retain(m));
	clj_release(old);
	return clj_from_ptr(s);
}

const clj_type clj_set_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "set",
	.core_bits = CLJ_CORE_SEQABLE | CLJ_CORE_COLL | CLJ_CORE_COUNTED | CLJ_CORE_FN | CLJ_CORE_SET | CLJ_CORE_META | CLJ_CORE_OBJ | CLJ_CORE_REDUCE,
	.each_child = set_each_child,
	.hash = set_hash,
	.equals = set_equals,
	.seq = set_seq,
	.count = set_count,
	.lookup = set_lookup,
	.conj = clj_set_conj,
	.dissoc = clj_set_disj,
	.reduce = set_reduce,
	.invoke = set_invoke,
	.meta = set_meta,
	.with_meta = set_with_meta,
};

static clj_set empty_set = {.h = {1, CLJ_FLAG_IMMORTAL, &clj_set_type}, .impl = (clj_value)&clj_map_empty_object};

clj_value clj_set_empty(void) { return clj_from_ptr(&empty_set); }

uint32_t clj_set_count(clj_value set) { return clj_map_count(clj_set_of(set)->impl); }

bool clj_set_contains(clj_value set, clj_value x) { return clj_map_contains(clj_set_of(set)->impl, x); }

clj_value clj_set_get(clj_value set, clj_value x, clj_value not_found) { return clj_map_get(clj_set_of(set)->impl, x, not_found); }

// The wrapper's own reference to the trie goes to the map op, so uniqueness propagates into it.
static clj_value set_update(clj_value set, clj_value x, bool add) {
	clj_set  *s = set_own(set);
	clj_value impl = s->impl;
	s->impl = CLJ_NIL;
	store(&s->h, &s->impl, add ? clj_map_assoc(impl, x, x) : clj_map_dissoc(impl, x));
	return clj_from_ptr(s);
}

clj_value clj_set_conj(clj_value set, clj_value x) {
	if (clj_set_contains(set, x)) return set;
	return set_update(set, x, true);
}

clj_value clj_set_disj(clj_value set, clj_value x) {
	if (!clj_set_contains(set, x)) return set;
	return set_update(set, x, false);
}

clj_value clj_set_from_array(const clj_value *items, size_t n) {
	clj_value s = clj_set_empty();
	for (size_t i = 0; i < n; i++) s = clj_set_conj(s, items[i]);
	return s;
}
