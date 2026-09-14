// @ai-generated(guided)
#ifndef CLJ_EVAL_H
#define CLJ_EVAL_H

#include "analyzer.h"

typedef struct clj_frame clj_frame;
typedef clj_value (*clj_eval_fn)(const clj_node *node, clj_frame *frame);

// Execution state of one node; inline caches, profile counters and rewrites land here, never in the node.
typedef struct {
	clj_eval_fn eval;
	uint64_t    hits; // counted only while clj_exec_count is on
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

// Slot ownership: a fixed param and the self slot borrow from the caller's argument array, which the +0
// convention keeps alive for the whole call; let/loop/recur/catch store owned values. `owned` has one bit
// per slot for the first 64; a frame with more slots retains every param at entry and treats every slot
// as owned, so a slot index >= 64 is owned by definition.
struct clj_frame {
	clj_value      *slots;
	clj_value      *captured; // borrowed from the running closure
	const clj_exec *exec;     // of the tree the running node belongs to
	uint64_t        owned;
};

// Builds the table in one walk of the tree. Owned.
clj_value clj_exec_new(const clj_node *root);
// The interpreter's eval for a node kind: what an exec table holds when no rewrite is on.
clj_eval_fn clj_node_eval_fn(clj_node_kind kind);
// Swaps every node's eval for a hit-counting wrapper and back; off costs nothing, not even a branch.
void     clj_exec_count(clj_value exec, bool on);
uint64_t clj_exec_hits(clj_value exec, uint32_t id);
// Evaluates the root in a fresh frame. Owned result or CLJ_THROWN.
clj_value clj_exec_run(clj_value exec);
// clj_exec_new, run, release: for a tree executed once.
clj_value clj_eval_node(const clj_node *node);
// Analyze and evaluate one form; env NULL means the current namespace and no position.
clj_value clj_eval(clj_value form, const clj_env *env);

// Arity dispatch and body evaluation of a closure; clj_invoke calls it.
clj_value clj_closure_invoke(clj_value f, const clj_value *args, size_t n);
// The same with the invoke node for the shadow frame (shadow.h); NULL when there is none.
clj_value clj_closure_invoke_at(clj_value f, const clj_value *args, size_t n, const clj_node *site);

#endif
