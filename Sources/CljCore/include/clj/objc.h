// @ai-generated(solo)
#ifndef CLJ_OBJC_H
#define CLJ_OBJC_H

#include "object.h"

// Level 1 of the bridge (design.md §5), in C rather than the Swift target: a Swift dispatcher would pay
// clj_host_invoke per call (~64 ns, bench/RESULTS.md "Host-defined fns"). Apple-only: docs/portability.md.

// A Class is immortal, so is_class spares the finalizer a runtime query; obj is never NULL.
typedef struct {
	clj_header h;
	void      *obj;
	bool       is_class;
} clj_objc_object;

extern const clj_type clj_objc_object_type;

static inline bool clj_is_objc_object(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_objc_object_type; }
// Borrowed: valid while v is.
static inline void *clj_objc_id(clj_value v) { return ((clj_objc_object *)clj_to_ptr(v))->obj; }

// +0 in: the wrapper retains. A nil obj gives CLJ_NIL.
clj_value clj_objc_wrap(void *obj);
// +1 in: the wrapper takes the caller's reference (the alloc/new/copy/mutableCopy/init families).
clj_value clj_objc_wrap_owned(void *obj);
// Objective-C spelling ("NSString", not "ns-string"); nil when no such class.
clj_value clj_objc_class(const char *name);

// selector is the kebab spelling with its colons ("add-target:action:for-control-events:"), matched against
// the receiver's real selectors and cached per (class, spelling); raw takes the Objective-C text instead.
// target is a wrapper or nil, args borrowed. Owned result, or CLJ_THROWN.
clj_value clj_objc_send(clj_value target, clj_value selector, const clj_value *args, uint32_t nargs, bool raw);

// Selector text to the kebab spelling a call site writes: a capital run is one word, digits join the word
// before them, so UTF8String is utf8-string. snprintf's contract: returns the length it wanted.
size_t clj_objc_kebab(const char *selector, char *out, size_t cap);

// Registers objc-class, objc-send and objc-object?; clj_builtins_install calls it.
void clj_objc_builtins_install(void);

#endif
