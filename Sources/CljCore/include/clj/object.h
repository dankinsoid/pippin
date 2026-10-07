// @ai-generated(guided)
#ifndef CLJ_OBJECT_H
#define CLJ_OBJECT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "value.h"

// Ownership convention: arguments are borrowed (+0), return values are owned (+1).
// A callee that stores an argument retains it itself.
// Exception: an operation that may reuse its argument in place (assoc, conj, ...) consumes it (+1 in);
// with a borrowed unique argument the mutation would silently rewrite the caller's old value.

typedef struct clj_type clj_type;

// Every heap object starts with this. Kept to 16 bytes so a cons cell is 32.
// rc is _Atomic only so the non-shared path can use relaxed load/store, which
// compiles to plain instructions; the shared path uses real RMW.
typedef struct {
	_Atomic uint32_t rc;
	uint32_t         flags;
	const clj_type  *type;
} clj_header;

// An edge of a heap object: a clj_value field or slot-array element its each_child visits. A bare assignment to it
// does not compile, so every write goes through the primitives below and a publication cannot be forgotten (design
// §4 «Запись в слот»; make slot-audit keeps other code off `.v`). Reads take `.v`.
typedef struct {
	clj_value v;
} clj_slot;

// An edge read and replaced with atomics: an atom's value, a var's root and meta, a cancel cause, a trace.
typedef struct {
	_Atomic clj_value v;
} clj_atomic_slot;

// A value a heap object keeps for its own execution: each_child does not visit it and no other execution reads
// it, so it is no edge and never published (a coroutine's pending exception, its retired roots).
typedef clj_value clj_private_value;

_Static_assert(sizeof(clj_slot) == sizeof(clj_value), "a slot is the bare word");

// TSan reports nothing between two atomics, relaxed or not, so under it the unshared count is plain (docs/notes/gates.md, "TSan").
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define CLJ_RC_UNSHARED_LOAD(h) (*(const uint32_t *)(const void *)&(h)->rc)
#define CLJ_RC_UNSHARED_STORE(h, v) (*(uint32_t *)(void *)&(h)->rc = (v))
#endif
#endif
#ifndef CLJ_RC_UNSHARED_LOAD
#define CLJ_RC_UNSHARED_LOAD(h) atomic_load_explicit(&(h)->rc, memory_order_relaxed)
#define CLJ_RC_UNSHARED_STORE(h, v) atomic_store_explicit(&(h)->rc, (v), memory_order_relaxed)
#endif

// Set on the whole reachable graph once it is published: stored into a cell another thread can read
// (var, atom, channel, lazy-seq cell), captured by a spawn, or exported to the host; from then on RC is
// atomic. A handoff (park/resume, a channel move) is not a publication: one side touches the object at
// a time. An unshared object is reachable from one stack only, so the owner sets the flag with a plain
// write before publishing. Invariant: every child of a shared object is shared, so clj_share stops at
// a shared node. In-place reuse of a shared object (rc == 1) must clj_share any new child before storing it.
#define CLJ_FLAG_SHARED   ((uint32_t)1 << 0)
// Static objects (builtin type descriptors): retain/release are no-ops.
#define CLJ_FLAG_IMMORTAL ((uint32_t)1 << 1)
// Lives in the system allocator, not a pool slab: too big for a size class, or CLJ_SYSTEM_ALLOC=1.
#define CLJ_FLAG_LARGE    ((uint32_t)1 << 2)
// The object carries one extra trailing clj_value word holding its metadata (cons, empty list, the seq
// views): only a with-meta'd or reader-produced value pays for the slot, a plain cons stays 32 bytes.
#define CLJ_FLAG_META     ((uint32_t)1 << 3)
// A map object laid out as a shape map (map.h): the shape and inline values instead of a trie.
#define CLJ_FLAG_SHAPE    ((uint32_t)1 << 4)
// The cycle collector's bits (design §7, «Сборщик циклов: как он устроен»; cc.c). MUTABLE: a reference type whose
// slots are replaced after publication; a store into a shared one tells a running collection (clj_cc_note_store).
#define CLJ_FLAG_MUTABLE     ((uint32_t)1 << 5)
// May lie on a reference cycle: a cycle-capable type, or a child with the bit at the slot's write. Trial deletion
// descends only through it, and a release of a shared object with it to a nonzero count makes a candidate.
#define CLJ_FLAG_REACH       ((uint32_t)1 << 6)
// The same through unshared objects only: the inline release takes the slow path for it, the local candidate.
#define CLJ_FLAG_REACH_LOCAL ((uint32_t)1 << 7)
// Bits above it: the owning execution's tag in debug builds (clj_debug_owner_check), 0 for none; release leaves them 0.
#define CLJ_OWNER_SHIFT 16

// The top bits of rc (cc.c). Each is set only while the collector or a candidate buffer holds a reference of its
// own, so the count is at least 2 under either and `rc == 1` and "reached zero" need no mask.
// WATCH: the node is in a running collection's snapshot; any RC change or slot store on it clears the bit.
#define CLJ_RC_WATCH    ((uint32_t)1 << 31)
// BUFFERED: one of the references is a candidate buffer's.
#define CLJ_RC_BUFFERED ((uint32_t)1 << 30)
#define CLJ_RC_COUNT_MASK    (CLJ_RC_BUFFERED - 1)

typedef void (*clj_visitor)(clj_value child, void *ctx);

// Core interfaces a type implements, mirroring Clojure's: `(map? x)` is one AND on the bitset.
// A slot may exist without the bit (a string has lookup and count, as RT.get/RT.count special-case it),
// so dispatch reads the slot and predicates read the bit.
// Plain literals: static initializers need constant expressions and Swift imports literal macros.
#define CLJ_CORE_SEQABLE     0x001 // seq slot
#define CLJ_CORE_SEQ         0x002 // ISeq: a value that is its own seq (seq? x)
#define CLJ_CORE_SEQUENTIAL  0x004 // ordered; sequential equality applies
#define CLJ_CORE_COLL        0x008 // IPersistentCollection (coll? x)
#define CLJ_CORE_COUNTED     0x010 // O(1) count
#define CLJ_CORE_LOOKUP      0x020 // ILookup
#define CLJ_CORE_ASSOCIATIVE 0x040 // Associative
#define CLJ_CORE_INDEXED     0x080 // O(1) nth
#define CLJ_CORE_FN          0x100 // IFn (ifn? x)
#define CLJ_CORE_LIST        0x200 // IPersistentList (list? x)
#define CLJ_CORE_VECTOR      0x400 // IPersistentVector
#define CLJ_CORE_MAP         0x800 // IPersistentMap
#define CLJ_CORE_ERROR       0x1000 // IExceptionInfo: ex_message/ex_data/ex_cause slots; what (catch ExceptionInfo e) takes
// Only a deftype/reify overriding hash or equals carries these; builtins answer (satisfies? IHashEq x) false.
#define CLJ_CORE_HASHEQ      0x2000 // IHashEq: hasheq method behind the hash slot
#define CLJ_CORE_EQUIV       0x4000 // IEquiv: equiv method behind the equals slot
#define CLJ_CORE_META        0x8000 // IMeta: meta slot
#define CLJ_CORE_OBJ         0x10000 // IObj: with_meta slot (implies IMeta)
#define CLJ_CORE_REDUCE      0x20000 // IReduceInit: reduce slot (cons, (), string and lazy-seq have the slot without the bit)
#define CLJ_CORE_SET         0x40000 // IPersistentSet (set? x); a set has lookup without ILookup, as RT.get special-cases it
#define CLJ_CORE_RECORD      0x80000 // IRecord (record? x): a named shape, map bits plus a basis (record.h)
#define CLJ_CORE_EDITABLE    0x100000 // IEditableCollection: only the three types the JVM answers true for

// Type descriptors are heap objects themselves: deftype creates them at runtime
// and builtin types must be indistinguishable from user ones.
// Core-interface slots are write-once: a builtin's are static, a deftype's are filled at creation from
// the interfaces its form names (the extend-type boundary in the design); protocols hang off user_protos.
// Slot convention as for every function: arguments borrowed, results owned or CLJ_THROWN;
// conj is the exception and consumes self, so a unique collection can be updated in place.
struct clj_type {
	clj_header  h;
	const char *name;
	uint64_t    core_bits;
	// Children replaced after publication (an atom's value): checked at the store (CLJ_SLOT_CHECK), not by a walk.
	bool        mutable_children;
	// NULL for leaf types. Drives both drop and share.
	void (*each_child)(void *self, clj_visitor visit, void *ctx);
	// Resources beyond child values (mutex, external buffer). NULL if none.
	void (*finalize)(void *self);
	// Runs as the last reference drops, header still intact, before the children go: for a registry that holds
	// the object without a reference and reads its count under its own lock (specialize.c). NULL if none.
	void (*unlink)(void *self);
	// NULL when values of the type cannot be map keys; clj_hash/clj_equals abort on them.
	uint32_t (*hash)(void *self);
	bool     (*equals)(void *self, clj_value other);
	// Seqable: nil when empty, else a value of a type with CLJ_CORE_SEQ. The one mandatory seq slot.
	clj_value (*seq)(clj_value self);
	// Fast paths, only where seq is an O(1) view; NULL goes through seq. Mandatory on CLJ_CORE_SEQ types.
	clj_value (*first)(clj_value self);
	clj_value (*next)(clj_value self);
	// ISeq.more: NULL means next, or () when that is nil. A cons overrides it to hand out an unrealized tail.
	clj_value (*rest)(clj_value self);
	// NULL: count walks the seq. A fixnum, or CLJ_THROWN like every other slot.
	clj_value (*count)(clj_value self);
	clj_value (*lookup)(clj_value self, clj_value key, clj_value not_found);
	clj_value (*conj)(clj_value self, clj_value x);
	// Associative/IPersistentMap; consume self like conj. A set's dissoc is its disj: both remove a key.
	clj_value (*assoc)(clj_value self, clj_value key, clj_value val);
	clj_value (*dissoc)(clj_value self, clj_value key);
	// IReduceInit: (f acc item) over the elements, stopping at a `reduced` result, which comes back unwrapped;
	// init CLJ_UNBOUND seeds with the first element and answers (f) when empty (coll.h, clj_reducer). NULL walks the seq.
	clj_value (*reduce)(clj_value self, clj_value f, clj_value init);
	// Arity is checked by the object (a fn carries its arity table), not the type.
	clj_value (*invoke)(clj_value self, const clj_value *args, size_t n);
	// IMeta: a map or nil. IObj: consumes self like conj, m is a map or nil. Equality, hash and printing ignore meta.
	clj_value (*meta)(clj_value self);
	clj_value (*with_meta)(clj_value self, clj_value m);
	// IExceptionInfo: a string or nil, a map or nil, a thrown value or nil. Mandatory on CLJ_CORE_ERROR types.
	clj_value (*ex_message)(clj_value self);
	clj_value (*ex_data)(clj_value self);
	clj_value (*ex_cause)(clj_value self);
	// defprotocol tables, NULL until protocols exist (NOTES.md).
	void *user_protos;
	// Debug builds: whether the running execution holds the lock a mutable_children object's slots are replaced
	// under (clj_slot_store asks); NULL where no cheap answer exists.
	bool (*debug_lock_held)(const void *self);
	// The concurrent collector reads a shared object of this type under the lock its writers hold: runs inside(self,
	// ctx) under it, false without running when the lock is busy. A store into such an object needs no barrier
	// (clj_cc_note_store). NULL: each_child is read without a lock (cc.c, the lock-free protocol).
	bool (*cc_locked)(void *self, void (*inside)(void *self, void *ctx), void *ctx);
};

extern const clj_type clj_type_type;

static inline clj_header *clj_header_of(clj_value v) { return (clj_header *)clj_to_ptr(v); }
static inline const clj_type *clj_type_of(clj_value v) { return clj_header_of(v)->type; }
// 0 for immediates: nil, numbers, chars and booleans implement no core interface at the type level.
static inline uint64_t clj_core_bits(clj_value v) { return clj_is_ptr(v) ? clj_type_of(v)->core_bits : 0; }
static inline bool     clj_has_core(clj_value v, uint64_t bits) { return (clj_core_bits(v) & bits) == bits; }

// The trailing meta word of an object whose header has CLJ_FLAG_META; obj_size is the size without it.
static inline clj_slot *clj_meta_slot_at(void *obj, size_t obj_size) { return (clj_slot *)((char *)obj + obj_size); }

// Borrowed, nil when the object carries no metadata.
static inline clj_value clj_meta_trailing(void *obj, size_t obj_size) {
	clj_header *h = obj;
	return h->flags & CLJ_FLAG_META ? clj_meta_slot_at(obj, obj_size)->v : CLJ_NIL;
}

// Zero-filled, rc = 1. Zero memory reads as nil, so value slots need no init.
// Size-class pool per thread; CLJ_SYSTEM_ALLOC=1 in the environment routes to calloc/realloc/free.
void *clj_alloc(const clj_type *type, size_t size);
// obj must be unique (rc == 1): the object may move, so no one else can hold its address.
// Children are untouched; bytes beyond the old size are uninitialized. Same size class keeps the address.
void *clj_realloc(void *obj, size_t size);

// False in a -DCLJ_NO_REUSE build, where clj_is_unique always answers "not unique" (make test-noreuse): the
// §7 invariant that only clj_is_unique reads the counter, so the suites pass with reuse off. A test that
// asserts an address survived an in-place operation gates on this.
bool      clj_reuse_enabled(void);

void      clj_retain_slow(clj_header *h);
void      clj_release_slow(clj_header *h);
bool      clj_is_unique(clj_value v);
bool      clj_is_shared(clj_value v);
// A store into a shared MUTABLE owner, after the slot was written and before the old value is released (cc.c).
void      clj_cc_note_store(clj_header *owner);
// Marks v and everything reachable from it shared. Call before handing v to another thread.
void      clj_share(clj_value v);
void      clj_fatal(const char *msg) __attribute__((noreturn));

// Declared regardless of CLJ_DEBUG: the Swift importer reads this header without the C target's defines.
// Returns -1 when the build does not track it.
int64_t clj_debug_live_objects(void);
// Takes n objects out of the count: a compiled unit's constant pools live for the process by design (compiled eval).
void clj_debug_live_objects_exclude(int64_t n);
// Live objects of one type; -1 when untracked. A deftype descriptor's own object counts under clj_type_type.
int64_t clj_debug_live_objects_of(const clj_type *type);
// "type: count" per type with live objects, to stderr: what a leaking test left behind.
void clj_debug_live_report(void);
// True when v and everything reachable from it is shared or immortal. No other thread may write a slot it reaches.
bool clj_debug_all_shared(clj_value v);
// The calling thread checks one clj_share cutoff in n (NOTES "RC"); 0 restores the default.
void clj_debug_share_check_every(uint32_t n);
bool clj_debug_pool_enabled(void);
// Pool cell size an allocation of `size` bytes gets; 0 when it goes to the system allocator.
size_t clj_debug_cell_size(size_t size);
// Bytes of pool cells this thread's heap has handed out and not taken back (cells freed by other threads stay
// counted until drained); 0 under the system allocator.
size_t clj_debug_pool_used_bytes(void);
// Test hook: replaces clj_hash for every value while set. NULL restores the default.
void clj_debug_set_hash_override(uint32_t (*fn)(clj_value v));
extern uint32_t (*clj_debug_hash_override)(clj_value v);

// After clj_slot_store into a mutable_children owner: the slot check, and the owner's lock is held (debug_lock_held).
// Declared in every build for the tests; release builds check nothing.
void clj_debug_slot_store_check(const clj_header *owner, clj_value v);

// Retains and releases per path, debug builds only: the share measurement of design §4 (bench/RESULTS.md, "Atoms").
enum { CLJ_RC_PLAIN, CLJ_RC_SHARED, CLJ_RC_IMMORTAL };
void clj_debug_rc_ops(int64_t out[3]);

#if CLJ_DEBUG
#define CLJ_ASSERT(cond, msg) do { if (!(cond)) clj_fatal(msg); } while (0)
extern _Atomic uint64_t clj_debug_rc_counters[3];
#define CLJ_RC_COUNT(path) atomic_fetch_add_explicit(&clj_debug_rc_counters[path], 1, memory_order_relaxed)
// Out of line: the running execution is read through a call, never a TLS address cached across a park.
void clj_debug_owner_check(const clj_header *h);
#define CLJ_OWNER_CHECK(h) do { if ((h)->flags >> CLJ_OWNER_SHIFT) clj_debug_owner_check(h); } while (0)
// After a store of v into a mutable_children slot of owner: a shared owner holds only shared values.
void clj_debug_slot_check(const clj_header *owner, clj_value v);
#define CLJ_SLOT_CHECK(owner, v) clj_debug_slot_check((owner), (v))
#define CLJ_SLOT_STORE_CHECK(owner, v) \
	do { if ((owner)->type->mutable_children) clj_debug_slot_store_check((owner), (v)); } while (0)
#define CLJ_SLOT_INIT_CHECK(owner, v) \
	do { \
		if (((owner)->flags & CLJ_FLAG_SHARED) && clj_is_ptr(v) && \
		    !(clj_header_of(v)->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL))) \
			clj_debug_slot_check((owner), (v)); \
	} while (0)
#else
#define CLJ_ASSERT(cond, msg) ((void)0)
#define CLJ_RC_COUNT(path) ((void)0)
#define CLJ_OWNER_CHECK(h) ((void)0)
#define CLJ_SLOT_CHECK(owner, v) ((void)0)
#define CLJ_SLOT_STORE_CHECK(owner, v) ((void)0)
#define CLJ_SLOT_INIT_CHECK(owner, v) ((void)0)
#endif

// The reach bits of a new edge into owner. A MUTABLE owner keeps the bits it was born with: a reference type that
// can close a cycle has them from birth, and a lazy seq's realization must not give them (design §7). The owner is
// unshared, or shared and unique, so the plain write races with no reader.
static inline void clj_reach_from(clj_header *owner, clj_value v) {
	if (!clj_is_ptr(v) || (owner->flags & CLJ_FLAG_MUTABLE)) return;
	uint32_t f = clj_header_of(v)->flags;
	owner->flags |= (f & CLJ_FLAG_REACH) |
	                ((f & (CLJ_FLAG_REACH_LOCAL | CLJ_FLAG_SHARED)) == CLJ_FLAG_REACH_LOCAL ? CLJ_FLAG_REACH_LOCAL : 0);
}

// After a store into owner: a shared reference type tells a running collection its slots moved.
static inline void clj_slot_stored(clj_header *owner) {
	if ((owner->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_MUTABLE)) == (CLJ_FLAG_SHARED | CLJ_FLAG_MUTABLE)) clj_cc_note_store(owner);
}

// The store into a live object: a shared owner publishes v before v becomes reachable through it. A reference type
// (atom, channel, ...) stores under its own lock, which this does not take; debug builds check that it is held.
// The caller releases the old value after this returns, never before (the collector's lock-free protocol, cc.c).
static inline void clj_slot_store(clj_header *owner, clj_slot *slot, clj_value v) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	clj_reach_from(owner, v);
	slot->v = v;
	clj_slot_stored(owner);
	CLJ_SLOT_STORE_CHECK(owner, v);
}

// Fills a slot of an object no other execution can reach yet, without the flag test. An owner born shared (var,
// coroutine) takes only values that need no publication; debug builds check it.
static inline void clj_slot_init(clj_header *owner, clj_slot *slot, clj_value v) {
	CLJ_SLOT_INIT_CHECK(owner, v);
	clj_reach_from(owner, v);
	slot->v = v;
}

// A node copy has its source's children, so its reach bits once rather than per slot: the path copy is hot.
static inline void clj_reach_copy(clj_header *owner, const clj_header *src) {
	uint32_t f = src->flags;
	owner->flags |= f & ((f & CLJ_FLAG_SHARED) ? CLJ_FLAG_REACH : CLJ_FLAG_REACH | CLJ_FLAG_REACH_LOCAL);
}

// clj_slot_init of a slot copied from src, whose bits clj_reach_copy gives the owner.
static inline void clj_slot_init_copied(clj_header *owner, clj_slot *slot, clj_value v) {
	CLJ_SLOT_INIT_CHECK(owner, v);
	(void)owner;
	slot->v = v;
}

// nil publishes nothing, so clearing needs neither the owner nor the flag test.
static inline void clj_slot_clear(clj_slot *slot) { slot->v = CLJ_NIL; }

// A place outside the heap that every execution reads under its own lock: a C global, a registry.
static inline void clj_root_store(clj_value *root, clj_value v) {
	clj_share(v);
	*root = v;
}

static inline clj_value clj_slot_load(const clj_atomic_slot *slot, memory_order order) {
	return atomic_load_explicit(&slot->v, order);
}

static inline void clj_slot_store_atomic(clj_header *owner, clj_atomic_slot *slot, clj_value v, memory_order order) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	clj_reach_from(owner, v);
	atomic_store_explicit(&slot->v, v, order);
	clj_slot_stored(owner);
	CLJ_SLOT_STORE_CHECK(owner, v);
}

static inline clj_value clj_slot_exchange(clj_header *owner, clj_atomic_slot *slot, clj_value v, memory_order order) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	clj_reach_from(owner, v);
	clj_value old = atomic_exchange_explicit(&slot->v, v, order);
	clj_slot_stored(owner);
	CLJ_SLOT_STORE_CHECK(owner, v);
	return old;
}

static inline bool clj_slot_cas(clj_header *owner, clj_atomic_slot *slot, clj_value *expected, clj_value v,
                                memory_order success, memory_order failure) {
	if (owner->flags & CLJ_FLAG_SHARED) clj_share(v);
	clj_reach_from(owner, v);
	if (!atomic_compare_exchange_strong_explicit(&slot->v, expected, v, success, failure)) return false;
	clj_slot_stored(owner);
	CLJ_SLOT_STORE_CHECK(owner, v);
	return true;
}

// A read-only view of a slot array as values, for an API that takes clj_value * (a frame's captures).
static inline const clj_value *clj_slot_values(const clj_slot *slots) { return (const clj_value *)(const void *)slots; }

static inline clj_value clj_retain(clj_value v) {
	if (!clj_is_ptr(v)) return v;
	clj_header *h = clj_header_of(v);
	if (__builtin_expect(h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL), 0)) {
		CLJ_RC_COUNT(h->flags & CLJ_FLAG_IMMORTAL ? CLJ_RC_IMMORTAL : CLJ_RC_SHARED);
		clj_retain_slow(h);
		return v;
	}
	CLJ_RC_COUNT(CLJ_RC_PLAIN);
	CLJ_OWNER_CHECK(h);
	uint32_t rc = CLJ_RC_UNSHARED_LOAD(h);
	CLJ_ASSERT(rc > 0, "retain of a freed object");
	CLJ_RC_UNSHARED_STORE(h, rc + 1);
	return v;
}

// REACH_LOCAL in the mask: an unshared object that may lie on a cycle becomes a candidate on a nonzero release.
static inline void clj_release(clj_value v) {
	if (!clj_is_ptr(v)) return;
	clj_header *h = clj_header_of(v);
	if (__builtin_expect(h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL | CLJ_FLAG_REACH_LOCAL), 0)) {
		CLJ_RC_COUNT(h->flags & CLJ_FLAG_IMMORTAL ? CLJ_RC_IMMORTAL : h->flags & CLJ_FLAG_SHARED ? CLJ_RC_SHARED : CLJ_RC_PLAIN);
		clj_release_slow(h);
		return;
	}
	CLJ_RC_COUNT(CLJ_RC_PLAIN);
	CLJ_OWNER_CHECK(h);
	uint32_t rc = CLJ_RC_UNSHARED_LOAD(h);
	CLJ_ASSERT(rc > 0, "release of a freed object");
	if (rc == 1) {
		clj_release_slow(h);
		return;
	}
	CLJ_RC_UNSHARED_STORE(h, rc - 1);
}

// murmur3 finalizer: spreads entropy across all 32 bits.
static inline uint32_t clj_fmix32(uint32_t h) {
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

// murmur3 mixing of an aggregate hash with the element count (Clojure's mixCollHash).
uint32_t clj_mix_coll_hash(uint32_t hash, uint32_t count);

// Java's hashCombine, which Clojure uses for symbols.
static inline uint32_t clj_hash_combine(uint32_t seed, uint32_t hash) {
	return seed ^ (hash + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

// Cached-hash slot (string, symbol, keyword, map): 0 means not computed; a computed 0 is stored as 1
// so the slot never reads as empty. Relaxed suffices: racing writers store the same value.
// In-place mutation of the owner must reset the slot to 0.
static inline uint32_t clj_hash_cache_load(const _Atomic uint32_t *slot) {
	return atomic_load_explicit(slot, memory_order_relaxed);
}

static inline uint32_t clj_hash_cache_store(_Atomic uint32_t *slot, uint32_t hash) {
	if (hash == 0) hash = 1;
	atomic_store_explicit(slot, hash, memory_order_relaxed);
	return hash;
}

uint32_t clj_hash_slow(clj_value v);
bool     clj_equals_slow(clj_value a, clj_value b);

static inline uint32_t clj_hash(clj_value v) {
	if (__builtin_expect(clj_debug_hash_override != NULL, 0)) return clj_debug_hash_override(v);
	if (clj_is_fixnum(v)) {
		uint64_t n = (uint64_t)(int64_t)clj_fixnum_val(v);
		return clj_fmix32((uint32_t)(n ^ (n >> 32)));
	}
	if (clj_is_ptr(v)) return clj_hash_slow(v);
	if (v == CLJ_NIL) return 0;
	if (v == CLJ_TRUE) return 1231;
	if (v == CLJ_FALSE) return 1237;
	return clj_char_val(v);
}

// Clojure `=`: immediates compare by word, heap objects by their type's equals.
static inline bool clj_equals(clj_value a, clj_value b) {
	if (a == b) return true;
	if (!clj_is_ptr(a) && !clj_is_ptr(b)) return false;
	return clj_equals_slow(a, b);
}

#endif
