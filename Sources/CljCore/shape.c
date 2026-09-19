// @ai-generated(solo)
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "clj/compare.h"
#include "clj/keyword.h"
#include "clj/lock.h"
#include "clj/shape.h"

// A transition; immutable once published, so readers walk the list without the lock.
typedef struct edge {
	clj_value        key;
	const clj_shape *to;
	struct edge     *next;
	bool             identity; // to's order is the source's with the key appended, or removed at index
	uint8_t          index;
	uint8_t          perm[CLJ_SHAPE_MAX_KEYS]; // to's slot i takes the source's slot perm[i]; NEW_KEY takes the value
} edge;

enum { NEW_KEY = 0xFF };

struct clj_shape {
	uint32_t          nkeys;
	uint32_t          sethash; // order-independent sum of the key hashes: the key-set table's hash
	_Atomic uint32_t  nchildren;
	_Atomic bool      dictionary;
	_Atomic(edge *)   adds;
	_Atomic(edge *)   removes;
	_Atomic(struct clj_shape *) bucket_next;
	clj_value         keys[];
};

// key-set → shape, chained through bucket_next. Shapes are never removed and a chain only grows at its head, so a
// reader walks a table without the lock; a miss is confirmed under it (a rethread during growth can hide a shape).
typedef struct {
	uint32_t             cap;
	_Atomic(clj_shape *) buckets[];
} table;

#if CLJ_DEBUG
static _Atomic int64_t counters[CLJ_MAPS_COUNTERS];
#define COUNT(i) atomic_fetch_add_explicit(&counters[i], 1, memory_order_relaxed)
#else
#define COUNT(i) ((void)0)
#endif

void clj_debug_map_generic(int reason) {
	(void)reason;
	COUNT(reason);
}

void clj_debug_map_stats(int64_t out[CLJ_MAPS_COUNTERS]) {
	for (int i = 0; i < CLJ_MAPS_COUNTERS; i++) {
#if CLJ_DEBUG
		out[i] = atomic_load_explicit(&counters[i], memory_order_relaxed);
#else
		out[i] = -1;
#endif
	}
}

static clj_lock             lock = CLJ_LOCK_INIT;
static _Atomic(clj_shape *) root;
static _Atomic(table *)     shapes; // written under the lock only
static uint32_t             nshapes;
static _Atomic bool         enabled = true;

// ---- shapes

uint32_t  clj_shape_nkeys(const clj_shape *s) { return s->nkeys; }
clj_value clj_shape_key(const clj_shape *s, uint32_t i) { return s->keys[i]; }
bool      clj_shape_is_dictionary(const clj_shape *s) { return atomic_load_explicit(&s->dictionary, memory_order_relaxed); }
uint32_t  clj_debug_shape_children(const clj_shape *s) { return atomic_load_explicit(&s->nchildren, memory_order_relaxed); }

int32_t clj_shape_index(const clj_shape *s, clj_value key) {
	for (uint32_t i = 0; i < s->nkeys; i++) {
		if (s->keys[i] == key) return (int32_t)i;
	}
	return -1;
}

static void *xcalloc(size_t n, size_t size) {
	void *p = calloc(n, size);
	if (!p) clj_fatal("out of memory");
	return p;
}

static uint32_t key_hash(clj_value key) { return clj_hash(key); }

// The canonical order of a key set is `compare`'s (namespace, then name): deterministic across runs and processes,
// where the first-seen order would make printing and seq depend on what ran before. Insertion sort, n ≤ 32.
static void sort_keys(clj_value *keys, uint32_t n) {
	for (uint32_t i = 1; i < n; i++) {
		clj_value k = keys[i];
		uint32_t  j = i;
		int       c;
		while (j > 0 && (clj_compare(keys[j - 1], k, &c), c > 0)) {
			keys[j] = keys[j - 1];
			j--;
		}
		keys[j] = k;
	}
}

// keys sorted, as every shape's are.
static bool same_set(const clj_shape *s, const clj_value *keys, uint32_t n) {
	return s->nkeys == n && memcmp(s->keys, keys, n * sizeof *keys) == 0;
}

static void table_insert(table *t, clj_shape *s) {
	_Atomic(clj_shape *) *b = &t->buckets[s->sethash & (t->cap - 1)];
	atomic_store_explicit(&s->bucket_next, atomic_load_explicit(b, memory_order_relaxed), memory_order_relaxed);
	atomic_store_explicit(b, s, memory_order_release);
}

// Under the lock. The old table is left to readers still walking it: at most log2(cap) of them ever exist.
static table *table_grow(table *old) {
	uint32_t cap = old ? old->cap * 2 : 256;
	table   *t = xcalloc(1, sizeof *t + cap * sizeof *t->buckets);
	t->cap = cap;
	for (uint32_t i = 0; old && i < old->cap; i++) {
		for (clj_shape *s = atomic_load_explicit(&old->buckets[i], memory_order_relaxed); s;) {
			clj_shape *next = atomic_load_explicit(&s->bucket_next, memory_order_relaxed);
			table_insert(t, s);
			s = next;
		}
	}
	atomic_store_explicit(&shapes, t, memory_order_release);
	return t;
}

static clj_shape *table_find(const clj_value *keys, uint32_t n, uint32_t sethash) {
	table *t = atomic_load_explicit(&shapes, memory_order_acquire);
	if (!t) return NULL;
	for (clj_shape *s = atomic_load_explicit(&t->buckets[sethash & (t->cap - 1)], memory_order_acquire); s;
	     s = atomic_load_explicit(&s->bucket_next, memory_order_acquire)) {
		if (s->sethash == sethash && same_set(s, keys, n)) return s;
	}
	return NULL;
}

// Under the lock. NULL only at the cap.
static clj_shape *find_or_make(const clj_value *keys, uint32_t n, uint32_t sethash) {
	clj_shape *found = table_find(keys, n, sethash);
	if (found) return found;
	if (nshapes >= CLJ_SHAPE_MAX_SHAPES) return NULL;
	table *t = atomic_load_explicit(&shapes, memory_order_relaxed);
	if (!t || nshapes >= t->cap) t = table_grow(t);
	clj_shape *s = xcalloc(1, sizeof *s + n * sizeof(clj_value));
	s->nkeys = n;
	s->sethash = sethash;
	memcpy(s->keys, keys, n * sizeof *keys);
	table_insert(t, s);
	nshapes++;
	return s;
}

static clj_shape *root_get(void) { return atomic_load_explicit(&root, memory_order_acquire); }

static clj_shape *root_locked(void) {
	clj_shape *r = root_get();
	if (!r) {
		r = find_or_make(NULL, 0, 0);
		atomic_store_explicit(&root, r, memory_order_release);
	}
	return r;
}

const clj_shape *clj_shape_root(void) {
	clj_shape *r = root_get();
	if (r) return r;
	clj_lock_lock(&lock);
	r = root_locked();
	clj_lock_unlock(&lock);
	return r;
}

static bool keys_qualify(const clj_value *keys, uint32_t n) {
	if (n > CLJ_SHAPE_MAX_KEYS) return false;
	for (uint32_t i = 0; i < n; i++) {
		if (!clj_is_keyword(keys[i])) return false;
		for (uint32_t j = 0; j < i; j++) {
			if (keys[j] == keys[i]) return false;
		}
	}
	return true;
}

const clj_shape *clj_shape_for_keys(const clj_value *given, uint32_t n) {
	if (!clj_shapes_enabled() || !keys_qualify(given, n)) return NULL;
	clj_value keys[CLJ_SHAPE_MAX_KEYS];
	uint32_t  sethash = 0;
	memcpy(keys, given, n * sizeof *keys);
	sort_keys(keys, n);
	for (uint32_t i = 0; i < n; i++) sethash += key_hash(keys[i]);
	const clj_shape *s = table_find(keys, n, sethash);
	if (s) return s;
	clj_lock_lock(&lock);
	root_locked();
	s = find_or_make(keys, n, sethash);
	clj_lock_unlock(&lock);
	return s;
}

static const edge *edge_find(const _Atomic(edge *) *head, clj_value key) {
	for (const edge *e = atomic_load_explicit(head, memory_order_acquire); e; e = e->next) {
		if (e->key == key) return e;
	}
	return NULL;
}

// The edge from `from` to `to`, whose keys are from's plus or minus `key`.
static edge *edge_make(const clj_shape *from, const clj_shape *to, clj_value key, bool add) {
	edge *e = xcalloc(1, sizeof *e);
	e->key = key;
	e->to = to;
	e->identity = true;
	e->index = (uint8_t)(add ? from->nkeys : clj_shape_index(from, key));
	for (uint32_t i = 0; i < to->nkeys; i++) {
		if (to->keys[i] == key) {
			e->perm[i] = NEW_KEY;
			if (i != from->nkeys) e->identity = false;
			continue;
		}
		int32_t p = clj_shape_index(from, to->keys[i]);
		e->perm[i] = (uint8_t)p;
		uint32_t expected = add ? i : (i < e->index ? i : i + 1);
		if ((uint32_t)p != expected) e->identity = false;
	}
	return e;
}

// The shape an assoc of `key` (absent from `from`) leads to; NULL past the caps or through a dictionary shape.
static const edge *edge_add(clj_shape *from, clj_value key) {
	if (clj_shape_is_dictionary(from)) return NULL;
	const edge *e = edge_find(&from->adds, key);
	if (e) return e;
	if (from->nkeys >= CLJ_SHAPE_MAX_KEYS) return NULL;
	clj_lock_lock(&lock);
	e = edge_find(&from->adds, key);
	if (!e) {
		if (from != root_get() && atomic_load_explicit(&from->nchildren, memory_order_relaxed) >= CLJ_SHAPE_MAX_CHILDREN) {
			atomic_store_explicit(&from->dictionary, true, memory_order_relaxed);
		} else {
			clj_value keys[CLJ_SHAPE_MAX_KEYS];
			memcpy(keys, from->keys, from->nkeys * sizeof *keys);
			keys[from->nkeys] = key;
			sort_keys(keys, from->nkeys + 1);
			const clj_shape *to = find_or_make(keys, from->nkeys + 1, from->sethash + key_hash(key));
			if (to) {
				edge *n = edge_make(from, to, key, true);
				n->next = atomic_load_explicit(&from->adds, memory_order_relaxed);
				atomic_store_explicit(&from->adds, n, memory_order_release);
				atomic_fetch_add_explicit(&from->nchildren, 1, memory_order_relaxed);
				e = n;
			}
		}
	}
	clj_lock_unlock(&lock);
	return e;
}

// The shape a dissoc of `key` (held by `from`, not its only key) leads to; NULL at the cap.
static const edge *edge_remove(clj_shape *from, clj_value key) {
	const edge *e = edge_find(&from->removes, key);
	if (e) return e;
	clj_lock_lock(&lock);
	e = edge_find(&from->removes, key);
	if (!e) {
		clj_value keys[CLJ_SHAPE_MAX_KEYS];
		uint32_t  n = 0;
		for (uint32_t i = 0; i < from->nkeys; i++) {
			if (from->keys[i] != key) keys[n++] = from->keys[i];
		}
		const clj_shape *to = find_or_make(keys, n, from->sethash - key_hash(key));
		if (to) {
			edge *m = edge_make(from, to, key, false);
			m->next = atomic_load_explicit(&from->removes, memory_order_relaxed);
			atomic_store_explicit(&from->removes, m, memory_order_release);
			e = m;
		}
	}
	clj_lock_unlock(&lock);
	return e;
}

void clj_shapes_enable(bool on) { atomic_store_explicit(&enabled, on, memory_order_relaxed); }
bool clj_shapes_enabled(void) { return atomic_load_explicit(&enabled, memory_order_relaxed); }

uint32_t clj_debug_shape_count(void) {
	clj_lock_lock(&lock);
	uint32_t n = nshapes;
	clj_lock_unlock(&lock);
	return n;
}

// Bytes the shapes and their transitions hold, for the memory rows of the bench.
size_t clj_debug_shape_bytes(void) {
	clj_lock_lock(&lock);
	size_t total = 0;
	table *t = atomic_load_explicit(&shapes, memory_order_relaxed);
	for (uint32_t i = 0; t && i < t->cap; i++) {
		for (const clj_shape *s = atomic_load_explicit(&t->buckets[i], memory_order_relaxed); s; s = atomic_load_explicit(&s->bucket_next, memory_order_relaxed)) {
			total += sizeof *s + s->nkeys * sizeof(clj_value);
			for (const edge *e = atomic_load_explicit(&s->adds, memory_order_relaxed); e; e = e->next) total += sizeof *e;
			for (const edge *e = atomic_load_explicit(&s->removes, memory_order_relaxed); e; e = e->next) total += sizeof *e;
		}
	}
	if (t) total += sizeof *t + t->cap * sizeof *t->buckets;
	clj_lock_unlock(&lock);
	return total;
}

// ---- shape maps

static clj_shape_map *smap_of(clj_value v) { return clj_shape_map_of(v); }

// Storing into a shared map must keep the invariant that its children are shared.
static void store(clj_header *owner, clj_value *slot, clj_value v) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	*slot = v;
}

static clj_shape_map *smap_alloc(const clj_shape *s) {
	clj_shape_map *m = clj_alloc(&clj_map_type, sizeof *m + s->nkeys * sizeof(clj_value));
	m->h.flags |= CLJ_FLAG_SHAPE;
	m->shape = s;
	return m;
}

clj_value clj_shape_map_alloc(const clj_shape *s) {
	COUNT(CLJ_MAPS_SHAPE_SET);
	return clj_from_ptr(smap_alloc(s));
}

clj_value clj_shape_map_new(const clj_shape *s, const clj_value *vals) {
	COUNT(CLJ_MAPS_SHAPE_SET);
	clj_shape_map *m = smap_alloc(s);
	for (uint32_t i = 0; i < s->nkeys; i++) m->slots[i] = clj_retain(vals[i]);
	return clj_from_ptr(m);
}

void clj_shape_slots_of(const clj_shape *s, const clj_value *keys, uint8_t *slots) {
	for (uint32_t i = 0; i < s->nkeys; i++) slots[i] = (uint8_t)clj_shape_index(s, keys[i]);
}

clj_value clj_shape_map_from_keys(const clj_shape *s, const clj_value *keys, const clj_value *vals) {
	if (memcmp(s->keys, keys, s->nkeys * sizeof *keys) == 0) return clj_shape_map_new(s, vals);
	clj_shape_map *m = smap_alloc(s);
	for (uint32_t i = 0; i < s->nkeys; i++) m->slots[clj_shape_index(s, keys[i])] = clj_retain(vals[i]);
	return clj_from_ptr(m);
}

clj_value clj_shape_map_single(clj_value key, clj_value val) {
	if (!clj_shapes_enabled()) {
		COUNT(CLJ_MAPS_TRIE_OFF);
		return CLJ_UNBOUND;
	}
	if (!clj_is_keyword(key)) {
		COUNT(CLJ_MAPS_TRIE_KEY);
		return CLJ_UNBOUND;
	}
	clj_keyword     *kw = clj_keyword_of(key);
	const clj_shape *s = atomic_load_explicit(&kw->shape1, memory_order_acquire);
	if (!s) {
		const edge *e = edge_add((clj_shape *)clj_shape_root(), key);
		if (!e) {
			COUNT(CLJ_MAPS_TRIE_CAP);
			return CLJ_UNBOUND;
		}
		s = e->to;
		atomic_store_explicit(&kw->shape1, s, memory_order_release);
	}
	COUNT(CLJ_MAPS_SHAPE_EDGE);
	clj_shape_map *m = smap_alloc(s);
	m->slots[0] = clj_retain(val);
	return clj_from_ptr(m);
}

// Consumes map; the entries go in the shape's order, the trie is canonical anyway.
clj_value clj_shape_map_to_hash_map(clj_value map) {
	clj_shape_map *m = smap_of(map);
	clj_value      h = clj_map_empty();
	for (uint32_t i = 0; i < m->shape->nkeys; i++) h = clj_hash_map_assoc(h, m->shape->keys[i], m->slots[i]);
	clj_release(map);
	return h;
}

// Consumes map: the map itself when unique, else a copy with every slot retained.
static clj_shape_map *smap_own(clj_value map) {
	if (clj_is_unique(map)) return smap_of(map);
	clj_shape_map *m = smap_of(map), *c = smap_alloc(m->shape);
	for (uint32_t i = 0; i < m->shape->nkeys; i++) c->slots[i] = clj_retain(m->slots[i]);
	clj_release(map);
	return c;
}

// A map of e->to whose slots come from `map` (consumed) through the edge's permutation, `val` in the new slot.
static clj_value smap_permuted(clj_value map, const edge *e, clj_value val) {
	clj_shape_map *m = smap_of(map), *c = smap_alloc(e->to);
	bool           unique = clj_is_unique(map);
	// the copy stands in for a unique published map, whose children are all shared already
	if (unique) c->h.flags |= m->h.flags & CLJ_FLAG_SHARED;
	for (uint32_t i = 0; i < e->to->nkeys; i++) {
		uint8_t p = e->perm[i];
		c->slots[i] = p == NEW_KEY ? clj_retain(val) : unique ? m->slots[p] : clj_retain(m->slots[p]);
		if (p == NEW_KEY && (c->h.flags & CLJ_FLAG_SHARED)) clj_share(val);
	}
	if (unique) {
		// the moved slots are the copy's now; a removed key's value stays behind
		if (e->to->nkeys < m->shape->nkeys) clj_release(m->slots[e->index]);
		clj_dealloc(&m->h);
	} else {
		clj_release(map);
	}
	return clj_from_ptr(c);
}

clj_value clj_shape_map_assoc(clj_value map, clj_value key, clj_value val) {
	clj_shape_map *m = smap_of(map);
	int32_t        i = clj_shape_index(m->shape, key);
	if (i >= 0) {
		if (m->slots[i] == val) return map;
		m = smap_own(map);
		clj_value old = m->slots[i];
		store(&m->h, &m->slots[i], clj_retain(val));
		clj_release(old);
		return clj_from_ptr(m);
	}
	const edge *e = clj_is_keyword(key) ? edge_add((clj_shape *)m->shape, key) : NULL;
	if (!e) {
		COUNT(!clj_is_keyword(key) ? CLJ_MAPS_TRIE_KEY : clj_shape_is_dictionary(m->shape) ? CLJ_MAPS_TRIE_DICT : m->shape->nkeys >= CLJ_SHAPE_MAX_KEYS ? CLJ_MAPS_TRIE_FULL : CLJ_MAPS_TRIE_CAP);
		return clj_hash_map_assoc(clj_shape_map_to_hash_map(map), key, val);
	}
	if (!e->identity) return smap_permuted(map, e, val);
	uint32_t n = m->shape->nkeys;
	if (clj_is_unique(map)) {
		m = clj_realloc(m, sizeof *m + (n + 1) * sizeof(clj_value));
	} else {
		clj_shape_map *c = smap_alloc(e->to);
		for (uint32_t k = 0; k < n; k++) c->slots[k] = clj_retain(m->slots[k]);
		clj_release(map);
		m = c;
	}
	m->shape = e->to;
	store(&m->h, &m->slots[n], clj_retain(val));
	return clj_from_ptr(m);
}

clj_value clj_shape_map_dissoc(clj_value map, clj_value key) {
	clj_shape_map *m = smap_of(map);
	int32_t        i = clj_shape_index(m->shape, key);
	if (i < 0) return map;
	if (m->shape->nkeys == 1) {
		clj_release(map);
		return clj_map_empty_new();
	}
	const edge *e = edge_remove((clj_shape *)m->shape, key);
	if (!e) {
		COUNT(CLJ_MAPS_TRIE_CAP);
		return clj_hash_map_dissoc(clj_shape_map_to_hash_map(map), key);
	}
	if (!e->identity || !clj_is_unique(map)) return smap_permuted(map, e, CLJ_NIL);
	uint32_t  n = m->shape->nkeys;
	clj_value old = m->slots[i];
	memmove(m->slots + i, m->slots + i + 1, (n - 1 - (uint32_t)i) * sizeof(clj_value));
	m = clj_realloc(m, sizeof *m + (n - 1) * sizeof(clj_value));
	m->shape = e->to;
	clj_release(old);
	return clj_from_ptr(m);
}
