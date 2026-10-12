// @ai-generated(solo)
#ifndef CLJ_LOWERED_H
#define CLJ_LOWERED_H

#include "fn.h"

// A native higher-order fn over a literal fn without its closure (design §10 step 7a′, optimizer.c try_lower).
typedef enum { CLJ_LOWER_REDUCE, CLJ_LOWER_REDUCE_KV, CLJ_LOWER_SWAP, CLJ_LOWER_SWAP_VALS, CLJ_LOWER_HOW_COUNT } clj_lowered_how;

bool clj_lowered_find(clj_value var, uint32_t nargs, clj_lowered_how *how);
// *at: the fn's argument of a call of nargs; *arity: the count the native calls it with.
void clj_lowered_shape(clj_lowered_how how, uint32_t nargs, uint32_t *at, uint32_t *arity);
// The builtin clj_lowered_call runs: a lowered site's guard checks that the var still holds it.
clj_value clj_lowered_root(clj_lowered_how how);
// Records the builtins' vars and roots; clj_init calls it before core.clj, whose fns then lower too.
void clj_lowered_install(void);

typedef struct {
	clj_native_ctx_fn step;          // the literal's arity of `arity` args
	clj_value (*make)(void *ctx);    // the literal as a closure: another count, or a source that lets the fn out of C
	void     *ctx;
	uint32_t  arity;
	clj_value closure; // made on the first need; clj_lowered_call releases it
} clj_lowered;

// The fn the native gets lives on this C stack: it must not outlive the call.
clj_value clj_lowered_call(clj_lowered_how how, clj_value *args, size_t n, clj_lowered *l);

#endif
