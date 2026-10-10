// @ai-generated(solo)
// The teardown of a dead object, specialized by type (clj_type.drop): Perceus' drop, rc.c's worklist underneath.
#ifndef CLJ_RC_INTERNAL_H
#define CLJ_RC_INTERNAL_H

#include <stddef.h>

#include "clj/object.h"

// A buffered dead object waits aside, not as a link: a candidate entry still reads its header (cc.c, the zombie).
struct clj_drop {
	clj_header  *stack;
	clj_header **aside;
	size_t       naside, caside;
	bool         deep;
};

// A shared or REACH_LOCAL child: release_reaches_zero, the deep walk and the collector's deferred frees.
void clj_drop_slow(clj_drop *d, clj_header *h);
// An unshared child whose count just reached zero: freed now when it has nothing to tear down, else queued.
void clj_drop_dead(clj_drop *d, clj_header *h);

// The last reference of an object whose cell a reuse token takes (reuse.c): nothing runs at its death but the drop.
bool clj_rc_token_ok(const clj_header *h);
// Releases the children of such an object, the dead below torn down as usual; its own cell stays.
void clj_rc_drop_children(clj_header *h);

// No deep test: clj_cc_deep_release leaves every unshared object to the plain release.
static inline void clj_drop_value(clj_drop *d, clj_value v) {
	if (!clj_is_ptr(v)) return;
	clj_header *h = clj_header_of(v);
	uint32_t    f = h->flags;
	if (f & CLJ_FLAG_IMMORTAL) return;
	if (__builtin_expect(f & (CLJ_FLAG_SHARED | CLJ_FLAG_REACH_LOCAL), 0)) {
		clj_drop_slow(d, h);
		return;
	}
	CLJ_OWNER_CHECK(h);
	uint32_t rc = CLJ_RC_UNSHARED_LOAD(h);
	CLJ_ASSERT(rc > 0, "release of a freed object");
	CLJ_RC_UNSHARED_STORE(h, rc - 1);
	if (rc == 1) clj_drop_dead(d, h);
}

static inline void clj_drop_visit(clj_value v, void *ctx) { clj_drop_value(ctx, v); }

// children must be CLJ_CHILDREN_INLINE, so the visitor is a constant and clj_drop_value inlines at each child.
#define CLJ_CHILDREN_SLOTS(prefix, children)                                                                          \
	static void prefix##_each_child(void *self, clj_visitor visit, void *ctx) { children(self, visit, ctx); }      \
	static void prefix##_drop(void *self, clj_drop *d) { children(self, clj_drop_visit, d); }

#define CLJ_CHILDREN_INLINE static inline __attribute__((always_inline))

#endif
