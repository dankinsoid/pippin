// @ai-generated(guided)
#ifndef CLJ_VAR_H
#define CLJ_VAR_H

#include "object.h"

// Immortal: a var lives for the process, like the name that reaches it. Made through clj_ns_intern.
typedef struct {
	clj_header        h;
	clj_value         ns;   // symbol
	clj_value         name; // symbol
	_Atomic clj_value root; // CLJ_UNBOUND until the first def
	_Atomic clj_value meta; // map or nil; published like root
	bool              macro; // set by defmacro, cleared by def; the analyzer expands calls through such vars
	bool              dynamic; // :dynamic true in the def's meta: deref looks at the thread's bindings first
	_Atomic uint32_t  thread_bound; // live thread bindings across all threads; 0 lets deref skip the frame lookup
	_Atomic uint32_t  epoch; // 0 until the first root bind, then bumped by every one: the guard a cached summary holds (facts.h)
} clj_var;

extern const clj_type clj_var_type;

clj_value clj_var_new(clj_value ns, clj_value name);

static inline bool     clj_is_var(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_var_type; }
static inline clj_var *clj_var_of(clj_value v) { return (clj_var *)clj_to_ptr(v); }
// Borrowed: the var never dies.
static inline clj_value clj_var_ns(clj_value var) { return clj_var_of(var)->ns; }
static inline clj_value clj_var_name(clj_value var) { return clj_var_of(var)->name; }

// Counts root binds of this var alone, 0 while it has never been bound: what invalidates a summary of its root.
static inline uint32_t clj_var_epoch(clj_value var) { return atomic_load_explicit(&clj_var_of(var)->epoch, memory_order_acquire); }
// Borrowed root, CLJ_UNBOUND when unbound. Not safe against a concurrent def (NOTES.md).
clj_value clj_var_root(clj_value var);
// The same without ordering: for a guard that only compares the root against a known immortal object.
static inline clj_value clj_var_root_relaxed(clj_value var) { return atomic_load_explicit(&clj_var_of(var)->root, memory_order_relaxed); }
static inline bool clj_var_is_bound(clj_value var) { return clj_var_root(var) != CLJ_UNBOUND; }
// Shares and retains val (a var is reachable from every thread), releases the previous root (a fn root once the
// thread is idle: clj_eval_retire_root).
void clj_var_bind_root(clj_value var, clj_value val);
// Owned: the thread's binding of a dynamic var, else the root; throws when unbound.
clj_value clj_var_deref(clj_value var);

// ---- thread bindings (binding, set!): a per-thread stack of frames, each a map var → box (a volatile).
// Pushes bindings (map var → value); every var must be dynamic, or "Can't dynamically bind non-dynamic var".
clj_value clj_var_push_bindings(clj_value bindings);
// Pops the frame this thread pushed last; throws "Pop without matching push" when none.
clj_value clj_var_pop_bindings(void);
// map var → value of every binding visible on this thread, owned.
clj_value clj_var_get_thread_bindings(void);
// Borrowed box (a volatile) of var on this thread, nil when it has no binding here.
clj_value clj_var_thread_binding(clj_value var);
// set!: stores into the thread's binding; throws "Can't change/establish root binding of: x with set" without one.
clj_value clj_var_set(clj_value var, clj_value val);
static inline bool clj_var_is_thread_bound(clj_value var) { return !clj_is_nil(clj_var_thread_binding(var)); }

static inline bool clj_var_is_macro(clj_value var) { return clj_var_of(var)->macro; }
static inline void clj_var_set_macro(clj_value var, bool macro) { clj_var_of(var)->macro = macro; }
static inline bool clj_var_is_dynamic(clj_value var) { return clj_var_of(var)->dynamic; }
static inline void clj_var_set_dynamic(clj_value var, bool dynamic) { clj_var_of(var)->dynamic = dynamic; }

// Borrowed meta, nil when none. Same caveat as clj_var_root against a concurrent writer.
clj_value clj_var_meta(clj_value var);
// Shares and retains m (a map or nil), releases the previous meta.
void clj_var_set_meta(clj_value var, clj_value m);
// Replaces the meta only while it is still `expected`; m is shared and retained on success.
bool clj_var_cas_meta(clj_value var, clj_value expected, clj_value m);
// (:private (meta var)) is logical true.
bool clj_var_is_private(clj_value var);

#endif
