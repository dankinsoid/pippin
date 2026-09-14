// @ai-generated(guided)
#ifndef CLJ_EVAL_H
#define CLJ_EVAL_H

#include "analyzer.h"

typedef struct clj_frame clj_frame;
typedef clj_value (*clj_eval_fn)(const clj_node *node, clj_frame *frame);

// Execution state of one node; inline caches, profile counters and rewrites land here, never in the node.
typedef struct {
	clj_eval_fn eval;
} clj_exec_node;

// One per tree; a closure retains the exec of the tree it was created in.
typedef struct {
	clj_header      h;
	const clj_node *root;   // retained
	uint32_t        nslots; // frame slots the root needs at top level
	clj_exec_node   nodes[]; // indexed by node id
} clj_exec;

extern const clj_type clj_exec_type;

static inline clj_exec *clj_exec_of(clj_value v) { return (clj_exec *)clj_to_ptr(v); }

struct clj_frame {
	clj_value      *slots;    // owned by the frame, released when it ends
	clj_value      *captured; // borrowed from the running closure
	const clj_exec *exec;     // of the tree the running node belongs to
};

// Builds the table in one walk of the tree. Owned.
clj_value clj_exec_new(const clj_node *root);
// Evaluates the root in a fresh frame. Owned result or CLJ_THROWN.
clj_value clj_exec_run(clj_value exec);
// clj_exec_new, run, release: for a tree executed once.
clj_value clj_eval_node(const clj_node *node);
// Analyze and evaluate one form; env NULL means the current namespace and no position.
clj_value clj_eval(clj_value form, const clj_env *env);

// Arity dispatch and body evaluation of a closure; clj_invoke calls it.
clj_value clj_closure_invoke(clj_value f, const clj_value *args, size_t n);

#endif
