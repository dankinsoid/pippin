// @ai-generated(guided)
#ifndef CLJ_EVAL_H
#define CLJ_EVAL_H

#include "analyzer.h"

// Slots are owned by the frame and released when it ends; captured is borrowed from the running closure.
struct clj_frame {
	clj_value *slots;
	clj_value *captured;
};

// Evaluates a top-level tree in a fresh frame of nslots slots. Owned result or CLJ_THROWN.
clj_value clj_eval_node(const clj_node *node, uint32_t nslots);
// Analyze and evaluate one form; env NULL means the current namespace and no position.
clj_value clj_eval(clj_value form, const clj_env *env);

// The eval function of a node kind; the analyzer stores it in the node.
clj_eval_fn clj_node_eval_fn(clj_node_kind kind);
// Arity dispatch and body evaluation of a closure; clj_invoke calls it.
clj_value clj_closure_invoke(clj_value f, const clj_value *args, size_t n);

#endif
