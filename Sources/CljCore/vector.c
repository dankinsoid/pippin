// @ai-generated(solo)
#include <stddef.h>

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
	clj_slot   slots[];
} node;

// The layout is told by `shift`, not a header flag: rc.c overwrites a dead object's flags before each_child.
typedef struct {
	clj_header       h;
	uint32_t         count;
	uint32_t         shift;
	_Atomic uint32_t hash; // see clj_hash_cache_load
	clj_slot         meta; // map or nil; kept across conj/assoc/pop, ignored by equality and hash
	clj_slot         root;
	clj_slot         tail;
} clj_vector;

// Design §4 «Tuples»: up to TUPLE_MAX elements inline after the shared prefix, one allocation, nth one load.
typedef struct {
	clj_header       h;
	uint32_t         count;
	uint32_t         shift;
	_Atomic uint32_t hash;
	clj_slot         meta;
	clj_slot         items[];
} tuple;

enum { TUPLE = 0, TUPLE_MAX = 6 };

_Static_assert(offsetof(tuple, count) == offsetof(clj_vector, count) && offsetof(tuple, shift) == offsetof(clj_vector, shift) &&
                   offsetof(tuple, hash) == offsetof(clj_vector, hash) && offsetof(tuple, meta) == offsetof(clj_vector, meta),
               "the tuple and the trie wrapper share their prefix");

static void node_each_child(void *self, clj_visitor visit, void *ctx) {
	node *n = self;
	for (size_t i = 0; i < n->len; i++) visit(n->slots[i].v, ctx);
}

static const clj_type node_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "vector-node",
	.each_child = node_each_child,
};

static inline node       *node_of(clj_value v) { return clj_to_ptr(v); }
static inline clj_vector *vector_of(clj_value v) { return clj_to_ptr(v); }
static inline tuple      *tuple_of(clj_value v) { return clj_to_ptr(v); }
static inline bool        is_tuple(const clj_vector *v) { return v->shift == TUPLE; }
static inline size_t      tuple_size(uint32_t n) { return sizeof(tuple) + n * sizeof(clj_slot); }
static inline uint32_t    tail_off(uint32_t count) { return count < WIDTH ? 0 : ((count - 1) >> BITS) << BITS; }

static inline size_t slot_index(const node *n, uint32_t i, uint32_t shift) {
	if (n->flags & NODE_RELAXED) clj_fatal("relaxed nodes are not implemented");
	return (i >> shift) & MASK;
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
		for (uint32_t i = n->cap; i < cap; i++) clj_slot_clear(&n->slots[i]);
		n->cap = (uint16_t)cap;
	}
	for (uint32_t i = len; i < n->len; i++) clj_slot_clear(&n->slots[i]);
	n->len = (uint16_t)len;
	return n;
}

static node *node_copy(const node *n) {
	node *c = node_alloc(n->len);
	for (size_t i = 0; i < n->len; i++) clj_slot_init_copied(&c->h, &c->slots[i], clj_retain(n->slots[i].v));
	clj_reach_copy(&c->h, &n->h);
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
	if (i >= tail_off(v->count)) return node_of(v->tail.v);
	node *n = node_of(v->root.v);
	for (uint32_t shift = v->shift; shift > 0; shift -= BITS) n = node_of(n->slots[slot_index(n, i, shift)].v);
	return n;
}

// A trie leaf or the whole tuple: element j is run[j - *start] for *start <= j < *end.
static const clj_slot *run_at(const clj_vector *v, uint32_t i, uint32_t *start, uint32_t *end) {
	if (is_tuple(v)) {
		*start = 0;
		*end = v->count;
		return ((const tuple *)v)->items;
	}
	*start = i & ~(uint32_t)MASK;
	*end = v->count - *start > WIDTH ? *start + WIDTH : v->count;
	return leaf_for(v, i)->slots;
}

// Consumes leaf: a chain of single-child nodes down to it.
static clj_value new_path(uint32_t shift, clj_value leaf) {
	if (shift == 0) return leaf;
	node *n = node_alloc(1);
	clj_slot_init(&n->h, &n->slots[0], new_path(shift - BITS, leaf));
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
		child = push_tail(n->slots[j].v, shift - BITS, count, tail);
	} else {
		child = new_path(shift - BITS, tail);
	}
	if (j == n->len) n = node_resize(n, n->len + 1);
	clj_slot_store(&n->h, &n->slots[j], child);
	return clj_from_ptr(n);
}

// Consumes nv; the caller already holds its own reference to the dropped leaf. nil when the node empties.
static clj_value pop_tail(clj_value nv, uint32_t shift, uint32_t count) {
	node *n = node_own(nv);
	size_t j = n->len - 1;
	CLJ_ASSERT(j == slot_index(n, count - 2, shift), "vector trie out of shape");
	if (shift > BITS) {
		clj_value child = pop_tail(n->slots[j].v, shift - BITS, count);
		if (child != CLJ_NIL) {
			clj_slot_store(&n->h, &n->slots[j], child);
			return clj_from_ptr(n);
		}
	} else {
		clj_release(n->slots[j].v);
	}
	clj_slot_clear(&n->slots[j]);
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
		clj_value old = n->slots[j].v;
		clj_slot_store(&n->h, &n->slots[j], clj_retain(val));
		clj_release(old);
	} else {
		clj_slot_store(&n->h, &n->slots[j], node_assoc(n->slots[j].v, shift - BITS, i, val));
	}
	return clj_from_ptr(n);
}

static void vector_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_vector *v = self;
	visit(v->meta.v, ctx);
	if (is_tuple(v)) {
		tuple *t = self;
		for (uint32_t i = 0; i < t->count; i++) visit(t->items[i].v, ctx);
		return;
	}
	visit(v->root.v, ctx);
	visit(v->tail.v, ctx);
}

// Element by element in either layout, so a tuple and a trie with the same elements hash alike.
static uint32_t vector_hash(void *self) {
	clj_vector *v = self;
	uint32_t h = clj_hash_cache_load(&v->hash);
	if (h) return h;
	h = 1;
	for (uint32_t i = 0, start, end; i < v->count;) {
		const clj_slot *run = run_at(v, i, &start, &end);
		for (; i < end; i++) h = 31 * h + clj_hash(run[i - start].v);
	}
	return clj_hash_cache_store(&v->hash, clj_mix_coll_hash(h, v->count));
}

static bool vector_equals(void *self, clj_value other) {
	if (!clj_is_vector(other)) return clj_has_core(other, CLJ_CORE_SEQUENTIAL) && clj_seq_equals(clj_from_ptr(self), other);
	const clj_vector *a = self, *b = vector_of(other);
	if (a->count != b->count) return false;
	for (uint32_t i = 0, sa, ea, sb, eb; i < a->count;) {
		const clj_slot *ra = run_at(a, i, &sa, &ea), *rb = run_at(b, i, &sb, &eb);
		for (uint32_t end = ea < eb ? ea : eb; i < end; i++) {
			if (!clj_equals(ra[i - sa].v, rb[i - sb].v)) return false;
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
	for (uint32_t i = from, start, end; i < v->count;) {
		const clj_slot *run = run_at(v, i, &start, &end);
		for (; i < end; i++) {
			if (!clj_reducer_step(&r, run[i - start].v)) return clj_reducer_finish(&r);
		}
	}
	return clj_reducer_finish(&r);
}

clj_value clj_vector_reduce_kv(clj_value vec, clj_value f, clj_value init) {
	const clj_vector *v = vector_of(vec);
	clj_reducer       r = clj_reducer_start(f, init, 3);
	for (uint32_t i = 0, start, end; i < v->count;) {
		const clj_slot *run = run_at(v, i, &start, &end);
		for (; i < end; i++) {
			if (!clj_reducer_step_kv(&r, clj_fixnum(i), run[i - start].v)) return clj_reducer_finish(&r);
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

static clj_value vector_meta(clj_value self) { return clj_retain(vector_of(self)->meta.v); }

static clj_vector *vector_own(clj_value vec);

// @ai-generated(guided)
static clj_value vector_with_meta(clj_value self, clj_value m) {
	if (clj_is_nil(m) && clj_is_nil(vector_of(self)->meta.v)) return self;
	clj_vector *v = vector_own(self);
	clj_value   old = v->meta.v;
	clj_slot_store(&v->h, &v->meta, clj_retain(m));
	clj_release(old);
	return clj_from_ptr(v);
}

const clj_type clj_vector_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "vector",
	.core_bits = CLJ_CORE_SEQABLE | CLJ_CORE_SEQUENTIAL | CLJ_CORE_COLL | CLJ_CORE_COUNTED | CLJ_CORE_LOOKUP |
	             CLJ_CORE_ASSOCIATIVE | CLJ_CORE_INDEXED | CLJ_CORE_FN | CLJ_CORE_VECTOR | CLJ_CORE_META | CLJ_CORE_OBJ | CLJ_CORE_REDUCE |
	             CLJ_CORE_EDITABLE,
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
	.root = {(clj_value)&empty_node},
	.tail = {(clj_value)&empty_node},
};

clj_value clj_vector_empty(void) { return clj_from_ptr(&empty_vector); }

uint32_t clj_vector_count(clj_value vec) { return vector_of(vec)->count; }

clj_value clj_vector_nth(clj_value vec, uint32_t i) {
	const clj_vector *v = vector_of(vec);
	if (i >= v->count) clj_fatal("vector index out of bounds");
	if (is_tuple(v)) return ((const tuple *)v)->items[i].v;
	return leaf_for(v, i)->slots[i & MASK].v;
}

static _Atomic bool tuples_on = true;

void clj_tuples_enable(bool on) { atomic_store_explicit(&tuples_on, on, memory_order_relaxed); }
bool clj_tuples_enabled(void) { return atomic_load_explicit(&tuples_on, memory_order_relaxed); }

#if CLJ_DEBUG
static _Atomic int64_t counters[CLJ_VECTORS_COUNTERS];
#define COUNT(i) atomic_fetch_add_explicit(&counters[i], 1, memory_order_relaxed)
#else
#define COUNT(i) ((void)0)
#endif

void clj_debug_vector_stats(int64_t out[CLJ_VECTORS_COUNTERS]) {
	for (int i = 0; i < CLJ_VECTORS_COUNTERS; i++) {
#if CLJ_DEBUG
		out[i] = atomic_load_explicit(&counters[i], memory_order_relaxed);
#else
		out[i] = -1;
#endif
	}
}

// Items borrowed; n <= cap.
static tuple *tuple_new(const clj_value *items, uint32_t n, uint32_t cap) {
	tuple *t = clj_alloc(&clj_vector_type, tuple_size(cap));
	t->count = n;
	t->shift = TUPLE;
	for (uint32_t i = 0; i < n; i++) clj_slot_init(&t->h, &t->items[i], clj_retain(items[i]));
	return t;
}

// Consumes vec; room for cap >= count elements, hash cache cleared.
static tuple *tuple_own(clj_value vec, uint32_t cap) {
	tuple *t = tuple_of(vec);
	if (clj_is_unique(vec)) {
		atomic_store_explicit(&t->hash, 0, memory_order_relaxed);
		return cap > t->count ? clj_realloc(t, tuple_size(cap)) : t;
	}
	tuple *c = tuple_new(clj_slot_values(t->items), t->count, cap);
	clj_slot_init(&c->h, &c->meta, clj_retain(t->meta.v));
	clj_release(vec);
	return c;
}

// Consumes a full tuple: its elements plus val become a trie, which grows as one from here on.
static clj_value tuple_promote(clj_value vec, clj_value val) {
	const tuple *t = tuple_of(vec);
	clj_vector  *v = clj_alloc(&clj_vector_type, sizeof *v);
	node        *tail = node_alloc(t->count + 1);
	for (uint32_t i = 0; i < t->count; i++) clj_slot_init_copied(&tail->h, &tail->slots[i], clj_retain(t->items[i].v));
	clj_reach_copy(&tail->h, &t->h);
	clj_slot_init(&tail->h, &tail->slots[t->count], clj_retain(val));
	v->count = t->count + 1;
	v->shift = BITS;
	clj_slot_init(&v->h, &v->root, clj_from_ptr(&empty_node));
	clj_slot_init(&v->h, &v->tail, clj_from_ptr(tail));
	clj_slot_init(&v->h, &v->meta, clj_retain(t->meta.v));
	clj_release(vec);
	COUNT(CLJ_VECTORS_PROMOTED);
	return clj_from_ptr(v);
}

static clj_value tuple_conj(clj_value vec, clj_value val) {
	uint32_t n = tuple_of(vec)->count;
	if (n == TUPLE_MAX) return tuple_promote(vec, val);
	tuple *t = tuple_own(vec, n + 1);
	clj_slot_store(&t->h, &t->items[n], clj_retain(val));
	t->count = n + 1;
	return clj_from_ptr(t);
}

static clj_value tuple_assoc(clj_value vec, uint32_t i, clj_value val) {
	tuple    *t = tuple_own(vec, tuple_of(vec)->count);
	clj_value old = t->items[i].v;
	clj_slot_store(&t->h, &t->items[i], clj_retain(val));
	clj_release(old);
	return clj_from_ptr(t);
}

// The cell keeps its size, so a conj after it moves nothing.
static clj_value tuple_pop(clj_value vec) {
	tuple    *t = tuple_own(vec, tuple_of(vec)->count);
	uint32_t  n = t->count - 1;
	clj_value last = t->items[n].v;
	clj_slot_clear(&t->items[n]);
	t->count = n;
	clj_release(last);
	return clj_from_ptr(t);
}

clj_value clj_vector_peek(clj_value vec) {
	const clj_vector *v = vector_of(vec);
	return v->count ? clj_vector_nth(vec, v->count - 1) : CLJ_NIL;
}

// Consumes vec; returns a wrapper this operation may mutate, with its hash cache cleared.
static clj_vector *vector_own(clj_value vec) {
	clj_vector *v = vector_of(vec);
	if (is_tuple(v)) return (clj_vector *)tuple_own(vec, v->count);
	if (clj_is_unique(vec)) {
		atomic_store_explicit(&v->hash, 0, memory_order_relaxed);
		return v;
	}
	clj_vector *c = clj_alloc(&clj_vector_type, sizeof *c);
	c->count = v->count;
	c->shift = v->shift;
	clj_slot_init(&c->h, &c->root, clj_retain(v->root.v));
	clj_slot_init(&c->h, &c->tail, clj_retain(v->tail.v));
	clj_slot_init(&c->h, &c->meta, clj_retain(v->meta.v));
	clj_release(vec);
	return c;
}

clj_value clj_vector_conj(clj_value vec, clj_value val) {
	if (is_tuple(vector_of(vec))) return tuple_conj(vec, val);
	if (vec == clj_from_ptr(&empty_vector)) COUNT(CLJ_VECTORS_TRIE_CONJ);
	clj_vector *v = vector_own(vec);
	uint32_t tail_len = v->count - tail_off(v->count);
	if (tail_len < WIDTH) {
		node *t = node_resize(node_own(v->tail.v), tail_len + 1);
		clj_slot_store(&t->h, &t->slots[tail_len], clj_retain(val));
		clj_slot_store(&v->h, &v->tail, clj_from_ptr(t));
	} else {
		clj_value root;
		if ((v->count >> BITS) > (1u << v->shift)) {
			node *r = node_alloc(2);
			clj_slot_init(&r->h, &r->slots[0], v->root.v);
			clj_slot_init(&r->h, &r->slots[1], new_path(v->shift, v->tail.v));
			v->shift += BITS;
			root = clj_from_ptr(r);
		} else {
			root = push_tail(v->root.v, v->shift, v->count, v->tail.v);
		}
		clj_slot_store(&v->h, &v->root, root);
		node *t = node_alloc(1);
		clj_slot_store(&t->h, &t->slots[0], clj_retain(val));
		clj_slot_store(&v->h, &v->tail, clj_from_ptr(t));
	}
	v->count++;
	return clj_from_ptr(v);
}

clj_value clj_vector_assoc(clj_value vec, uint32_t i, clj_value val) {
	uint32_t count = vector_of(vec)->count;
	if (i == count) return clj_vector_conj(vec, val);
	if (i > count) clj_fatal("vector index out of bounds");
	if (is_tuple(vector_of(vec))) return tuple_assoc(vec, i, val);
	clj_vector *v = vector_own(vec);
	if (i >= tail_off(count)) {
		node *t = node_own(v->tail.v);
		clj_value old = t->slots[i & MASK].v;
		clj_slot_store(&t->h, &t->slots[i & MASK], clj_retain(val));
		clj_release(old);
		clj_slot_store(&v->h, &v->tail, clj_from_ptr(t));
	} else {
		clj_slot_store(&v->h, &v->root, node_assoc(v->root.v, v->shift, i, val));
	}
	return clj_from_ptr(v);
}

clj_value clj_vector_pop(clj_value vec) {
	uint32_t count = vector_of(vec)->count;
	if (count == 0) clj_fatal("pop of an empty vector");
	if (count == 1 && clj_is_nil(vector_of(vec)->meta.v)) {
		clj_release(vec);
		return clj_vector_empty();
	}
	if (is_tuple(vector_of(vec))) return tuple_pop(vec);
	clj_vector *v = vector_own(vec);
	if (count == 1) {
		clj_release(v->root.v);
		clj_release(v->tail.v);
		clj_slot_store(&v->h, &v->root, clj_from_ptr(&empty_node));
		clj_slot_store(&v->h, &v->tail, clj_from_ptr(&empty_node));
		v->shift = BITS;
		v->count = 0;
		return clj_from_ptr(v);
	}
	uint32_t tail_len = count - tail_off(count);
	if (tail_len > 1) {
		node *t = node_own(v->tail.v);
		clj_release(t->slots[tail_len - 1].v);
		clj_slot_store(&v->h, &v->tail, clj_from_ptr(node_resize(t, tail_len - 1)));
	} else {
		clj_value tail = clj_retain(clj_from_ptr(leaf_for(v, count - 2)));
		clj_value root = pop_tail(v->root.v, v->shift, count);
		if (root == CLJ_NIL) {
			root = clj_from_ptr(&empty_node);
		} else if (v->shift > BITS && node_of(root)->len == 1) {
			node *r = node_of(root);
			clj_value inner = r->slots[0].v;
			if (clj_is_unique(root)) r->len = 0;
			else clj_retain(inner);
			clj_release(root);
			root = inner;
			v->shift -= BITS;
		}
		clj_slot_store(&v->h, &v->root, root);
		clj_release(v->tail.v);
		clj_slot_store(&v->h, &v->tail, tail);
	}
	v->count--;
	return clj_from_ptr(v);
}

clj_value clj_vector_from_array(const clj_value *items, uint32_t n) {
	if (n && n <= TUPLE_MAX) {
		if (clj_tuples_enabled()) {
			COUNT(CLJ_VECTORS_TUPLE);
			return clj_from_ptr(tuple_new(items, n, n));
		}
		COUNT(CLJ_VECTORS_TRIE_OFF);
	}
	clj_value v = clj_vector_empty();
	for (uint32_t i = 0; i < n; i++) v = clj_vector_conj(v, items[i]);
	return v;
}

void clj_vector_each(clj_value vec, clj_vector_item_fn fn, void *ctx) {
	const clj_vector *v = vector_of(vec);
	for (uint32_t i = 0, start, end; i < v->count;) {
		const clj_slot *run = run_at(v, i, &start, &end);
		for (; i < end; i++) {
			if (!fn(run[i - start].v, ctx)) return;
		}
	}
}

bool clj_vector_is_tuple(clj_value vec) { return is_tuple(vector_of(vec)); }

clj_value clj_debug_vector_root(clj_value vec) { return is_tuple(vector_of(vec)) ? CLJ_NIL : vector_of(vec)->root.v; }
clj_value clj_debug_vector_tail(clj_value vec) { return is_tuple(vector_of(vec)) ? CLJ_NIL : vector_of(vec)->tail.v; }
uint32_t  clj_debug_vector_cached_hash(clj_value vec) { return clj_hash_cache_load(&vector_of(vec)->hash); }
