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

// Set on the whole reachable graph once it crosses a thread or is exported to the host;
// from then on RC is atomic. Only the owning thread sets it, before publishing, so a plain write suffices.
// Invariant: every child of a shared object is shared. In-place reuse of a shared object
// (rc == 1) must clj_share any new child before storing it.
#define CLJ_FLAG_SHARED   ((uint32_t)1 << 0)
// Static objects (builtin type descriptors): retain/release are no-ops.
#define CLJ_FLAG_IMMORTAL ((uint32_t)1 << 1)

typedef void (*clj_visitor)(clj_value child, void *ctx);

// Type descriptors are heap objects themselves: deftype creates them at runtime
// and builtin types must be indistinguishable from user ones.
struct clj_type {
	clj_header  h;
	const char *name;
	// NULL for leaf types. Drives both drop and share.
	void (*each_child)(void *self, clj_visitor visit, void *ctx);
	// Resources beyond child values (mutex, external buffer). NULL if none.
	void (*finalize)(void *self);
	// NULL when values of the type cannot be map keys; clj_hash/clj_equals abort on them.
	uint32_t (*hash)(void *self);
	bool     (*equals)(void *self, clj_value other);
};

extern const clj_type clj_type_type;

static inline clj_header *clj_header_of(clj_value v) { return (clj_header *)clj_to_ptr(v); }

// Zero-filled, rc = 1. Zero memory reads as nil, so value slots need no init.
void *clj_alloc(const clj_type *type, size_t size);
// obj must be unique (rc == 1): the object may move, so no one else can hold its address.
// Children are untouched; bytes beyond the old size are uninitialized.
void *clj_realloc(void *obj, size_t size);

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
// True when v and everything reachable from it is shared or immortal.
bool clj_debug_all_shared(clj_value v);
// Test hook: replaces clj_hash for every value while set. NULL restores the default.
void clj_debug_set_hash_override(uint32_t (*fn)(clj_value v));
extern uint32_t (*clj_debug_hash_override)(clj_value v);

#if CLJ_DEBUG
#define CLJ_ASSERT(cond, msg) do { if (!(cond)) clj_fatal(msg); } while (0)
#else
#define CLJ_ASSERT(cond, msg) ((void)0)
#endif

static inline clj_value clj_retain(clj_value v) {
	if (!clj_is_ptr(v)) return v;
	clj_header *h = clj_header_of(v);
	if (__builtin_expect(h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL), 0)) {
		clj_retain_slow(h);
		return v;
	}
	uint32_t rc = atomic_load_explicit(&h->rc, memory_order_relaxed);
	CLJ_ASSERT(rc > 0, "retain of a freed object");
	atomic_store_explicit(&h->rc, rc + 1, memory_order_relaxed);
	return v;
}

static inline void clj_release(clj_value v) {
	if (!clj_is_ptr(v)) return;
	clj_header *h = clj_header_of(v);
	if (__builtin_expect(h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL), 0)) {
		clj_release_slow(h);
		return;
	}
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
