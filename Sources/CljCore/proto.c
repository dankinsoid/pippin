// @ai-generated(guided)
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/number.h"
#include "clj/printer.h"
#include "clj/proto.h"
#include "clj/seq.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"

// ---- snapshots

typedef struct {
	clj_value proto; // retained, shared
	clj_value fns;   // vector with a fn or nil per method; retained, shared
} proto_entry;

typedef struct {
	uint32_t    n;
	proto_entry entries[];
} proto_table;

typedef struct {
	const clj_type *key; // NULL: free slot
	proto_table    *table;
} side_slot;

typedef struct {
	uint32_t  cap, n; // cap is a power of two
	side_slot slots[];
} side_table;

// One writer at a time; readers never take it.
static pthread_mutex_t       lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic(side_table *) side;
static _Atomic uint64_t      epoch;
static _Atomic uint32_t      next_proto_id;

// A thread's dispatch window; a writer frees a retired snapshot only after every window has closed.
typedef struct reader {
	_Atomic uint32_t active;
	struct reader   *next;
} reader;

static reader               *readers; // pushed under lock, never removed
static _Thread_local reader *my_reader;

static reader *reader_here(void) {
	if (my_reader) return my_reader;
	reader *r = malloc(sizeof *r);
	if (!r) clj_fatal("out of memory");
	atomic_init(&r->active, 0);
	pthread_mutex_lock(&lock);
	r->next = readers;
	readers = r;
	pthread_mutex_unlock(&lock);
	my_reader = r;
	return r;
}

// seq_cst on both sides (Dekker): a reader either shows up to the writer's scan or sees the new snapshot.
static reader *window_open(void) {
	reader *r = reader_here();
	atomic_store_explicit(&r->active, 1, memory_order_seq_cst);
	return r;
}

static void window_close(reader *r) { atomic_store_explicit(&r->active, 0, memory_order_seq_cst); }

static void wait_readers(void) {
	for (reader *r = readers; r; r = r->next) {
		while (atomic_load_explicit(&r->active, memory_order_seq_cst)) sched_yield();
	}
}

uint64_t clj_proto_epoch(void) { return atomic_load_explicit(&epoch, memory_order_acquire); }

// ---- pseudo-descriptors: dispatch keys for immediates, Object and the core interfaces

#define PSEUDO(nm) {.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type}, .name = nm}

static const clj_type nil_type = PSEUDO("nil");
static const clj_type fixnum_type = PSEUDO("fixnum");
static const clj_type boolean_type = PSEUDO("boolean");
static const clj_type char_type = PSEUDO("char");
static const clj_type object_type = PSEUDO("Object");

typedef struct {
	const char *name;
	uint64_t    bits;
	clj_type    type;
	clj_value   proto; // the clojure.core var's value, made by clj_proto_install
} core_interface;

#define IFACE(nm, b) {nm, b, PSEUDO(nm), CLJ_NIL}

// Most specific first: the order a miss on the concrete type falls back through.
static core_interface interfaces[] = {
	IFACE("IPersistentMap", CLJ_CORE_MAP),
	IFACE("IPersistentVector", CLJ_CORE_VECTOR),
	IFACE("IPersistentList", CLJ_CORE_LIST),
	IFACE("IExceptionInfo", CLJ_CORE_ERROR),
	IFACE("Indexed", CLJ_CORE_INDEXED),
	IFACE("Associative", CLJ_CORE_ASSOCIATIVE),
	IFACE("ILookup", CLJ_CORE_LOOKUP),
	IFACE("Counted", CLJ_CORE_COUNTED),
	IFACE("ISeq", CLJ_CORE_SEQ),
	IFACE("Sequential", CLJ_CORE_SEQUENTIAL),
	IFACE("IPersistentCollection", CLJ_CORE_COLL),
	IFACE("IFn", CLJ_CORE_FN),
	IFACE("Seqable", CLJ_CORE_SEQABLE),
};

enum { NINTERFACES = sizeof interfaces / sizeof *interfaces };

const clj_type *clj_dispatch_type(clj_value v) {
	if (clj_is_ptr(v)) return clj_type_of(v);
	if (clj_is_nil(v)) return &nil_type;
	if (clj_is_fixnum(v)) return &fixnum_type;
	if (clj_is_bool(v)) return &boolean_type;
	if (clj_is_char(v)) return &char_type;
	clj_fatal("protocol dispatch on a non-value");
}

const clj_type *clj_object_type(void) { return &object_type; }

static const core_interface *interface_of(clj_value proto) {
	uint64_t bits = clj_protocol_of(proto)->core_bits;
	for (size_t i = 0; i < NINTERFACES; i++) {
		if (interfaces[i].bits == bits) return &interfaces[i];
	}
	clj_fatal("core interface without a descriptor");
}

// The descriptor a type designator names, or NULL with the exception pending.
static const clj_type *designated_type(clj_value type) {
	if (clj_is_nil(type)) return &nil_type;
	if (clj_is_type(type)) return clj_to_ptr(type);
	if (clj_is_protocol(type) && clj_protocol_of(type)->core_bits) return &interface_of(type)->type;
	clj_value text = clj_pr_str(type);
	if (text == CLJ_THROWN) return NULL;
	clj_throw_msg("%s is not a type", clj_string_bytes(text));
	clj_release(text);
	return NULL;
}

// ---- lookup, inside a window

static uint32_t hash_ptr(const clj_type *t) { return clj_fmix32((uint32_t)((uintptr_t)t >> 4)); }

static proto_table *side_find(const side_table *s, const clj_type *t) {
	if (!s) return NULL;
	uint32_t mask = s->cap - 1;
	for (uint32_t i = hash_ptr(t) & mask;; i = (i + 1) & mask) {
		const side_slot *slot = &s->slots[i];
		if (slot->key == t) return slot->table;
		if (!slot->key) return NULL;
	}
}

static proto_table *table_of(const clj_type *t) {
	if (t->h.flags & CLJ_FLAG_IMMORTAL) return side_find(atomic_load_explicit(&side, memory_order_seq_cst), t);
	return __atomic_load_n((void *const *)&t->user_protos, __ATOMIC_SEQ_CST);
}

static clj_value table_find(const proto_table *tbl, clj_value proto) {
	if (!tbl) return CLJ_NIL;
	for (uint32_t i = 0; i < tbl->n; i++) {
		if (tbl->entries[i].proto == proto) return tbl->entries[i].fns;
	}
	return CLJ_NIL;
}

// The type's own entry, then the core interfaces it implements, then Object. Borrowed while the window is open.
static clj_value find_fns(const clj_type *t, clj_value proto) {
	clj_value fns = table_find(table_of(t), proto);
	if (!clj_is_nil(fns)) return fns;
	if (t->core_bits) {
		for (size_t i = 0; i < NINTERFACES; i++) {
			if ((t->core_bits & interfaces[i].bits) != interfaces[i].bits) continue;
			fns = table_find(table_of(&interfaces[i].type), proto);
			if (!clj_is_nil(fns)) return fns;
		}
	}
	return table_find(table_of(&object_type), proto);
}

// Owned impl of method idx for v, or nil.
static clj_value impl_of(clj_value proto, uint32_t idx, clj_value v) {
	const clj_type *t = clj_dispatch_type(v);
	reader         *r = window_open();
	clj_value       fns = find_fns(t, proto);
	clj_value       f = clj_is_nil(fns) ? CLJ_NIL : clj_retain(clj_vector_nth(fns, idx));
	window_close(r);
	return f;
}

static bool type_satisfies(const clj_type *t, clj_value proto) {
	reader   *r = window_open();
	clj_value fns = find_fns(t, proto);
	window_close(r);
	return !clj_is_nil(fns);
}

// ---- protocol objects

static void protocol_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_protocol *p = self;
	visit(p->name, ctx);
	visit(p->methods, ctx);
	visit(p->sigs, ctx);
}

static uint32_t identity_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool identity_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

const clj_type clj_protocol_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "protocol",
	.each_child = protocol_each_child,
	.hash = identity_hash,
	.equals = identity_equals,
};

static clj_value qualify(clj_value name) {
	if (!clj_is_nil(clj_symbol_ns(name))) return clj_retain(name);
	return clj_symbol_new(clj_symbol_name(clj_ns_name(clj_ns_current())), clj_symbol_name(name));
}

static bool is_unqualified_symbol(clj_value v) { return clj_is_symbol(v) && clj_is_nil(clj_symbol_ns(v)); }

static clj_value bad_sigs(clj_value sigs) {
	clj_value text = clj_pr_str(sigs);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("protocol* expects [[method [params...]] ...], got: %s", clj_string_bytes(text));
	clj_release(text);
	return r;
}

static clj_value protocol_alloc(clj_value qualified_name, clj_value methods, clj_value sigs, uint64_t core_bits) {
	clj_protocol *p = clj_alloc(&clj_protocol_type, sizeof *p);
	p->id = atomic_fetch_add_explicit(&next_proto_id, 1, memory_order_relaxed) + 1;
	p->core_bits = core_bits;
	p->name = clj_retain(qualified_name);
	p->methods = clj_retain(methods);
	p->sigs = clj_retain(sigs);
	return clj_from_ptr(p);
}

clj_value clj_protocol_new(clj_value name, clj_value sigs) {
	if (!clj_is_symbol(name)) return clj_throw_msg("protocol name must be a symbol, got: %s", clj_type_name(name));
	if (!clj_is_vector(sigs)) return bad_sigs(sigs);
	uint32_t  n = clj_vector_count(sigs);
	clj_value methods = clj_vector_empty(), arities = clj_vector_empty();
	for (uint32_t i = 0; i < n; i++) {
		clj_value sig = clj_vector_nth(sigs, i);
		if (!clj_is_vector(sig) || clj_vector_count(sig) != 2 || !is_unqualified_symbol(clj_vector_nth(sig, 0)) || !clj_is_vector(clj_vector_nth(sig, 1))) {
			clj_release(methods);
			clj_release(arities);
			return bad_sigs(sigs);
		}
		clj_value method = clj_vector_nth(sig, 0), params = clj_vector_nth(sig, 1);
		for (uint32_t j = 0; j < clj_vector_count(params); j++) {
			if (!clj_is_vector(clj_vector_nth(params, j))) {
				clj_release(methods);
				clj_release(arities);
				return bad_sigs(sigs);
			}
		}
		for (uint32_t j = 0; j < i; j++) {
			if (clj_equals(clj_vector_nth(methods, j), method)) {
				clj_release(methods);
				clj_release(arities);
				return clj_throw_msg("Duplicate method %s in protocol %s", clj_string_bytes(clj_symbol_name(method)), clj_string_bytes(clj_symbol_name(name)));
			}
		}
		methods = clj_vector_conj(methods, method);
		arities = clj_vector_conj(arities, params);
	}
	clj_value qualified = qualify(name);
	clj_value p = protocol_alloc(qualified, methods, arities, 0);
	clj_release(qualified);
	clj_release(methods);
	clj_release(arities);
	return p;
}

// ---- method fns

typedef struct {
	clj_value proto; // borrowed: the fn's code slot keeps the protocol alive
	uint32_t  idx;
} method_ctx;

static clj_value no_impl(const method_ctx *m, clj_value v) {
	const clj_protocol *p = clj_protocol_of(m->proto);
	clj_value           method = clj_vector_nth(p->methods, m->idx);
	return clj_throw_msg("No implementation of method: :%s of protocol: #'%s/%s found for type: %s", clj_string_bytes(clj_symbol_name(method)),
	                     clj_string_bytes(clj_symbol_ns(p->name)), clj_string_bytes(clj_symbol_name(p->name)), clj_type_name(v));
}

static clj_value method_invoke(void *ctx, const clj_value *args, size_t n) {
	const method_ctx *m = ctx;
	clj_value         f = impl_of(m->proto, m->idx, args[0]);
	if (clj_is_nil(f)) return no_impl(m, args[0]);
	clj_value r = clj_invoke(f, args, n);
	clj_release(f);
	return r;
}

static bool is_amp(clj_value v) { return is_unqualified_symbol(v) && strcmp(clj_string_bytes(clj_symbol_name(v)), "&") == 0; }

clj_value clj_protocol_method(clj_value proto, uint32_t idx) {
	const clj_protocol *p = clj_protocol_of(proto);
	CLJ_ASSERT(idx < clj_vector_count(p->methods), "protocol method index out of range");
	clj_value sigs = clj_vector_nth(p->sigs, idx);
	uint32_t  min = UINT32_MAX, max = 0;
	for (uint32_t i = 0; i < clj_vector_count(sigs); i++) {
		clj_value params = clj_vector_nth(sigs, i);
		uint32_t  count = clj_vector_count(params), fixed = count;
		for (uint32_t j = 0; j < count; j++) {
			if (is_amp(clj_vector_nth(params, j))) fixed = j;
		}
		if (fixed < min) min = fixed;
		if (fixed < count) max = CLJ_ARITY_ANY;
		else if (max != CLJ_ARITY_ANY && fixed > max) max = fixed;
	}
	if (min == UINT32_MAX) max = CLJ_ARITY_ANY;
	if (min < 1 || min == UINT32_MAX) min = 1;
	method_ctx *ctx = malloc(sizeof *ctx);
	if (!ctx) clj_fatal("out of memory");
	ctx->proto = proto;
	ctx->idx = idx;
	clj_value name = clj_symbol_new(clj_symbol_ns(p->name), clj_symbol_name(clj_vector_nth(p->methods, idx)));
	clj_value f = clj_fn_native_ctx(name, method_invoke, ctx, free, min, max);
	clj_release(name);
	clj_fn_of(f)->code = clj_retain(proto);
	return f;
}

// ---- extend

static proto_table *table_with(const proto_table *old, clj_value proto, clj_value fns) {
	uint32_t n = old ? old->n : 0, at = n;
	for (uint32_t i = 0; i < n; i++) {
		if (old->entries[i].proto == proto) at = i;
	}
	uint32_t     count = at == n ? n + 1 : n;
	proto_table *t = malloc(sizeof *t + count * sizeof *t->entries);
	if (!t) clj_fatal("out of memory");
	t->n = count;
	for (uint32_t i = 0; i < n; i++) t->entries[i] = old->entries[i];
	t->entries[at] = (proto_entry){proto, fns};
	for (uint32_t i = 0; i < count; i++) {
		clj_retain(t->entries[i].proto);
		clj_retain(t->entries[i].fns);
	}
	return t;
}

static void table_free(proto_table *t) {
	if (!t) return;
	for (uint32_t i = 0; i < t->n; i++) {
		clj_release(t->entries[i].proto);
		clj_release(t->entries[i].fns);
	}
	free(t);
}

static void side_put(side_table *s, const clj_type *key, proto_table *table) {
	uint32_t mask = s->cap - 1;
	for (uint32_t i = hash_ptr(key) & mask;; i = (i + 1) & mask) {
		side_slot *slot = &s->slots[i];
		if (slot->key == key) {
			slot->table = table;
			return;
		}
		if (!slot->key) {
			*slot = (side_slot){key, table};
			s->n++;
			return;
		}
	}
}

static side_table *side_with(const side_table *old, const clj_type *key, proto_table *table) {
	uint32_t n = old ? old->n : 0, cap = old ? old->cap : 16;
	if ((n + 1) * 2 > cap) cap *= 2;
	side_table *s = calloc(1, sizeof *s + cap * sizeof *s->slots);
	if (!s) clj_fatal("out of memory");
	s->cap = cap;
	for (uint32_t i = 0; old && i < old->cap; i++) {
		if (old->slots[i].key) side_put(s, old->slots[i].key, old->slots[i].table);
	}
	side_put(s, key, table);
	return s;
}

typedef struct {
	const clj_protocol *proto;
	clj_value          *fns;
	clj_value           bad_key; // the first key that names no method, or CLJ_UNBOUND
	clj_value           bad_val; // the first value that is neither nil nor invokable, or CLJ_UNBOUND
} collect_ctx;

static bool collect_method(clj_value key, clj_value val, void *ctx) {
	collect_ctx *c = ctx;
	uint32_t     n = clj_vector_count(c->proto->methods);
	for (uint32_t i = 0; i < n; i++) {
		if (!clj_is_keyword(key) || !clj_equals(clj_keyword_name(key), clj_symbol_name(clj_vector_nth(c->proto->methods, i)))) continue;
		if (!clj_is_nil(val) && !clj_has_core(val, CLJ_CORE_FN)) {
			c->bad_val = val;
			return false;
		}
		c->fns[i] = val;
		return true;
	}
	c->bad_key = key;
	return false;
}

// Owned vector of impls in method order; unknown keys are errors, as a silent miss is the classic extend typo.
static clj_value impl_vector(clj_value proto, clj_value method_map) {
	const clj_protocol *p = clj_protocol_of(proto);
	uint32_t            n = clj_vector_count(p->methods);
	clj_value          *fns = calloc(n ? n : 1, sizeof *fns);
	if (!fns) clj_fatal("out of memory");
	collect_ctx c = {p, fns, CLJ_UNBOUND, CLJ_UNBOUND};
	if (!clj_is_nil(method_map)) clj_map_each(method_map, collect_method, &c);
	clj_value r;
	if (c.bad_key != CLJ_UNBOUND || c.bad_val != CLJ_UNBOUND) {
		clj_value text = clj_pr_str(c.bad_key != CLJ_UNBOUND ? c.bad_key : c.bad_val);
		if (text == CLJ_THROWN) r = CLJ_THROWN;
		else if (c.bad_key != CLJ_UNBOUND) r = clj_throw_msg("No method %s in protocol %s", clj_string_bytes(text), clj_string_bytes(clj_symbol_name(p->name)));
		else r = clj_throw_msg("Method implementation must be a fn, got: %s", clj_string_bytes(text));
		clj_release(text);
	} else {
		r = clj_vector_from_array(fns, n);
	}
	free(fns);
	return r;
}

clj_value clj_proto_extend(clj_value type, clj_value proto, clj_value method_map) {
	if (!clj_is_protocol(proto)) return clj_throw_msg("%s is not a protocol", clj_type_name(proto));
	const clj_protocol *p = clj_protocol_of(proto);
	if (p->core_bits) return clj_throw_msg("%s is a core interface, not a protocol: the core-interface slots of a type are write-once", clj_string_bytes(clj_symbol_name(p->name)));
	const clj_type *t = designated_type(type);
	if (!t) return CLJ_THROWN;
	if (!clj_is_nil(method_map) && !clj_is_map(method_map)) return clj_throw_msg("extend expects a map of method fns, got: %s", clj_type_name(method_map));
	clj_value fns = impl_vector(proto, method_map);
	if (fns == CLJ_THROWN) return CLJ_THROWN;
	clj_share(fns);
	clj_share(proto);

	pthread_mutex_lock(&lock);
	bool         immortal = (t->h.flags & CLJ_FLAG_IMMORTAL) != 0;
	side_table  *old_side = immortal ? atomic_load_explicit(&side, memory_order_relaxed) : NULL;
	proto_table *old = immortal ? side_find(old_side, t) : __atomic_load_n((void *const *)&t->user_protos, __ATOMIC_RELAXED);
	proto_table *fresh = table_with(old, proto, fns);
	if (immortal) atomic_store_explicit(&side, side_with(old_side, t, fresh), memory_order_seq_cst);
	else __atomic_store_n(&((clj_type *)t)->user_protos, fresh, __ATOMIC_SEQ_CST);
	atomic_fetch_add_explicit(&epoch, 1, memory_order_release);
	wait_readers();
	pthread_mutex_unlock(&lock);

	table_free(old);
	free(old_side);
	clj_release(fns);
	return CLJ_NIL;
}

clj_value clj_proto_satisfies(clj_value proto, clj_value v) {
	if (!clj_is_protocol(proto)) return clj_throw_msg("%s is not a protocol", clj_type_name(proto));
	uint64_t bits = clj_protocol_of(proto)->core_bits;
	if (bits) return clj_bool(clj_has_core(v, bits));
	return clj_bool(type_satisfies(clj_dispatch_type(v), proto));
}

clj_value clj_proto_extends(clj_value proto, clj_value type) {
	if (!clj_is_protocol(proto)) return clj_throw_msg("%s is not a protocol", clj_type_name(proto));
	const clj_type *t = designated_type(type);
	if (!t) return CLJ_THROWN;
	uint64_t bits = clj_protocol_of(proto)->core_bits;
	if (bits) return clj_bool((t->core_bits & bits) == bits);
	return clj_bool(type_satisfies(t, proto));
}

// ---- deftype

static void instance_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_instance        *o = self;
	const clj_user_type *ut = (const clj_user_type *)o->h.type;
	for (uint32_t i = 0; i < ut->nfields; i++) visit(o->fields[i], ctx);
	visit(clj_from_ptr((void *)ut), ctx);
}

// A window, so an extend racing with the first share of the descriptor cannot free the table under the walk.
static void type_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_user_type *ut = self;
	visit(ut->name, ctx);
	visit(ut->fields, ctx);
	reader      *r = window_open();
	proto_table *tbl = __atomic_load_n((void *const *)&ut->t.user_protos, __ATOMIC_SEQ_CST);
	for (uint32_t i = 0; tbl && i < tbl->n; i++) {
		visit(tbl->entries[i].proto, ctx);
		visit(tbl->entries[i].fns, ctx);
	}
	window_close(r);
}

static void type_finalize(void *self) { free(((clj_user_type *)self)->t.user_protos); }

// Only heap descriptors (deftype, reify) reach each_child and finalize; builtin ones are immortal.
const clj_type clj_type_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "type",
	.each_child = type_each_child,
	.finalize = type_finalize,
	.hash = identity_hash,
	.equals = identity_equals,
};

clj_value clj_user_type_new(clj_value name, clj_value fields) {
	if (!is_unqualified_symbol(name)) return clj_throw_msg("deftype name must be an unqualified symbol, got: %s", clj_type_name(name));
	if (!clj_is_vector(fields)) return clj_throw_msg("deftype fields must be a vector, got: %s", clj_type_name(fields));
	for (uint32_t i = 0; i < clj_vector_count(fields); i++) {
		if (!is_unqualified_symbol(clj_vector_nth(fields, i))) return clj_throw_msg("deftype fields must be symbols");
	}
	const char *ns = clj_string_bytes(clj_symbol_name(clj_ns_name(clj_ns_current())));
	const char *bare = clj_string_bytes(clj_symbol_name(name));
	size_t      len = strlen(ns) + 1 + strlen(bare);
	char       *text = malloc(len + 1);
	if (!text) clj_fatal("out of memory");
	snprintf(text, len + 1, "%s.%s", ns, bare);
	clj_user_type *ut = clj_alloc(&clj_type_type, sizeof *ut);
	ut->name = clj_string_new(text, len);
	free(text);
	ut->fields = clj_retain(fields);
	ut->nfields = clj_vector_count(fields);
	ut->t.name = clj_string_bytes(ut->name);
	ut->t.each_child = instance_each_child;
	ut->t.hash = identity_hash;
	ut->t.equals = identity_equals;
	return clj_from_ptr(ut);
}

clj_value clj_instance_new(clj_value type, const clj_value *fields, size_t n) {
	if (!clj_is_user_type(type)) return clj_throw_msg("new* expects a deftype, got: %s", clj_type_name(type));
	clj_user_type *ut = clj_to_ptr(type);
	if (n != ut->nfields) return clj_throw_msg("%s has %u fields, got %zu", ut->t.name, ut->nfields, n);
	clj_instance *o = clj_alloc(&ut->t, sizeof *o + n * sizeof *o->fields);
	for (size_t i = 0; i < n; i++) o->fields[i] = clj_retain(fields[i]);
	clj_retain(type);
	return clj_from_ptr(o);
}

clj_value clj_instance_field(clj_value obj, uint32_t i) {
	CLJ_ASSERT(clj_is_instance(obj) && i < ((const clj_user_type *)clj_type_of(obj))->nfields, "instance field out of range");
	return ((clj_instance *)clj_to_ptr(obj))->fields[i];
}

clj_value clj_is_instance_of(clj_value type, clj_value v) {
	if (clj_is_protocol(type) && clj_protocol_of(type)->core_bits) return clj_bool(clj_has_core(v, clj_protocol_of(type)->core_bits));
	const clj_type *t = designated_type(type);
	if (!t) return CLJ_THROWN;
	return clj_bool(t == &object_type || clj_dispatch_type(v) == t);
}

// ---- builtins

static clj_value b_protocol(const clj_value *args, size_t n) {
	(void)n;
	return clj_protocol_new(args[0], args[1]);
}

static clj_value b_protocol_method(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_protocol(args[0])) return clj_throw_msg("%s is not a protocol", clj_type_name(args[0]));
	if (!clj_is_fixnum(args[1]) || clj_fixnum_val(args[1]) < 0 || (uintptr_t)clj_fixnum_val(args[1]) >= clj_vector_count(clj_protocol_of(args[0])->methods)) {
		return clj_throw_msg("protocol-method* index out of range");
	}
	return clj_protocol_method(args[0], (uint32_t)clj_fixnum_val(args[1]));
}

static clj_value b_deftype(const clj_value *args, size_t n) {
	(void)n;
	return clj_user_type_new(args[0], args[1]);
}

static clj_value b_new(const clj_value *args, size_t n) { return clj_instance_new(args[0], args + 1, n - 1); }

static clj_value b_field(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_instance(args[0])) return clj_throw_msg("field* expects a deftype instance, got: %s", clj_type_name(args[0]));
	uint32_t nfields = ((const clj_user_type *)clj_type_of(args[0]))->nfields;
	if (!clj_is_fixnum(args[1]) || clj_fixnum_val(args[1]) < 0 || (uintptr_t)clj_fixnum_val(args[1]) >= nfields) return clj_throw_msg("field* index out of range");
	return clj_retain(clj_instance_field(args[0], (uint32_t)clj_fixnum_val(args[1])));
}

// A reify method: the impl closure sits in field idx of the instance it is called on.
static clj_value trampoline_invoke(void *ctx, const clj_value *args, size_t n) {
	uint32_t idx = (uint32_t)(uintptr_t)ctx;
	if (n == 0 || !clj_is_instance(args[0]) || idx >= ((const clj_user_type *)clj_type_of(args[0]))->nfields) {
		return clj_throw_msg("reify method called on %s", n ? clj_type_name(args[0]) : "nothing");
	}
	return clj_invoke(clj_instance_field(args[0], idx), args, n);
}

static clj_value b_trampoline(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_fixnum(args[0]) || clj_fixnum_val(args[0]) < 0) return clj_throw_msg("trampoline* expects a field index");
	return clj_fn_native_ctx(CLJ_NIL, trampoline_invoke, (void *)(uintptr_t)clj_fixnum_val(args[0]), NULL, 1, CLJ_ARITY_ANY);
}

static clj_value b_extend(const clj_value *args, size_t n) {
	(void)n;
	return clj_proto_extend(args[0], args[1], args[2]);
}

static clj_value b_satisfies(const clj_value *args, size_t n) {
	(void)n;
	return clj_proto_satisfies(args[0], args[1]);
}

static clj_value b_extends(const clj_value *args, size_t n) {
	(void)n;
	return clj_proto_extends(args[0], args[1]);
}

static clj_value b_instance(const clj_value *args, size_t n) {
	(void)n;
	return clj_is_instance_of(args[0], args[1]);
}

static clj_value b_type(const clj_value *args, size_t n) {
	(void)n;
	return clj_retain(clj_from_ptr((void *)clj_dispatch_type(args[0])));
}

static clj_value b_identical(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(args[0] == args[1]);
}

static clj_value b_proto_epoch(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_fixnum((intptr_t)clj_proto_epoch());
}

static void bind_core(const char *name, clj_value val) {
	clj_value sym = clj_symbol_from_cstr(name);
	clj_var_bind_root(clj_ns_intern(clj_ns_core(), sym), val);
	clj_release(sym);
}

void clj_proto_install(void) {
	static const struct {
		const char   *name;
		clj_native_fn fn;
		uint32_t      min, max;
	} fns[] = {
		{"protocol*", b_protocol, 2, 2},       {"protocol-method*", b_protocol_method, 2, 2}, {"deftype*", b_deftype, 2, 2},
		{"new*", b_new, 1, CLJ_ARITY_ANY},     {"field*", b_field, 2, 2},                     {"trampoline*", b_trampoline, 1, 1},
		{"extend*", b_extend, 3, 3},           {"satisfies?", b_satisfies, 2, 2},             {"extends?", b_extends, 2, 2},
		{"instance?", b_instance, 2, 2},       {"type", b_type, 1, 1},                        {"identical?", b_identical, 2, 2},
		{"protocol-epoch*", b_proto_epoch, 0, 0},
	};
	clj_value core_name = clj_symbol_name(clj_ns_name(clj_ns_core()));
	for (size_t i = 0; i < sizeof fns / sizeof *fns; i++) {
		clj_value name = clj_string_from_cstr(fns[i].name);
		clj_value qualified = clj_symbol_new(core_name, name);
		clj_value f = clj_fn_native(qualified, fns[i].fn, fns[i].min, fns[i].max);
		bind_core(fns[i].name, f);
		clj_release(f);
		clj_release(qualified);
		clj_release(name);
	}
	// The canonical builtin type names; (type x) reaches every other descriptor.
	static const struct {
		const char     *name;
		const clj_type *type;
	} types[] = {
		{"Object", &object_type},          {"Long", &fixnum_type},         {"Integer", &fixnum_type},        {"Double", &clj_double_type},
		{"Boolean", &boolean_type},        {"Character", &char_type},      {"String", &clj_string_type},     {"Keyword", &clj_keyword_type},
		{"Symbol", &clj_symbol_type},      {"PersistentVector", &clj_vector_type}, {"PersistentHashMap", &clj_map_type},
		{"PersistentList", &clj_cons_type}, {"Cons", &clj_cons_type},      {"EmptyList", &clj_empty_list_type}, {"LazySeq", &clj_lazy_seq_type},
		{"Range", &clj_range_type},        {"Fn", &clj_fn_type},           {"Var", &clj_var_type},           {"Namespace", &clj_ns_type},
		{"ExceptionInfo", &clj_exception_type}, {"HostError", &clj_host_error_type}, {"Protocol", &clj_protocol_type}, {"Type", &clj_type_type},
	};
	for (size_t i = 0; i < sizeof types / sizeof *types; i++) bind_core(types[i].name, clj_from_ptr((void *)types[i].type));
	clj_value empty = clj_vector_empty();
	for (size_t i = 0; i < NINTERFACES; i++) {
		clj_value name = clj_string_from_cstr(interfaces[i].name);
		clj_value qualified = clj_symbol_new(core_name, name);
		interfaces[i].proto = protocol_alloc(qualified, empty, empty, interfaces[i].bits);
		bind_core(interfaces[i].name, interfaces[i].proto);
		clj_release(qualified);
		clj_release(name);
	}
}
