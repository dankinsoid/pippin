// @ai-generated(guided)
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/array.h"
#include "clj/coll.h"
#include "clj/core.h"
#include "clj/epoch.h"
#include "clj/error.h"
#include "clj/hosttype.h"
#include "clj/fn.h"
#include "clj/ice.h"
#include "clj/intrinsics.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/lock.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/number.h"
#include "clj/printer.h"
#include "clj/record.h"
#include "clj/reduce.h"
#include "clj/proto.h"
#include "proto_internal.h"
#include "clj/seq.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "cc_internal.h"
#include "coro_internal.h"
#include "roots_internal.h"

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
static clj_lock       lock = CLJ_LOCK_INIT;
static _Atomic(side_table *) side;
static _Atomic uint32_t      next_proto_id;

typedef clj_proto_reader reader;

static reader               *readers; // pushed under lock, never removed
_Thread_local clj_proto_reader *clj_proto_reader_tls;

// Readers of exited threads, for the next thread to take: the scan's list stays as long as the most threads
// alive at once, not every thread that ever lived (the blocking pools retire idle threads).
static pthread_mutex_t spare_mu = PTHREAD_MUTEX_INITIALIZER;
static reader         *spare;
static pthread_once_t  reader_key_once = PTHREAD_ONCE_INIT;
static pthread_key_t   reader_key;

// A plain mutex, not `lock`: a clj_lock counts itself on the execution, which the thread's exit may have freed.
// The reader comes as the key's value: Darwin may have torn the thread's TLS down first. Its window is closed.
static void reader_retire(void *p) {
	reader *r = p;
	clj_proto_reader_tls = NULL;
	pthread_mutex_lock(&spare_mu);
	r->spare_next = spare;
	spare = r;
	pthread_mutex_unlock(&spare_mu);
}

static void make_reader_key(void) {
	if (pthread_key_create(&reader_key, reader_retire) != 0) clj_fatal("pthread_key_create failed");
}

clj_proto_reader *clj_proto_reader_init(void) {
	pthread_once(&reader_key_once, make_reader_key);
	pthread_mutex_lock(&spare_mu);
	reader *r = spare;
	if (r) spare = r->spare_next;
	pthread_mutex_unlock(&spare_mu);
	if (!r) {
		r = malloc(sizeof *r);
		if (!r) clj_fatal("out of memory");
		atomic_init(&r->active, 0);
		clj_lock_lock(&lock);
		r->next = readers;
		__atomic_store_n(&readers, r, __ATOMIC_RELEASE);
		clj_lock_unlock(&lock);
	}
	clj_proto_reader_tls = r;
	pthread_setspecific(reader_key, r);
	return r;
}

// Live user descriptors (deftype, record, reify), borrowed: what a consumer enumerates when a protocol's user
// implementors are counted but not named (the compiler's CHA). The proto lock is never held here, so a descriptor
// dying inside an extend's releases can unregister without nesting.
static clj_lock          users_lock = CLJ_LOCK_INIT;
static const clj_type  **users;
static uint32_t          nusers, users_cap;

static void users_add(const clj_type *t) {
	clj_lock_lock(&users_lock);
	if (nusers == users_cap) {
		users_cap = users_cap ? users_cap * 2 : 64;
		users = realloc(users, users_cap * sizeof *users);
		if (!users) clj_fatal("out of memory");
	}
	users[nusers++] = t;
	clj_lock_unlock(&users_lock);
}

static void users_remove(const clj_type *t) {
	clj_lock_lock(&users_lock);
	for (uint32_t i = 0; i < nusers; i++) {
		if (users[i] == t) {
			users[i] = users[--nusers];
			break;
		}
	}
	clj_lock_unlock(&users_lock);
}

static reader *window_open(void) { return clj_proto_window_open_inline(); }

static void window_close(reader *r) { clj_proto_window_close_inline(r); }

size_t clj_debug_proto_readers(void) {
	clj_lock_lock(&lock);
	size_t n = 0;
	for (reader *r = readers; r; r = r->next) n++;
	clj_lock_unlock(&lock);
	return n;
}

void clj_proto_wait_readers(void) {
	// Acquire against the publication under `lock`: a reader added after this walk opened its window later.
	for (reader *r = __atomic_load_n(&readers, __ATOMIC_ACQUIRE); r; r = r->next) {
		while (atomic_load_explicit(&r->active, memory_order_seq_cst)) sched_yield();
	}
}

// ---- pseudo-descriptors: dispatch keys for immediates, Object and the core interfaces

#define PSEUDO(nm) {.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type}, .name = nm}

const clj_type clj_nil_dispatch_type = PSEUDO("nil");
const clj_type clj_boolean_dispatch_type = PSEUDO("boolean");
const clj_type clj_char_dispatch_type = PSEUDO("char");
#define nil_type     clj_nil_dispatch_type
#define boolean_type clj_boolean_dispatch_type
#define char_type    clj_char_dispatch_type
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
	IFACE("IRecord", CLJ_CORE_RECORD),
	IFACE("IPersistentSet", CLJ_CORE_SET),
	IFACE("IPersistentMap", CLJ_CORE_MAP),
	IFACE("IPersistentVector", CLJ_CORE_VECTOR),
	IFACE("IPersistentList", CLJ_CORE_LIST),
	IFACE("IEditableCollection", CLJ_CORE_EDITABLE),
	IFACE("IExceptionInfo", CLJ_CORE_ERROR),
	IFACE("Indexed", CLJ_CORE_INDEXED),
	IFACE("Associative", CLJ_CORE_ASSOCIATIVE),
	IFACE("ILookup", CLJ_CORE_LOOKUP),
	IFACE("Counted", CLJ_CORE_COUNTED),
	IFACE("ISeq", CLJ_CORE_SEQ),
	IFACE("Sequential", CLJ_CORE_SEQUENTIAL),
	IFACE("IPersistentCollection", CLJ_CORE_COLL),
	IFACE("IFn", CLJ_CORE_FN),
	IFACE("IHashEq", CLJ_CORE_HASHEQ),
	IFACE("IEquiv", CLJ_CORE_EQUIV),
	IFACE("IReduceInit", CLJ_CORE_REDUCE),
	IFACE("Seqable", CLJ_CORE_SEQABLE),
	IFACE("IObj", CLJ_CORE_OBJ),
	IFACE("IMeta", CLJ_CORE_META),
};

enum { NINTERFACES = sizeof interfaces / sizeof *interfaces };

const clj_type *clj_dispatch_type(clj_value v) { return clj_dispatch_type_inline(v); }

const clj_type *clj_object_type(void) { return &object_type; }

static const core_interface *interface_of(clj_value proto) {
	uint64_t bits = clj_protocol_of(proto)->core_bits;
	for (size_t i = 0; i < NINTERFACES; i++) {
		if (interfaces[i].bits == bits) return &interfaces[i];
	}
	clj_ice("proto/core-interface", NULL, "a core interface without a descriptor");
}

// The descriptor a type designator names, or NULL with the exception pending.
static const clj_type *designated_type(clj_value type) {
	if (clj_is_nil(type)) return &nil_type;
	if (clj_is_type(type)) return clj_to_ptr(type);
	if (clj_is_protocol(type) && clj_protocol_of(type)->core_bits) return &interface_of(type)->type;
	CLJ_NO_CYCLE clj_value text = clj_pr_str(type);
	if (text == CLJ_THROWN) return NULL;
	clj_throw_msg("%s is not a type", CLJ_STR(text).bytes);
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

// A protocol method of the instance's own type named exactly name (pass 0) or -name (pass 1), owned, or nil.
static clj_value method_named(const clj_type *t, const char *name, size_t len) {
	reader            *r = window_open();
	const proto_table *tbl = table_of(t);
	clj_value          f = CLJ_NIL;
	for (int pass = 0; pass < 2 && clj_is_nil(f); pass++) {
		for (uint32_t i = 0; tbl && i < tbl->n && clj_is_nil(f); i++) {
			clj_value methods = clj_protocol_of(tbl->entries[i].proto)->methods.v;
			for (uint32_t k = 0; k < clj_vector_count(methods); k++) {
				clj_value   sym = clj_symbol_name(clj_vector_nth(methods, k));
				const char *s = CLJ_STR(sym).bytes;
				size_t      n = clj_string_len(sym);
				bool        match = pass == 0 ? n == len && memcmp(s, name, len) == 0 : n == len + 1 && s[0] == '-' && memcmp(s + 1, name, len) == 0;
				if (!match) continue;
				clj_value fn = clj_vector_nth(tbl->entries[i].fns, k);
				if (!clj_is_nil(fn)) f = clj_retain(fn);
				break;
			}
		}
	}
	window_close(r);
	return f;
}

// The field of t named name, or -1.
static int64_t field_named(const clj_user_type *t, const char *name, size_t len) {
	clj_value fields = t->fields.v;
	for (uint32_t i = 0; i < t->nfields; i++) {
		clj_value s = clj_symbol_name(clj_vector_nth(fields, i));
		if (clj_string_len(s) == len && memcmp(CLJ_STR(s).bytes, name, len) == 0) return i;
	}
	return -1;
}

int64_t clj_instance_field_index(clj_value obj, clj_value selector) {
	const char          *sel = CLJ_STR(selector).bytes;
	const clj_user_type *ut = (const clj_user_type *)clj_type_of(obj);
	return sel[0] == '-' && !ut->reified ? field_named(ut, sel + 1, clj_string_len(selector) - 1) : -1;
}

static clj_value mutable_read(clj_instance *o, uint32_t i);

clj_value clj_instance_send(clj_value target, clj_value selector, const clj_value *args, uint32_t nargs) {
	const char          *sel = CLJ_STR(selector).bytes;
	const clj_user_type *ut = (const clj_user_type *)clj_type_of(target);
	if (nargs == 0) {
		int64_t i = clj_instance_field_index(target, selector);
		if (i >= 0 && clj_user_type_field_mutable(ut, (uint32_t)i)) return mutable_read(clj_to_ptr(target), (uint32_t)i);
		if (i >= 0) return clj_retain(clj_instance_field(target, (uint32_t)i));
	}
	clj_value f = method_named(&ut->t, sel, strcspn(sel, ":"));
	if (clj_is_nil(f)) return CLJ_UNBOUND;
	CLJ_ROOTS(&f);
	clj_value               small[8];
	CLJ_BORROWED clj_value *call = nargs + 1 <= sizeof small / sizeof *small ? small : malloc((nargs + 1) * sizeof *call);
	if (!call) clj_fatal("out of memory");
	call[0] = target;
	memcpy(call + 1, args, nargs * sizeof *call);
	clj_value r = clj_invoke(f, call, nargs + 1);
	if (call != small) free(call);
	clj_release(f);
	f = CLJ_NIL;
	return r;
}

void clj_proto_each_immortal(clj_value proto, CLJ_NO_PARK void (*visit)(const clj_type *t, void *ctx), void *ctx) {
	reader     *r = window_open();
	side_table *s = atomic_load_explicit(&side, memory_order_seq_cst);
	for (uint32_t i = 0; s && i < s->cap; i++) {
		const side_slot *slot = &s->slots[i];
		if (slot->key && !clj_is_nil(table_find(slot->table, proto))) visit(slot->key, ctx);
	}
	window_close(r);
}

void clj_proto_each_user(clj_value proto, CLJ_NO_PARK void (*visit)(const clj_type *t, void *ctx), void *ctx) {
	clj_lock_lock(&users_lock);
	reader *r = window_open();
	for (uint32_t i = 0; i < nusers; i++) {
		if (!clj_is_nil(table_find(table_of(users[i]), proto))) visit(users[i], ctx);
	}
	window_close(r);
	clj_lock_unlock(&users_lock);
}

clj_value clj_proto_impl_for_type(clj_value method, const clj_type *t) {
	const clj_method_ctx *m = clj_method_ctx_of(method);
	reader               *r = window_open();
	clj_value             fns = find_fns(t, m->proto);
	clj_value             f = clj_is_nil(fns) ? CLJ_NIL : clj_retain(clj_vector_nth(fns, m->idx));
	window_close(r);
	return f;
}

bool clj_proto_is_interface_type(const clj_type *t) {
	for (size_t i = 0; i < NINTERFACES; i++) {
		if (t == &interfaces[i].type) return true;
	}
	return false;
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
	visit(p->name.v, ctx);
	visit(p->methods.v, ctx);
	visit(p->sigs.v, ctx);
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
	CLJ_NO_CYCLE clj_value text = clj_pr_str(sigs);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("protocol* expects [[method [params...]] ...], got: %s", CLJ_STR(text).bytes);
	clj_release(text);
	return r;
}

static clj_value protocol_alloc(clj_value qualified_name, clj_value methods, clj_value sigs, uint64_t core_bits) {
	clj_protocol *p = clj_alloc(&clj_protocol_type, sizeof *p);
	p->id = atomic_fetch_add_explicit(&next_proto_id, 1, memory_order_relaxed) + 1;
	p->core_bits = core_bits;
	clj_slot_init(&p->h, &p->name, clj_retain(qualified_name));
	clj_slot_init(&p->h, &p->methods, clj_retain(methods));
	clj_slot_init(&p->h, &p->sigs, clj_retain(sigs));
	return clj_from_ptr(p);
}

clj_value clj_protocol_new(clj_value name, clj_value sigs) {
	if (!clj_is_symbol(name)) return clj_throw_msg("protocol name must be a symbol, got: %s", clj_type_name(name));
	if (!clj_is_vector(sigs)) return bad_sigs(sigs);
	uint32_t  n = clj_vector_count(sigs);
	clj_value methods = clj_vector_empty(), arities = clj_vector_empty();
	CLJ_ROOTS(&methods, &arities);
	for (uint32_t i = 0; i < n; i++) {
		clj_value sig = clj_vector_nth(sigs, i);
		if (!clj_is_vector(sig) || clj_vector_count(sig) != 2 || !is_unqualified_symbol(clj_vector_nth(sig, 0)) || !clj_is_vector(clj_vector_nth(sig, 1))) {
			clj_release(methods);
			clj_release(arities);
			methods = arities = CLJ_NIL;
			return bad_sigs(sigs);
		}
		clj_value method = clj_vector_nth(sig, 0), params = clj_vector_nth(sig, 1);
		for (uint32_t j = 0; j < clj_vector_count(params); j++) {
			if (!clj_is_vector(clj_vector_nth(params, j))) {
				clj_release(methods);
				clj_release(arities);
				methods = arities = CLJ_NIL;
				return bad_sigs(sigs);
			}
		}
		for (uint32_t j = 0; j < i; j++) {
			if (clj_equals(clj_vector_nth(methods, j), method)) {
				clj_release(methods);
				clj_release(arities);
				methods = arities = CLJ_NIL;
				return clj_throw_msg("Duplicate method %s in protocol %s", CLJ_STR(clj_symbol_name(method)).bytes, CLJ_STR(clj_symbol_name(name)).bytes);
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

typedef clj_method_ctx method_ctx;

static _Atomic uint64_t method_serials;

static clj_value no_impl(const method_ctx *m, clj_value v) {
	const clj_protocol *p = clj_protocol_of(m->proto);
	clj_value           method = clj_vector_nth(p->methods.v, m->idx);
	return clj_throw_msg("No implementation of method: :%s of protocol: #'%s/%s found for type: %s", CLJ_STR(clj_symbol_name(method)).bytes,
	                     CLJ_STR(clj_symbol_ns(p->name.v)).bytes, CLJ_STR(clj_symbol_name(p->name.v)).bytes, clj_type_name(v));
}

clj_value clj_protocol_method_invoke(void *ctx, const clj_value *args, size_t n) {
	CLJ_STAT(CLJ_STAT_PROTO_GENERIC);
	const method_ctx *m = ctx;
	clj_value         f = impl_of(m->proto, m->idx, args[0]);
	if (clj_is_nil(f)) return no_impl(m, args[0]);
	CLJ_ROOTS(&f);
	clj_value r = clj_invoke(f, args, n);
	clj_release(f);
	return r;
}

clj_value clj_protocol_method_impl(clj_value method, clj_value v) {
	const method_ctx *m = clj_fn_of(method)->u.native_ctx.ctx;
	return impl_of(m->proto, m->idx, v);
}

clj_value clj_protocol_no_impl(clj_value method, clj_value v) { return no_impl(clj_fn_of(method)->u.native_ctx.ctx, v); }

static bool is_amp(clj_value v) { return is_unqualified_symbol(v) && strcmp(CLJ_STR(clj_symbol_name(v)).bytes, "&") == 0; }

clj_value clj_protocol_method(clj_value proto, uint32_t idx) {
	const clj_protocol *p = clj_protocol_of(proto);
	CLJ_ASSERT(idx < clj_vector_count(p->methods.v), "protocol method index out of range");
	clj_value sigs = clj_vector_nth(p->sigs.v, idx);
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
	ctx->serial = atomic_fetch_add_explicit(&method_serials, 1, memory_order_relaxed) + 1;
	clj_value name = clj_symbol_new(clj_symbol_ns(p->name.v), clj_symbol_name(clj_vector_nth(p->methods.v, idx)));
	clj_value f = clj_fn_native_ctx(name, clj_protocol_method_invoke, ctx, free, min, max);
	clj_release(name);
	clj_slot_store(&(clj_fn_of(f))->h, &clj_fn_of(f)->code, clj_retain(proto));
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
	CLJ_BORROWED const clj_protocol *proto;
	CLJ_BORROWED clj_value          *fns;
	CLJ_BORROWED clj_value           bad_key; // the first key that names no method, or CLJ_UNBOUND
	CLJ_BORROWED clj_value           bad_val; // the first value that is neither nil nor invokable, or CLJ_UNBOUND
} collect_ctx;

static bool collect_method(clj_value key, clj_value val, void *ctx) {
	collect_ctx *c = ctx;
	uint32_t     n = clj_vector_count(c->proto->methods.v);
	for (uint32_t i = 0; i < n; i++) {
		if (!clj_is_keyword(key) || !clj_equals(clj_keyword_name(key), clj_symbol_name(clj_vector_nth(c->proto->methods.v, i)))) continue;
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
	const clj_protocol     *p = clj_protocol_of(proto);
	uint32_t                n = clj_vector_count(p->methods.v);
	CLJ_BORROWED clj_value *fns = calloc(n ? n : 1, sizeof *fns);
	if (!fns) clj_fatal("out of memory");
	collect_ctx c = {p, fns, CLJ_UNBOUND, CLJ_UNBOUND};
	if (!clj_is_nil(method_map)) clj_map_each(method_map, collect_method, &c);
	clj_value r;
	if (c.bad_key != CLJ_UNBOUND || c.bad_val != CLJ_UNBOUND) {
		CLJ_NO_CYCLE clj_value text = clj_pr_str_max(c.bad_key != CLJ_UNBOUND ? c.bad_key : c.bad_val, CLJ_ERROR_PRINT_MAX);
		if (text == CLJ_THROWN) r = CLJ_THROWN;
		else if (c.bad_key != CLJ_UNBOUND) r = clj_throw_msg("No method %s in protocol %s", CLJ_STR(text).bytes, CLJ_STR(clj_symbol_name(p->name.v)).bytes);
		else r = clj_throw_msg("Method implementation must be a fn, got: %s", CLJ_STR(text).bytes);
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
	if (p->core_bits) return clj_throw_msg("%s is a core interface, not a protocol: the core-interface slots of a type are write-once", CLJ_STR(clj_symbol_name(p->name.v)).bytes);
	const clj_type *t = designated_type(type);
	if (!t) return CLJ_THROWN;
	if (!clj_is_nil(method_map) && !clj_is_map(method_map)) return clj_throw_msg("extend expects a map of method fns, got: %s", clj_type_name(method_map));
	clj_value fns = impl_vector(proto, method_map);
	if (fns == CLJ_THROWN) return CLJ_THROWN;
	clj_share(fns);
	clj_share(proto);

	clj_lock_lock(&lock);
	bool         immortal = (t->h.flags & CLJ_FLAG_IMMORTAL) != 0;
	side_table  *old_side = immortal ? atomic_load_explicit(&side, memory_order_relaxed) : NULL;
	proto_table *old = immortal ? side_find(old_side, t) : __atomic_load_n((void *const *)&t->user_protos, __ATOMIC_RELAXED);
	proto_table *fresh = table_with(old, proto, fns);
	if (immortal) atomic_store_explicit(&side, side_with(old_side, t, fresh), memory_order_seq_cst);
	else {
		__atomic_store_n(&((clj_type *)t)->user_protos, fresh, __ATOMIC_SEQ_CST);
		_Atomic uint32_t *counter = (t->core_bits & CLJ_CORE_RECORD) ? &clj_protocol_of(proto)->user_records : &clj_protocol_of(proto)->user_types;
		atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
	}
	clj_epoch_bump();
	clj_proto_wait_readers();
	clj_lock_unlock(&lock);

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
	for (uint32_t i = 0; i < ut->nfields; i++) {
		if (!ut->muts || !(ut->muts[i] & CLJ_FIELD_RAW)) visit(o->fields[i].v, ctx);
	}
	visit(clj_from_ptr((void *)ut), ctx);
}

// A window, so an extend racing with the first share of the descriptor cannot free the table under the walk.
static void type_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_user_type *ut = self;
	visit(ut->name.v, ctx);
	visit(ut->fields.v, ctx);
	for (int m = 0; m < CLJ_CORE_METHOD_COUNT; m++) visit(ut->core_fns[m].v, ctx);
	reader      *r = window_open();
	proto_table *tbl = __atomic_load_n((void *const *)&ut->t.user_protos, __ATOMIC_SEQ_CST);
	for (uint32_t i = 0; tbl && i < tbl->n; i++) {
		visit(tbl->entries[i].proto, ctx);
		visit(tbl->entries[i].fns, ctx);
	}
	window_close(r);
}

// A dying type retires its table without an extend: the bump invalidates every call-site cache that borrows
// an impl from it.
static void type_finalize(void *self) {
	users_remove(self);
	free(((clj_user_type *)self)->t.user_protos);
	free(((clj_user_type *)self)->hints);
	free(((clj_user_type *)self)->muts);
	clj_epoch_bump();
}

// Only heap descriptors (deftype, reify) reach each_child and finalize; builtin ones are immortal.
const clj_type clj_type_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "type",
	.each_child = type_each_child,
	.finalize = type_finalize,
	.hash = identity_hash,
	.equals = identity_equals,
};

// ---- core-interface slots of a user type: trampolines into the method fns

static const char *const core_method_names[CLJ_CORE_METHOD_COUNT] = {
	"seq", "first", "next", "more", "count", "valAt", "cons", "invoke", "ex-message", "ex-data", "ex-cause", "hasheq", "equiv",
	"meta", "withMeta", "reduce",
};

// Which method names a core interface accepts; JVM names so Clojure code reads as is, `rest` and the
// ex-* names as aliases. Object-level methods (empty, applyTo) have no slot and are refused by name.
typedef struct {
	uint64_t        iface;
	const char     *name;
	clj_core_method method;
} method_row;

static const method_row method_rows[] = {
	{CLJ_CORE_SEQABLE, "seq", CLJ_CM_SEQ},
	{CLJ_CORE_SEQ, "seq", CLJ_CM_SEQ},         {CLJ_CORE_SEQ, "first", CLJ_CM_FIRST},     {CLJ_CORE_SEQ, "next", CLJ_CM_NEXT},
	{CLJ_CORE_SEQ, "more", CLJ_CM_REST},       {CLJ_CORE_SEQ, "rest", CLJ_CM_REST},       {CLJ_CORE_SEQ, "cons", CLJ_CM_CONJ},
	{CLJ_CORE_SEQ, "count", CLJ_CM_COUNT},     {CLJ_CORE_SEQ, "equiv", CLJ_CM_EQUALS},
	{CLJ_CORE_COLL, "seq", CLJ_CM_SEQ},        {CLJ_CORE_COLL, "cons", CLJ_CM_CONJ},      {CLJ_CORE_COLL, "count", CLJ_CM_COUNT},
	{CLJ_CORE_COLL, "equiv", CLJ_CM_EQUALS},
	{CLJ_CORE_COUNTED, "count", CLJ_CM_COUNT},
	{CLJ_CORE_LOOKUP, "valAt", CLJ_CM_LOOKUP},
	{CLJ_CORE_FN, "invoke", CLJ_CM_INVOKE},
	{CLJ_CORE_HASHEQ, "hasheq", CLJ_CM_HASH},
	{CLJ_CORE_EQUIV, "equiv", CLJ_CM_EQUALS},
	{CLJ_CORE_ERROR, "ex-message", CLJ_CM_EX_MESSAGE}, {CLJ_CORE_ERROR, "getMessage", CLJ_CM_EX_MESSAGE},
	{CLJ_CORE_ERROR, "ex-data", CLJ_CM_EX_DATA},       {CLJ_CORE_ERROR, "getData", CLJ_CM_EX_DATA},
	{CLJ_CORE_ERROR, "ex-cause", CLJ_CM_EX_CAUSE},     {CLJ_CORE_ERROR, "getCause", CLJ_CM_EX_CAUSE},
	{CLJ_CORE_META, "meta", CLJ_CM_META},
	{CLJ_CORE_OBJ, "meta", CLJ_CM_META},               {CLJ_CORE_OBJ, "withMeta", CLJ_CM_WITH_META},
	{CLJ_CORE_REDUCE, "reduce", CLJ_CM_REDUCE},
};

// The bits an interface gives a type: ISeq and IPersistentCollection carry their Clojure superinterfaces.
// 0 for interfaces with no slots behind them (Associative, Indexed, the IPersistent* ones).
static uint64_t implied_bits(uint64_t iface) {
	switch (iface) {
	case CLJ_CORE_SEQ: return CLJ_CORE_SEQ | CLJ_CORE_SEQABLE | CLJ_CORE_COLL;
	case CLJ_CORE_COLL: return CLJ_CORE_COLL | CLJ_CORE_SEQABLE;
	case CLJ_CORE_OBJ: return CLJ_CORE_OBJ | CLJ_CORE_META;
	case CLJ_CORE_SEQABLE:
	case CLJ_CORE_SEQUENTIAL:
	case CLJ_CORE_COUNTED:
	case CLJ_CORE_LOOKUP:
	case CLJ_CORE_FN:
	case CLJ_CORE_HASHEQ:
	case CLJ_CORE_EQUIV:
	case CLJ_CORE_META:
	case CLJ_CORE_REDUCE:
	case CLJ_CORE_ERROR: return iface;
	default: return 0;
	}
}

static const clj_user_type *user_type_of(clj_value self) { return (const clj_user_type *)clj_type_of(self); }

static clj_value no_core_impl(clj_value self, clj_core_method m) {
	return clj_throw_msg("No implementation of method: :%s found for type: %s", core_method_names[m], clj_type_name(self));
}

// (method self rest...) through the fn the type holds for m; the type outlives the call because self does.
static clj_value call_core(clj_value self, clj_core_method m, const clj_value *rest, size_t nrest) {
	clj_value f = user_type_of(self)->core_fns[m].v;
	if (clj_is_nil(f)) return no_core_impl(self, m);
	clj_value args[4];
	CLJ_ASSERT(nrest < sizeof args / sizeof *args, "core method arity");
	args[0] = self;
	for (size_t i = 0; i < nrest; i++) args[i + 1] = rest[i];
	return clj_invoke(f, args, nrest + 1);
}

// A method result that the slot contract types: seq/next yield a seq or nil, more a seq, ex-* their field types.
static clj_value checked(clj_value self, clj_core_method m, CLJ_OWNED clj_value r, bool ok, const char *expected) {
	if (r == CLJ_THROWN || ok) return r;
	CLJ_ROOTS(&r);
	clj_value e = clj_throw_msg("%s of %s must return %s, got: %s", core_method_names[m], clj_type_name(self), expected, clj_type_name(r));
	clj_release(r);
	r = CLJ_NIL;
	return e;
}

static clj_value user_seq(clj_value self) {
	clj_value r = call_core(self, CLJ_CM_SEQ, NULL, 0);
	return checked(self, CLJ_CM_SEQ, r, clj_is_nil(r) || clj_is_seq(r), "a seq or nil");
}

static clj_value user_first(clj_value self) { return call_core(self, CLJ_CM_FIRST, NULL, 0); }

static clj_value user_rest(clj_value self) {
	clj_value r = call_core(self, CLJ_CM_REST, NULL, 0);
	return checked(self, CLJ_CM_REST, r, clj_is_seq(r), "a seq");
}

// Without next, more's result is seq'd, as Cons.next() does on the JVM.
static clj_value user_next(clj_value self) {
	const clj_user_type *ut = user_type_of(self);
	if (clj_is_nil(ut->core_fns[CLJ_CM_NEXT].v) && !clj_is_nil(ut->core_fns[CLJ_CM_REST].v)) {
		clj_value more = user_rest(self);
		if (more == CLJ_THROWN) return more;
		CLJ_ROOTS(&more);
		clj_value s = clj_seq(more);
		clj_release(more);
		return s;
	}
	clj_value r = call_core(self, CLJ_CM_NEXT, NULL, 0);
	return checked(self, CLJ_CM_NEXT, r, clj_is_nil(r) || clj_is_seq(r), "a seq or nil");
}

static clj_value user_count(clj_value self) {
	clj_value r = call_core(self, CLJ_CM_COUNT, NULL, 0);
	return checked(self, CLJ_CM_COUNT, r, clj_is_fixnum(r) && clj_fixnum_val(r) >= 0, "a non-negative integer");
}

// (get x k) reaches valAt with 2 args when the fn has no 3-arity, as RT.get on the JVM; a not-found needs the 3-arity.
static clj_value user_lookup(clj_value self, clj_value key, clj_value not_found) {
	clj_value f = user_type_of(self)->core_fns[CLJ_CM_LOOKUP].v;
	if (clj_is_nil(f)) return no_core_impl(self, CLJ_CM_LOOKUP);
	clj_value args[3] = {self, key, not_found};
	return clj_invoke(f, args, !clj_is_nil(not_found) || clj_fn_accepts(f, 3) ? 3 : 2);
}

static clj_value user_conj(CLJ_OWNED clj_value self, CLJ_BORROWED clj_value x) {
	CLJ_ROOTS(&self);
	clj_value r = call_core(self, CLJ_CM_CONJ, &x, 1);
	clj_release(self);
	self = CLJ_NIL;
	return r;
}

static clj_value user_invoke(clj_value self, const clj_value *args, size_t n) {
	clj_value f = user_type_of(self)->core_fns[CLJ_CM_INVOKE].v;
	if (clj_is_nil(f)) return no_core_impl(self, CLJ_CM_INVOKE);
	if (!clj_fn_accepts(f, n + 1)) return clj_throw_msg("Wrong number of args (%zu) passed to: %s", n, clj_type_name(self));
	clj_value               small[8];
	CLJ_BORROWED clj_value *all = n + 1 <= sizeof small / sizeof *small ? small : malloc((n + 1) * sizeof *all);
	if (!all) clj_fatal("out of memory");
	all[0] = self;
	for (size_t i = 0; i < n; i++) all[i + 1] = args[i];
	clj_value r = clj_invoke(f, all, n + 1);
	if (all != small) free(all);
	return r;
}

// Throwable's getMessage/getCause default to null; nil here for all three when the form gives none.
static clj_value user_ex_field(clj_value self, clj_core_method m, CLJ_NO_PARK bool (*ok)(clj_value), const char *expected) {
	if (clj_is_nil(user_type_of(self)->core_fns[m].v)) return CLJ_NIL;
	clj_value r = call_core(self, m, NULL, 0);
	return checked(self, m, r, clj_is_nil(r) || ok(r), expected);
}

static bool is_string_value(clj_value v) { return clj_is_string(v); }
static bool is_map_value(clj_value v) { return clj_is_map(v); }
static bool is_error_value(clj_value v) { return clj_is_exception(v); }

static clj_value user_ex_message(clj_value self) { return user_ex_field(self, CLJ_CM_EX_MESSAGE, is_string_value, "a string or nil"); }
static clj_value user_ex_data(clj_value self) { return user_ex_field(self, CLJ_CM_EX_DATA, is_map_value, "a map or nil"); }
static clj_value user_ex_cause(clj_value self) { return user_ex_field(self, CLJ_CM_EX_CAUSE, is_error_value, "an error or nil"); }

static clj_value user_meta(clj_value self) {
	clj_value r = call_core(self, CLJ_CM_META, NULL, 0);
	return checked(self, CLJ_CM_META, r, clj_is_nil(r) || clj_is_map(r), "a map or nil");
}

static clj_value user_with_meta(CLJ_OWNED clj_value self, CLJ_BORROWED clj_value m) {
	CLJ_ROOTS(&self);
	clj_value r = call_core(self, CLJ_CM_WITH_META, &m, 1);
	clj_release(self);
	self = CLJ_NIL;
	return r;
}

// IReduceInit has only (reduce this f init); the 2-arity seeds with (f), as CollReduce's extension to it does on
// the JVM. The slot contract unwraps a reduced result the method returned.
static clj_value user_reduce(clj_value self, clj_value f, clj_value init) {
	clj_value seed = init == CLJ_UNBOUND ? clj_invoke(f, NULL, 0) : clj_retain(init);
	if (seed == CLJ_THROWN) return seed;
	CLJ_ROOTS(&seed);
	clj_value args[2] = {f, seed};
	clj_value r = call_core(self, CLJ_CM_REDUCE, args, 2);
	clj_release(seed);
	seed = CLJ_NIL;
	if (r != CLJ_THROWN && clj_is_reduced(r)) {
		clj_value v = clj_retain(clj_reduced_value(r));
		clj_release(r);
		return v;
	}
	return r;
}

bool clj_proto_reduce_is_user(const clj_type *t) { return t->reduce == user_reduce; }

// hash/equals cannot throw (NOTES.md drop_thrown): a hasheq that throws or yields a non-integer hashes 0,
// an equiv that throws compares unequal, the exception dropped.
static uint32_t user_hash(void *self) {
	clj_value r = call_core(clj_from_ptr(self), CLJ_CM_HASH, NULL, 0);
	if (r == CLJ_THROWN) {
		clj_equals_drop_pending();
		return 0;
	}
	uint32_t h = clj_is_fixnum(r) ? (uint32_t)clj_fixnum_val(r) : 0;
	clj_release(r);
	return h;
}

static bool user_equals(void *self, CLJ_BORROWED clj_value other) {
	clj_value r = call_core(clj_from_ptr(self), CLJ_CM_EQUALS, &other, 1);
	if (r == CLJ_THROWN) {
		clj_equals_drop_pending();
		return false;
	}
	bool eq = clj_truthy(r);
	clj_release(r);
	return eq;
}

// Slots follow the bits, as for builtins: a slot behind a declared interface always exists and throws
// "No implementation" when the form gave no method. Sequential without equiv/hasheq gets the ASeq trait.
static void fill_slots(clj_user_type *ut) {
	clj_type        *t = &ut->t;
	uint64_t         bits = t->core_bits;
	const clj_value *m = clj_slot_values(ut->core_fns);
	if (bits & CLJ_CORE_SEQABLE) t->seq = user_seq;
	if (bits & CLJ_CORE_SEQ) {
		t->first = user_first;
		t->next = user_next;
	}
	if (!clj_is_nil(m[CLJ_CM_REST])) t->rest = user_rest;
	if ((bits & CLJ_CORE_COUNTED) || !clj_is_nil(m[CLJ_CM_COUNT])) t->count = user_count;
	if (bits & CLJ_CORE_LOOKUP) t->lookup = user_lookup;
	if (bits & CLJ_CORE_COLL) t->conj = user_conj;
	if (bits & CLJ_CORE_FN) t->invoke = user_invoke;
	if (bits & CLJ_CORE_ERROR) {
		t->ex_message = user_ex_message;
		t->ex_data = user_ex_data;
		t->ex_cause = user_ex_cause;
	}
	if (bits & CLJ_CORE_META) t->meta = user_meta;
	if (bits & CLJ_CORE_OBJ) t->with_meta = user_with_meta;
	if (bits & CLJ_CORE_REDUCE) t->reduce = user_reduce;
	if (!clj_is_nil(m[CLJ_CM_HASH])) t->hash = user_hash;
	else if (bits & CLJ_CORE_SEQUENTIAL) t->hash = clj_aseq_hash;
	if (!clj_is_nil(m[CLJ_CM_EQUALS])) t->equals = user_equals;
	else if (bits & CLJ_CORE_SEQUENTIAL) t->equals = clj_aseq_equals;
	for (size_t i = 0; i < CLJ_CORE_METHOD_COUNT; i++) {
		if (!clj_is_nil(m[i])) ut->birth_flags = CLJ_FLAG_LAZY;
	}
}

typedef struct {
	clj_user_type                   *ut;
	CLJ_BORROWED const clj_protocol *iface;
	CLJ_BORROWED clj_value           thrown; // CLJ_THROWN once an entry was refused
} core_collect_ctx;

static clj_value refuse_method(const core_collect_ctx *c, clj_value key, const char *why) {
	CLJ_NO_CYCLE clj_value text = clj_pr_str_max(key, CLJ_ERROR_PRINT_MAX);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("%s %s in interface %s", why, CLJ_STR(text).bytes, CLJ_STR(clj_symbol_name(c->iface->name.v)).bytes);
	clj_release(text);
	return r;
}

static bool collect_core_method(clj_value key, clj_value val, void *ctx) {
	core_collect_ctx *c = ctx;
	if (clj_is_nil(val)) return true;
	if (!clj_is_keyword(key)) {
		c->thrown = refuse_method(c, key, "No method");
		return false;
	}
	const char *name = CLJ_STR(clj_keyword_name(key)).bytes;
	for (size_t i = 0; i < sizeof method_rows / sizeof *method_rows; i++) {
		if (method_rows[i].iface != c->iface->core_bits || strcmp(method_rows[i].name, name) != 0) continue;
		if (!clj_has_core(val, CLJ_CORE_FN)) {
			c->thrown = clj_throw_msg("Method implementation must be a fn, got: %s", clj_type_name(val));
			return false;
		}
		if (!clj_is_nil(c->ut->core_fns[method_rows[i].method].v)) {
			c->thrown = refuse_method(c, key, "Duplicate method");
			return false;
		}
		clj_slot_init(&c->ut->t.h, &c->ut->core_fns[method_rows[i].method], clj_retain(val));
		return true;
	}
	bool known = strcmp(name, "empty") == 0 || strcmp(name, "applyTo") == 0;
	c->thrown = refuse_method(c, key, known ? "No slot for method" : "No method");
	return false;
}

// Fills the slots and bits a core interface gives the type from its method map; the type is not yet published.
static clj_value implement_interface(clj_user_type *ut, clj_value iface, clj_value method_map) {
	const clj_protocol *p = clj_protocol_of(iface);
	uint64_t            implied = implied_bits(p->core_bits);
	if (!implied) return clj_throw_msg("%s cannot be implemented by deftype: no slots behind it", CLJ_STR(clj_symbol_name(p->name.v)).bytes);
	ut->t.core_bits |= implied;
	core_collect_ctx c = {ut, p, CLJ_NIL};
	if (!clj_is_nil(method_map)) clj_map_each(method_map, collect_core_method, &c);
	return c.thrown;
}

static clj_value user_type_shape(clj_value name, clj_value fields, size_t nimpls) {
	if (!is_unqualified_symbol(name)) return clj_throw_msg("deftype name must be an unqualified symbol, got: %s", clj_type_name(name));
	if (!clj_is_vector(fields)) return clj_throw_msg("deftype fields must be a vector, got: %s", clj_type_name(fields));
	for (uint32_t i = 0; i < clj_vector_count(fields); i++) {
		if (!is_unqualified_symbol(clj_vector_nth(fields, i))) return clj_throw_msg("deftype fields must be symbols");
	}
	if (nimpls % 2) return clj_throw_msg("deftype* expects protocol and method-map pairs");
	return CLJ_NIL;
}

static _Atomic uint64_t type_serials;

static const struct {
	const char    *name;
	clj_field_hint hint;
} hint_names[] = {
	{"long", CLJ_HINT_LONG},       {"int", CLJ_HINT_INT},         {"short", CLJ_HINT_SHORT},
	{"byte", CLJ_HINT_BYTE},       {"double", CLJ_HINT_DOUBLE},   {"float", CLJ_HINT_FLOAT},
	{"boolean", CLJ_HINT_BOOLEAN}, {"char", CLJ_HINT_CHAR},       {"String", CLJ_HINT_STRING},
	{"java.lang.String", CLJ_HINT_STRING}, {"Keyword", CLJ_HINT_KEYWORD}, {"clojure.lang.Keyword", CLJ_HINT_KEYWORD},
	{"Symbol", CLJ_HINT_SYMBOL},   {"clojure.lang.Symbol", CLJ_HINT_SYMBOL},
	{"longs", CLJ_HINT_ARRAY + CLJ_ARRAY_I64}, {"ints", CLJ_HINT_ARRAY + CLJ_ARRAY_I32}, {"shorts", CLJ_HINT_ARRAY + CLJ_ARRAY_I16},
	{"bytes", CLJ_HINT_ARRAY + CLJ_ARRAY_I8},  {"doubles", CLJ_HINT_ARRAY + CLJ_ARRAY_F64}, {"floats", CLJ_HINT_ARRAY + CLJ_ARRAY_F32},
	{"booleans", CLJ_HINT_ARRAY + CLJ_ARRAY_BOOL}, {"chars", CLJ_HINT_ARRAY + CLJ_ARRAY_CHAR}, {"objects", CLJ_HINT_ARRAY + CLJ_ARRAY_OBJECT},
};

static clj_value kw_tag;

clj_field_hint clj_field_hint_of(clj_value field) {
	if (!clj_is_symbol(field)) return CLJ_HINT_NONE;
	clj_value m = clj_meta(field);
	CLJ_ROOTS(&m);
	clj_value tag = clj_is_nil(m) || m == CLJ_THROWN ? CLJ_NIL : clj_map_get(m, kw_tag, CLJ_NIL);
	clj_field_hint h = CLJ_HINT_NONE;
	// A qualified tag (clojure.core/long) names no class; the reader leaves a class name unqualified.
	if (clj_is_symbol(tag) && clj_is_nil(clj_symbol_ns(tag))) {
		const char *name = CLJ_STR(clj_symbol_name(tag)).bytes;
		for (size_t i = 0; i < sizeof hint_names / sizeof *hint_names; i++) {
			if (strcmp(hint_names[i].name, name) == 0) h = hint_names[i].hint;
		}
	}
	if (m != CLJ_THROWN) clj_release(m);
	return h;
}

static uint8_t *field_hints(clj_value fields) {
	if (!clj_is_vector(fields)) return NULL;
	uint32_t n = clj_vector_count(fields);
	uint8_t *hints = NULL;
	for (uint32_t i = 0; i < n; i++) {
		clj_field_hint h = clj_field_hint_of(clj_vector_nth(fields, i));
		if (h == CLJ_HINT_NONE) continue;
		if (!hints && !(hints = calloc(n, 1))) clj_fatal("out of memory");
		hints[i] = (uint8_t)h;
	}
	return hints;
}

static clj_value kw_unsync, kw_volatile;

static bool meta_flag(clj_value m, clj_value kw) { return clj_is_map(m) && clj_truthy(clj_map_get(m, kw, CLJ_NIL)); }

// The CLJ_FIELD_* bits a field's meta and hint give it, 0 for an immutable field; CLJ_THROWN for both marks.
static clj_value field_mut_of(clj_value field, uint8_t hint) {
	clj_value m = clj_meta(field);
	if (m == CLJ_THROWN) return CLJ_THROWN;
	CLJ_ROOTS(&m);
	bool unsync = meta_flag(m, kw_unsync), vol = meta_flag(m, kw_volatile);
	clj_release(m);
	m = CLJ_NIL;
	if (unsync && vol) return clj_throw_msg("Field %s is both ^:unsynchronized-mutable and ^:volatile-mutable", CLJ_STR(clj_symbol_name(field)).bytes);
	if (!unsync && !vol) return clj_fixnum(0);
	uint8_t bits = unsync ? CLJ_FIELD_UNSYNC : CLJ_FIELD_VOLATILE;
	switch ((clj_field_hint)hint) {
	case CLJ_HINT_LONG:
	case CLJ_HINT_DOUBLE:
	case CLJ_HINT_FLOAT: bits |= CLJ_FIELD_RAW; break;
	// what these admit is an immediate, so a store is no edge
	case CLJ_HINT_INT:
	case CLJ_HINT_SHORT:
	case CLJ_HINT_BYTE:
	case CLJ_HINT_BOOLEAN:
	case CLJ_HINT_CHAR: break;
	default: bits |= CLJ_FIELD_HEAP; break;
	}
	return clj_fixnum(bits);
}

// ut->muts, owned and heap_mut from the field symbols; false with the exception pending.
static bool field_muts(clj_user_type *ut, clj_value fields) {
	for (uint32_t i = 0; i < ut->nfields; i++) {
		clj_value bits = field_mut_of(clj_vector_nth(fields, i), ut->hints ? ut->hints[i] : CLJ_HINT_NONE);
		if (bits == CLJ_THROWN) return false;
		uint8_t b = (uint8_t)clj_fixnum_val(bits);
		if (!b) continue;
		if (!ut->muts && !(ut->muts = calloc(ut->nfields, 1))) clj_fatal("out of memory");
		ut->muts[i] = b;
		if (b & CLJ_FIELD_UNSYNC) ut->owned = true;
		if (b & CLJ_FIELD_HEAP) ut->heap_mut = true;
	}
	return true;
}

bool clj_fields_mutable(clj_value fields) {
	clj_value m = CLJ_NIL;
	CLJ_ROOTS(&m);
	for (uint32_t i = 0; clj_is_vector(fields) && i < clj_vector_count(fields); i++) {
		m = clj_meta(clj_vector_nth(fields, i));
		bool mut = m != CLJ_THROWN && (meta_flag(m, kw_unsync) || meta_flag(m, kw_volatile));
		if (m != CLJ_THROWN) clj_release(m);
		m = CLJ_NIL;
		if (mut) return true;
	}
	return false;
}

static clj_value refuse_field(const clj_user_type *ut, uint32_t i, clj_value v, const char *hint) {
	clj_value f = clj_vector_nth(ut->fields.v, i);
	return clj_throw_msg("%s cannot be cast to %s: field %s of %s", clj_type_name(v), hint, CLJ_STR(clj_symbol_name(f)).bytes, ut->t.name);
}

clj_value clj_field_admit(const clj_user_type *ut, uint32_t i, clj_value v) {
	switch (ut->hints ? (clj_field_hint)ut->hints[i] : CLJ_HINT_NONE) {
	case CLJ_HINT_NONE: return clj_retain(v);
	case CLJ_HINT_LONG: return clj_is_fixnum(v) ? v : clj_long_cast(v);
	case CLJ_HINT_INT: return clj_is_fixnum(v) && clj_fixnum_val(v) >= INT32_MIN && clj_fixnum_val(v) <= INT32_MAX ? v : clj_int_cast(v);
	case CLJ_HINT_SHORT: return clj_array_cast(CLJ_ARRAY_I16, v);
	case CLJ_HINT_BYTE: return clj_array_cast(CLJ_ARRAY_I8, v);
	case CLJ_HINT_DOUBLE: return clj_is_double(v) ? clj_retain(v) : clj_array_cast(CLJ_ARRAY_F64, v);
	case CLJ_HINT_FLOAT: return clj_array_cast(CLJ_ARRAY_F32, v);
	case CLJ_HINT_BOOLEAN: return clj_array_cast(CLJ_ARRAY_BOOL, v);
	case CLJ_HINT_CHAR: return clj_array_cast(CLJ_ARRAY_CHAR, v);
	case CLJ_HINT_STRING: return clj_is_nil(v) || clj_is_string(v) ? clj_retain(v) : refuse_field(ut, i, v, "String");
	case CLJ_HINT_KEYWORD: return clj_is_nil(v) || clj_is_keyword(v) ? clj_retain(v) : refuse_field(ut, i, v, "Keyword");
	case CLJ_HINT_SYMBOL: return clj_is_nil(v) || clj_is_symbol(v) ? clj_retain(v) : refuse_field(ut, i, v, "Symbol");
	default: break;
	}
	uint8_t h = ut->hints[i];
	if (h < CLJ_HINT_ARRAY || h >= CLJ_HINT_ARRAY + CLJ_ARRAY_KIND_COUNT) clj_ice("proto/hint", NULL, "field hint %u", (unsigned)h);
	clj_array_kind k = (clj_array_kind)(h - CLJ_HINT_ARRAY);
	// (bytes x) admits the bridge's u8 buffer as well (builtins_array.c)
	if (clj_is_nil(v) || (clj_is_array(v) && (clj_array_kind_of(v) == k || (k == CLJ_ARRAY_I8 && clj_array_kind_of(v) == CLJ_ARRAY_U8)))) return clj_retain(v);
	char what[24];
	snprintf(what, sizeof what, "%s array", clj_array_kind_name(k));
	return refuse_field(ut, i, v, what);
}

clj_value clj_user_type_init(clj_user_type *ut, clj_value name, clj_value fields, const clj_value *impls, size_t nimpls) {
	ut->serial = atomic_fetch_add_explicit(&type_serials, 1, memory_order_relaxed) + 1;
	users_add(&ut->t);
	if (user_type_shape(name, fields, nimpls) == CLJ_THROWN) {
		clj_release(clj_from_ptr(ut));
		return CLJ_THROWN;
	}
	const char *ns = CLJ_STR(clj_symbol_name(clj_ns_name(clj_ns_current()))).bytes;
	const char *bare = CLJ_STR(clj_symbol_name(name)).bytes;
	size_t      len = strlen(ns) + 1 + strlen(bare);
	char       *text = malloc(len + 1);
	if (!text) clj_fatal("out of memory");
	snprintf(text, len + 1, "%s.%s", ns, bare);
	clj_value spelled = clj_string_new(text, len);
	free(text);
	// t.name points into it for the type's life.
	clj_slot_init(&ut->t.h, &ut->name, clj_string_materialize(spelled));
	clj_release(spelled);
	clj_slot_init(&ut->t.h, &ut->fields, clj_retain(fields));
	ut->nfields = clj_vector_count(fields);
	ut->hints = field_hints(fields);
	if (!field_muts(ut, fields)) {
		clj_release(clj_from_ptr(ut));
		return CLJ_THROWN;
	}
	ut->t.mutable_children = ut->muts != NULL;
	ut->reified = false;
	ut->t.name = clj_string_heap_bytes(ut->name.v);
	ut->t.each_child = instance_each_child;
	ut->t.hash = identity_hash;
	ut->t.equals = identity_equals;
	CLJ_NO_CYCLE clj_value type = clj_from_ptr(ut);
	for (size_t i = 0; i < nimpls; i += 2) {
		clj_value p = impls[i], mm = impls[i + 1];
		clj_value r;
		if (!clj_is_protocol(p)) r = clj_throw_msg("%s is not a protocol", clj_type_name(p));
		else if (!clj_is_nil(mm) && !clj_is_map(mm)) r = clj_throw_msg("deftype* expects a map of method fns, got: %s", clj_type_name(mm));
		else if (clj_protocol_of(p)->core_bits) r = implement_interface(ut, p, mm);
		else r = clj_proto_extend(type, p, mm);
		if (r == CLJ_THROWN) {
			clj_release(type);
			return CLJ_THROWN;
		}
	}
	// Methods given under an ISeq/IPersistentCollection group mark the override as IHashEq/IEquiv would.
	if (!clj_is_nil(ut->core_fns[CLJ_CM_HASH].v)) ut->t.core_bits |= CLJ_CORE_HASHEQ;
	if (!clj_is_nil(ut->core_fns[CLJ_CM_EQUALS].v)) ut->t.core_bits |= CLJ_CORE_EQUIV;
	fill_slots(ut);
	clj_epoch_bump();
	return type;
}

clj_value clj_user_type_new(clj_value name, clj_value fields, const clj_value *impls, size_t nimpls) {
	if (user_type_shape(name, fields, nimpls) == CLJ_THROWN) return CLJ_THROWN;
	return clj_user_type_init(clj_alloc(&clj_type_type, sizeof(clj_user_type)), name, fields, impls, nimpls);
}

clj_value clj_instance_new(clj_value type, const clj_value *fields, size_t n) {
	if (!clj_is_user_type(type)) return clj_throw_msg("new* expects a deftype, got: %s", clj_type_name(type));
	clj_user_type *ut = clj_to_ptr(type);
	if (n != ut->nfields) return clj_throw_msg("%s has %u fields, got %zu", ut->t.name, ut->nfields, n);
	if (!ut->hints) return clj_instance_make(ut, fields, n);
	clj_value  small[8];
	clj_value *vals = n <= 8 ? small : malloc(n * sizeof *vals);
	if (!vals) clj_fatal("out of memory");
	size_t k = 0;
	CLJ_ROOTS_SPAN(&vals, &k);
	for (; k < n; k++) {
		vals[k] = clj_field_admit(ut, (uint32_t)k, fields[k]);
		if (vals[k] == CLJ_THROWN) break;
	}
	clj_value r = k == n ? clj_instance_make(ut, vals, n) : CLJ_THROWN;
	for (size_t i = 0; i < k; i++) clj_release(vals[i]);
	if (vals != small) free(vals);
	return r;
}

clj_value clj_instance_field(clj_value obj, uint32_t i) {
	CLJ_ASSERT(clj_is_instance(obj) && i < ((const clj_user_type *)clj_type_of(obj))->nfields, "instance field out of range");
	CLJ_ASSERT(!clj_user_type_field_mutable((const clj_user_type *)clj_type_of(obj), i), "a plain read of a mutable field");
	return ((clj_instance *)clj_to_ptr(obj))->fields[i].v;
}

// ---- mutable fields (design §4 «Роли конструкций и ссылочные типы»)

static uint64_t raw_bits(clj_field_hint hint, clj_value admitted) {
	if (hint == CLJ_HINT_LONG) {
		int64_t x = 0;
		if (!clj_int64_of(admitted, &x)) clj_ice("proto/raw-long", NULL, "a ^long field admitted a %s", clj_type_name(admitted));
		return (uint64_t)x;
	}
	if (!clj_is_double(admitted)) clj_ice("proto/raw-double", NULL, "a ^double field admitted a %s", clj_type_name(admitted));
	double   d = clj_double_val(admitted);
	uint64_t bits;
	memcpy(&bits, &d, sizeof bits);
	return bits;
}

static clj_value raw_value(clj_field_hint hint, uint64_t bits) {
	if (hint == CLJ_HINT_LONG) return clj_long_new((int64_t)bits);
	double d;
	memcpy(&d, &bits, sizeof d);
	return clj_double_new(d);
}

// The execution an instance's ^:unsynchronized-mutable fields belong to: the one that made it.
static uintptr_t owner_here(void) { return (uintptr_t)clj_coro_current(); }

clj_value clj_instance_make_mutable(const clj_user_type *ut, const clj_value *fields, size_t n) {
	clj_instance *o = clj_alloc(&ut->t, sizeof *o + (n + (ut->owned ? 1 : 0)) * sizeof *o->fields);
	// before the slots: a MUTABLE owner takes no reach bits from its children, so it is born with them (object.h)
	if (ut->heap_mut) o->h.flags |= CLJ_FLAG_MUTABLE | CLJ_FLAG_REACH | clj_cc_born_local();
	o->h.flags |= ut->birth_flags;
	for (size_t i = 0; i < n; i++) {
		if (ut->muts[i] & CLJ_FIELD_RAW) clj_slot_set_bits(&o->fields[i], raw_bits((clj_field_hint)ut->hints[i], fields[i]), memory_order_relaxed);
		else clj_slot_init(&o->h, &o->fields[i], clj_retain(fields[i]));
	}
	if (ut->owned) clj_slot_set_bits(&o->fields[n], owner_here(), memory_order_relaxed);
	clj_retain(clj_from_ptr((void *)ut));
	return clj_from_ptr(o);
}

// The instance x and its mutable field i, or false with the throw pending.
static bool mutable_at(clj_value x, clj_value i, const char *op, clj_instance **o, uint32_t *k) {
	if (!clj_is_instance(x)) {
		clj_throw_msg("%s expects a deftype instance, got: %s", op, clj_type_name(x));
		return false;
	}
	const clj_user_type *ut = (const clj_user_type *)clj_type_of(x);
	if (!clj_is_fixnum(i) || clj_fixnum_val(i) < 0 || (uintptr_t)clj_fixnum_val(i) >= ut->nfields) {
		clj_throw_msg("%s index out of range", op);
		return false;
	}
	*k = (uint32_t)clj_fixnum_val(i);
	if (!clj_user_type_field_mutable(ut, *k)) {
		clj_throw_msg("%s: field %s of %s is not mutable", op, CLJ_STR(clj_symbol_name(clj_vector_nth(ut->fields.v, *k))).bytes, ut->t.name);
		return false;
	}
	*o = clj_to_ptr(x);
	return true;
}

static memory_order field_order(uint8_t m) { return (m & CLJ_FIELD_VOLATILE) ? memory_order_seq_cst : memory_order_relaxed; }

// A writer releases a published slot's old value after every window closed, as an atom's commit does.
static clj_value mutable_read(clj_instance *o, uint32_t i) {
	const clj_user_type *ut = (const clj_user_type *)o->h.type;
	uint8_t              m = ut->muts[i];
	memory_order         order = field_order(m);
	if (m & CLJ_FIELD_RAW) return raw_value((clj_field_hint)ut->hints[i], clj_slot_bits(&o->fields[i], order));
	const clj_atomic_slot *slot = (const clj_atomic_slot *)&o->fields[i];
	if (!(o->h.flags & CLJ_FLAG_SHARED)) return clj_retain(clj_slot_load(slot, order));
	clj_value v = clj_slot_load(slot, order);
	if (!clj_is_ptr(v)) return v;
	clj_proto_reader *r = clj_proto_window_open_inline();
	v = clj_retain(clj_slot_load(slot, order));
	clj_proto_window_close_inline(r);
	return v;
}

static clj_value field_name_msg(const clj_user_type *ut, uint32_t i) { return clj_symbol_name(clj_vector_nth(ut->fields.v, i)); }

static clj_value mutable_write(clj_instance *o, uint32_t i, clj_value v, bool check) {
	const clj_user_type *ut = (const clj_user_type *)o->h.type;
	uint8_t              m = ut->muts[i];
	bool                 shared = (o->h.flags & CLJ_FLAG_SHARED) != 0;
	if (check && (m & CLJ_FIELD_UNSYNC) && shared && clj_slot_bits(&o->fields[ut->nfields], memory_order_relaxed) != owner_here()) {
		return clj_throw_msg("Field %s of %s is ^:unsynchronized-mutable and set! from an execution other than the one that made the "
		                     "instance; ^:volatile-mutable is the field written from several",
		                     CLJ_STR(field_name_msg(ut, i)).bytes, ut->t.name);
	}
	clj_value admitted = clj_field_admit(ut, i, v);
	if (admitted == CLJ_THROWN) return CLJ_THROWN;
	memory_order order = field_order(m);
	if (m & CLJ_FIELD_RAW) {
		clj_slot_set_bits(&o->fields[i], raw_bits((clj_field_hint)ut->hints[i], admitted), order);
		return admitted;
	}
	if (!shared) {
		clj_value old = o->fields[i].v;
		clj_slot_store(&o->h, &o->fields[i], clj_retain(admitted));
		clj_release(old);
		return admitted;
	}
	// An exchange, so two writers racing on an unsynchronized field never release one old value twice.
	clj_value old = clj_slot_exchange(&o->h, (clj_atomic_slot *)&o->fields[i], clj_retain(admitted), (m & CLJ_FIELD_VOLATILE) ? memory_order_seq_cst : memory_order_acq_rel);
	if (clj_is_ptr(old)) clj_proto_wait_readers();
	clj_release(old);
	return admitted;
}

clj_value clj_mutable_field(clj_value x, clj_value i) {
	CLJ_BORROWED clj_instance *o;
	uint32_t                   k;
	return mutable_at(x, i, "mutable-field*", &o, &k) ? mutable_read(o, k) : CLJ_THROWN;
}

clj_value clj_set_field(clj_value x, clj_value i, clj_value v) {
	CLJ_BORROWED clj_instance *o;
	uint32_t                   k;
	return mutable_at(x, i, "set-field!*", &o, &k) ? mutable_write(o, k, v, true) : CLJ_THROWN;
}

CLJ_UNIT_ENTRY clj_value clj_set_field_closed(clj_value x, clj_value i, clj_value v) {
	CLJ_BORROWED clj_instance *o;
	uint32_t                   k;
	return mutable_at(x, i, "set-field!*", &o, &k) ? mutable_write(o, k, v, false) : CLJ_THROWN;
}

clj_value clj_catch_instance(clj_value var, clj_value ex) {
	clj_value type = clj_var_deref(var);
	if (type == CLJ_THROWN) return CLJ_THROWN;
	CLJ_ROOTS(&type);
	clj_value r = clj_is_instance_of(type, ex);
	clj_release(type);
	type = CLJ_NIL;
	return r;
}

clj_value clj_is_instance_of(clj_value type, clj_value v) {
	if (clj_is_protocol(type) && clj_protocol_of(type)->core_bits) return clj_bool(clj_has_core(v, clj_protocol_of(type)->core_bits));
	// A host type answers the same cast a catch clause naming it makes (design §4).
	if (clj_is_host_type(type)) return clj_bool(clj_host_type_instance(type, v));
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
	if (!clj_is_fixnum(args[1]) || clj_fixnum_val(args[1]) < 0 || (uintptr_t)clj_fixnum_val(args[1]) >= clj_vector_count(clj_protocol_of(args[0])->methods.v)) {
		return clj_throw_msg("protocol-method* index out of range");
	}
	return clj_protocol_method(args[0], (uint32_t)clj_fixnum_val(args[1]));
}

static clj_value b_deftype(const clj_value *args, size_t n) { return clj_user_type_new(args[0], args[1], args + 2, n - 2); }

CLJ_UNIT_ENTRY clj_value clj_b_new_star(const clj_value *args, size_t n) {
	if (clj_is_record_type(args[0])) return clj_record_new(args[0], args + 1, n - 1);
	return clj_instance_new(args[0], args + 1, n - 1);
}

CLJ_UNIT_ENTRY clj_value clj_b_field_star(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_instance(args[0])) return clj_throw_msg("field* expects a deftype instance, got: %s", clj_type_name(args[0]));
	uint32_t nfields = ((const clj_user_type *)clj_type_of(args[0]))->nfields;
	if (!clj_is_fixnum(args[1]) || clj_fixnum_val(args[1]) < 0 || (uintptr_t)clj_fixnum_val(args[1]) >= nfields) return clj_throw_msg("field* index out of range");
	if (clj_user_type_field_mutable((const clj_user_type *)clj_type_of(args[0]), (uint32_t)clj_fixnum_val(args[1]))) return clj_throw_msg("field* of a mutable field: mutable-field* reads it");
	return clj_retain(clj_instance_field(args[0], (uint32_t)clj_fixnum_val(args[1])));
}

static clj_value b_mutable_field(const clj_value *args, size_t n) {
	(void)n;
	return clj_mutable_field(args[0], args[1]);
}

static clj_value b_set_field(const clj_value *args, size_t n) {
	(void)n;
	return clj_set_field(args[0], args[1], args[2]);
}

// A reify method: the impl closure sits in field idx of the instance it is called on.
static clj_value trampoline_invoke(void *ctx, const clj_value *args, size_t n) {
	uint32_t idx = (uint32_t)(uintptr_t)ctx;
	if (n == 0 || !clj_is_instance(args[0]) || idx >= ((const clj_user_type *)clj_type_of(args[0]))->nfields) {
		return clj_throw_msg("reify method called on %s", n ? clj_type_name(args[0]) : "nothing");
	}
	return clj_invoke(clj_instance_field(args[0], idx), args, n);
}

// ---- reify types: one per expansion site for the life of the process, keyed by the site's gensym'd name

static clj_lock reify_lock = CLJ_LOCK_INIT;
static clj_value       reify_types; // map symbol -> type; nil until the first site

typedef struct {
	clj_value              map; // owned: {:method trampoline}
	CLJ_BORROWED clj_value thrown;
} trampoline_ctx;

static bool add_trampoline(clj_value key, clj_value val, void *ctx) {
	trampoline_ctx *c = ctx;
	if (!clj_is_fixnum(val) || clj_fixnum_val(val) < 0) {
		c->thrown = clj_throw_msg("reify-type* expects a field index per method, got: %s", clj_type_name(val));
		return false;
	}
	clj_value f = clj_fn_native_ctx(CLJ_NIL, trampoline_invoke, (void *)(uintptr_t)clj_fixnum_val(val), NULL, 1, CLJ_ARITY_ANY);
	CLJ_ROOTS(&f);
	// The caller names c->map: it is cleared before the consuming assoc, which may park.
	clj_value m = c->map;
	c->map = CLJ_NIL;
	c->map = clj_map_assoc(m, key, f);
	clj_release(f);
	f = CLJ_NIL;
	return true;
}

// impls alternate protocol and {:method field-index}; owned type, or CLJ_THROWN.
static clj_value reify_type_new(clj_value name, clj_value fields, const clj_value *impls, size_t nimpls) {
	if (nimpls % 2) return clj_throw_msg("reify-type* expects protocol and method-map pairs");
	size_t                  npairs = nimpls / 2;
	clj_value              *maps = calloc(npairs ? npairs : 1, sizeof *maps);
	CLJ_BORROWED clj_value *with_fns = calloc(nimpls ? nimpls : 1, sizeof *with_fns);
	if (!maps || !with_fns) clj_fatal("out of memory");
	CLJ_NO_CYCLE clj_value type = CLJ_NIL;
	size_t                 made = 0;
	trampoline_ctx         c = {CLJ_NIL, CLJ_NIL};
	CLJ_ROOTS_SPAN(&maps, &made);
	CLJ_ROOTS(&c.map);
	for (; made < npairs; made++) {
		clj_value mm = impls[2 * made + 1];
		if (!clj_is_nil(mm) && !clj_is_map(mm)) {
			type = clj_throw_msg("reify-type* expects a map of field indices, got: %s", clj_type_name(mm));
			break;
		}
		c.map = clj_map_empty();
		if (!clj_is_nil(mm)) clj_map_each(mm, add_trampoline, &c);
		if (c.thrown == CLJ_THROWN) {
			clj_release(c.map);
			c.map = CLJ_NIL;
			type = CLJ_THROWN;
			break;
		}
		maps[made] = c.map;
		c.map = CLJ_NIL;
		with_fns[2 * made] = impls[2 * made];
		with_fns[2 * made + 1] = maps[made];
	}
	if (type != CLJ_THROWN) type = clj_user_type_new(name, fields, with_fns, nimpls);
	if (type != CLJ_THROWN) ((clj_user_type *)clj_to_ptr(type))->reified = true;
	while (made) clj_release(maps[--made]);
	free(maps);
	free(with_fns);
	return type;
}

// Whether every protocol the site names is one the type's tables hold.
static bool reify_type_current(clj_value type, const clj_value *impls, size_t nimpls) {
	for (size_t i = 0; i < nimpls; i += 2) {
		if (clj_is_protocol(impls[i]) && !clj_truthy(clj_proto_extends(impls[i], type))) return false;
	}
	return true;
}

// The type behind a reify site: made on the first call, a lookup after. Creation runs under the
// registry lock so a racing first call of one site cannot make two types.
static clj_value reify_type(clj_value name, clj_value fields, const clj_value *impls, size_t nimpls) {
	if (!is_unqualified_symbol(name)) return clj_throw_msg("reify-type* expects a symbol, got: %s", clj_type_name(name));
	if (!clj_is_vector(fields)) return clj_throw_msg("reify-type* expects a field vector, got: %s", clj_type_name(fields));
	clj_lock_lock(&reify_lock);
	CLJ_NO_CYCLE clj_value type = clj_is_nil(reify_types) ? CLJ_NIL : clj_map_get(reify_types, name, CLJ_NIL);
	// A protocol redefined since the type was made leaves it implementing the old one: the site gets a new type.
	if (!clj_is_nil(type) && !reify_type_current(type, impls, nimpls)) type = CLJ_NIL;
	if (!clj_is_nil(type)) {
		// Gensym names are unique per process; a tree loaded from elsewhere may reuse one for another site.
		if (((clj_user_type *)clj_to_ptr(type))->nfields != clj_vector_count(fields)) {
			clj_lock_unlock(&reify_lock);
			return clj_throw_msg("reify type %s already exists with a different shape", CLJ_STR(clj_symbol_name(name)).bytes);
		}
		clj_retain(type);
	} else if ((type = reify_type_new(name, fields, impls, nimpls)) != CLJ_THROWN) {
		// Shares the type too: every execution reaching the site gets it, and instances retain it outside the lock.
		clj_root_store(&reify_types, clj_map_assoc(clj_is_nil(reify_types) ? clj_map_empty() : reify_types, name, type));
	}
	clj_lock_unlock(&reify_lock);
	return type;
}

static clj_value b_reify_type(const clj_value *args, size_t n) { return reify_type(args[0], args[1], args + 2, n - 2); }

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

static clj_value b_proto_epoch(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_fixnum((intptr_t)clj_epoch());
}

static void bind_core(const char *name, clj_value val) {
	CLJ_NO_CYCLE clj_value sym = clj_symbol_from_cstr(name);
	clj_var_bind_root(clj_ns_intern(clj_ns_core(), sym), val);
	clj_release(sym);
}

void clj_proto_install(void) {
	kw_tag = clj_keyword_from_cstr("tag");
	kw_unsync = clj_keyword_from_cstr("unsynchronized-mutable");
	kw_volatile = clj_keyword_from_cstr("volatile-mutable");
	static const struct {
		const char   *name;
		clj_native_fn fn;
		uint32_t      min, max;
	} fns[] = {
		{"protocol*", b_protocol, 2, 2},       {"protocol-method*", b_protocol_method, 2, 2}, {"deftype*", b_deftype, 2, CLJ_ARITY_ANY},
		{"new*", clj_b_new_star, 1, CLJ_ARITY_ANY},     {"field*", clj_b_field_star, 2, 2},                     {"reify-type*", b_reify_type, 2, CLJ_ARITY_ANY},
		{"extend*", b_extend, 3, 3},           {"satisfies?", b_satisfies, 2, 2},             {"extends?", b_extends, 2, 2},
		{"instance?", b_instance, 2, 2},       {"type", b_type, 1, 1},
		{"protocol-epoch*", b_proto_epoch, 0, 0},
		{"mutable-field*", b_mutable_field, 2, 2}, {"set-field!*", b_set_field, 3, 3},
	};
	clj_value core_name = clj_symbol_name(clj_ns_name(clj_ns_core()));
	clj_value f = CLJ_NIL;
	CLJ_ROOTS(&f);
	for (size_t i = 0; i < sizeof fns / sizeof *fns; i++) {
		CLJ_NO_CYCLE clj_value name = clj_string_from_cstr(fns[i].name);
		CLJ_NO_CYCLE clj_value qualified = clj_symbol_new(core_name, name);
		f = clj_fn_native(qualified, fns[i].fn, fns[i].min, fns[i].max);
		bind_core(fns[i].name, f);
		clj_release(f);
		f = CLJ_NIL;
		clj_release(qualified);
		clj_release(name);
	}
	// The canonical builtin type names; (type x) reaches every other descriptor.
	static const struct {
		const char     *name;
		const clj_type *type;
	} types[] = {
		{"Object", &object_type},          {"Long", &clj_long_type},       {"Integer", &clj_long_type},        {"Double", &clj_double_type},
		{"Boolean", &boolean_type},        {"Character", &char_type},      {"String", &clj_string_type},     {"Keyword", &clj_keyword_type},
		{"Symbol", &clj_symbol_type},      {"PersistentVector", &clj_vector_type}, {"PersistentHashMap", &clj_map_type}, {"PersistentHashSet", &clj_set_type},
		{"PersistentList", &clj_list_type}, {"Cons", &clj_cons_type},      {"EmptyList", &clj_empty_list_type}, {"LazySeq", &clj_lazy_seq_type},
		{"Range", &clj_range_type},        {"Fn", &clj_fn_type},           {"Var", &clj_var_type},           {"Namespace", &clj_ns_type},
		{"ExceptionInfo", &clj_exception_type}, {"HostError", &clj_host_error_type}, {"Protocol", &clj_protocol_type}, {"Type", &clj_type_type},
		{"HostType", &clj_host_type_type},
		{"Reduced", &clj_reduced_type},    {"Volatile", &clj_volatile_type},  {"Atom", &clj_atom_type},
		{"PersistentTreeMap", &clj_sorted_map_type}, {"PersistentTreeSet", &clj_sorted_set_type},
		{"Pattern", &clj_regex_type},      {"Matcher", &clj_matcher_type},    {"UUID", &clj_uuid_type},         {"Date", &clj_inst_type},
		// The JVM class names libraries spell out in an instance? check; the static methods stay interop.
		{"java.util.UUID", &clj_uuid_type}, {"java.util.Date", &clj_inst_type},
	};
	// Every designator and interface below is a var of this core alone: the JVM spells them as classes.
	for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
		bind_core(types[i].name, clj_from_ptr((void *)types[i].type));
		clj_core_mark_extension(types[i].name);
	}
	clj_value empty = clj_vector_empty();
	for (size_t i = 0; i < NINTERFACES; i++) {
		CLJ_NO_CYCLE clj_value name = clj_string_from_cstr(interfaces[i].name);
		CLJ_NO_CYCLE clj_value qualified = clj_symbol_new(core_name, name);
		interfaces[i].proto = protocol_alloc(qualified, empty, empty, interfaces[i].bits);
		bind_core(interfaces[i].name, interfaces[i].proto);
		clj_core_mark_extension(interfaces[i].name);
		// A .clj library spells the interface out in full, in instance? and in a deftype body; the bare name is
		// ours. IEquiv is ClojureScript's, the JVM has no such class.
		if (interfaces[i].bits != CLJ_CORE_EQUIV) {
			char full[64];
			snprintf(full, sizeof full, "clojure.lang.%s", interfaces[i].name);
			bind_core(full, interfaces[i].proto);
			clj_core_mark_extension(full);
		}
		// (instance? Throwable e) is how portable code asks whether a value is an error: ex-info or a host error.
		if (interfaces[i].bits == CLJ_CORE_ERROR) {
			static const char *const throwables[] = {"Throwable", "Exception"};
			for (size_t j = 0; j < sizeof throwables / sizeof *throwables; j++) {
				bind_core(throwables[j], interfaces[i].proto);
				clj_core_mark_extension(throwables[j]);
			}
		}
		clj_release(qualified);
		clj_release(name);
	}
}
