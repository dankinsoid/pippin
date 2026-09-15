// @ai-generated(solo)
#include "clj/coll.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/reduce.h"
#include "clj/seq.h"
#include "clj/vector.h"

enum { BITS = 5, WIDTH = 32, MASK = 31 };

enum { NODE_RELAXED = 1u << 0 };

// `relaxed` and `sizes` (cumulative counts per slot, NULL here) are reserved for RRB nodes.
// cap >= len: slots are over-allocated so conj does not move the tail on every element.
typedef struct {
	clj_header h;
	uint16_t   len;
	uint16_t   cap;
	uint32_t   flags;
	uint32_t  *sizes;
	clj_value  slots[];
} node;

typedef struct {
	clj_header       h;
	uint32_t         count;
	uint32_t         shift;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	clj_value        root;
	clj_value        tail;
	clj_value        meta; // map or nil; kept across conj/assoc/pop, ignored by equality and hash
} clj_vector;

static void node_each_child(void *self, clj_visitor visit, void *ctx) {
	node *n = self;
	for (size_t i = 0; i < n->len; i++) visit(n->slots[i], ctx);
}

static const clj_type node_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "vector-node",
	.each_child = node_each_child,
};

static inline node       *node_of(clj_value v) { return clj_to_ptr(v); }
static inline clj_vector *vector_of(clj_value v) { return clj_to_ptr(v); }
static inline uint32_t    tail_off(uint32_t count) { return count < WIDTH ? 0 : ((count - 1) >> BITS) << BITS; }

static inline size_t slot_index(const node *n, uint32_t i, uint32_t shift) {
	if (n->flags & NODE_RELAXED) clj_fatal("relaxed nodes are not implemented");
	return (i >> shift) & MASK;
}

// Storing into a shared owner must keep the invariant that its children are shared.
static void store(clj_header *owner, clj_value *slot, clj_value v) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	*slot = v;
}

// Powers of two up to 8, then a full node: a 32-slot leaf is 288 bytes and lands in the 320 size
// class either way, so a growing tail pays 4 moves per 32 conj instead of 12 and small vectors stay small.
static uint32_t cap_for(uint32_t len) {
	if (len > 8) return WIDTH;
	uint32_t c = 1;
	while (c < len) c <<= 1;
	return c;
}

static node *node_alloc(uint32_t len) {
	uint32_t cap = cap_for(len);
	node *n = clj_alloc(&node_type, sizeof *n + cap * sizeof(clj_value));
	n->len = (uint16_t)len;
	n->cap = (uint16_t)cap;
	return n;
}

// n unique; slots past the old len are nil until stored, vacated slots must already be released.
static node *node_resize(node *n, uint32_t len) {
	if (len > n->cap) {
		uint32_t cap = cap_for(len);
		n = clj_realloc(n, sizeof *n + cap * sizeof(clj_value));
		for (uint32_t i = n->cap; i < cap; i++) n->slots[i] = CLJ_NIL;
		n->cap = (uint16_t)cap;
	}
	for (uint32_t i = len; i < n->len; i++) n->slots[i] = CLJ_NIL;
	n->len = (uint16_t)len;
	return n;
}

static node *node_copy(const node *n) {
	node *c = node_alloc(n->len);
	for (size_t i = 0; i < n->len; i++) c->slots[i] = clj_retain(n->slots[i]);
	return c;
}

// Consumes v; returns a node this operation may mutate.
static node *node_own(clj_value v) {
	if (clj_is_unique(v)) return node_of(v);
	node *c = node_copy(node_of(v));
	clj_release(v);
	return c;
}

static node empty_node = {.h = {1, CLJ_FLAG_IMMORTAL, &node_type}};

static node *leaf_for(const clj_vector *v, uint32_t i) {
	if (i >= tail_off(v->count)) return node_of(v->tail);
	node *n = node_of(v->root);
	for (uint32_t shift = v->shift; shift > 0; shift -= BITS) n = node_of(n->slots[slot_index(n, i, shift)]);
	return n;
}

// Consumes leaf: a chain of single-child nodes down to it.
static clj_value new_path(uint32_t shift, clj_value leaf) {
	if (shift == 0) return leaf;
	node *n = node_alloc(1);
	n->slots[0] = new_path(shift - BITS, leaf);
	return clj_from_ptr(n);
}

// Consumes nv and tail; count is the element count before the push, tail holds the last WIDTH of them.
static clj_value push_tail(clj_value nv, uint32_t shift, uint32_t count, clj_value tail) {
	node *n = node_own(nv);
	size_t j = slot_index(n, count - 1, shift);
	clj_value child;
	if (shift == BITS) {
		child = tail;
	} else if (j < n->len) {
		child = push_tail(n->slots[j], shift - BITS, count, tail);
	} else {
		child = new_path(shift - BITS, tail);
	}
	if (j == n->len) n = node_resize(n, n->len + 1);
	store(&n->h, &n->slots[j], child);
	return clj_from_ptr(n);
}

// Consumes nv; the caller already holds its own reference to the dropped leaf. nil when the node empties.
static clj_value pop_tail(clj_value nv, uint32_t shift, uint32_t count) {
	node *n = node_own(nv);
	size_t j = n->len - 1;
	CLJ_ASSERT(j == slot_index(n, count - 2, shift), "vector trie out of shape");
	if (shift > BITS) {
		clj_value child = pop_tail(n->slots[j], shift - BITS, count);
		if (child != CLJ_NIL) {
			store(&n->h, &n->slots[j], child);
			return clj_from_ptr(n);
		}
	} else {
		clj_release(n->slots[j]);
	}
	n->slots[j] = CLJ_NIL;
	if (j == 0) {
		clj_release(clj_from_ptr(n));
		return CLJ_NIL;
	}
	return clj_from_ptr(node_resize(n, (uint32_t)j));
}

// Consumes nv.
static clj_value node_assoc(clj_value nv, uint32_t shift, uint32_t i, clj_value val) {
	node *n = node_own(nv);
	size_t j = slot_index(n, i, shift);
	if (shift == 0) {
		clj_value old = n->slots[j];
		store(&n->h, &n->slots[j], clj_retain(val));
		clj_release(old);
	} else {
		store(&n->h, &n->slots[j], node_assoc(n->slots[j], shift - BITS, i, val));
	}
	return clj_from_ptr(n);
}

static void vector_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_vector *v = self;
	visit(v->root, ctx);
	visit(v->tail, ctx);
	visit(v->meta, ctx);
}

static uint32_t vector_hash(void *self) {
	clj_vector *v = self;
	uint32_t h = clj_hash_cache_load(&v->hash);
	if (h) return h;
	h = 1;
	for (uint32_t base = 0; base < v->count; base += WIDTH) {
		const node *leaf = leaf_for(v, base);
		for (size_t i = 0; i < leaf->len; i++) h = 31 * h + clj_hash(leaf->slots[i]);
	}
	return clj_hash_cache_store(&v->hash, clj_mix_coll_hash(h, v->count));
}

static bool vector_equals(void *self, clj_value other) {
	if (!clj_is_vector(other)) return clj_has_core(other, CLJ_CORE_SEQUENTIAL) && clj_seq_equals(clj_from_ptr(self), other);
	const clj_vector *a = self, *b = vector_of(other);
	if (a->count != b->count) return false;
	for (uint32_t base = 0; base < a->count; base += WIDTH) {
		const node *la = leaf_for(a, base), *lb = leaf_for(b, base);
		for (size_t i = 0; i < la->len; i++) {
			if (!clj_equals(la->slots[i], lb->slots[i])) return false;
		}
	}
	return true;
}

static clj_value vector_seq(clj_value self) { return vector_of(self)->count ? clj_vector_seq_new(self, 0) : CLJ_NIL; }

static clj_value vector_first(clj_value self) { return vector_of(self)->count ? clj_retain(clj_vector_nth(self, 0)) : CLJ_NIL; }

static clj_value vector_next(clj_value self) { return vector_of(self)->count > 1 ? clj_vector_seq_new(self, 1) : CLJ_NIL; }

static clj_value vector_count(clj_value self) { return clj_fixnum(vector_of(self)->count); }

// Leaf by leaf: one trie descent per 32 elements.
clj_value clj_vector_reduce_from(clj_value vec, uint32_t from, clj_value f, clj_value init) {
	const clj_vector *v = vector_of(vec);
	clj_reducer       r = clj_reducer_start(f, init, 2);
	uint32_t          i = from, count = v->count;
	while (i < count) {
		const node *leaf = leaf_for(v, i);
		uint32_t    end = (i | MASK) + 1 < count ? (i | MASK) + 1 : count;
		for (; i < end; i++) {
			if (!clj_reducer_step(&r, leaf->slots[i & MASK])) return clj_reducer_finish(&r);
		}
	}
	return clj_reducer_finish(&r);
}

clj_value clj_vector_reduce_kv(clj_value vec, clj_value f, clj_value init) {
	const clj_vector *v = vector_of(vec);
	clj_reducer       r = clj_reducer_start(f, init, 3);
	uint32_t          i = 0, count = v->count;
	while (i < count) {
		const node *leaf = leaf_for(v, i);
		uint32_t    end = (i | MASK) + 1 < count ? (i | MASK) + 1 : count;
		for (; i < end; i++) {
			if (!clj_reducer_step_kv(&r, clj_fixnum(i), leaf->slots[i & MASK])) return clj_reducer_finish(&r);
		}
	}
	return clj_reducer_finish(&r);
}

static clj_value vector_reduce(clj_value self, clj_value f, clj_value init) { return clj_vector_reduce_from(self, 0, f, init); }

static clj_value vector_lookup(clj_value self, clj_value key, clj_value not_found) {
	if (clj_is_fixnum(key)) {
		intptr_t i = clj_fixnum_val(key);
		if (i >= 0 && (uintptr_t)i < vector_of(self)->count) return clj_retain(clj_vector_nth(self, (uint32_t)i));
	}
	return clj_retain(not_found);
}

static clj_value vector_invoke(clj_value self, const clj_value *args, size_t n) {
	if (n != 1) return clj_arity_error(self, n);
	return clj_nth(self, args[0], false, CLJ_NIL);
}

static clj_value vector_meta(clj_value self) { return clj_retain(vector_of(self)->meta); }

static clj_vector *vector_own(clj_value vec);

// @ai-generated(guided)
static clj_value vector_with_meta(clj_value self, clj_value m) {
	if (clj_is_nil(m) && clj_is_nil(vector_of(self)->meta)) return self;
	clj_vector *v = vector_own(self);
	clj_value   old = v->meta;
	store(&v->h, &v->meta, clj_retain(m));
	clj_release(old);
	return clj_from_ptr(v);
}

const clj_type clj_vector_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "vector",
	.core_bits = CLJ_CORE_SEQABLE | CLJ_CORE_SEQUENTIAL | CLJ_CORE_COLL | CLJ_CORE_COUNTED | CLJ_CORE_LOOKUP |
	             CLJ_CORE_ASSOCIATIVE | CLJ_CORE_INDEXED | CLJ_CORE_FN | CLJ_CORE_VECTOR | CLJ_CORE_META | CLJ_CORE_OBJ | CLJ_CORE_REDUCE,
	.each_child = vector_each_child,
	.hash = vector_hash,
	.equals = vector_equals,
	.seq = vector_seq,
	.first = vector_first,
	.next = vector_next,
	.count = vector_count,
	.lookup = vector_lookup,
	.conj = clj_vector_conj,
	.reduce = vector_reduce,
	.invoke = vector_invoke,
	.meta = vector_meta,
	.with_meta = vector_with_meta,
};

static clj_vector empty_vector = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_vector_type},
	.shift = BITS,
	.root = (clj_value)&empty_node,
	.tail = (clj_value)&empty_node,
};

clj_value clj_vector_empty(void) { return clj_from_ptr(&empty_vector); }

uint32_t clj_vector_count(clj_value vec) { return vector_of(vec)->count; }

clj_value clj_vector_nth(clj_value vec, uint32_t i) {
	const clj_vector *v = vector_of(vec);
	if (i >= v->count) clj_fatal("vector index out of bounds");
	return leaf_for(v, i)->slots[i & MASK];
}

clj_value clj_vector_peek(clj_value vec) {
	const clj_vector *v = vector_of(vec);
	return v->count ? clj_vector_nth(vec, v->count - 1) : CLJ_NIL;
}

// Consumes vec; returns a wrapper this operation may mutate, with its hash cache cleared.
static clj_vector *vector_own(clj_value vec) {
	clj_vector *v = vector_of(vec);
	if (clj_is_unique(vec)) {
		atomic_store_explicit(&v->hash, 0, memory_order_relaxed);
		return v;
	}
	clj_vector *c = clj_alloc(&clj_vector_type, sizeof *c);
	c->count = v->count;
	c->shift = v->shift;
	c->root = clj_retain(v->root);
	c->tail = clj_retain(v->tail);
	c->meta = clj_retain(v->meta);
	clj_release(vec);
	return c;
}

clj_value clj_vector_conj(clj_value vec, clj_value val) {
	clj_vector *v = vector_own(vec);
	uint32_t tail_len = v->count - tail_off(v->count);
	if (tail_len < WIDTH) {
		node *t = node_resize(node_own(v->tail), tail_len + 1);
		store(&t->h, &t->slots[tail_len], clj_retain(val));
		store(&v->h, &v->tail, clj_from_ptr(t));
	} else {
		clj_value root;
		if ((v->count >> BITS) > (1u << v->shift)) {
			node *r = node_alloc(2);
			r->slots[0] = v->root;
			r->slots[1] = new_path(v->shift, v->tail);
			v->shift += BITS;
			root = clj_from_ptr(r);
		} else {
			root = push_tail(v->root, v->shift, v->count, v->tail);
		}
		store(&v->h, &v->root, root);
		node *t = node_alloc(1);
		store(&t->h, &t->slots[0], clj_retain(val));
		store(&v->h, &v->tail, clj_from_ptr(t));
	}
	v->count++;
	return clj_from_ptr(v);
}

clj_value clj_vector_assoc(clj_value vec, uint32_t i, clj_value val) {
	uint32_t count = vector_of(vec)->count;
	if (i == count) return clj_vector_conj(vec, val);
	if (i > count) clj_fatal("vector index out of bounds");
	clj_vector *v = vector_own(vec);
	if (i >= tail_off(count)) {
		node *t = node_own(v->tail);
		clj_value old = t->slots[i & MASK];
		store(&t->h, &t->slots[i & MASK], clj_retain(val));
		clj_release(old);
		store(&v->h, &v->tail, clj_from_ptr(t));
	} else {
		store(&v->h, &v->root, node_assoc(v->root, v->shift, i, val));
	}
	return clj_from_ptr(v);
}

clj_value clj_vector_pop(clj_value vec) {
	uint32_t count = vector_of(vec)->count;
	if (count == 0) clj_fatal("pop of an empty vector");
	if (count == 1 && clj_is_nil(vector_of(vec)->meta)) {
		clj_release(vec);
		return clj_vector_empty();
	}
	clj_vector *v = vector_own(vec);
	if (count == 1) {
		clj_release(v->root);
		clj_release(v->tail);
		v->root = v->tail = clj_from_ptr(&empty_node);
		v->shift = BITS;
		v->count = 0;
		return clj_from_ptr(v);
	}
	uint32_t tail_len = count - tail_off(count);
	if (tail_len > 1) {
		node *t = node_own(v->tail);
		clj_release(t->slots[tail_len - 1]);
		store(&v->h, &v->tail, clj_from_ptr(node_resize(t, tail_len - 1)));
	} else {
		clj_value tail = clj_retain(clj_from_ptr(leaf_for(v, count - 2)));
		clj_value root = pop_tail(v->root, v->shift, count);
		if (root == CLJ_NIL) {
			root = clj_from_ptr(&empty_node);
		} else if (v->shift > BITS && node_of(root)->len == 1) {
			node *r = node_of(root);
			clj_value inner = r->slots[0];
			if (clj_is_unique(root)) r->len = 0;
			else clj_retain(inner);
			clj_release(root);
			root = inner;
			v->shift -= BITS;
		}
		store(&v->h, &v->root, root);
		clj_release(v->tail);
		store(&v->h, &v->tail, tail);
	}
	v->count--;
	return clj_from_ptr(v);
}

clj_value clj_vector_from_array(const clj_value *items, uint32_t n) {
	clj_value v = clj_vector_empty();
	for (uint32_t i = 0; i < n; i++) v = clj_vector_conj(v, items[i]);
	return v;
}

void clj_vector_each(clj_value vec, clj_vector_item_fn fn, void *ctx) {
	const clj_vector *v = vector_of(vec);
	for (uint32_t base = 0; base < v->count; base += WIDTH) {
		const node *leaf = leaf_for(v, base);
		for (size_t i = 0; i < leaf->len; i++) {
			if (!fn(leaf->slots[i], ctx)) return;
		}
	}
}

clj_value clj_debug_vector_root(clj_value vec) { return vector_of(vec)->root; }
clj_value clj_debug_vector_tail(clj_value vec) { return vector_of(vec)->tail; }
uint32_t  clj_debug_vector_cached_hash(clj_value vec) { return clj_hash_cache_load(&vector_of(vec)->hash); }
