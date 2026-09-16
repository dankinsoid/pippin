// @ai-generated(solo)
#include <stdlib.h>

#include "clj/coll.h"
#include "clj/compare.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/list.h"
#include "clj/number.h"
#include "clj/reduce.h"
#include "clj/sorted.h"
#include "clj/vector.h"

// Red links lean left, which halves the rebalance cases (Sedgewick's LLRB).
typedef struct {
	clj_header h;
	clj_value  key;
	clj_value  val;
	clj_value  left;
	clj_value  right;
	uint32_t   red;
} tnode;

static void tnode_each_child(void *self, clj_visitor visit, void *ctx) {
	tnode *n = self;
	visit(n->key, ctx);
	visit(n->val, ctx);
	visit(n->left, ctx);
	visit(n->right, ctx);
}

static const clj_type tnode_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "sorted-node",
	.each_child = tnode_each_child,
};

static inline tnode    *tnode_of(clj_value v) { return clj_to_ptr(v); }
static inline bool      is_red(clj_value node) { return !clj_is_nil(node) && tnode_of(node)->red; }
static inline clj_value left_of(clj_value node) { return clj_is_nil(node) ? CLJ_NIL : tnode_of(node)->left; }
static inline clj_value right_of(clj_value node) { return clj_is_nil(node) ? CLJ_NIL : tnode_of(node)->right; }

// Storing into a shared object must keep the invariant that its children are shared.
static void store(clj_header *owner, clj_value *slot, clj_value v) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	*slot = v;
}

static clj_value node_new(clj_value key, clj_value val) {
	tnode *n = clj_alloc(&tnode_type, sizeof *n);
	n->key = clj_retain(key);
	n->val = clj_retain(val);
	n->red = 1;
	return clj_from_ptr(n);
}

// Consumes node; returns a node this operation may mutate.
static tnode *node_own(clj_value node) {
	tnode *n = tnode_of(node);
	if (clj_is_unique(node)) return n;
	tnode *c = clj_alloc(&tnode_type, sizeof *c);
	c->key = clj_retain(n->key);
	c->val = clj_retain(n->val);
	c->left = clj_retain(n->left);
	c->right = clj_retain(n->right);
	c->red = n->red;
	clj_release(node);
	return c;
}

// The reference moves to the caller, so a throw below leaves nothing dangling in h.
static clj_value take_child(tnode *h, bool left) {
	clj_value *slot = left ? &h->left : &h->right;
	clj_value  c = *slot;
	*slot = CLJ_NIL;
	return c;
}

static void set_child(tnode *h, bool left, clj_value c) {
	clj_value *slot = left ? &h->left : &h->right;
	CLJ_ASSERT(clj_is_nil(*slot), "set_child over an occupied slot");
	store(&h->h, slot, c);
}

static tnode *rotate_left(tnode *h) {
	tnode *x = node_own(take_child(h, false));
	set_child(h, false, take_child(x, true));
	x->red = h->red;
	h->red = 1;
	set_child(x, true, clj_from_ptr(h));
	return x;
}

static tnode *rotate_right(tnode *h) {
	tnode *x = node_own(take_child(h, true));
	set_child(h, true, take_child(x, false));
	x->red = h->red;
	h->red = 1;
	set_child(x, false, clj_from_ptr(h));
	return x;
}

static tnode *flip_colors(tnode *h) {
	CLJ_ASSERT(!clj_is_nil(h->left) && !clj_is_nil(h->right), "flip_colors needs both children");
	h->red = !h->red;
	for (int i = 0; i < 2; i++) {
		bool   left = i == 0;
		tnode *c = node_own(take_child(h, left));
		c->red = !c->red;
		set_child(h, left, clj_from_ptr(c));
	}
	return h;
}

static tnode *fix_up(tnode *h) {
	if (is_red(h->right) && !is_red(h->left)) h = rotate_left(h);
	if (is_red(h->left) && is_red(left_of(h->left))) h = rotate_right(h);
	if (is_red(h->left) && is_red(h->right)) h = flip_colors(h);
	return h;
}

static tnode *move_red_left(tnode *h) {
	h = flip_colors(h);
	if (is_red(left_of(h->right))) {
		set_child(h, false, clj_from_ptr(rotate_right(node_own(take_child(h, false)))));
		h = rotate_left(h);
		h = flip_colors(h);
	}
	return h;
}

static tnode *move_red_right(tnode *h) {
	h = flip_colors(h);
	if (is_red(left_of(h->left))) {
		h = rotate_right(h);
		h = flip_colors(h);
	}
	return h;
}

// ---- comparator

clj_value clj_sorted_compare(clj_value c, clj_value a, clj_value b) {
	clj_call call = clj_call_prepare(clj_sorted_of(c)->cmp, 2);
	int      r;
	if (!clj_compare_with(&call, a, b, &r)) return CLJ_THROWN;
	return clj_fixnum(r);
}

// CLJ_THROWN, the node holding key (borrowed), or nil.
static clj_value node_find(clj_value node, clj_value key, const clj_call *call) {
	while (!clj_is_nil(node)) {
		int c;
		if (!clj_compare_with(call, key, tnode_of(node)->key, &c)) return CLJ_THROWN;
		if (c == 0) return node;
		node = c < 0 ? tnode_of(node)->left : tnode_of(node)->right;
	}
	return CLJ_NIL;
}

// ---- insert and delete

typedef struct {
	bool changed;
	bool added;
} tedit;

// Consumes node; owned result or CLJ_THROWN, which released it.
static clj_value node_assoc(clj_value node, clj_value key, clj_value val, const clj_call *call, tedit *e) {
	if (clj_is_nil(node)) {
		e->changed = e->added = true;
		return node_new(key, val);
	}
	int c;
	if (!clj_compare_with(call, key, tnode_of(node)->key, &c)) {
		clj_release(node);
		return CLJ_THROWN;
	}
	if (c == 0) {
		if (tnode_of(node)->val == val) return node;
		tnode    *h = node_own(node);
		clj_value old = h->val;
		h->val = CLJ_NIL;
		store(&h->h, &h->val, clj_retain(val));
		clj_release(old);
		e->changed = true;
		return clj_from_ptr(h);
	}
	tnode    *h = node_own(node);
	bool      left = c < 0;
	clj_value nc = node_assoc(take_child(h, left), key, val, call, e);
	if (nc == CLJ_THROWN) {
		clj_release(clj_from_ptr(h));
		return CLJ_THROWN;
	}
	set_child(h, left, nc);
	return clj_from_ptr(fix_up(h));
}

// Leftmost node, borrowed.
static clj_value min_node(clj_value node) {
	while (!clj_is_nil(tnode_of(node)->left)) node = tnode_of(node)->left;
	return node;
}

// Rightmost node, borrowed.
static clj_value max_node(clj_value node) {
	while (!clj_is_nil(tnode_of(node)->right)) node = tnode_of(node)->right;
	return node;
}

// Consumes node, which is non-nil; owned result or nil.
static clj_value node_delete_min(clj_value node) {
	tnode *h = node_own(node);
	if (clj_is_nil(h->left)) {
		clj_release(clj_from_ptr(h));
		return CLJ_NIL;
	}
	if (!is_red(h->left) && !is_red(left_of(h->left))) h = move_red_left(h);
	set_child(h, true, node_delete_min(take_child(h, true)));
	return clj_from_ptr(fix_up(h));
}

// Consumes node; key is known to be in this subtree. Owned result, nil, or CLJ_THROWN.
static clj_value node_delete(clj_value node, clj_value key, const clj_call *call) {
	tnode *h = node_own(node);
	int    c;
	if (!clj_compare_with(call, key, h->key, &c)) {
		clj_release(clj_from_ptr(h));
		return CLJ_THROWN;
	}
	if (c < 0) {
		if (!is_red(h->left) && !is_red(left_of(h->left))) h = move_red_left(h);
		clj_value nl = node_delete(take_child(h, true), key, call);
		if (nl == CLJ_THROWN) {
			clj_release(clj_from_ptr(h));
			return CLJ_THROWN;
		}
		set_child(h, true, nl);
		return clj_from_ptr(fix_up(h));
	}
	if (is_red(h->left)) h = rotate_right(h);
	if (!clj_compare_with(call, key, h->key, &c)) {
		clj_release(clj_from_ptr(h));
		return CLJ_THROWN;
	}
	if (c == 0 && clj_is_nil(h->right)) {
		clj_release(clj_from_ptr(h));
		return CLJ_NIL;
	}
	if (!is_red(h->right) && !is_red(left_of(h->right))) h = move_red_right(h);
	if (!clj_compare_with(call, key, h->key, &c)) {
		clj_release(clj_from_ptr(h));
		return CLJ_THROWN;
	}
	if (c == 0) {
		clj_value  m = min_node(h->right);
		clj_value  nk = clj_retain(tnode_of(m)->key), nv = clj_retain(tnode_of(m)->val);
		clj_value  ok = h->key, ov = h->val;
		h->key = h->val = CLJ_NIL;
		store(&h->h, &h->key, nk);
		store(&h->h, &h->val, nv);
		clj_release(ok);
		clj_release(ov);
		set_child(h, false, node_delete_min(take_child(h, false)));
		return clj_from_ptr(fix_up(h));
	}
	clj_value nr = node_delete(take_child(h, false), key, call);
	if (nr == CLJ_THROWN) {
		clj_release(clj_from_ptr(h));
		return CLJ_THROWN;
	}
	set_child(h, false, nr);
	return clj_from_ptr(fix_up(h));
}

// ---- walks

typedef struct {
	clj_sorted_entry_fn fn;
	void               *ctx;
} each_ctx;

static bool each_asc(clj_value node, const each_ctx *c) {
	if (clj_is_nil(node)) return true;
	tnode *n = tnode_of(node);
	return each_asc(n->left, c) && c->fn(n->key, n->val, c->ctx) && each_asc(n->right, c);
}

static bool each_desc(clj_value node, const each_ctx *c) {
	if (clj_is_nil(node)) return true;
	tnode *n = tnode_of(node);
	return each_desc(n->right, c) && c->fn(n->key, n->val, c->ctx) && each_desc(n->left, c);
}

void clj_sorted_each(clj_value c, clj_sorted_entry_fn fn, void *ctx) {
	each_ctx e = {fn, ctx};
	each_asc(clj_sorted_of(c)->root, &e);
}

static bool walk_from(clj_value node, clj_value key, const clj_call *call, bool asc, const each_ctx *c, bool *thrown) {
	if (clj_is_nil(node)) return true;
	tnode *n = tnode_of(node);
	int    r;
	if (!clj_compare_with(call, key, n->key, &r)) {
		*thrown = true;
		return false;
	}
	if (asc) {
		if (r > 0) return walk_from(n->right, key, call, asc, c, thrown);
		if (!walk_from(n->left, key, call, asc, c, thrown)) return false;
		return c->fn(n->key, n->val, c->ctx) && each_asc(n->right, c);
	}
	if (r < 0) return walk_from(n->left, key, call, asc, c, thrown);
	if (!walk_from(n->right, key, call, asc, c, thrown)) return false;
	return c->fn(n->key, n->val, c->ctx) && each_desc(n->left, c);
}

// ---- wrapper

static clj_value sorted_new(const clj_type *type, clj_value cmp) {
	clj_sorted *s = clj_alloc(type, sizeof *s);
	s->cmp = clj_retain(cmp);
	return clj_from_ptr(s);
}

clj_value clj_sorted_map_new(clj_value cmp) { return sorted_new(&clj_sorted_map_type, cmp); }
clj_value clj_sorted_set_new(clj_value cmp) { return sorted_new(&clj_sorted_set_type, cmp); }

clj_value clj_sorted_empty(clj_value c) { return sorted_new(clj_header_of(c)->type, clj_sorted_of(c)->cmp); }

static void sorted_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_sorted *s = self;
	visit(s->root, ctx);
	visit(s->cmp, ctx);
	visit(s->meta, ctx);
}

// root is owned; a unique wrapper had its root slot emptied and takes it back.
static clj_value sorted_commit(clj_value coll, bool unique, clj_value root, tedit e, int32_t delta) {
	clj_sorted *s = clj_sorted_of(coll);
	if (unique) {
		if (e.changed) atomic_store_explicit(&s->hash, 0, memory_order_relaxed);
		store(&s->h, &s->root, root);
		if (e.added) s->count = (uint32_t)((int32_t)s->count + delta);
		return coll;
	}
	if (!e.changed) {
		clj_release(root);
		return coll;
	}
	clj_sorted *c = clj_alloc(clj_header_of(coll)->type, sizeof *c);
	c->count = (uint32_t)((int32_t)s->count + (e.added ? delta : 0));
	c->cmp = clj_retain(s->cmp);
	c->meta = clj_retain(s->meta);
	c->root = root;
	clj_release(coll);
	return clj_from_ptr(c);
}

// Blackening the root is what keeps the black height countable from the top.
static clj_value blacken(clj_value root) {
	if (!is_red(root)) return root;
	tnode *r = node_own(root);
	r->red = 0;
	return clj_from_ptr(r);
}

clj_value clj_sorted_assoc(clj_value map, clj_value key, clj_value val) {
	clj_sorted *s = clj_sorted_of(map);
	clj_call    call = clj_call_prepare(s->cmp, 2);
	bool        unique = clj_is_unique(map);
	clj_value   root;
	if (unique) {
		root = s->root;
		s->root = CLJ_NIL;
	} else {
		root = clj_retain(s->root);
	}
	tedit e = {0};
	root = node_assoc(root, key, val, &call, &e);
	if (root == CLJ_THROWN) {
		clj_release(map);
		return CLJ_THROWN;
	}
	return sorted_commit(map, unique, blacken(root), e, 1);
}

clj_value clj_sorted_dissoc(clj_value coll, clj_value key) {
	clj_sorted *s = clj_sorted_of(coll);
	clj_call    call = clj_call_prepare(s->cmp, 2);
	clj_value   found = node_find(s->root, key, &call);
	if (found == CLJ_THROWN) {
		clj_release(coll);
		return CLJ_THROWN;
	}
	if (clj_is_nil(found)) return coll;
	bool      unique = clj_is_unique(coll);
	clj_value root;
	if (unique) {
		root = s->root;
		s->root = CLJ_NIL;
	} else {
		root = clj_retain(s->root);
	}
	if (!is_red(left_of(root)) && !is_red(right_of(root))) {
		tnode *r = node_own(root);
		r->red = 1;
		root = clj_from_ptr(r);
	}
	root = node_delete(root, key, &call);
	if (root == CLJ_THROWN) {
		clj_release(coll);
		return CLJ_THROWN;
	}
	tedit e = {true, true};
	return sorted_commit(coll, unique, blacken(root), e, -1);
}

clj_value clj_sorted_get(clj_value c, clj_value key, clj_value not_found) {
	clj_sorted *s = clj_sorted_of(c);
	clj_call    call = clj_call_prepare(s->cmp, 2);
	clj_value   n = node_find(s->root, key, &call);
	if (n == CLJ_THROWN) return CLJ_THROWN;
	if (clj_is_nil(n)) return not_found;
	return clj_is_sorted_set(c) ? tnode_of(n)->key : tnode_of(n)->val;
}

clj_value clj_sorted_contains(clj_value c, clj_value key) {
	clj_sorted *s = clj_sorted_of(c);
	clj_call    call = clj_call_prepare(s->cmp, 2);
	clj_value   n = node_find(s->root, key, &call);
	if (n == CLJ_THROWN) return CLJ_THROWN;
	return clj_bool(!clj_is_nil(n));
}

// ---- seqs

typedef struct {
	clj_value *items;
	size_t     n;
} collect_ctx;

static bool collect_entry(clj_value key, clj_value val, void *ctx) {
	collect_ctx *c = ctx;
	c->items[c->n++] = key;
	c->items[c->n++] = val;
	return true;
}

// Eager list, as a map's and a set's seq are: the tree has no O(1) view.
static clj_value seq_of(clj_value coll, clj_value *entries, size_t pairs) {
	if (!pairs) {
		free(entries);
		return CLJ_NIL;
	}
	clj_value list;
	if (clj_is_sorted_set(coll)) {
		for (size_t i = 0; i < pairs; i++) entries[i] = entries[2 * i];
		list = clj_list_from_array(entries, pairs);
	} else {
		clj_value *made = malloc(pairs * sizeof *made);
		if (!made) clj_fatal("out of memory");
		for (size_t i = 0; i < pairs; i++) made[i] = clj_vector_from_array(entries + 2 * i, 2);
		list = clj_list_from_array(made, pairs);
		for (size_t i = 0; i < pairs; i++) clj_release(made[i]);
		free(made);
	}
	free(entries);
	return list;
}

static void entry_buffer(clj_value coll, collect_ctx *c) {
	c->items = malloc((2 * (size_t)clj_sorted_count(coll) + 1) * sizeof *c->items);
	if (!c->items) clj_fatal("out of memory");
	c->n = 0;
}

clj_value clj_sorted_seq(clj_value c, bool ascending) {
	collect_ctx cc;
	entry_buffer(c, &cc);
	each_ctx e = {collect_entry, &cc};
	if (ascending) each_asc(clj_sorted_of(c)->root, &e);
	else each_desc(clj_sorted_of(c)->root, &e);
	return seq_of(c, cc.items, cc.n / 2);
}

clj_value clj_sorted_seq_from(clj_value c, clj_value key, bool ascending) {
	collect_ctx cc;
	entry_buffer(c, &cc);
	each_ctx e = {collect_entry, &cc};
	clj_call  call = clj_call_prepare(clj_sorted_of(c)->cmp, 2);
	bool      thrown = false;
	walk_from(clj_sorted_of(c)->root, key, &call, ascending, &e, &thrown);
	if (thrown) {
		free(cc.items);
		return CLJ_THROWN;
	}
	return seq_of(c, cc.items, cc.n / 2);
}

// ---- slots

static clj_value sorted_seq_slot(clj_value self) { return clj_sorted_seq(self, true); }
static clj_value sorted_count(clj_value self) { return clj_fixnum(clj_sorted_count(self)); }

static clj_value sorted_lookup(clj_value self, clj_value key, clj_value not_found) {
	clj_value v = clj_sorted_get(self, key, not_found);
	return v == CLJ_THROWN ? v : clj_retain(v);
}

static clj_value sorted_invoke(clj_value self, const clj_value *args, size_t n) {
	if (n != 1 && n != 2) return clj_arity_error(self, n);
	return sorted_lookup(self, args[0], n == 2 ? args[1] : CLJ_NIL);
}

clj_value clj_sorted_conj(clj_value coll, clj_value x) {
	if (clj_is_sorted_set(coll)) return clj_sorted_assoc(coll, x, CLJ_NIL);
	if (clj_is_nil(x)) return coll;
	if (clj_is_vector(x) && clj_vector_count(x) == 2) return clj_sorted_assoc(coll, clj_vector_nth(x, 0), clj_vector_nth(x, 1));
	if (clj_has_core(x, CLJ_CORE_MAP)) {
		clj_value s = clj_seq(x);
		if (s == CLJ_THROWN) {
			clj_release(coll);
			return CLJ_THROWN;
		}
		clj_seq_iter it = clj_seq_iter_start(s);
		clj_value    item;
		while (coll != CLJ_THROWN && clj_seq_iter_next(&it, &item)) coll = clj_sorted_conj(coll, item);
		clj_seq_iter_close(&it);
		clj_release(s);
		if (it.thrown && coll != CLJ_THROWN) {
			clj_release(coll);
			return CLJ_THROWN;
		}
		return coll;
	}
	clj_release(coll);
	return clj_throw_msg("Vector arg to map conj must be a pair");
}

static bool reduce_pair(clj_value key, clj_value val, void *ctx) {
	clj_value pair[2] = {key, val};
	clj_value entry = clj_vector_from_array(pair, 2);
	bool      more = clj_reducer_step(ctx, entry);
	clj_release(entry);
	return more;
}

static clj_value sorted_map_reduce(clj_value self, clj_value f, clj_value init) {
	clj_reducer r = clj_reducer_start(f, init, 2);
	clj_sorted_each(self, reduce_pair, &r);
	return clj_reducer_finish(&r);
}

static bool reduce_element(clj_value key, clj_value val, void *ctx) {
	(void)val;
	return clj_reducer_step(ctx, key);
}

static clj_value sorted_set_reduce(clj_value self, clj_value f, clj_value init) {
	clj_reducer r = clj_reducer_start(f, init, 2);
	clj_sorted_each(self, reduce_element, &r);
	return clj_reducer_finish(&r);
}

static bool reduce_kv_entry(clj_value key, clj_value val, void *ctx) { return clj_reducer_step_kv(ctx, key, val); }

clj_value clj_sorted_map_reduce_kv(clj_value map, clj_value f, clj_value init) {
	clj_reducer r = clj_reducer_start(f, init, 3);
	clj_sorted_each(map, reduce_kv_entry, &r);
	return clj_reducer_finish(&r);
}

static bool hash_entry(clj_value key, clj_value val, void *ctx) {
	*(uint32_t *)ctx += clj_mix_coll_hash(31 * (31 + clj_hash(key)) + clj_hash(val), 2);
	return true;
}

static bool hash_element(clj_value key, clj_value val, void *ctx) {
	(void)val;
	*(uint32_t *)ctx += clj_hash(key);
	return true;
}

// The hash-map / hash-set formula, so equal content hashes equal across representations.
static uint32_t sorted_hash(void *self) {
	clj_sorted *s = self;
	uint32_t    h = clj_hash_cache_load(&s->hash);
	if (h) return h;
	clj_value me = clj_from_ptr(s);
	uint32_t  sum = 0;
	clj_sorted_each(me, clj_is_sorted_set(me) ? hash_element : hash_entry, &sum);
	return clj_hash_cache_store(&s->hash, clj_mix_coll_hash(sum, s->count));
}

typedef struct {
	clj_value other;
	bool      equal;
} equals_ctx;

static bool equals_entry(clj_value key, clj_value val, void *ctx) {
	equals_ctx *ec = ctx;
	clj_value   found = clj_equals_lookup(ec->other, key, CLJ_UNBOUND);
	ec->equal = found != CLJ_UNBOUND && clj_equals(found, val);
	clj_release(found);
	return ec->equal;
}

static bool equals_element(clj_value key, clj_value val, void *ctx) {
	(void)val;
	equals_ctx *ec = ctx;
	clj_value   found = clj_equals_lookup(ec->other, key, CLJ_UNBOUND);
	ec->equal = found != CLJ_UNBOUND;
	clj_release(found);
	return ec->equal;
}

static bool sorted_equals(void *self, clj_value other) {
	clj_value me = clj_from_ptr(self);
	bool      set = clj_is_sorted_set(me);
	if (!clj_has_core(other, set ? CLJ_CORE_SET : CLJ_CORE_MAP)) return false;
	clj_value n = clj_count(other);
	if (n == CLJ_THROWN) {
		clj_release(clj_take_pending());
		return false;
	}
	if ((uint32_t)clj_fixnum_val(n) != clj_sorted_count(me)) return false;
	equals_ctx ec = {other, true};
	clj_sorted_each(me, set ? equals_element : equals_entry, &ec);
	return ec.equal;
}

static clj_value sorted_meta(clj_value self) { return clj_retain(clj_sorted_of(self)->meta); }

static clj_value sorted_with_meta(clj_value self, clj_value m) {
	clj_sorted *s = clj_sorted_of(self);
	if (clj_is_nil(m) && clj_is_nil(s->meta)) return self;
	if (!clj_is_unique(self)) {
		clj_sorted *c = clj_alloc(clj_header_of(self)->type, sizeof *c);
		c->count = s->count;
		atomic_store_explicit(&c->hash, clj_hash_cache_load(&s->hash), memory_order_relaxed);
		c->root = clj_retain(s->root);
		c->cmp = clj_retain(s->cmp);
		clj_release(self);
		s = c;
	}
	clj_value old = s->meta;
	s->meta = CLJ_NIL;
	store(&s->h, &s->meta, clj_retain(m));
	clj_release(old);
	return clj_from_ptr(s);
}

const clj_type clj_sorted_map_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "sorted-map",
	.core_bits = CLJ_CORE_SEQABLE | CLJ_CORE_COLL | CLJ_CORE_COUNTED | CLJ_CORE_LOOKUP | CLJ_CORE_ASSOCIATIVE | CLJ_CORE_FN | CLJ_CORE_MAP |
	             CLJ_CORE_META | CLJ_CORE_OBJ | CLJ_CORE_REDUCE,
	.each_child = sorted_each_child,
	.hash = sorted_hash,
	.equals = sorted_equals,
	.seq = sorted_seq_slot,
	.count = sorted_count,
	.lookup = sorted_lookup,
	.conj = clj_sorted_conj,
	.assoc = clj_sorted_assoc,
	.dissoc = clj_sorted_dissoc,
	.reduce = sorted_map_reduce,
	.invoke = sorted_invoke,
	.meta = sorted_meta,
	.with_meta = sorted_with_meta,
};

const clj_type clj_sorted_set_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "sorted-set",
	.core_bits = CLJ_CORE_SEQABLE | CLJ_CORE_COLL | CLJ_CORE_COUNTED | CLJ_CORE_FN | CLJ_CORE_SET | CLJ_CORE_META | CLJ_CORE_OBJ |
	             CLJ_CORE_REDUCE,
	.each_child = sorted_each_child,
	.hash = sorted_hash,
	.equals = sorted_equals,
	.seq = sorted_seq_slot,
	.count = sorted_count,
	.lookup = sorted_lookup,
	.conj = clj_sorted_conj,
	.dissoc = clj_sorted_dissoc,
	.reduce = sorted_set_reduce,
	.invoke = sorted_invoke,
	.meta = sorted_meta,
	.with_meta = sorted_with_meta,
};

// ---- test hooks

clj_value clj_debug_sorted_root(clj_value c) { return clj_sorted_of(c)->root; }

typedef struct {
	const clj_call *call;
	uint32_t        count;
} check_ctx;

// Black height of the subtree, or -1 when an invariant fails.
static int check_node(clj_value node, check_ctx *cc) {
	if (clj_is_nil(node)) return 0;
	tnode *n = tnode_of(node);
	if (is_red(n->right)) return -1;
	if (n->red && is_red(n->left)) return -1;
	int l = check_node(n->left, cc), r = check_node(n->right, cc);
	if (l < 0 || r < 0 || l != r) return -1;
	// max(left) < key < min(right) at every node is the whole ordering, since the subtrees are checked too.
	int c;
	if (!clj_is_nil(n->left)) {
		if (!clj_compare_with(cc->call, tnode_of(max_node(n->left))->key, n->key, &c)) {
			clj_release(clj_take_pending());
			return -1;
		}
		if (c >= 0) return -1;
	}
	if (!clj_is_nil(n->right)) {
		if (!clj_compare_with(cc->call, n->key, tnode_of(min_node(n->right))->key, &c)) {
			clj_release(clj_take_pending());
			return -1;
		}
		if (c >= 0) return -1;
	}
	cc->count++;
	return l + (n->red ? 0 : 1);
}

bool clj_debug_sorted_valid(clj_value c) {
	clj_sorted *s = clj_sorted_of(c);
	if (is_red(s->root)) return false;
	clj_call  call = clj_call_prepare(s->cmp, 2);
	check_ctx cc = {&call, 0};
	if (check_node(s->root, &cc) < 0) return false;
	return cc.count == s->count;
}
