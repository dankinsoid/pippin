// @ai-generated(solo)
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/list.h"
#include "clj/map.h"
#include "clj/record.h"
#include "clj/reduce.h"
#include "clj/shape.h"
#include "clj/vector.h"

enum { BITS = 5, MASK = 31 };

// Slots hold key/value pairs from the front and sub-node pointers packed from the end.
typedef struct {
	clj_header h;
	uint32_t   datamap;
	uint32_t   nodemap;
	clj_value  slots[];
} bnode;

// Keys whose full 32-bit hashes coincide. Sits wherever its hash is alone among siblings.
typedef struct {
	clj_header h;
	uint32_t   hash;
	uint32_t   count;
	clj_value  slots[];
} cnode;

// Internal nodes report whether the operation changed anything; pointer identity cannot,
// since in-place updates return the same node.
typedef struct {
	bool changed;
	bool count_changed;
} edit;

static inline uint32_t popcount(uint32_t x) { return (uint32_t)__builtin_popcount(x); }
static inline uint32_t bit_pos(uint32_t hash, uint32_t shift) { return 1u << ((hash >> shift) & MASK); }
static inline size_t   bit_index(uint32_t bitmap, uint32_t bit) { return popcount(bitmap & (bit - 1)); }
static inline size_t   bnode_slots(const bnode *n) { return 2 * popcount(n->datamap) + popcount(n->nodemap); }
static inline size_t   node_slot(const bnode *n, size_t total, uint32_t bit) { return total - 1 - bit_index(n->nodemap, bit); }

static void bnode_each_child(void *self, clj_visitor visit, void *ctx) {
	bnode *n = self;
	size_t total = bnode_slots(n);
	for (size_t i = 0; i < total; i++) visit(n->slots[i], ctx);
}

static void cnode_each_child(void *self, clj_visitor visit, void *ctx) {
	cnode *c = self;
	for (size_t i = 0; i < 2 * (size_t)c->count; i++) visit(c->slots[i], ctx);
}

static const clj_type bnode_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "map-node",
	.each_child = bnode_each_child,
};

static const clj_type cnode_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "map-collision-node",
	.each_child = cnode_each_child,
};

static inline bool   is_cnode(clj_value v) { return clj_header_of(v)->type == &cnode_type; }
static inline bool   is_shape(clj_value v) { return clj_header_of(v)->flags & CLJ_FLAG_SHAPE; }
// The trie layout; a shape map never reaches it.
static inline clj_map *clj_map_of(clj_value v) {
	CLJ_ASSERT(!is_shape(v), "a shape map has no trie");
	return (clj_map *)clj_to_ptr(v);
}
static inline bnode *bnode_of(clj_value v) { return clj_to_ptr(v); }
static inline cnode *cnode_of(clj_value v) { return clj_to_ptr(v); }

// Storing into a shared node must keep the invariant that its children are shared.
static void store(clj_header *owner, clj_value *slot, clj_value v) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	*slot = v;
}

static bnode *bnode_alloc(uint32_t datamap, uint32_t nodemap, size_t total) {
	bnode *n = clj_alloc(&bnode_type, sizeof *n + total * sizeof(clj_value));
	n->datamap = datamap;
	n->nodemap = nodemap;
	return n;
}

static cnode *cnode_alloc(uint32_t hash, uint32_t count) {
	cnode *c = clj_alloc(&cnode_type, sizeof *c + 2 * (size_t)count * sizeof(clj_value));
	c->hash = hash;
	c->count = count;
	return c;
}

static bnode *bnode_resize(bnode *n, size_t total) {
	return clj_realloc(n, sizeof *n + total * sizeof(clj_value));
}

static cnode *cnode_resize(cnode *c, uint32_t count) {
	return clj_realloc(c, sizeof *c + 2 * (size_t)count * sizeof(clj_value));
}

static bnode *bnode_copy(const bnode *n) {
	size_t total = bnode_slots(n);
	bnode *c = bnode_alloc(n->datamap, n->nodemap, total);
	for (size_t i = 0; i < total; i++) c->slots[i] = clj_retain(n->slots[i]);
	return c;
}

static cnode *cnode_copy(const cnode *c) {
	cnode *d = cnode_alloc(c->hash, c->count);
	for (size_t i = 0; i < 2 * (size_t)c->count; i++) d->slots[i] = clj_retain(c->slots[i]);
	return d;
}

// Consumes node; returns a node this operation may mutate.
static bnode *bnode_own(clj_value node) {
	if (clj_is_unique(node)) return bnode_of(node);
	bnode *c = bnode_copy(bnode_of(node));
	clj_release(node);
	return c;
}

static cnode *cnode_own(clj_value node) {
	if (clj_is_unique(node)) return cnode_of(node);
	cnode *c = cnode_copy(cnode_of(node));
	clj_release(node);
	return c;
}

// n unique. Opens `count` nil slots at `at`; `total` is the slot count before.
static bnode *bnode_open(bnode *n, size_t total, size_t at, size_t count) {
	n = bnode_resize(n, total + count);
	memmove(n->slots + at + count, n->slots + at, (total - at) * sizeof(clj_value));
	memset(n->slots + at, 0, count * sizeof(clj_value));
	return n;
}

// n unique. Drops `count` slots at `at` whose contents the caller has already taken.
static bnode *bnode_close(bnode *n, size_t total, size_t at, size_t count) {
	memmove(n->slots + at, n->slots + at + count, (total - at - count) * sizeof(clj_value));
	return bnode_resize(n, total - count);
}

// All values consumed.
static clj_value merge_pairs(clj_value k0, clj_value v0, uint32_t h0,
                             clj_value k1, clj_value v1, uint32_t h1, uint32_t shift) {
	if (h0 == h1) {
		cnode *c = cnode_alloc(h0, 2);
		c->slots[0] = k0;
		c->slots[1] = v0;
		c->slots[2] = k1;
		c->slots[3] = v1;
		return clj_from_ptr(c);
	}
	uint32_t b0 = bit_pos(h0, shift), b1 = bit_pos(h1, shift);
	if (b0 == b1) {
		bnode *n = bnode_alloc(0, b0, 1);
		n->slots[0] = merge_pairs(k0, v0, h0, k1, v1, h1, shift + BITS);
		return clj_from_ptr(n);
	}
	bnode *n = bnode_alloc(b0 | b1, 0, 4);
	size_t i0 = b0 < b1 ? 0 : 2, i1 = 2 - i0;
	n->slots[i0] = k0;
	n->slots[i0 + 1] = v0;
	n->slots[i1] = k1;
	n->slots[i1 + 1] = v1;
	return clj_from_ptr(n);
}

// A unique parent hands its slot's own reference down, so uniqueness propagates along the path.
typedef struct {
	bnode    *n;
	size_t    j;
	clj_value child;
	bool      unique;
} descent;

static descent descend(clj_value node, size_t j) {
	bnode *n = bnode_of(node);
	bool unique = clj_is_unique(node);
	return (descent){n, j, unique ? n->slots[j] : clj_retain(n->slots[j]), unique};
}

// NULL when the child was untouched; otherwise the owned parent whose slot j awaits the new child.
static bnode *ascend(descent d, clj_value nc, bool changed) {
	if (!changed) {
		if (!d.unique) clj_release(nc);
		return NULL;
	}
	if (d.unique) return d.n;
	bnode *n = bnode_copy(d.n);
	clj_release(clj_from_ptr(d.n));
	clj_release(n->slots[d.j]);
	n->slots[d.j] = CLJ_NIL;
	return n;
}

static clj_value node_assoc(clj_value node, uint32_t shift, uint32_t hash, clj_value key, clj_value val, edit *e);

static clj_value cnode_assoc(clj_value node, uint32_t shift, uint32_t hash, clj_value key, clj_value val, edit *e) {
	cnode *c = cnode_of(node);
	if (c->hash != hash) {
		// The hashes diverge below this level: push the collision node down and add beside it.
		bnode *w = bnode_alloc(0, bit_pos(c->hash, shift), 1);
		w->slots[0] = node;
		return node_assoc(clj_from_ptr(w), shift, hash, key, val, e);
	}
	for (size_t i = 0; i < 2 * (size_t)c->count; i += 2) {
		if (!clj_equals(c->slots[i], key)) continue;
		if (c->slots[i + 1] == val) return node;
		c = cnode_own(node);
		clj_value old = c->slots[i + 1];
		store(&c->h, &c->slots[i + 1], clj_retain(val));
		clj_release(old);
		e->changed = true;
		return clj_from_ptr(c);
	}
	c = cnode_own(node);
	size_t i = 2 * (size_t)c->count;
	c = cnode_resize(c, c->count + 1);
	c->count++;
	store(&c->h, &c->slots[i], clj_retain(key));
	store(&c->h, &c->slots[i + 1], clj_retain(val));
	e->changed = e->count_changed = true;
	return clj_from_ptr(c);
}

static clj_value node_assoc(clj_value node, uint32_t shift, uint32_t hash, clj_value key, clj_value val, edit *e) {
	if (is_cnode(node)) return cnode_assoc(node, shift, hash, key, val, e);
	bnode *n = bnode_of(node);
	uint32_t bit = bit_pos(hash, shift);
	if (n->datamap & bit) {
		size_t i = 2 * bit_index(n->datamap, bit);
		if (clj_equals(n->slots[i], key)) {
			if (n->slots[i + 1] == val) return node;
			n = bnode_own(node);
			clj_value old = n->slots[i + 1];
			store(&n->h, &n->slots[i + 1], clj_retain(val));
			clj_release(old);
			e->changed = true;
			return clj_from_ptr(n);
		}
		n = bnode_own(node);
		size_t total = bnode_slots(n);
		clj_value k0 = n->slots[i], v0 = n->slots[i + 1];
		clj_value sub = merge_pairs(k0, v0, clj_hash(k0), clj_retain(key), clj_retain(val), hash, shift + BITS);
		n = bnode_close(n, total, i, 2);
		n->datamap &= ~bit;
		n->nodemap |= bit;
		size_t j = node_slot(n, total - 1, bit);
		n = bnode_open(n, total - 2, j, 1);
		store(&n->h, &n->slots[j], sub);
		e->changed = e->count_changed = true;
		return clj_from_ptr(n);
	}
	if (n->nodemap & bit) {
		descent d = descend(node, node_slot(n, bnode_slots(n), bit));
		clj_value nc = node_assoc(d.child, shift + BITS, hash, key, val, e);
		n = ascend(d, nc, e->changed);
		if (!n) return node;
		store(&n->h, &n->slots[d.j], nc);
		return clj_from_ptr(n);
	}
	n = bnode_own(node);
	size_t total = bnode_slots(n);
	size_t i = 2 * bit_index(n->datamap, bit);
	n = bnode_open(n, total, i, 2);
	n->datamap |= bit;
	store(&n->h, &n->slots[i], clj_retain(key));
	store(&n->h, &n->slots[i + 1], clj_retain(val));
	e->changed = e->count_changed = true;
	return clj_from_ptr(n);
}

// Moves the pair out of a node holding exactly one; releases the node.
static void take_pair(clj_value node, clj_value *k, clj_value *v) {
	clj_value *slots = is_cnode(node) ? cnode_of(node)->slots : bnode_of(node)->slots;
	if (clj_is_unique(node)) {
		*k = slots[0];
		*v = slots[1];
		if (is_cnode(node)) cnode_of(node)->count = 0;
		else bnode_of(node)->datamap = 0;
	} else {
		*k = clj_retain(slots[0]);
		*v = clj_retain(slots[1]);
	}
	clj_release(node);
}

static bool holds_single_pair(clj_value node) {
	if (is_cnode(node)) return cnode_of(node)->count == 1;
	const bnode *n = bnode_of(node);
	return n->nodemap == 0 && popcount(n->datamap) == 1;
}

// A collision node alone in a bitmap node is alone at the parent's level too.
static bool wraps_lone_collision(clj_value node) {
	if (is_cnode(node)) return false;
	const bnode *n = bnode_of(node);
	return n->datamap == 0 && popcount(n->nodemap) == 1 && is_cnode(n->slots[0]);
}

static clj_value take_lone_collision(clj_value node) {
	bnode *n = bnode_of(node);
	clj_value c;
	if (clj_is_unique(node)) {
		c = n->slots[0];
		n->nodemap = 0;
	} else {
		c = clj_retain(n->slots[0]);
	}
	clj_release(node);
	return c;
}

// n unique, slot j vacated for `child` (consumed). Restores the canonical shape.
static bnode *bnode_adopt(bnode *n, uint32_t bit, size_t j, clj_value child) {
	if (holds_single_pair(child)) {
		clj_value k, v;
		take_pair(child, &k, &v);
		size_t total = bnode_slots(n);
		n = bnode_close(n, total, j, 1);
		n->nodemap &= ~bit;
		size_t i = 2 * bit_index(n->datamap, bit);
		n = bnode_open(n, total - 1, i, 2);
		n->datamap |= bit;
		store(&n->h, &n->slots[i], k);
		store(&n->h, &n->slots[i + 1], v);
		return n;
	}
	if (wraps_lone_collision(child)) child = take_lone_collision(child);
	store(&n->h, &n->slots[j], child);
	return n;
}

static clj_value cnode_dissoc(clj_value node, uint32_t hash, clj_value key, edit *e) {
	cnode *c = cnode_of(node);
	if (c->hash != hash) return node;
	size_t len = 2 * (size_t)c->count;
	for (size_t i = 0; i < len; i += 2) {
		if (!clj_equals(c->slots[i], key)) continue;
		c = cnode_own(node);
		clj_value k = c->slots[i], v = c->slots[i + 1];
		memmove(c->slots + i, c->slots + i + 2, (len - i - 2) * sizeof(clj_value));
		c->count--;
		c = cnode_resize(c, c->count);
		clj_release(k);
		clj_release(v);
		e->changed = e->count_changed = true;
		return clj_from_ptr(c);
	}
	return node;
}

static clj_value node_dissoc(clj_value node, uint32_t shift, uint32_t hash, clj_value key, edit *e) {
	if (is_cnode(node)) return cnode_dissoc(node, hash, key, e);
	bnode *n = bnode_of(node);
	uint32_t bit = bit_pos(hash, shift);
	if (n->datamap & bit) {
		size_t i = 2 * bit_index(n->datamap, bit);
		if (!clj_equals(n->slots[i], key)) return node;
		n = bnode_own(node);
		clj_value k = n->slots[i], v = n->slots[i + 1];
		n = bnode_close(n, bnode_slots(n), i, 2);
		n->datamap &= ~bit;
		clj_release(k);
		clj_release(v);
		e->changed = e->count_changed = true;
		return clj_from_ptr(n);
	}
	if (n->nodemap & bit) {
		descent d = descend(node, node_slot(n, bnode_slots(n), bit));
		clj_value nc = node_dissoc(d.child, shift + BITS, hash, key, e);
		n = ascend(d, nc, e->changed);
		if (!n) return node;
		return clj_from_ptr(bnode_adopt(n, bit, d.j, nc));
	}
	return node;
}

static const clj_value *map_find(clj_value map, clj_value key) {
	uint32_t hash = clj_hash(key);
	clj_value node = clj_map_of(map)->root;
	for (uint32_t shift = 0;; shift += BITS) {
		if (is_cnode(node)) {
			const cnode *c = cnode_of(node);
			if (c->hash != hash) return NULL;
			for (size_t i = 0; i < 2 * (size_t)c->count; i += 2) {
				if (clj_equals(c->slots[i], key)) return &c->slots[i + 1];
			}
			return NULL;
		}
		const bnode *n = bnode_of(node);
		uint32_t bit = bit_pos(hash, shift);
		if (n->datamap & bit) {
			size_t i = 2 * bit_index(n->datamap, bit);
			return clj_equals(n->slots[i], key) ? &n->slots[i + 1] : NULL;
		}
		if (!(n->nodemap & bit)) return NULL;
		node = n->slots[node_slot(n, bnode_slots(n), bit)];
	}
}

static bool node_each(clj_value node, clj_map_entry_fn fn, void *ctx) {
	if (is_cnode(node)) {
		const cnode *c = cnode_of(node);
		for (size_t i = 0; i < 2 * (size_t)c->count; i += 2) {
			if (!fn(c->slots[i], c->slots[i + 1], ctx)) return false;
		}
		return true;
	}
	const bnode *n = bnode_of(node);
	size_t pairs = 2 * popcount(n->datamap), total = pairs + popcount(n->nodemap);
	for (size_t i = 0; i < pairs; i += 2) {
		if (!fn(n->slots[i], n->slots[i + 1], ctx)) return false;
	}
	for (size_t j = pairs; j < total; j++) {
		if (!node_each(n->slots[j], fn, ctx)) return false;
	}
	return true;
}

static void map_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_header *h = self;
	if (h->flags & CLJ_FLAG_SHAPE) {
		clj_shape_map *m = self;
		uint32_t       n = clj_shape_nkeys(m->shape);
		for (uint32_t i = 0; i < n; i++) visit(m->slots[i], ctx);
		return;
	}
	visit(((clj_map *)self)->root, ctx);
	visit(((clj_map *)self)->meta, ctx);
}

uint32_t clj_map_entry_hash(clj_value key, clj_value val) {
	return clj_mix_coll_hash(31 * (31 + clj_hash(key)) + clj_hash(val), 2);
}

static bool hash_entry(clj_value key, clj_value val, void *ctx) {
	uint32_t *sum = ctx;
	*sum += clj_map_entry_hash(key, val);
	return true;
}

// A shape map has no cache slot (records have none either); trigger: shape maps as keys in a profile.
static uint32_t map_hash(void *self) {
	clj_value me = clj_from_ptr(self);
	if (is_shape(me)) {
		uint32_t sum = 0;
		clj_map_each(me, hash_entry, &sum);
		return clj_mix_coll_hash(sum, clj_map_count(me));
	}
	clj_map *m = self;
	uint32_t h = clj_hash_cache_load(&m->hash);
	if (h) return h;
	uint32_t sum = 0;
	clj_map_each(me, hash_entry, &sum);
	return clj_hash_cache_store(&m->hash, clj_mix_coll_hash(sum, m->count));
}

typedef struct {
	clj_value other;
	bool      equal;
} equals_ctx;

static bool equals_entry(clj_value key, clj_value val, void *ctx) {
	equals_ctx *ec = ctx;
	clj_value   found = clj_map_get(ec->other, key, CLJ_UNBOUND);
	ec->equal = found != CLJ_UNBOUND && clj_equals(found, val);
	return ec->equal;
}

// Another IPersistentMap representation (a sorted map) is equal by content, so the entries go through its lookup.
static bool equals_foreign_entry(clj_value key, clj_value val, void *ctx) {
	equals_ctx *ec = ctx;
	clj_value   found = clj_equals_lookup(ec->other, key, CLJ_UNBOUND);
	ec->equal = found != CLJ_UNBOUND && clj_equals(found, val);
	clj_release(found);
	return ec->equal;
}

// A record is no map's equal, in either direction, as on the JVM.
static bool map_equals(void *self, clj_value other) {
	if (!clj_has_core(other, CLJ_CORE_MAP) || clj_is_record(other)) return false;
	clj_value me = clj_from_ptr(self);
	if (is_shape(me) && clj_is_shape_map(other) && clj_shape_map_of(me)->shape == clj_shape_map_of(other)->shape) {
		const clj_shape_map *a = self, *b = clj_shape_map_of(other);
		uint32_t             n = clj_shape_nkeys(a->shape);
		for (uint32_t i = 0; i < n; i++) {
			if (!clj_equals(a->slots[i], b->slots[i])) return false;
		}
		return true;
	}
	clj_value n = clj_count(other);
	if (n == CLJ_THROWN) {
		clj_release(clj_take_pending());
		return false;
	}
	if (clj_map_count(me) != (uint32_t)clj_fixnum_val(n)) return false;
	equals_ctx ec = {other, true};
	clj_map_each(me, clj_is_map(other) ? equals_entry : equals_foreign_entry, &ec);
	return ec.equal;
}

typedef struct {
	clj_value *entries;
	size_t     n;
} collect_ctx;

static bool collect_entry(clj_value key, clj_value val, void *ctx) {
	collect_ctx *c = ctx;
	c->entries[c->n++] = key;
	c->entries[c->n++] = val;
	return true;
}

// Owned key value ... of the map in a malloc'd array, borrowed from the live map.
static clj_value *entries_of(clj_value map, size_t *n) {
	*n = 2 * (size_t)clj_map_count(map);
	collect_ctx c = {malloc((*n + 1) * sizeof(clj_value)), 0};
	if (!c.entries) clj_fatal("out of memory");
	clj_map_each(map, collect_entry, &c);
	return c.entries;
}

// Eager list of [k v] vectors; a map has no O(1) seq view, so no first/next fast path (NOTES.md).
static clj_value map_seq(clj_value self) {
	if (!clj_map_count(self)) return CLJ_NIL;
	size_t     n;
	clj_value *entries = entries_of(self, &n);
	clj_value *pairs = malloc((n / 2) * sizeof *pairs);
	if (!pairs) clj_fatal("out of memory");
	for (size_t i = 0; i < n; i += 2) pairs[i / 2] = clj_vector_from_array(entries + i, 2);
	clj_value list = clj_list_from_array(pairs, n / 2);
	for (size_t i = 0; i < n / 2; i++) clj_release(pairs[i]);
	free(pairs);
	free(entries);
	return list;
}

static clj_value map_count(clj_value self) { return clj_fixnum(clj_map_count(self)); }

static bool reduce_entry(clj_value key, clj_value val, void *ctx) {
	clj_value pair[2] = {key, val};
	clj_value entry = clj_vector_from_array(pair, 2);
	bool      more = clj_reducer_step(ctx, entry);
	clj_release(entry);
	return more;
}

// (reduce f init map) feeds [k v] entries, one vector per entry, without the eager entry list of map_seq.
static clj_value map_reduce(clj_value self, clj_value f, clj_value init) {
	clj_reducer r = clj_reducer_start(f, init, 2);
	clj_map_each(self, reduce_entry, &r);
	return clj_reducer_finish(&r);
}

static bool reduce_kv_entry(clj_value key, clj_value val, void *ctx) { return clj_reducer_step_kv(ctx, key, val); }

clj_value clj_map_reduce_kv(clj_value map, clj_value f, clj_value init) {
	clj_reducer r = clj_reducer_start(f, init, 3);
	clj_map_each(map, reduce_kv_entry, &r);
	return clj_reducer_finish(&r);
}

static clj_value map_lookup(clj_value self, clj_value key, clj_value not_found) {
	return clj_retain(clj_map_get(self, key, not_found));
}

// Consumes self. An entry is a [k v] pair, a map (all its entries) or nil (no-op).
static clj_value map_conj(clj_value self, clj_value item) {
	if (clj_is_nil(item)) return self;
	if (clj_is_vector(item) && clj_vector_count(item) == 2) return clj_map_assoc(self, clj_vector_nth(item, 0), clj_vector_nth(item, 1));
	if (clj_is_map(item)) {
		size_t     n;
		clj_value *entries = entries_of(item, &n);
		for (size_t j = 0; j < n; j += 2) self = clj_map_assoc(self, entries[j], entries[j + 1]);
		free(entries);
		return self;
	}
	// Another IPersistentMap representation (a sorted map, a record) hands its entries over through its seq.
	if (clj_has_core(item, CLJ_CORE_MAP)) {
		clj_value s = clj_seq(item);
		if (s == CLJ_THROWN) {
			clj_release(self);
			return CLJ_THROWN;
		}
		clj_seq_iter it = clj_seq_iter_start(s);
		clj_value    entry;
		while (clj_seq_iter_next(&it, &entry)) self = clj_map_assoc(self, clj_vector_nth(entry, 0), clj_vector_nth(entry, 1));
		clj_release(s);
		if (it.thrown) {
			clj_release(self);
			return CLJ_THROWN;
		}
		return self;
	}
	clj_release(self);
	return clj_throw_msg("Vector arg to map conj must be a pair");
}

static clj_value map_invoke(clj_value self, const clj_value *args, size_t n) {
	if (n != 1 && n != 2) return clj_arity_error(self, n);
	return map_lookup(self, args[0], n == 2 ? args[1] : CLJ_NIL);
}

static clj_value map_meta(clj_value self) { return is_shape(self) ? CLJ_NIL : clj_retain(clj_map_of(self)->meta); }

// A shape map carries no meta: with-meta gives the trie layout up, one way (design §4).
// @ai-generated(guided)
static clj_value map_with_meta(clj_value self, clj_value m) {
	if (is_shape(self)) {
		if (clj_is_nil(m)) return self;
		self = clj_shape_map_to_hash_map(self);
		clj_debug_map_generic(CLJ_MAPS_TRIE_META);
	}
	clj_map *map = clj_map_of(self);
	if (clj_is_nil(m) && clj_is_nil(map->meta)) return self;
	if (!clj_is_unique(self)) {
		clj_map *c = clj_alloc(&clj_map_type, sizeof *c);
		c->count = map->count;
		atomic_store_explicit(&c->hash, clj_hash_cache_load(&map->hash), memory_order_relaxed);
		c->root = clj_retain(map->root);
		clj_release(self);
		map = c;
	}
	clj_value old = map->meta;
	store(&map->h, &map->meta, clj_retain(m));
	clj_release(old);
	return clj_from_ptr(map);
}

const clj_type clj_map_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "map",
	.core_bits = CLJ_CORE_SEQABLE | CLJ_CORE_COLL | CLJ_CORE_COUNTED | CLJ_CORE_LOOKUP | CLJ_CORE_ASSOCIATIVE | CLJ_CORE_FN | CLJ_CORE_MAP |
	             CLJ_CORE_META | CLJ_CORE_OBJ | CLJ_CORE_REDUCE | CLJ_CORE_EDITABLE,
	.each_child = map_each_child,
	.hash = map_hash,
	.equals = map_equals,
	.seq = map_seq,
	.count = map_count,
	.lookup = map_lookup,
	.conj = map_conj,
	.assoc = clj_map_assoc,
	.dissoc = clj_map_dissoc,
	.reduce = map_reduce,
	.invoke = map_invoke,
	.meta = map_meta,
	.with_meta = map_with_meta,
};

static bnode   empty_root = {.h = {1, CLJ_FLAG_IMMORTAL, &bnode_type}};
clj_map clj_map_empty_object = {.h = {1, CLJ_FLAG_IMMORTAL, &clj_map_type}, .root = (clj_value)&empty_root};

clj_value clj_map_empty(void) { return clj_from_ptr(&clj_map_empty_object); }

clj_value clj_map_empty_new(void) {
	clj_map *m = clj_alloc(&clj_map_type, sizeof *m);
	m->root = clj_from_ptr(&empty_root);
	return clj_from_ptr(m);
}

uint32_t clj_map_count(clj_value map) {
	return is_shape(map) ? clj_shape_nkeys(clj_shape_map_of(map)->shape) : clj_map_of(map)->count;
}

clj_value clj_map_get(clj_value map, clj_value key, clj_value not_found) {
	if (is_shape(map)) {
		const clj_shape_map *m = clj_shape_map_of(map);
		int32_t              i = clj_shape_index(m->shape, key);
		return i >= 0 ? m->slots[i] : not_found;
	}
	const clj_value *found = map_find(map, key);
	return found ? *found : not_found;
}

bool clj_map_contains(clj_value map, clj_value key) {
	if (is_shape(map)) return clj_shape_index(clj_shape_map_of(map)->shape, key) >= 0;
	return map_find(map, key) != NULL;
}

void clj_map_each(clj_value map, clj_map_entry_fn fn, void *ctx) {
	if (is_shape(map)) {
		const clj_shape_map *m = clj_shape_map_of(map);
		uint32_t             n = clj_shape_nkeys(m->shape);
		for (uint32_t i = 0; i < n; i++) {
			if (!fn(clj_shape_key(m->shape, i), m->slots[i], ctx)) return;
		}
		return;
	}
	node_each(clj_map_of(map)->root, fn, ctx);
}

// Applies the root produced by a node op to the consumed map, copying the wrapper only when needed.
static clj_value map_commit(clj_value map, bool unique, clj_value root, edit e, int32_t delta) {
	clj_map *m = clj_map_of(map);
	if (!e.changed) {
		if (!unique) clj_release(root);
		return map;
	}
	if (!unique) {
		clj_map *c = clj_alloc(&clj_map_type, sizeof *c);
		c->count = m->count;
		c->meta = clj_retain(m->meta);
		clj_release(map);
		m = c;
	} else {
		atomic_store_explicit(&m->hash, 0, memory_order_relaxed);
	}
	store(&m->h, &m->root, root);
	if (e.count_changed) m->count = (uint32_t)((int32_t)m->count + delta);
	return clj_from_ptr(m);
}

clj_value clj_hash_map_assoc(clj_value map, clj_value key, clj_value val) {
	clj_map *m = clj_map_of(map);
	bool unique = clj_is_unique(map);
	clj_value root = unique ? m->root : clj_retain(m->root);
	edit e = {0};
	root = node_assoc(root, 0, clj_hash(key), key, val, &e);
	return map_commit(map, unique, root, e, 1);
}

clj_value clj_hash_map_dissoc(clj_value map, clj_value key) {
	clj_map *m = clj_map_of(map);
	bool unique = clj_is_unique(map);
	clj_value root = unique ? m->root : clj_retain(m->root);
	edit e = {0};
	root = node_dissoc(root, 0, clj_hash(key), key, &e);
	return map_commit(map, unique, root, e, -1);
}

// An empty map without meta is where the shape tree starts; a non-empty trie stays one (the one-way rule).
clj_value clj_map_assoc(clj_value map, clj_value key, clj_value val) {
	if (is_shape(map)) return clj_shape_map_assoc(map, key, val);
	clj_map *m = clj_map_of(map);
	if (m->count == 0 && clj_is_nil(m->meta)) {
		clj_value s = clj_shape_map_single(key, val);
		if (s != CLJ_UNBOUND) {
			clj_release(map);
			return s;
		}
	}
	return clj_hash_map_assoc(map, key, val);
}

clj_value clj_map_dissoc(clj_value map, clj_value key) {
	if (is_shape(map)) return clj_shape_map_dissoc(map, key);
	return clj_hash_map_dissoc(map, key);
}

clj_value clj_map_from_items(const clj_value *items, uint32_t n, uint32_t *dup) {
	uint32_t nkeys = n / 2;
	if (nkeys && nkeys <= CLJ_SHAPE_MAX_KEYS) {
		clj_value keys[CLJ_SHAPE_MAX_KEYS], vals[CLJ_SHAPE_MAX_KEYS];
		for (uint32_t i = 0; i < nkeys; i++) {
			keys[i] = items[2 * i];
			vals[i] = items[2 * i + 1];
		}
		const clj_shape *shape = clj_shape_for_keys(keys, nkeys);
		if (shape) return clj_shape_map_from_keys(shape, keys, vals);
	}
	clj_value m = clj_map_empty();
	for (uint32_t i = 0; i < n; i += 2) {
		if (dup && clj_map_contains(m, items[i])) {
			*dup = i;
			clj_release(m);
			return CLJ_UNBOUND;
		}
		m = clj_map_assoc(m, items[i], items[i + 1]);
	}
	return m;
}

static bool node_same_shape(clj_value a, clj_value b) {
	if (is_cnode(a) != is_cnode(b)) return false;
	if (is_cnode(a)) {
		const cnode *x = cnode_of(a), *y = cnode_of(b);
		if (x->hash != y->hash || x->count != y->count) return false;
		for (size_t i = 0; i < 2 * (size_t)x->count; i += 2) {
			bool found = false;
			for (size_t j = 0; j < 2 * (size_t)y->count && !found; j += 2) {
				found = clj_equals(x->slots[i], y->slots[j]) && clj_equals(x->slots[i + 1], y->slots[j + 1]);
			}
			if (!found) return false;
		}
		return true;
	}
	const bnode *x = bnode_of(a), *y = bnode_of(b);
	if (x->datamap != y->datamap || x->nodemap != y->nodemap) return false;
	size_t pairs = 2 * popcount(x->datamap), total = pairs + popcount(x->nodemap);
	for (size_t i = 0; i < pairs; i++) {
		if (!clj_equals(x->slots[i], y->slots[i])) return false;
	}
	for (size_t j = pairs; j < total; j++) {
		if (!node_same_shape(x->slots[j], y->slots[j])) return false;
	}
	return true;
}

bool clj_debug_map_same_shape(clj_value a, clj_value b) {
	if (is_shape(a) || is_shape(b)) return is_shape(a) && is_shape(b) && clj_shape_map_of(a)->shape == clj_shape_map_of(b)->shape;
	return clj_map_of(a)->count == clj_map_of(b)->count &&
	       node_same_shape(clj_map_of(a)->root, clj_map_of(b)->root);
}

clj_value clj_debug_map_root(clj_value map) { return clj_map_of(map)->root; }

uint32_t clj_debug_map_cached_hash(clj_value map) { return is_shape(map) ? 0 : clj_hash_cache_load(&clj_map_of(map)->hash); }
