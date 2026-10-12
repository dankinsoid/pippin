// @ai-generated(solo)
// The captures a lazy-seq thunk lets go at its tail cons (NOTES "Compiler", "Owned parameters": lazy-seq thunks).
#ifndef CLJC_THUNK_H
#define CLJC_THUNK_H

#include "clj/analyzer.h"
#include "reuse.h"

typedef struct {
	uint32_t node; // the cons INTRINSIC holding a TOKEN pair
	uint64_t mask; // captures released once its operands are evaluated, a bit each
} cljc_let_go;

#define CLJC_LET_GO_MAX 8

// Past a tail cons's operands the thunk cannot fail, so its cell never runs it again; a directly read capture stays.
// site: the lazy-seq* call; plan: its thunk arity's pairs.
size_t cljc_thunk_let_go(const clj_node *site, const cljc_reuse_plan *plan, cljc_let_go out[CLJC_LET_GO_MAX]);

#endif
