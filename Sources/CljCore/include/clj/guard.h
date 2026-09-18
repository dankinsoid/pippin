// @ai-generated(solo)
#ifndef CLJ_GUARD_H
#define CLJ_GUARD_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#include "value.h"

// Where a C stack overflow lands as "Stack overflow" (NOTES.md "Compiler", the guard page): push, sigsetjmp on
// buf, clj_recovery_throw when it returns nonzero, pop.
typedef struct clj_recovery {
	sigjmp_buf           buf;
	struct clj_recovery *prev;
	void                *shadow;
	size_t               depth;
	uint32_t             exec_depth;
	void                *bindings;
} clj_recovery;

void      clj_recovery_push(clj_recovery *r);
void      clj_recovery_pop(clj_recovery *r);
clj_value clj_recovery_throw(clj_recovery *r);

#endif
