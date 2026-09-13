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
// from then on RC is atomic. Invariant: every child of a shared object is shared.
// Only the owning thread sets it, before publishing, so a plain write suffices.
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
};

extern const clj_type clj_type_type;

static inline clj_header *clj_header_of(clj_value v) { return (clj_header *)clj_to_ptr(v); }

// Zero-filled, rc = 1. Zero memory reads as nil, so value slots need no init.
void *clj_alloc(const clj_type *type, size_t size);

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

#endif
