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
// The object carries one extra trailing clj_value word holding its metadata (cons, empty list): only
// with-meta'd and reader-produced lists pay for the slot, a plain cons stays 32 bytes.
#define CLJ_FLAG_META     ((uint32_t)1 << 3)
// A map object laid out as a shape map (map.h): the shape and inline values instead of a trie.
#define CLJ_FLAG_SHAPE    ((uint32_t)1 << 4)

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
	// NULL for leaf types. Drives both drop and share.
	void (*each_child)(void *self, clj_visitor visit, void *ctx);
	// Resources beyond child values (mutex, external buffer). NULL if none.
	void (*finalize)(void *self);
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
};

extern const clj_type clj_type_type;

static inline clj_header *clj_header_of(clj_value v) { return (clj_header *)clj_to_ptr(v); }
static inline const clj_type *clj_type_of(clj_value v) { return clj_header_of(v)->type; }
// 0 for immediates: nil, numbers, chars and booleans implement no core interface at the type level.
static inline uint64_t clj_core_bits(clj_value v) { return clj_is_ptr(v) ? clj_type_of(v)->core_bits : 0; }
static inline bool     clj_has_core(clj_value v, uint64_t bits) { return (clj_core_bits(v) & bits) == bits; }

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
// True when v and everything reachable from it is shared or immortal.
bool clj_debug_all_shared(clj_value v);
bool clj_debug_pool_enabled(void);
// Pool cell size an allocation of `size` bytes gets; 0 when it goes to the system allocator.
size_t clj_debug_cell_size(size_t size);
// Bytes of pool cells this thread's heap has handed out and not taken back (cells freed by other threads stay
// counted until drained); 0 under the system allocator.
size_t clj_debug_pool_used_bytes(void);
// Test hook: replaces clj_hash for every value while set. NULL restores the default.
void clj_debug_set_hash_override(uint32_t (*fn)(clj_value v));
extern uint32_t (*clj_debug_hash_override)(clj_value v);

// Retains and releases per path, debug builds only: the share measurement of design §4 (bench/RESULTS.md, "Atoms").
enum { CLJ_RC_PLAIN, CLJ_RC_SHARED, CLJ_RC_IMMORTAL };
void clj_debug_rc_ops(int64_t out[3]);

#if CLJ_DEBUG
#define CLJ_ASSERT(cond, msg) do { if (!(cond)) clj_fatal(msg); } while (0)
extern _Atomic uint64_t clj_debug_rc_counters[3];
#define CLJ_RC_COUNT(path) atomic_fetch_add_explicit(&clj_debug_rc_counters[path], 1, memory_order_relaxed)
#else
#define CLJ_ASSERT(cond, msg) ((void)0)
#define CLJ_RC_COUNT(path) ((void)0)
#endif

static inline clj_value clj_retain(clj_value v) {
	if (!clj_is_ptr(v)) return v;
	clj_header *h = clj_header_of(v);
	if (__builtin_expect(h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL), 0)) {
		CLJ_RC_COUNT(h->flags & CLJ_FLAG_IMMORTAL ? CLJ_RC_IMMORTAL : CLJ_RC_SHARED);
		clj_retain_slow(h);
		return v;
	}
	CLJ_RC_COUNT(CLJ_RC_PLAIN);
	uint32_t rc = atomic_load_explicit(&h->rc, memory_order_relaxed);
	CLJ_ASSERT(rc > 0, "retain of a freed object");
	atomic_store_explicit(&h->rc, rc + 1, memory_order_relaxed);
	return v;
}

static inline void clj_release(clj_value v) {
	if (!clj_is_ptr(v)) return;
	clj_header *h = clj_header_of(v);
	if (__builtin_expect(h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL), 0)) {
		CLJ_RC_COUNT(h->flags & CLJ_FLAG_IMMORTAL ? CLJ_RC_IMMORTAL : CLJ_RC_SHARED);
		clj_release_slow(h);
		return;
	}
	CLJ_RC_COUNT(CLJ_RC_PLAIN);
	uint32_t rc = atomic_load_explicit(&h->rc, memory_order_relaxed);
	CLJ_ASSERT(rc > 0, "release of a freed object");
	if (rc == 1) {
		clj_release_slow(h);
		return;
	}
	atomic_store_explicit(&h->rc, rc - 1, memory_order_relaxed);
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
