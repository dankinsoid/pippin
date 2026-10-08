// @ai-generated(guided)
#ifndef CLJ_VAR_H
#define CLJ_VAR_H

#include "object.h"

// Immortal: a var lives for the process, like the name that reaches it. Made through clj_ns_intern.
typedef struct {
	clj_header        h;
	clj_slot          ns;   // symbol
	clj_slot          name; // symbol
	clj_atomic_slot   root; // CLJ_UNBOUND until the first def; a clj_lazy_def until a lazy def's first deref
	clj_atomic_slot   meta; // map or nil; published like root
	clj_atomic_slot   lazy; // the lazy def root binds, held past its force until the next def (clj_lazy_def)
	bool              macro; // :macro true in the def's meta, as Var.isMacro reads it on the JVM; the analyzer expands calls through such vars
	bool              dynamic; // :dynamic true in the def's meta: deref looks at the thread's bindings first
	_Atomic uint32_t  thread_bound; // live thread bindings across all threads; 0 lets deref skip the frame lookup
	_Atomic uint32_t  epoch; // 0 until the first root bind, then bumped by every one: the guard a cached summary holds (facts.h)
	_Atomic uint32_t  callers_epoch; // bumped when the set of recorded call sites or value reads of this var changes (summary.h)
} clj_var;

extern const clj_type clj_var_type;

clj_value clj_var_new(clj_value ns, clj_value name);

static inline bool     clj_is_var(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_var_type; }
static inline clj_var *clj_var_of(clj_value v) { return (clj_var *)clj_to_ptr(v); }
// Borrowed: the var never dies.
static inline clj_value clj_var_ns(clj_value var) { return clj_var_of(var)->ns.v; }
static inline clj_value clj_var_name(clj_value var) { return clj_var_of(var)->name.v; }

// Counts root binds of this var alone, 0 while it has never been bound: what invalidates a summary of its root.
static inline uint32_t clj_var_epoch(clj_value var) { return atomic_load_explicit(&clj_var_of(var)->epoch, memory_order_acquire); }
// Counts changes to who calls this var (the reverse index, summary.h): what invalidates a caller join, not a summary.
static inline uint32_t clj_var_callers_epoch(clj_value var) {
	return atomic_load_explicit(&clj_var_of(var)->callers_epoch, memory_order_acquire);
}
// Borrowed root, CLJ_UNBOUND when unbound. Not safe against a concurrent def (NOTES.md).
clj_value clj_var_root(clj_value var);
// The same without ordering: for a guard that only compares the root against a known immortal object.
static inline clj_value clj_var_root_relaxed(clj_value var) { return clj_slot_load(&clj_var_of(var)->root, memory_order_relaxed); }
static inline bool clj_var_is_bound(clj_value var) { return clj_var_root(var) != CLJ_UNBOUND; }
// Shares and retains val (a var is reachable from every thread), releases the previous root (a fn root once the
// thread is idle: clj_eval_retire_root).
void clj_var_bind_root(clj_value var, clj_value val);
// Owned: the thread's binding of a dynamic var, else the root; throws when unbound. Forces a lazy root.
clj_value clj_var_deref(clj_value var);
// Owned: the root, a lazy one forced, never a thread binding; throws when unbound or when the force throws.
clj_value clj_var_root_value(clj_value var);

// ---- lazy def (design §4 «Var и ленивые def»): a root that is the thunk of the def's init until the first deref.

// The init of a lazy def: owned value or CLJ_THROWN. code and env are what clj_var_bind_lazy was given; env is held
// by the thunk until the force and released after it.
typedef clj_value (*clj_lazy_def_fn)(const void *code, clj_value env);

typedef struct {
	clj_header       h;
	_Atomic uint32_t state;    // seq.h's forcing states, plus CLJ_FORCE_FAILED
	bool             inferred; // pure by the facts: forced by a redefinition barrier and by :after-load; ^:lazy is not
	clj_lazy_def_fn  fn;
	const void      *code;
	clj_slot         env;
	clj_slot         value;        // once forced, the value the root took
	clj_slot         error, trace; // what the failed force threw, rethrown by every deref
	// The root this def replaced, or CLJ_UNBOUND: what the init reads of its own var while it is forced, as an eager
	// redefinition's init would (`(def x (inc x))`). Released by the force.
	clj_slot         prev;
	clj_value        var; // borrowed: vars are immortal
} clj_lazy_def;

extern const clj_type clj_lazy_def_type;

static inline bool          clj_is_lazy_def(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_lazy_def_type; }
static inline clj_lazy_def *clj_lazy_def_of(clj_value v) { return (clj_lazy_def *)clj_to_ptr(v); }

// The root becomes a thunk of fn: the redefinition barrier first, as any rebind. env is retained.
void clj_var_bind_lazy(clj_value var, clj_lazy_def_fn fn, const void *code, clj_value env, bool inferred);
// The root is a thunk not yet forced (a failed one is not pending: it holds its exception).
bool clj_var_is_pending(clj_value var);
// Whether a def the analyzer decided (lazy: clj_def_lazy) defers its init now: not under *lazy-defs* :eager.
bool clj_def_defers(uint8_t lazy);
// *lazy-defs*: :lazy, :after-load (a load forces what it deferred when it ends), :eager (every def at def time).
typedef enum { CLJ_LAZY_DEFS_LAZY, CLJ_LAZY_DEFS_AFTER_LOAD, CLJ_LAZY_DEFS_EAGER } clj_lazy_defs_mode;
clj_lazy_defs_mode clj_lazy_defs_mode_now(void);
// Sets the root of *lazy-defs* (the binding of no thread): the dev client's switch.
void clj_lazy_defs_set_default(clj_lazy_defs_mode mode);
// The position in the order of lazy defs: what a load records before its forms and hands to clj_lazy_defs_force_since.
uint64_t clj_lazy_defs_mark(void);
// Forces every inferred lazy def bound since mark, in def order; CLJ_THROWN with the first failure pending, its var in
// *failed (meta :line/:column/:file name the def), else nil.
clj_value clj_lazy_defs_force_since(uint64_t mark, clj_value *failed);
// Inferred lazy defs bound since clj_init: what a load deferred, for the bench.
uint64_t clj_debug_lazy_defs_bound(void);
// Interns clojure.core/*lazy-defs* with its root from CLJ_LAZY_DEFS; clj_init calls it before core.clj.
void clj_lazy_defs_install(void);

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

// Interns the keywords this module otherwise makes on first use; clj_init calls it (runtime.c).
void clj_var_intern_keywords(void);

#endif
