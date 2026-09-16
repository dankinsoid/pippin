// @ai-generated(solo)
#include <stddef.h>
#include <stdlib.h>

#include "clj/coll.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/ns.h"
#include "clj/record.h"
#include "clj/reduce.h"
#include "clj/var.h"
#include "clj/vector.h"

// The bits and slots of the hash map, minus IEditableCollection: a record has no transient on the JVM either.
#define RECORD_BITS                                                                                          \
	(CLJ_CORE_SEQABLE | CLJ_CORE_COLL | CLJ_CORE_COUNTED | CLJ_CORE_LOOKUP | CLJ_CORE_ASSOCIATIVE |          \
	 CLJ_CORE_FN | CLJ_CORE_MAP | CLJ_CORE_META | CLJ_CORE_OBJ | CLJ_CORE_REDUCE | CLJ_CORE_RECORD)

// field* and the deftype method bodies reach a record's basis through clj_instance_field.
_Static_assert(offsetof(clj_record, slots) == offsetof(clj_instance, fields), "a record's basis must sit where a deftype instance's fields do");

static const clj_record_type *rtype_of(clj_value v) { return (const clj_record_type *)clj_type_of(v); }
static clj_record            *rec_of(clj_value v) { return clj_to_ptr(v); }
static uint32_t               nfields_of(clj_value v) { return rtype_of(v)->ut.nfields; }

// extmap sits at nfields, meta right after it.
static clj_value *ext_slot(clj_value v) { return &rec_of(v)->slots[nfields_of(v)]; }
static clj_value *meta_slot(clj_value v) { return &rec_of(v)->slots[nfields_of(v) + 1]; }

// Storing into a shared record must keep the invariant that its children are shared.
static void store(clj_header *owner, clj_value *slot, clj_value v) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	*slot = v;
}

static clj_record *record_alloc(const clj_user_type *ut) {
	clj_record *r = clj_alloc(&ut->t, sizeof *r + ((size_t)ut->nfields + 2) * sizeof(clj_value));
	clj_retain(clj_from_ptr((void *)ut));
	return r;
}

// Consumes self; returns a record this operation may write into.
static clj_record *record_own(clj_value self) {
	if (clj_is_unique(self)) return rec_of(self);
	const clj_user_type *ut = (const clj_user_type *)clj_type_of(self);
	clj_record          *c = record_alloc(ut);
	for (uint32_t i = 0; i < ut->nfields + 2u; i++) c->slots[i] = clj_retain(rec_of(self)->slots[i]);
	clj_release(self);
	return c;
}

static void record_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_record          *r = self;
	const clj_user_type *ut = (const clj_user_type *)r->h.type;
	for (uint32_t i = 0; i < ut->nfields + 2u; i++) visit(r->slots[i], ctx);
	visit(clj_from_ptr((void *)ut), ctx);
}

int32_t clj_record_basis_index(const clj_type *t, clj_value key) {
	const clj_record_type *rt = (const clj_record_type *)t;
	for (uint32_t i = 0; i < rt->ut.nfields; i++) {
		if (rt->basis[i] == key) return (int32_t)i;
	}
	return -1;
}

clj_value clj_record_field(clj_value r, uint32_t i) {
	CLJ_ASSERT(clj_is_record(r) && i < nfields_of(r), "record field out of range");
	return rec_of(r)->slots[i];
}

uint32_t clj_record_count(clj_value r) {
	clj_value ext = *ext_slot(r);
	return nfields_of(r) + (clj_is_nil(ext) ? 0 : clj_map_count(ext));
}

void clj_record_each(clj_value r, clj_map_entry_fn fn, void *ctx) {
	const clj_record_type *rt = rtype_of(r);
	for (uint32_t i = 0; i < rt->ut.nfields; i++) {
		if (!fn(rt->basis[i], rec_of(r)->slots[i], ctx)) return;
	}
	clj_value ext = *ext_slot(r);
	if (!clj_is_nil(ext)) clj_map_each(ext, fn, ctx);
}

// ---- slots

static clj_value record_lookup(clj_value self, clj_value key, clj_value not_found) {
	int32_t i = clj_record_basis_index(clj_type_of(self), key);
	if (i >= 0) return clj_retain(rec_of(self)->slots[i]);
	clj_value ext = *ext_slot(self);
	return clj_retain(clj_is_nil(ext) ? not_found : clj_map_get(ext, key, not_found));
}

static clj_value record_count_slot(clj_value self) { return clj_fixnum(clj_record_count(self)); }

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

static clj_value record_seq(clj_value self) {
	uint32_t n = clj_record_count(self);
	if (!n) return CLJ_NIL;
	clj_value *entries = malloc(2 * (size_t)n * sizeof *entries);
	clj_value *pairs = malloc((size_t)n * sizeof *pairs);
	if (!entries || !pairs) clj_fatal("out of memory");
	collect_ctx c = {entries, 0};
	clj_record_each(self, collect_entry, &c);
	for (uint32_t i = 0; i < n; i++) pairs[i] = clj_vector_from_array(entries + 2 * (size_t)i, 2);
	clj_value list = clj_list_from_array(pairs, n);
	for (uint32_t i = 0; i < n; i++) clj_release(pairs[i]);
	free(pairs);
	free(entries);
	return list;
}

static bool reduce_entry(clj_value key, clj_value val, void *ctx) {
	clj_value pair[2] = {key, val};
	clj_value entry = clj_vector_from_array(pair, 2);
	bool      more = clj_reducer_step(ctx, entry);
	clj_release(entry);
	return more;
}

static clj_value record_reduce(clj_value self, clj_value f, clj_value init) {
	clj_reducer r = clj_reducer_start(f, init, 2);
	clj_record_each(self, reduce_entry, &r);
	return clj_reducer_finish(&r);
}

static bool reduce_kv_entry(clj_value key, clj_value val, void *ctx) { return clj_reducer_step_kv(ctx, key, val); }

clj_value clj_record_reduce_kv(clj_value r, clj_value f, clj_value init) {
	clj_reducer red = clj_reducer_start(f, init, 3);
	clj_record_each(r, reduce_kv_entry, &red);
	return clj_reducer_finish(&red);
}

static bool hash_entry(clj_value key, clj_value val, void *ctx) {
	*(uint32_t *)ctx += clj_map_entry_hash(key, val);
	return true;
}

// The map hash of the same content: equal content hashes alike whichever representation holds it.
static uint32_t record_hash(void *self) {
	clj_value me = clj_from_ptr(self);
	uint32_t  sum = 0;
	clj_record_each(me, hash_entry, &sum);
	return clj_mix_coll_hash(sum, clj_record_count(me));
}

static bool record_equals(void *self, clj_value other) {
	clj_value me = clj_from_ptr(self);
	if (me == other) return true;
	if (!clj_is_ptr(other) || clj_type_of(other) != clj_type_of(me)) return false;
	for (uint32_t i = 0; i < nfields_of(me); i++) {
		if (!clj_equals(rec_of(me)->slots[i], rec_of(other)->slots[i])) return false;
	}
	clj_value a = *ext_slot(me), b = *ext_slot(other);
	return clj_is_nil(a) ? clj_is_nil(b) : !clj_is_nil(b) && clj_equals(a, b);
}

// An extmap of no entries is kept as nil, so two records of equal content are equal.
static clj_value normalized(clj_value m) {
	if (!clj_is_nil(m) && clj_map_count(m) == 0) {
		clj_release(m);
		return CLJ_NIL;
	}
	return m;
}

// Consumes self and the slot's own reference to the extmap.
static clj_record *ext_assoc(clj_value self, clj_value key, clj_value val, bool remove) {
	clj_record *r = record_own(self);
	clj_value  *slot = &r->slots[((const clj_user_type *)r->h.type)->nfields];
	clj_value   base = clj_is_nil(*slot) ? clj_map_empty() : *slot;
	*slot = CLJ_NIL;
	clj_value m = remove ? clj_map_dissoc(base, key) : clj_map_assoc(base, key, val);
	store(&r->h, slot, normalized(m));
	return r;
}

static clj_value record_assoc(clj_value self, clj_value key, clj_value val) {
	int32_t i = clj_record_basis_index(clj_type_of(self), key);
	if (i >= 0) {
		if (rec_of(self)->slots[i] == val) return self;
		clj_record *r = record_own(self);
		clj_value   old = r->slots[i];
		store(&r->h, &r->slots[i], clj_retain(val));
		clj_release(old);
		return clj_from_ptr(r);
	}
	clj_value ext = *ext_slot(self);
	if (!clj_is_nil(ext) && clj_map_get(ext, key, CLJ_UNBOUND) == val) return self;
	return clj_from_ptr(ext_assoc(self, key, val, false));
}

clj_value clj_record_to_map(clj_value r) {
	size_t     n = 2 * (size_t)clj_record_count(r);
	clj_value *entries = n ? malloc(n * sizeof *entries) : NULL;
	if (n && !entries) clj_fatal("out of memory");
	collect_ctx c = {entries, 0};
	clj_record_each(r, collect_entry, &c);
	clj_value m = clj_map_empty();
	for (size_t i = 0; i < n; i += 2) m = clj_map_assoc(m, entries[i], entries[i + 1]);
	free(entries);
	clj_value meta = *meta_slot(r);
	return clj_is_nil(meta) ? m : clj_with_meta(m, meta);
}

// A basis key cannot be removed from a record, so the whole content comes back as a plain map.
static clj_value record_dissoc(clj_value self, clj_value key) {
	if (clj_record_basis_index(clj_type_of(self), key) >= 0) {
		clj_value m = clj_record_to_map(self);
		clj_release(self);
		if (m == CLJ_THROWN) return m;
		return clj_map_dissoc(m, key);
	}
	clj_value ext = *ext_slot(self);
	if (clj_is_nil(ext) || !clj_map_contains(ext, key)) return self;
	return clj_from_ptr(ext_assoc(self, key, CLJ_NIL, true));
}

static bool conj_entry(clj_value key, clj_value val, void *ctx) {
	clj_value *acc = ctx;
	*acc = record_assoc(*acc, key, val);
	return *acc != CLJ_THROWN;
}

// Consumes self. An entry is a [k v] pair, a map (all its entries) or nil, as a hash map's conj takes them.
static clj_value record_conj(clj_value self, clj_value item) {
	if (clj_is_nil(item)) return self;
	if (clj_is_vector(item) && clj_vector_count(item) == 2) return record_assoc(self, clj_vector_nth(item, 0), clj_vector_nth(item, 1));
	clj_value acc = self;
	if (clj_is_map(item)) {
		clj_map_each(item, conj_entry, &acc);
		return acc;
	}
	if (clj_has_core(item, CLJ_CORE_MAP)) {
		clj_value s = clj_seq(item);
		if (s == CLJ_THROWN) {
			clj_release(acc);
			return CLJ_THROWN;
		}
		clj_seq_iter it = clj_seq_iter_start(s);
		clj_value    entry;
		while (clj_seq_iter_next(&it, &entry) && conj_entry(clj_vector_nth(entry, 0), clj_vector_nth(entry, 1), &acc)) {
		}
		clj_seq_iter_close(&it);
		clj_release(s);
		if (it.thrown && acc != CLJ_THROWN) {
			clj_release(acc);
			return CLJ_THROWN;
		}
		return acc;
	}
	clj_release(self);
	return clj_throw_msg("Vector arg to map conj must be a pair");
}

static clj_value record_invoke(clj_value self, const clj_value *args, size_t n) {
	if (n != 1 && n != 2) return clj_arity_error(self, n);
	return record_lookup(self, args[0], n == 2 ? args[1] : CLJ_NIL);
}

static clj_value record_meta(clj_value self) { return clj_retain(*meta_slot(self)); }

static clj_value record_with_meta(clj_value self, clj_value m) {
	if (clj_is_nil(m) && clj_is_nil(*meta_slot(self))) return self;
	clj_record *r = record_own(self);
	clj_value  *slot = &r->slots[((const clj_user_type *)r->h.type)->nfields + 1];
	clj_value   old = *slot;
	store(&r->h, slot, clj_retain(m));
	clj_release(old);
	return clj_from_ptr(r);
}

// ---- creation

clj_value clj_record_new(clj_value type, const clj_value *vals, size_t n) {
	if (!clj_is_record_type(type)) return clj_throw_msg("new* expects a record type, got: %s", clj_type_name(type));
	const clj_user_type *ut = clj_to_ptr(type);
	if (n != ut->nfields) return clj_throw_msg("%s has %u fields, got %zu", ut->t.name, ut->nfields, n);
	clj_record *r = record_alloc(ut);
	for (size_t i = 0; i < n; i++) r->slots[i] = clj_retain(vals[i]);
	return clj_from_ptr(r);
}

typedef struct {
	clj_record *rec;
	clj_value   ext;
} from_map_ctx;

static bool take_entry(clj_value key, clj_value val, void *ctx) {
	from_map_ctx *c = ctx;
	int32_t       i = clj_record_basis_index(c->rec->h.type, key);
	if (i >= 0) c->rec->slots[i] = clj_retain(val);
	else c->ext = clj_map_assoc(c->ext, key, val);
	return true;
}

clj_value clj_record_from_map(clj_value type, clj_value m) {
	if (!clj_is_record_type(type)) return clj_throw_msg("map->Name expects a record type, got: %s", clj_type_name(type));
	if (!clj_has_core(m, CLJ_CORE_MAP)) return clj_throw_msg("map->Name expects a map, got: %s", clj_type_name(m));
	const clj_user_type *ut = clj_to_ptr(type);
	from_map_ctx         c = {record_alloc(ut), clj_map_empty()};
	if (clj_is_map(m)) {
		clj_map_each(m, take_entry, &c);
	} else {
		clj_value s = clj_seq(m);
		if (s == CLJ_THROWN) {
			clj_release(c.ext);
			clj_release(clj_from_ptr(c.rec));
			return CLJ_THROWN;
		}
		clj_seq_iter it = clj_seq_iter_start(s);
		clj_value    entry;
		while (clj_seq_iter_next(&it, &entry)) take_entry(clj_vector_nth(entry, 0), clj_vector_nth(entry, 1), &c);
		clj_seq_iter_close(&it);
		clj_release(s);
		if (it.thrown) {
			clj_release(c.ext);
			clj_release(clj_from_ptr(c.rec));
			return CLJ_THROWN;
		}
	}
	c.rec->slots[ut->nfields] = normalized(c.ext);
	return clj_from_ptr(c.rec);
}

static void record_slots(clj_user_type *ut) {
	clj_type *t = &ut->t;
	t->core_bits = RECORD_BITS;
	t->each_child = record_each_child;
	t->hash = record_hash;
	t->equals = record_equals;
	t->seq = record_seq;
	t->count = record_count_slot;
	t->lookup = record_lookup;
	t->conj = record_conj;
	t->assoc = record_assoc;
	t->dissoc = record_dissoc;
	t->reduce = record_reduce;
	t->invoke = record_invoke;
	t->meta = record_meta;
	t->with_meta = record_with_meta;
}

clj_value clj_record_type_new(clj_value name, clj_value fields, const clj_value *impls, size_t nimpls) {
	for (size_t i = 0; i < nimpls; i += 2) {
		if (!clj_is_protocol(impls[i]) || !clj_protocol_of(impls[i])->core_bits) continue;
		return clj_throw_msg("%s cannot be implemented by defrecord: the map interfaces are the record's own",
		                     clj_string_bytes(clj_symbol_name(clj_protocol_of(impls[i])->name)));
	}
	if (!clj_is_vector(fields)) return clj_throw_msg("defrecord fields must be a vector, got: %s", clj_type_name(fields));
	uint32_t         n = clj_vector_count(fields);
	clj_record_type *rt = clj_alloc(&clj_type_type, sizeof *rt + (size_t)n * sizeof(clj_value));
	for (uint32_t i = 0; i < n; i++) {
		clj_value f = clj_vector_nth(fields, i);
		if (!clj_is_symbol(f)) break; // clj_user_type_init reports it
		rt->basis[i] = clj_keyword_intern(CLJ_NIL, clj_symbol_name(f));
	}
	clj_value type = clj_user_type_init(&rt->ut, name, fields, impls, nimpls);
	if (type == CLJ_THROWN) return CLJ_THROWN;
	record_slots(&rt->ut);
	return type;
}

// ---- builtins

static clj_value b_record_type(const clj_value *args, size_t n) { return clj_record_type_new(args[0], args[1], args + 2, n - 2); }

static clj_value b_record_map(const clj_value *args, size_t n) {
	(void)n;
	return clj_record_from_map(args[0], args[1]);
}

static clj_value b_record_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_record(args[0]));
}

void clj_record_install(void) {
	static const struct {
		const char   *name;
		clj_native_fn fn;
		uint32_t      min, max;
	} fns[] = {
		{"record*", b_record_type, 2, CLJ_ARITY_ANY},
		{"record-map*", b_record_map, 2, 2},
		{"record?", b_record_p, 1, 1},
	};
	clj_value core_name = clj_symbol_name(clj_ns_name(clj_ns_core()));
	for (size_t i = 0; i < sizeof fns / sizeof *fns; i++) {
		clj_value name = clj_string_from_cstr(fns[i].name);
		clj_value qualified = clj_symbol_new(core_name, name);
		clj_value f = clj_fn_native(qualified, fns[i].fn, fns[i].min, fns[i].max);
		clj_value sym = clj_symbol_from_cstr(fns[i].name);
		clj_var_bind_root(clj_ns_intern(clj_ns_core(), sym), f);
		clj_release(sym);
		clj_release(f);
		clj_release(qualified);
		clj_release(name);
	}
}
