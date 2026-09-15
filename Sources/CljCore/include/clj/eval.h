// @ai-generated(guided)
#ifndef CLJ_EVAL_H
#define CLJ_EVAL_H

#include "analyzer.h"
#include "fn.h"
#include "intrinsics.h"

typedef struct clj_frame clj_frame;
typedef clj_value (*clj_eval_fn)(const clj_node *node, clj_frame *frame);

// Execution state of one node; inline caches, profile counters and rewrites land here, never in the node.
typedef struct {
	clj_eval_fn eval;
	uint64_t    hits; // counted only while clj_exec_count is on
} clj_exec_node;

typedef struct clj_call_site clj_call_site;

// One per tree; a closure retains the exec of the tree it was created in.
typedef struct {
	clj_header      h;
	const clj_node *root;   // retained
	uint32_t        nslots; // frame slots the root needs at top level
	uint32_t        nsites; // invoke nodes in the tree
	clj_call_site  *sites;  // their inline caches, indexed by clj_node.site (eval.c)
	clj_exec_node   nodes[]; // indexed by node id
} clj_exec;

extern const clj_type clj_exec_type;

static inline clj_exec *clj_exec_of(clj_value v) { return (clj_exec *)clj_to_ptr(v); }

// Slot ownership: a fixed param and the self slot borrow from the caller's argument array, which the +0
// convention keeps alive for the whole call; let/loop/recur/catch store owned values. `owned` has one bit
// per slot for the first 64; a frame with more slots retains every param at entry and treats every slot
// as owned, so a slot index >= 64 is owned by definition.
struct clj_frame {
	clj_value       *slots;
	clj_value       *captured; // borrowed from the running closure; a direct fn's frame shares its definer's
	const clj_exec  *exec;     // of the tree the running node belongs to
	uint64_t         owned;
	const clj_frame *outer;    // the defining frame of a direct fn body, alive for the whole call; else NULL
};

// Builds the table in one walk of the tree. Owned.
clj_value clj_exec_new(const clj_node *root);
// The interpreter's eval for a node kind: what an exec table holds when no rewrite is on.
clj_eval_fn clj_node_eval_fn(clj_node_kind kind);
// Swaps every node's eval for a hit-counting wrapper and back; off costs nothing, not even a branch.
void     clj_exec_count(clj_value exec, bool on);
uint64_t clj_exec_hits(clj_value exec, uint32_t id);
// Call-site counters of the invoke node with this id, counted in debug builds only (-1 otherwise): a hit
// entered a closure body directly, called a plain native from the site or called a cached protocol impl, a
// miss went through the generic invoke (a host fn, a variadic or large-frame closure, a keyword or a
// collection at the head, a protocol dispatch through the tables, which refills).
int64_t clj_debug_exec_ic_hits(clj_value exec, uint32_t id);
int64_t clj_debug_exec_ic_misses(clj_value exec, uint32_t id);
// Receiver types the protocol cache of that node holds, at most CLJ_PROTO_IC_ENTRIES.
uint32_t clj_debug_exec_ic_proto_entries(clj_value exec, uint32_t id);
// The id of the tree's invoke node number `site` in pre-order (its clj_node.site); aborts past the last.
uint32_t clj_debug_exec_invoke_id(clj_value exec, uint32_t site);
// Direct calls of let/loop-bound fns run on this thread, counted in debug builds only (-1 otherwise).
int64_t clj_debug_direct_calls(void);
#define CLJ_PROTO_IC_ENTRIES 4
// Evaluates the root in a fresh frame. Owned result or CLJ_THROWN.
clj_value clj_exec_run(clj_value exec);
// clj_exec_new, run, release: for a tree executed once.
clj_value clj_eval_node(const clj_node *node);
// Analyze and evaluate one form; env NULL means the current namespace and no position.
clj_value clj_eval(clj_value form, const clj_env *env);

// Takes an old fn root a rebind replaced while this thread evaluates (a closure frame or clj_exec_run is up)
// and releases it once the thread is idle; false when the caller releases it itself. Only fn roots are read
// at +0 by the evaluator, so only they are parked.
bool clj_eval_retire_root(clj_value old);
// Parked roots on this thread, for tests.
size_t clj_debug_retired_roots(void);

// A call of f with a fixed argument count prepared once, for a native that calls f per element: a closure has its
// arity resolved and enters its body directly, a plain native skips its arity check. f is borrowed and must
// outlive the calls; a fn that takes no n arguments still goes through clj_invoke, which reports it.
typedef struct {
	clj_value            f;
	size_t               n;
	const clj_fn_arity  *arity;     // of a closure for n, else NULL
	clj_native_fn        native;    // of a plain native accepting n, else NULL
	const clj_intrinsic *consuming; // the consuming entry when f is its boot builtin (conj, assoc, ...), else NULL: a
	                                // driver that owns its accumulator hands it over through clj_intrinsic_call_consuming
} clj_call;

clj_call  clj_call_prepare(clj_value f, size_t n);
clj_value clj_call_invoke_slow(const clj_call *c, const clj_value *args);
// args: n of them, borrowed. Owned result or CLJ_THROWN.
static inline clj_value clj_call_invoke(const clj_call *c, const clj_value *args) {
	if (c->native) return c->native(args, c->n);
	return clj_call_invoke_slow(c, args);
}
// The same with args[0] at +1: a consuming native takes it, a closure's frame owns its first param (so the
// param's last-use read hands it over), anything else is called at +0 and args[0] released after.
clj_value clj_call_invoke_owning(const clj_call *c, const clj_value *args);

// Arity dispatch and body evaluation of a closure; clj_invoke calls it.
clj_value clj_closure_invoke(clj_value f, const clj_value *args, size_t n);
// The same with the invoke node for the shadow frame (shadow.h); NULL when there is none.
clj_value clj_closure_invoke_at(clj_value f, const clj_value *args, size_t n, const clj_node *site);

#endif
