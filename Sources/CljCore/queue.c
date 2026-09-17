// @ai-generated(solo)
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/queue.h"
#include "clj/reduce.h"

static void queue_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_queue *q = self;
	visit(q->front, ctx);
	visit(q->rear, ctx);
	visit(q->meta, ctx);
}

static void store(clj_header *owner, clj_value *slot, clj_value v) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	*slot = v;
}

// The singleton's rear is nil, which only the copy taken here ever replaces.
static clj_queue *queue_own(clj_value self) {
	if (clj_is_unique(self)) return clj_queue_of(self);
	const clj_queue *q = clj_queue_of(self);
	clj_queue       *c = clj_alloc(&clj_queue_type, sizeof *c);
	c->count = q->count;
	c->front = clj_retain(q->front);
	c->rear = clj_is_nil(q->rear) ? clj_vector_empty() : clj_retain(q->rear);
	c->meta = clj_retain(q->meta);
	clj_release(self);
	return c;
}

static uint32_t rear_count(const clj_queue *q) { return clj_is_nil(q->rear) ? 0 : clj_vector_count(q->rear); }

// One list of the front's items followed by the rear's: the JVM's Seq view, materialized.
static clj_value queue_seq(clj_value self) {
	const clj_queue *q = clj_queue_of(self);
	if (!q->count) return CLJ_NIL;
	uint32_t rn = rear_count(q);
	if (!rn) return clj_retain(q->front);
	size_t     n;
	clj_value  keep;
	clj_value *front = clj_seq_items(q->front, &n, &keep);
	if (!front) return CLJ_THROWN;
	clj_value *items = malloc((n + rn) * sizeof *items);
	if (!items) clj_fatal("out of memory");
	memcpy(items, front, n * sizeof *items);
	for (uint32_t i = 0; i < rn; i++) items[n + i] = clj_vector_nth(q->rear, i);
	clj_value list = clj_list_from_array(items, n + rn);
	free(items);
	free(front);
	clj_release(keep);
	return list;
}

static clj_value queue_first(clj_value self) { return clj_queue_peek(self); }

static clj_value queue_count(clj_value self) { return clj_fixnum(clj_queue_of(self)->count); }

static uint32_t queue_hash(void *self) { return clj_seq_hash(clj_from_ptr(self)); }

static bool queue_equals(void *self, clj_value other) { return clj_has_core(other, CLJ_CORE_SEQUENTIAL) && clj_seq_equals(clj_from_ptr(self), other); }

static clj_value queue_conj(clj_value self, clj_value x) {
	clj_queue *q = queue_own(self);
	if (clj_is_nil(q->front)) {
		store(&q->h, &q->front, clj_list_new(x, CLJ_NIL));
	} else {
		clj_value rear = clj_vector_conj(q->rear, x);
		if (rear != q->rear) store(&q->h, &q->rear, rear);
	}
	q->count++;
	return clj_from_ptr(q);
}

static clj_value queue_reduce(clj_value self, clj_value f, clj_value init) {
	const clj_queue *q = clj_queue_of(self);
	if (!q->count) return clj_reduce_empty(f, init);
	clj_reducer  r = clj_reducer_start(f, init, 2);
	clj_seq_iter it = clj_seq_iter_start(q->front);
	clj_value    item;
	bool         go = true;
	while (go && clj_seq_iter_next(&it, &item)) go = clj_reducer_step(&r, item);
	if (it.thrown) {
		clj_reducer_drop(&r);
		return CLJ_THROWN;
	}
	clj_seq_iter_close(&it);
	for (uint32_t i = 0, n = rear_count(q); go && i < n; i++) go = clj_reducer_step(&r, clj_vector_nth(q->rear, i));
	return clj_reducer_finish(&r);
}

static clj_value queue_meta(clj_value self) { return clj_retain(clj_queue_of(self)->meta); }

static clj_value queue_with_meta(clj_value self, clj_value m) {
	if (clj_is_nil(m) && clj_is_nil(clj_queue_of(self)->meta)) return self;
	clj_queue *q = queue_own(self);
	clj_value  old = q->meta;
	store(&q->h, &q->meta, clj_retain(m));
	clj_release(old);
	return clj_from_ptr(q);
}

const clj_type clj_queue_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "PersistentQueue",
	.core_bits = CLJ_CORE_SEQABLE | CLJ_CORE_SEQUENTIAL | CLJ_CORE_COLL | CLJ_CORE_COUNTED | CLJ_CORE_LIST | CLJ_CORE_META | CLJ_CORE_OBJ | CLJ_CORE_REDUCE,
	.each_child = queue_each_child,
	.hash = queue_hash,
	.equals = queue_equals,
	.seq = queue_seq,
	.first = queue_first,
	.count = queue_count,
	.conj = queue_conj,
	.reduce = queue_reduce,
	.meta = queue_meta,
	.with_meta = queue_with_meta,
};

static clj_queue empty_queue = {.h = {1, CLJ_FLAG_IMMORTAL, &clj_queue_type}};

clj_value clj_queue_empty(void) { return clj_from_ptr(&empty_queue); }

clj_value clj_queue_peek(clj_value self) {
	const clj_queue *q = clj_queue_of(self);
	return q->count ? clj_first(q->front) : CLJ_NIL;
}

clj_value clj_queue_pop(clj_value self) {
	if (!clj_queue_of(self)->count) return self;
	clj_queue *q = queue_own(self);
	clj_value  front = clj_next(q->front);
	if (front == CLJ_THROWN) {
		clj_release(clj_from_ptr(q));
		return CLJ_THROWN;
	}
	clj_value old_front = q->front;
	if (clj_is_nil(front)) {
		// The rear becomes the front; the vector lives on behind the seq view.
		front = clj_seq(q->rear);
		clj_value old_rear = q->rear;
		q->rear = clj_vector_empty();
		clj_release(old_rear);
	}
	store(&q->h, &q->front, front);
	clj_release(old_front);
	q->count--;
	return clj_from_ptr(q);
}

// ---- builtins

static clj_value b_queue_pop(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_queue(args[0])) return clj_throw_msg("queue-pop* expects a queue, got: %s", clj_type_name(args[0]));
	return clj_queue_pop(clj_retain(args[0]));
}

static void bind(clj_value ns, const char *name, clj_value val) {
	clj_value sym = clj_symbol_from_cstr(name);
	clj_var_bind_root(clj_ns_intern(ns, sym), val);
	clj_release(sym);
}

// The JVM spelling of the class and its static: a namespace stands in for the class, so
// clojure.lang.PersistentQueue/EMPTY resolves as any ns/var does.
void clj_queue_install(void) {
	clj_value core = clj_ns_core();
	clj_value type = clj_from_ptr((void *)&clj_queue_type);
	bind(core, "PersistentQueue", type);
	bind(core, "clojure.lang.PersistentQueue", type);
	clj_builtin_bind("queue-pop*", b_queue_pop, 1, 1);
	clj_value ns_name = clj_symbol_from_cstr("clojure.lang.PersistentQueue");
	bind(clj_ns_find_or_create(ns_name), "EMPTY", clj_queue_empty());
	clj_release(ns_name);
}
