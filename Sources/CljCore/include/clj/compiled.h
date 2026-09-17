// @ai-generated(solo)
#ifndef CLJ_COMPILED_H
#define CLJ_COMPILED_H

#include "analyzer.h"
#include "fn.h"
#include "fusion.h"
#include "intrinsics.h"
#include "runtime.h"

// Runtime side of the C the compiler (Sources/CljCompiler) emits; the inline helpers are in compiled_internal.h.

// The interpreter's clj_frame without the exec: owned has one bit per slot for the first 64 (eval.h).
typedef struct clj_cframe {
	clj_value               *slots;
	const clj_value         *captured;
	uint64_t                 owned;
	const struct clj_cframe *outer;
} clj_cframe;

// Per-call state between clj_c_enter and clj_c_leave: profile start, signpost id, the instrumentation byte read once.
typedef struct {
	uint64_t t0;
	uint64_t signpost;
	uint8_t  instrument;
} clj_ccall;

// The unit descriptor a compiled dylib exports as `clj_compiled_unit`; the init evaluates the unit's forms in order.
typedef struct {
	const char       *path; // what clj_load_file is given for this source
	clj_compiled_init init;
	void (*pools)(void); // fills the constant and var pools; init calls it when nobody did
} clj_compiled_unit;

// The path the compiler and the boot loader agree on for core.clj (#line directives, *file* of the hook).
#define CLJ_CORE_CLJ_PATH "Sources/CljCore/boot/core.clj"

// Interns a constant from its printed form; a build bug (fatal) when it does not read back.
clj_value clj_c_const(const char *edn, size_t len);
// The var ns/name, interned (the namespace created when missing). Borrowed: vars are immortal.
clj_value clj_c_var(const char *ns, const char *name);
// Table entries by qualified name, fatal when absent: the tables are fixed at build time.
const clj_intrinsic  *clj_c_intrinsic(const char *qualified, uint32_t arity);
const clj_fusion_var *clj_c_fusion_var(const char *qualified);
// A compiled closure over env (borrowed, retained): a context native whose ctx is the fn itself (clj_fn_native_env).
clj_value clj_c_closure(clj_value name, clj_native_ctx_fn fn, const clj_value *env, uint32_t nenv, uint32_t arities, uint32_t min_arity, uint32_t max_arity);
// The rest of a def after its root is bound: meta (borrowed, a map), the flags; returns the var retained.
clj_value clj_c_def(clj_value var, clj_value meta, bool macro, bool dynamic);
// Map and set literals from borrowed items (a map alternates key, value), with the duplicate-key check of the evaluator.
clj_value clj_c_map_literal(const clj_value *items, uint32_t n);
clj_value clj_c_set_literal(const clj_value *items, uint32_t n);
// "Wrong number of args (n) passed to: name", "fn" for an anonymous one, as the interpreter words it for a closure.
clj_value clj_c_arity_error(clj_value f, size_t n);
// A fn stub for the shadow stack: an immortal FN node carrying only a name and a position.
void clj_c_stub_init(clj_node *stub, clj_value name, uint32_t line, uint32_t col);
// The C stack limit of this thread's shadow stack, computed on first use (eval.c).
char *clj_eval_stack_limit(void *shadow_stack);
// Called with the deadline set: true when the deadline throw is now pending.
bool clj_eval_deadline_hit(void *shadow_stack);

#ifdef CLJ_COMPILED_CORE
// boot/core.c and boot/libs_*.c: the compiled core.clj and the embedded libs, registered by path.
clj_value clj_compiled_core_init(void);
void      clj_compiled_libs_register(void);
#endif

#endif
